
#include <gui/gui.h>
#include <stdlib.h>
#include <dolphin/dolphin.h>
#include <dialogs/dialogs.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include <math.h>
#include "util.h"
#include "defines.h"
#include "common/card.h"
#include "common/dml.h"
#include "common/queue.h"
#include "util.h"
#include "ui.h"

#include "blackjack_icons.h"

#define DEALER_MAX 17

// Menu item indices
#define MENU_DOUBLE 0

// character index: 0=2 .. 8=Ten, 9=J, 10=Q, 11=K, 12=A
static uint8_t card_bj_value(uint8_t character) {
    if(character >= 9 && character <= 11) return 10; // J, Q, K
    if(character == 12) return 11;                   // Ace
    return character + 2;                            // 2–Ten
}
#define MENU_SPLIT  1
#define MENU_HIT    2
#define MENU_STAY   3

// --- Sound sequences ---
static const NotificationSequence sfx_deal = {
    &message_note_g5, &message_delay_10, &message_sound_off, NULL,
};
static const NotificationSequence sfx_win = {
    &message_note_c5, &message_delay_50,
    &message_note_e5, &message_delay_50,
    &message_note_g5, &message_delay_100,
    &message_sound_off, NULL,
};
static const NotificationSequence sfx_lose = {
    &message_note_g4, &message_delay_50,
    &message_note_e4, &message_delay_50,
    &message_note_c4, &message_delay_100,
    &message_sound_off, NULL,
};
static const NotificationSequence sfx_bust = {
    &message_note_a4, &message_delay_50,
    &message_note_f4, &message_delay_100,
    &message_sound_off, NULL,
};
static const NotificationSequence sfx_blackjack = {
    &message_note_c5, &message_delay_50,
    &message_note_e5, &message_delay_50,
    &message_note_g5, &message_delay_50,
    &message_note_c6, &message_delay_250,
    &message_sound_off, NULL,
};
static const NotificationSequence sfx_jackpot = {
    &message_note_c5, &message_delay_50,
    &message_note_e5, &message_delay_50,
    &message_note_g5, &message_delay_50,
    &message_note_c6, &message_delay_50,
    &message_note_e6, &message_delay_250,
    &message_sound_off, NULL,
};
static const NotificationSequence sfx_push = {
    &message_note_e4, &message_delay_100,
    &message_sound_off, NULL,
};

static void play_sfx(const GameState *gs, const NotificationSequence *seq) {
    if (!gs->settings.sound_effects) return;
    NotificationApp *notif = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notif, seq);
    furi_record_close(RECORD_NOTIFICATION);
}

void start_round(GameState *game_state);
void init(GameState *game_state);
static void format_winamt(char *buf, size_t n, uint32_t amount);
void dealer_blackjack_action(void *ctx);
void to_dealer_blackjack_state(const void *ctx, Canvas *const canvas);
static void to_allin_state(const void *ctx, Canvas *const canvas);
static void start_allin_deal(void *ctx);
static bool is_soft_hand_at(const Card *cards, uint8_t count, uint8_t score);
static void to_new_shoe_state(const void *ctx, Canvas *const canvas);
static void to_dealer_777_state(const void *ctx, Canvas *const canvas);

static inline Card* curr_cards(GameState *gs) {
    return gs->current_hand == 0 ? gs->player_cards : gs->split_hands[gs->current_hand - 1];
}
static inline uint8_t* curr_count_ptr(GameState *gs) {
    return gs->current_hand == 0 ? &gs->player_card_count : &gs->split_hand_counts[gs->current_hand - 1];
}

static void draw_ui(Canvas *const canvas, const GameState *game_state) {
    draw_money(canvas, game_state->player_score);

    if (game_state->split) {
        canvas_set_font(canvas, FontSecondary);
        char drawChar[20];
        uint8_t h = game_state->current_hand;
        const Card *cards = (h == 0) ? game_state->player_cards : game_state->split_hands[h - 1];
        uint8_t count = (h == 0) ? game_state->player_card_count : game_state->split_hand_counts[h - 1];
        uint8_t score = hand_count(cards, count);
        bool soft = is_soft_hand_at(cards, count, score);
        if (soft)
            snprintf(drawChar, sizeof(drawChar), "Hand %d: S%i", h + 1, score);
        else
            snprintf(drawChar, sizeof(drawChar), "Hand %d: %i", h + 1, score);
        canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, drawChar);
    } else {
        uint8_t score = hand_count(game_state->player_cards, game_state->player_card_count);
        bool soft = is_soft_hand_at(game_state->player_cards, game_state->player_card_count, score);
        draw_score(canvas, true, score, soft);
    }

    if (!game_state->queue_state.running && game_state->state == GameStatePlay) {
        uint8_t menu_y = game_state->split ? 33 : 47;
        render_menu(game_state->menu, canvas, 2, menu_y);
    }

    if (game_state->state == GameStateDealer) {
        canvas_set_font(canvas, FontSecondary);
        char dealerStr[10];
        uint8_t d = hand_count(game_state->dealer_cards, game_state->dealer_card_count);
        if (d > 21) snprintf(dealerStr, sizeof(dealerStr), "D:BUST");
        else        snprintf(dealerStr, sizeof(dealerStr), "D:%d", d);
        canvas_draw_str_aligned(canvas, 2, 62, AlignLeft, AlignBottom, dealerStr);
    }

    // Shoe remaining — tiny, top-right below money
    {
        canvas_set_font(canvas, FontSecondary);
        char shoeBuf[12];
        int rem = game_state->deck.card_count - game_state->deck.index;
        if (rem < 0) rem = 0;
        snprintf(shoeBuf, sizeof(shoeBuf), "~%d", rem);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignTop, shoeBuf);
    }

    // Bottom strip: show all split hand scores when two or more hands are in play
    if (game_state->split && game_state->num_hands > 1) {
        canvas_set_font(canvas, FontSecondary);
        uint8_t x = 44;
        for (uint8_t h = 0; h < game_state->num_hands; h++) {
            const Card *hcards = (h == 0) ? game_state->player_cards : game_state->split_hands[h - 1];
            uint8_t hcnt = (h == 0) ? game_state->player_card_count : game_state->split_hand_counts[h - 1];
            uint8_t hscore = hand_count(hcards, hcnt);
            char label[8];
            if (game_state->hand_busted[h])
                snprintf(label, sizeof(label), "%d:B", h + 1);
            else
                snprintf(label, sizeof(label), "%d:%d", h + 1, (int)hscore);
            bool active = (game_state->state == GameStatePlay && h == game_state->current_hand);
            if (active) {
                canvas_set_color(canvas, ColorBlack);
                canvas_draw_box(canvas, x - 1, 55, 20, 9);
                canvas_set_color(canvas, ColorWhite);
            } else {
                canvas_set_color(canvas, ColorBlack);
            }
            canvas_draw_str(canvas, x, 63, label);
            canvas_set_color(canvas, ColorBlack);
            x += 21;
        }
    }
}

static void render_callback(Canvas *const canvas, void *ctx) {
    const GameState *game_state = ctx;
    furi_mutex_acquire(game_state->mutex, 25);

    if (game_state == NULL) {
        return;
    }

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 0, 0, 128, 64);

    if (game_state->state == GameStateStart) {
        canvas_draw_icon(canvas, 0, 0, &I_blackjack);
    }
    if (game_state->state == GameStateGameOver) {
        canvas_draw_icon(canvas, 0, 0, &I_endscreen);
    }

    if (game_state->state == GameStatePlay || game_state->state == GameStateDealer) {
        if (game_state->state == GameStatePlay)
            draw_player_scene(canvas, game_state);
        else
            draw_dealer_scene(canvas, game_state);
        render_queue(&(game_state->queue_state), game_state, canvas);
        draw_ui(canvas, game_state);
    } else if (game_state->state == GameStateBet) {
        draw_money(canvas, game_state->player_score);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 20, 22, 88, 24);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, 20, 22, 88, 24);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignTop, "Place your bet");
        bool is_all_in = game_state->settings.round_price >= game_state->player_score;
        char betBuf[12];
        if (is_all_in) {
            snprintf(betBuf, sizeof(betBuf), "All in!");
        } else {
            format_winamt(betBuf, sizeof(betBuf), game_state->settings.round_price);
        }
        bool can_dec = game_state->settings.round_price > game_state->settings_step;
        bool can_inc = !is_all_in && (game_state->settings.round_price + game_state->settings_step <= game_state->player_score);
        if (can_dec)
            canvas_draw_str_aligned(canvas, 28, 36, AlignLeft, AlignTop, "<");
        canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignTop, betBuf);
        if (can_inc)
            canvas_draw_str_aligned(canvas, 100, 36, AlignRight, AlignTop, ">");
        // Shoe remaining count — below money, top-right
        {
            canvas_set_font(canvas, FontSecondary);
            char shoeBuf[12];
            int rem = game_state->deck.card_count - game_state->deck.index;
            if (rem < 0) rem = 0;
            snprintf(shoeBuf, sizeof(shoeBuf), "~%d", rem);
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignTop, shoeBuf);
        }
    } else if (game_state->state == GameStateInsurance) {
        draw_player_scene(canvas, game_state);
        draw_money(canvas, game_state->player_score);
        uint8_t ins_score = hand_count(game_state->player_cards, game_state->player_card_count);
        draw_score(canvas, true, ins_score,
                   is_soft_hand_at(game_state->player_cards, game_state->player_card_count, ins_score));
        // Bottom insurance panel
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 0, 44, 128, 20);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_line(canvas, 0, 44, 127, 44);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 46, AlignCenter, AlignTop, "Dealer Ace - Insurance?");
        // No button
        if (game_state->insurance_selected == 0) {
            canvas_draw_box(canvas, 3, 54, 28, 10);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str_aligned(canvas, 17, 59, AlignCenter, AlignCenter, "No");
        canvas_set_color(canvas, ColorBlack);
        // Cost label
        char insBuf[12];
        format_winamt(insBuf, sizeof(insBuf), game_state->bet / 2);
        canvas_draw_str_aligned(canvas, 64, 59, AlignCenter, AlignCenter, insBuf);
        // Yes button
        if (game_state->insurance_selected == 1) {
            canvas_draw_box(canvas, 97, 54, 28, 10);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str_aligned(canvas, 111, 59, AlignCenter, AlignCenter, "Yes");
        canvas_set_color(canvas, ColorBlack);
    } else if (game_state->state == GameStateSettings) {
        settings_page(canvas, game_state);
    }

    furi_mutex_release(game_state->mutex);
}

//region card draw
Card draw_card(GameState *game_state) {
    Card c = game_state->deck.cards[game_state->deck.index];
    game_state->deck.index++;
    return c;
}

void drawPlayerCard(void *ctx) {
    GameState *gs = ctx;
    play_sfx(gs, &sfx_deal);
    Card c = draw_card(gs);
    Card *cards = curr_cards(gs);
    uint8_t *cnt = curr_count_ptr(gs);
    cards[(*cnt)++] = c;

    if (gs->player_score < gs->settings.round_price || gs->doubled)
        set_menu_state(gs->menu, MENU_DOUBLE, false);

    // Split available when current hand has exactly 2 matching cards and under the 4-hand cap
    if (gs->num_hands < 4 && *cnt == 2) {
        bool can_split = (card_bj_value(cards[0].character) == card_bj_value(cards[1].character)) &&
                         (gs->player_score >= gs->settings.round_price);
        // Resplit aces: block re-splitting if this hand came from an ace split and setting is off
        if (can_split && gs->ace_split_hands[gs->current_hand] &&
            cards[0].character == 12 && !gs->settings.resplit_aces) {
            can_split = false;
        }
        set_menu_state(gs->menu, MENU_SPLIT, can_split);
    }
}

void drawSplitInitCard(void *ctx) {
    GameState *gs = ctx;
    Card c = draw_card(gs);
    uint8_t idx = gs->num_hands - 2; // newest extra hand (0-indexed in split_hands)
    gs->split_hands[idx][gs->split_hand_counts[idx]++] = c;
}

void drawDealerCard(void *ctx) {
    GameState *game_state = ctx;
    play_sfx(game_state, &sfx_deal);
    Card c = draw_card(game_state);
    game_state->dealer_cards[game_state->dealer_card_count] = c;
    game_state->dealer_card_count++;
}
//endregion

static void format_winamt(char *buf, size_t n, uint32_t amount) {
    if (amount < 1000) {
        snprintf(buf, n, "$%lu", (unsigned long)amount);
    } else if (amount < 1000000) {
        uint32_t whole = amount / 1000;
        uint32_t frac  = (amount % 1000) / 100;
        if (frac == 0) snprintf(buf, n, "$%luk", (unsigned long)whole);
        else           snprintf(buf, n, "$%lu.%luk", (unsigned long)whole, (unsigned long)frac);
    } else {
        uint32_t whole = amount / 1000000;
        uint32_t frac  = (amount % 1000000) / 100000;
        if (frac == 0) snprintf(buf, n, "$%lum", (unsigned long)whole);
        else           snprintf(buf, n, "$%lu.%lum", (unsigned long)whole, (unsigned long)frac);
    }
}

//region queue callbacks
void to_lose_state(const void *ctx, Canvas *const canvas) {
    const GameState *gs = ctx;
    if (gs->settings.message_duration == 0) return;
    uint8_t dealer = hand_count(gs->dealer_cards, gs->dealer_card_count);
    uint8_t player = hand_count(gs->player_cards, gs->player_card_count);
    char buf[20];
    snprintf(buf, sizeof(buf), "D:%d  P:%d", dealer, (int)player);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 32, 12, 66, 22);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 32, 12, 66, 22);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 16, AlignCenter, AlignTop, "You lost");
    canvas_draw_str_aligned(canvas, 64, 25, AlignCenter, AlignTop, buf);
}

void to_bust_state(const void *ctx, Canvas *const canvas) {
    const GameState *game_state = ctx;
    if (game_state->settings.message_duration == 0)
        return;
    popup_frame(canvas);
    elements_multiline_text_aligned(canvas, 64, 22, AlignCenter, AlignCenter, "Busted!");
}

void to_draw_state(const void *ctx, Canvas *const canvas) {
    const GameState *game_state = ctx;
    if (game_state->settings.message_duration == 0)
        return;
    popup_frame(canvas);
    elements_multiline_text_aligned(canvas, 64, 22, AlignCenter, AlignCenter, "Push");
}

void to_dealer_turn(const void *ctx, Canvas *const canvas) {
    const GameState *game_state = ctx;
    if (game_state->settings.message_duration == 0)
        return;
    popup_frame(canvas);
    elements_multiline_text_aligned(canvas, 64, 22, AlignCenter, AlignCenter, "Dealers turn");
}

void to_win_state(const void *ctx, Canvas *const canvas) {
    const GameState *gs = ctx;
    if (gs->settings.message_duration == 0) return;
    char amtBuf[12];
    format_winamt(amtBuf, sizeof(amtBuf), gs->bet);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 32, 12, 66, 22);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 32, 12, 66, 22);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 16, AlignCenter, AlignTop, "You win");
    canvas_draw_str_aligned(canvas, 64, 25, AlignCenter, AlignTop, amtBuf);
}

void to_blackjack_state(const void *ctx, Canvas *const canvas) {
    const GameState *gs = ctx;
    if (gs->settings.message_duration == 0) return;
    const char *label;
    uint32_t profit;
    switch (gs->settings.blackjack_payout) {
        case 1:  label = "Blackjack! 6:5"; profit = gs->bet * 6 / 5; break;
        case 2:  label = "Blackjack! 1:1"; profit = gs->bet;          break;
        default: label = "Blackjack! 3:2"; profit = gs->bet * 3 / 2; break;
    }
    char amtBuf[12];
    format_winamt(amtBuf, sizeof(amtBuf), profit);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 24, 12, 80, 22);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 24, 12, 80, 22);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 16, AlignCenter, AlignTop, label);
    canvas_draw_str_aligned(canvas, 64, 25, AlignCenter, AlignTop, amtBuf);
}

static bool is_natural_blackjack(const Card *cards, uint8_t count) {
    if (count != 2) return false;
    uint8_t a = card_bj_value(cards[0].character);
    uint8_t b = card_bj_value(cards[1].character);
    return (a + b == 21);
}

static bool is_soft_hand_at(const Card *cards, uint8_t count, uint8_t score) {
    uint8_t hard = 0;
    for (uint8_t i = 0; i < count; i++) {
        uint8_t v = card_bj_value(cards[i].character);
        hard += (v == 11) ? 1 : v;
    }
    return hard + 10 == score;
}

static bool is_triple_sevens(const Card *cards, uint8_t count) {
    if (count != 3) return false;
    return (cards[0].character == 5 && cards[1].character == 5 && cards[2].character == 5);
}

void to_start(const void *ctx, Canvas *const canvas) {
    const GameState *game_state = ctx;
    if (game_state->settings.message_duration == 0)
        return;
    popup_frame(canvas);
    elements_multiline_text_aligned(canvas, 64, 22, AlignCenter, AlignCenter, "Round started");
}

static void to_new_shoe_state(const void *ctx, Canvas *const canvas) {
    const GameState *gs = ctx;
    if (gs->settings.message_duration == 0) return;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 20, 20, 88, 24);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 20, 20, 88, 24);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignTop, "New shoe,");
    canvas_draw_str_aligned(canvas, 64, 33, AlignCenter, AlignTop, "fresh odds!");
}

static void to_dealer_777_state(const void *ctx, Canvas *const canvas) {
    const GameState *gs = ctx;
    if (gs->settings.message_duration == 0) return;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 16, 18, 96, 24);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 16, 18, 96, 24);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignTop, "Dealer triple 7s.");
    canvas_draw_str_aligned(canvas, 64, 31, AlignCenter, AlignTop, "How convenient.");
}

static void to_allin_state(const void *ctx, Canvas *const canvas) {
    const GameState *gs = ctx;
    if (gs->settings.message_duration == 0) return;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 20, 20, 88, 24);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 20, 20, 88, 24);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignTop, "All in!");
    canvas_draw_str_aligned(canvas, 64, 33, AlignCenter, AlignTop, "Good luck!");
}

void to_handN_state(const void *ctx, Canvas *const canvas) {
    const GameState *gs = ctx;
    if (gs->settings.message_duration == 0) return;
    popup_frame(canvas);
    char buf[12];
    snprintf(buf, sizeof(buf), "Hand %d", gs->current_hand + 2); // +2: next hand after current
    elements_multiline_text_aligned(canvas, 64, 22, AlignCenter, AlignCenter, buf);
}

void to_split_results_state(const void *ctx, Canvas *const canvas) {
    const GameState *gs = ctx;
    if (gs->settings.message_duration == 0) return;

    uint8_t dealer = hand_count(gs->dealer_cards, gs->dealer_card_count);
    uint8_t box_h = (uint8_t)(gs->num_hands * 10 + 26); // +8 for dealer line, +18 for total
    uint8_t box_y = (uint8_t)((64 - box_h) / 2);

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 18, box_y, 92, box_h);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 18, box_y, 92, box_h);
    canvas_set_font(canvas, FontSecondary);

    // Dealer score header
    char dealerBuf[14];
    if (dealer > 21) snprintf(dealerBuf, sizeof(dealerBuf), "Dealer: BUST");
    else             snprintf(dealerBuf, sizeof(dealerBuf), "Dealer: %d", dealer);
    canvas_draw_str_aligned(canvas, 64, (uint8_t)(box_y + 4), AlignCenter, AlignTop, dealerBuf);

    for (uint8_t h = 0; h < gs->num_hands; h++) {
        const Card *cards = (h == 0) ? gs->player_cards : gs->split_hands[h - 1];
        uint8_t cnt  = (h == 0) ? gs->player_card_count : gs->split_hand_counts[h - 1];
        uint8_t score = hand_count(cards, cnt);
        const char *res;
        if (is_triple_sevens(cards, cnt))                                    res = "777!";
        else if (is_natural_blackjack(cards, cnt) && (dealer > 21 || score > dealer)) res = "BJ! ";
        else if (gs->hand_busted[h])                                         res = "BUST";
        else if (dealer > 21 || score > dealer)                              res = "WIN ";
        else if (score == dealer)                                             res = "PUSH";
        else                                                                  res = "LOST";
        char line[22];
        snprintf(line, sizeof(line), "Hand %d: %2d    %s", h + 1, score, res);
        canvas_draw_str(canvas, 22, (uint8_t)(box_y + 12 + h * 10 + 7), line);
    }

    // Separator + combined total
    uint8_t sep_y = (uint8_t)(box_y + 12 + gs->num_hands * 10);
    canvas_draw_line(canvas, 19, sep_y, 108, sep_y);
    char totalBuf[20];
    if (gs->last_win_amount > 0) {
        char amtBuf[12];
        format_winamt(amtBuf, sizeof(amtBuf), gs->last_win_amount);
        snprintf(totalBuf, sizeof(totalBuf), "Won: %s", amtBuf);
    } else {
        snprintf(totalBuf, sizeof(totalBuf), "No win");
    }
    canvas_draw_str_aligned(canvas, 63, (uint8_t)(sep_y + 3), AlignCenter, AlignTop, totalBuf);
}

void before_start(void *ctx) {
    GameState *game_state = ctx;
    game_state->dealer_card_count = 0;
    game_state->player_card_count = 0;
}

void start(void *ctx) {
    GameState *game_state = ctx;
    start_round(game_state);
}

void draw(void *ctx) {
    GameState *game_state = ctx;
    game_state->player_score += game_state->bet;
    game_state->bet = 0;
    play_sfx(game_state, &sfx_push);
    enqueue(&(game_state->queue_state), game_state, start, before_start, NULL, 0);
}

void game_over(void *ctx) {
    GameState *game_state = ctx;
    game_state->state = GameStateGameOver;
}

void lose(void *ctx) {
    GameState *game_state = ctx;
    game_state->state = GameStatePlay;
    game_state->bet = 0;
    play_sfx(game_state, &sfx_lose);
    if (game_state->player_score > 0) {
        enqueue(&(game_state->queue_state), game_state, start, before_start, NULL, 0);
    } else {
        enqueue(&(game_state->queue_state), game_state, game_over, NULL, NULL, 0);
    }
}

void win(void *ctx) {
    dolphin_deed(DolphinDeedPluginGameWin);
    GameState *game_state = ctx;
    game_state->state = GameStatePlay;
    game_state->player_score += game_state->bet * 2; // 1:1 payout
    game_state->bet = 0;
    play_sfx(game_state, &sfx_win);
    enqueue(&(game_state->queue_state), game_state, start, before_start, NULL, 0);
}

void win_blackjack(void *ctx) {
    dolphin_deed(DolphinDeedPluginGameWin);
    GameState *gs = ctx;
    gs->state = GameStatePlay;
    switch (gs->settings.blackjack_payout) {
        case 1:  gs->player_score += gs->bet * 11 / 5; break; // 6:5
        case 2:  gs->player_score += gs->bet * 2;      break; // 1:1
        default: gs->player_score += gs->bet * 5 / 2;  break; // 3:2
    }
    gs->bet = 0;
    play_sfx(gs, &sfx_blackjack);
    enqueue(&gs->queue_state, gs, start, before_start, NULL, 0);
}

void to_triple_sevens_state(const void *ctx, Canvas *const canvas) {
    const GameState *gs = ctx;
    if (gs->settings.message_duration == 0) return;
    char amtBuf[12];
    format_winamt(amtBuf, sizeof(amtBuf), gs->bet * 100);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 18, 12, 92, 22);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 18, 12, 92, 22);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 16, AlignCenter, AlignTop, "Triple 7s! 100:1");
    canvas_draw_str_aligned(canvas, 64, 25, AlignCenter, AlignTop, amtBuf);
}

void win_triple_sevens(void *ctx) {
    dolphin_deed(DolphinDeedPluginGameWin);
    GameState *gs = ctx;
    gs->state = GameStatePlay;
    gs->player_score += gs->bet * 101; // return bet + 100x profit
    gs->bet = 0;
    play_sfx(gs, &sfx_jackpot);
    enqueue(&gs->queue_state, gs, start, before_start, NULL, 0);
}

void to_dealer_blackjack_state(const void *ctx, Canvas *const canvas) {
    const GameState *gs = ctx;
    if (gs->settings.message_duration == 0) return;
    bool player_bj = is_natural_blackjack(gs->player_cards, gs->player_card_count);
    const char *result;
    if (player_bj && gs->insurance_bet > 0) result = "Push + insured";
    else if (player_bj)                      result = "Push";
    else if (gs->insurance_bet > 0)          result = "Insured";
    else                                     result = "You lose";
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 20, 12, 88, 22);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 20, 12, 88, 22);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 16, AlignCenter, AlignTop, "Dealer Blackjack!");
    canvas_draw_str_aligned(canvas, 64, 25, AlignCenter, AlignTop, result);
}

void dealer_blackjack_action(void *ctx) {
    GameState *gs = ctx;
    gs->state = GameStatePlay;
    // Pay out insurance at 2:1 if taken
    if (gs->insurance_bet > 0) {
        gs->player_score += gs->insurance_bet * 3; // return insurance_bet + 2x profit
        gs->insurance_bet = 0;
    }
    // If player also has blackjack → push (return original bet)
    if (is_natural_blackjack(gs->player_cards, gs->player_card_count)) {
        dolphin_deed(DolphinDeedPluginGameWin);
        gs->player_score += gs->bet;
        play_sfx(gs, &sfx_push);
    } else {
        play_sfx(gs, &sfx_lose);
    }
    gs->bet = 0;
    if (gs->player_score > 0) {
        enqueue(&gs->queue_state, gs, start, before_start, NULL, 0);
    } else {
        enqueue(&gs->queue_state, gs, game_over, NULL, NULL, 0);
    }
}

// Payout for a split round — evaluates each hand against the dealer independently.
void splitPayout(void *ctx) {
    GameState *gs = ctx;
    gs->state = GameStatePlay;

    uint8_t dealer_score = hand_count(gs->dealer_cards, gs->dealer_card_count);
    bool any_win = false;

    uint32_t total_profit = 0;

    // BJ payout helper (returns total back including original bet)
    #define BJ_PAYOUT(bet) ( \
        (gs->settings.blackjack_payout == 1) ? (bet) * 11 / 5 : \
        (gs->settings.blackjack_payout == 2) ? (bet) * 2      : \
                                               (bet) * 5 / 2  )
    #define BJ_PROFIT(bet) ( \
        (gs->settings.blackjack_payout == 1) ? (bet) * 6 / 5 : \
        (gs->settings.blackjack_payout == 2) ? (bet)          : \
                                               (bet) * 3 / 2  )

    // Hand 0 (player_cards)
    if (!gs->hand_busted[0]) {
        uint8_t s = hand_count(gs->player_cards, gs->player_card_count);
        if (is_triple_sevens(gs->player_cards, gs->player_card_count)) {
            gs->player_score += gs->bet * 101;
            total_profit += gs->bet * 100;
            any_win = true;
        } else if (is_natural_blackjack(gs->player_cards, gs->player_card_count) && (dealer_score > 21 || s > dealer_score)) {
            gs->player_score += BJ_PAYOUT(gs->bet);
            total_profit += BJ_PROFIT(gs->bet);
            any_win = true;
        } else if (dealer_score > 21 || s > dealer_score) {
            gs->player_score += gs->bet * 2;
            total_profit += gs->bet;
            any_win = true;
        } else if (s == dealer_score) {
            gs->player_score += gs->bet;
        }
    }

    // Hands 1–N (split_hands)
    for (uint8_t h = 0; h < gs->num_hands - 1; h++) {
        if (!gs->hand_busted[h + 1]) {
            uint8_t s = hand_count(gs->split_hands[h], gs->split_hand_counts[h]);
            uint32_t bet = gs->split_hand_bets[h];
            if (is_triple_sevens(gs->split_hands[h], gs->split_hand_counts[h])) {
                gs->player_score += bet * 101;
                total_profit += bet * 100;
                any_win = true;
            } else if (is_natural_blackjack(gs->split_hands[h], gs->split_hand_counts[h]) && (dealer_score > 21 || s > dealer_score)) {
                gs->player_score += BJ_PAYOUT(bet);
                total_profit += BJ_PROFIT(bet);
                any_win = true;
            } else if (dealer_score > 21 || s > dealer_score) {
                gs->player_score += bet * 2;
                total_profit += bet;
                any_win = true;
            } else if (s == dealer_score) {
                gs->player_score += bet;
            }
        }
    }

    #undef BJ_PAYOUT
    #undef BJ_PROFIT

    if (any_win) {
        dolphin_deed(DolphinDeedPluginGameWin);
        play_sfx(gs, &sfx_win);
    } else {
        play_sfx(gs, &sfx_lose);
    }

    gs->last_win_amount = total_profit;
    gs->bet = 0;
    memset(gs->split_hand_bets, 0, sizeof(gs->split_hand_bets));

    if (gs->player_score > 0) {
        enqueue(&gs->queue_state, gs, start, before_start, to_split_results_state,
                gs->settings.message_duration);
    } else {
        enqueue(&gs->queue_state, gs, game_over, NULL, to_split_results_state,
                gs->settings.message_duration);
    }
}

static void noop_action(void *ctx) {
    (void)ctx;
}

static void dealer_reveal_animation(const void *ctx, Canvas *const canvas) {
    (void)ctx;
    (void)canvas;
}

void dealerTurn(void *ctx) {
    GameState *game_state = ctx;
    game_state->state = GameStateDealer;
    // Pause briefly so the player sees both dealer cards before drawing begins
    enqueue(&game_state->queue_state, game_state, noop_action, NULL, dealer_reveal_animation,
            game_state->settings.animation_duration);
}

// Advances to the next split hand.
void advanceToNextHand(void *ctx) {
    GameState *gs = ctx;
    gs->current_hand++;
    set_menu_state(gs->menu, MENU_HIT, true);
    set_menu_state(gs->menu, MENU_STAY, true);
    gs->menu->enabled = true;
    gs->menu->current_menu = MENU_HIT;

    // Re-check split eligibility for the hand we just moved to
    Card *cards = curr_cards(gs);
    uint8_t count = *curr_count_ptr(gs);
    if (gs->num_hands < 4 && count == 2) {
        bool can_split = (card_bj_value(cards[0].character) == card_bj_value(cards[1].character)) &&
                         (gs->player_score >= gs->settings.round_price);
        set_menu_state(gs->menu, MENU_SPLIT, can_split);
    } else {
        set_menu_state(gs->menu, MENU_SPLIT, false);
    }
}

float animationTime(const GameState *game_state) {
    return (float)(furi_get_tick() - game_state->queue_state.start) /
           (float)(game_state->settings.animation_duration);
}

void dealer_card_animation(const void *ctx, Canvas *const canvas) {
    const GameState *game_state = ctx;
    float t = animationTime(game_state);

    Card animatingCard = game_state->deck.cards[game_state->deck.index];
    if (game_state->dealer_card_count > 1) {
        Vector end = card_pos_at_index(game_state->dealer_card_count);
        draw_card_animation(animatingCard, (Vector){0, 64}, (Vector){0, 32}, end, t, true, canvas);
    } else {
        draw_card_animation(animatingCard, (Vector){32, -CARD_HEIGHT}, (Vector){64, 32},
                            (Vector){2, 2}, t, false, canvas);
    }
}

void dealer_back_card_animation(const void *ctx, Canvas *const canvas) {
    const GameState *game_state = ctx;
    float t = animationTime(game_state);
    Vector currentPos = quadratic_2d((Vector){32, -CARD_HEIGHT}, (Vector){64, 32}, (Vector){13, 5}, t);
    draw_card_back_at(currentPos.x, currentPos.y, canvas);
}

void player_card_animation(const void *ctx, Canvas *const canvas) {
    const GameState *gs = ctx;
    float t = animationTime(gs);
    Card animatingCard = gs->deck.cards[gs->deck.index];
    uint8_t count = gs->current_hand == 0
        ? gs->player_card_count
        : gs->split_hand_counts[gs->current_hand - 1];
    Vector end = card_pos_at_index(count);
    draw_card_animation(animatingCard, (Vector){32, -CARD_HEIGHT}, (Vector){0, 32}, end, t, true, canvas);
}
//endregion

static void start_allin_deal(void *ctx) {
    GameState *gs = ctx;
    enqueue(&gs->queue_state, gs, drawDealerCard, NULL, dealer_back_card_animation,
            gs->settings.animation_duration);
    enqueue(&gs->queue_state, gs, drawPlayerCard, NULL, player_card_animation,
            gs->settings.animation_duration);
    enqueue(&gs->queue_state, gs, drawDealerCard, NULL, dealer_card_animation,
            gs->settings.animation_duration);
    enqueue(&gs->queue_state, gs, drawPlayerCard, NULL, player_card_animation,
            gs->settings.animation_duration);
}

void player_tick(GameState *gs) {
    // Insurance check: runs once after initial deal when dealer shows an Ace
    // Auto-stay ace-split hands when hit-after-ace-split is disabled
    if (gs->split && gs->ace_split_hands[gs->current_hand] && !gs->settings.hit_after_ace_split) {
        uint8_t cnt = *curr_count_ptr(gs);
        if (cnt >= 2) {
            bool more = gs->current_hand < gs->num_hands - 1;
            if (more)
                enqueue(&gs->queue_state, gs, advanceToNextHand, NULL, NULL, 0);
            else
                enqueue(&gs->queue_state, gs, dealerTurn, NULL, to_dealer_turn,
                        gs->settings.message_duration);
            return;
        }
        return; // still waiting for the initial post-split card
    }

    if (!gs->insurance_offered) {
        gs->insurance_offered = true;
        if (!gs->split && gs->player_card_count == 2 && gs->dealer_card_count == 2) {
            if (gs->dealer_cards[1].character == 12) {
                // Dealer shows Ace → offer insurance
                gs->insurance_selected = 0;
                gs->state = GameStateInsurance;
                return;
            } else if (card_bj_value(gs->dealer_cards[1].character) == 10 &&
                       is_natural_blackjack(gs->dealer_cards, gs->dealer_card_count)) {
                // Dealer shows 10-value, silent peek reveals BJ → end round immediately
                gs->state = GameStateDealer;
                enqueue(&gs->queue_state, gs, dealer_blackjack_action, NULL,
                        to_dealer_blackjack_state, gs->settings.message_duration);
                return;
            }
        }
    }

    uint8_t score = hand_count(curr_cards(gs), *curr_count_ptr(gs));
    bool more_hands = gs->split && gs->current_hand < gs->num_hands - 1;

    if ((gs->doubled && score <= 21) || score == 21) {
        if (!gs->split && is_triple_sevens(curr_cards(gs), *curr_count_ptr(gs))) {
            enqueue(&gs->queue_state, gs, win_triple_sevens, NULL, to_triple_sevens_state,
                    gs->settings.message_duration);
        } else if (!gs->split && is_natural_blackjack(curr_cards(gs), *curr_count_ptr(gs))) {
            if (is_natural_blackjack(gs->dealer_cards, gs->dealer_card_count)) {
                enqueue(&gs->queue_state, gs, draw, NULL, to_draw_state,
                        gs->settings.message_duration);
            } else {
                enqueue(&gs->queue_state, gs, win_blackjack, NULL, to_blackjack_state,
                        gs->settings.message_duration);
            }
        } else if (more_hands) {
            enqueue(&gs->queue_state, gs, advanceToNextHand, NULL, to_handN_state,
                    gs->settings.message_duration);
        } else {
            enqueue(&gs->queue_state, gs, dealerTurn, NULL, to_dealer_turn,
                    gs->settings.message_duration);
        }
    } else if (score > 21) {
        gs->hand_busted[gs->current_hand] = true;
        play_sfx(gs, &sfx_bust);
        if (more_hands) {
            enqueue(&gs->queue_state, gs, advanceToNextHand, NULL, to_bust_state,
                    gs->settings.message_duration);
        } else if (gs->split) {
            // Last hand busted — go to dealer; splitPayout handles all hands
            enqueue(&gs->queue_state, gs, dealerTurn, NULL, to_bust_state,
                    gs->settings.message_duration);
        } else {
            enqueue(&gs->queue_state, gs, lose, NULL, to_bust_state,
                    gs->settings.message_duration);
        }
    } else {
        if (gs->selectDirection == DirectionUp || gs->selectDirection == DirectionDown)
            move_menu(gs->menu, gs->selectDirection == DirectionUp ? -1 : 1);
        if (gs->selectDirection == Select)
            activate_menu(gs->menu, gs);
    }
}

void dealer_tick(GameState *game_state) {
    uint8_t dealer_score = hand_count(game_state->dealer_cards, game_state->dealer_card_count);
    uint8_t player_score = hand_count(game_state->player_cards, game_state->player_card_count);

    bool must_stand = (dealer_score >= DEALER_MAX);
    if (must_stand && dealer_score == 17 && game_state->settings.dealer_hits_soft17 &&
        is_soft_hand_at(game_state->dealer_cards, game_state->dealer_card_count, dealer_score)) {
        must_stand = false;
    }

    if (must_stand) {
        if (is_triple_sevens(game_state->dealer_cards, game_state->dealer_card_count)) {
            enqueue(&game_state->queue_state, game_state, noop_action, NULL, to_dealer_777_state,
                    game_state->settings.message_duration);
        }
        if (game_state->split) {
            enqueue(&(game_state->queue_state), game_state, splitPayout, NULL, NULL, 0);
        } else if (dealer_score > 21 || dealer_score < player_score) {
            enqueue(&(game_state->queue_state), game_state, win, NULL, to_win_state,
                    game_state->settings.message_duration);
        } else if (dealer_score > player_score) {
            enqueue(&(game_state->queue_state), game_state, lose, NULL, to_lose_state,
                    game_state->settings.message_duration);
        } else if (dealer_score == player_score) {
            enqueue(&(game_state->queue_state), game_state, draw, NULL, to_draw_state,
                    game_state->settings.message_duration);
        }
    } else {
        enqueue(&game_state->queue_state, game_state, drawDealerCard, NULL, dealer_card_animation,
                game_state->settings.animation_duration);
    }
}

void settings_tick(GameState *game_state) {
    if (game_state->selectDirection == DirectionDown && game_state->selectedMenu < 9)
        game_state->selectedMenu++;
    if (game_state->selectDirection == DirectionUp && game_state->selectedMenu > 0)
        game_state->selectedMenu--;

    // OK cycles step size for money settings (rows 0–1)
    if (game_state->selectDirection == Select && game_state->selectedMenu <= 1) {
        if (game_state->settings_step == 500)      game_state->settings_step = 100;
        else if (game_state->settings_step == 100) game_state->settings_step = 10;
        else                                        game_state->settings_step = 500;
    }

    if (game_state->selectDirection == DirectionLeft || game_state->selectDirection == DirectionRight) {
        bool right = (game_state->selectDirection == DirectionRight);
        int step = (int)game_state->settings_step;
        int v = 0;
        switch (game_state->selectedMenu) {
            case 0: // Start money
                v = (int)game_state->settings.starting_money + (right ? step : -step);
                if (v > 10000) v = 10000;
                if (v >= (int)game_state->settings.round_price)
                    game_state->settings.starting_money = (uint32_t)v;
                break;
            case 1: // Round price
                v = (int)game_state->settings.round_price + (right ? step : -step);
                if (v > (int)game_state->settings.starting_money) v = (int)game_state->settings.starting_money;
                if (v >= 5) game_state->settings.round_price = (uint32_t)v;
                break;
            case 2: // Num decks
                v = (int)game_state->settings.num_decks + (right ? 1 : -1);
                if (v >= 1 && v <= 8) game_state->settings.num_decks = (uint8_t)v;
                break;
            case 3: // BJ payout (cycle)
                game_state->settings.blackjack_payout =
                    (uint8_t)((game_state->settings.blackjack_payout + (right ? 1 : 2)) % 3);
                break;
            case 4: // Anim length
                v = (int)game_state->settings.animation_duration + (right ? 100 : -100);
                if (v >= 0 && v < 2000) game_state->settings.animation_duration = (uint32_t)v;
                break;
            case 5: // Popup time
                v = (int)game_state->settings.message_duration + (right ? 100 : -100);
                if (v >= 0 && v < 2000) game_state->settings.message_duration = (uint32_t)v;
                break;
            case 6: // Dealer hits soft 17
                game_state->settings.dealer_hits_soft17 = !game_state->settings.dealer_hits_soft17;
                break;
            case 7: // Re-split aces
                game_state->settings.resplit_aces = !game_state->settings.resplit_aces;
                break;
            case 8: // Hit after ace split
                game_state->settings.hit_after_ace_split = !game_state->settings.hit_after_ace_split;
                break;
            case 9: // Sound effects
                game_state->settings.sound_effects = !game_state->settings.sound_effects;
                break;
            default:
                break;
        }
    }
}

void tick(GameState *game_state) {
    game_state->last_tick = furi_get_tick();
    bool queue_ran = run_queue(&(game_state->queue_state), game_state);

    switch (game_state->state) {
        case GameStateGameOver:
        case GameStateStart:
            if (game_state->selectDirection == Select)
                init(game_state);
            else if (game_state->selectDirection == DirectionRight) {
                game_state->selectedMenu = 0;
                game_state->state = GameStateSettings;
            }
            break;
        case GameStateBet: {
            uint32_t step = game_state->settings_step;
            uint32_t cur  = game_state->settings.round_price;
            if (game_state->selectDirection == DirectionLeft && cur > step)
                game_state->settings.round_price = cur - step;
            if (game_state->selectDirection == DirectionRight) {
                uint32_t next = cur + step;
                if (next <= game_state->player_score)
                    game_state->settings.round_price = next;
                else if (cur < game_state->player_score)
                    game_state->settings.round_price = game_state->player_score;
            }
            if (game_state->selectDirection == Select) {
                bool all_in = (game_state->settings.round_price >= game_state->player_score);
                game_state->bet = game_state->settings.round_price;
                game_state->player_score -= game_state->bet;
                game_state->state = GameStatePlay;
                game_state->started = true;
                game_state->queue_state.running = true;
                if (game_state->new_shoe) {
                    enqueue(&(game_state->queue_state), game_state, noop_action, NULL, to_new_shoe_state,
                            game_state->settings.message_duration);
                    game_state->new_shoe = false;
                }
                if (all_in) {
                    enqueue(&(game_state->queue_state), game_state, start_allin_deal, NULL, to_allin_state,
                            game_state->settings.message_duration);
                } else {
                    enqueue(&(game_state->queue_state), game_state, drawDealerCard, NULL, dealer_back_card_animation,
                            game_state->settings.animation_duration);
                    enqueue(&(game_state->queue_state), game_state, drawPlayerCard, NULL, player_card_animation,
                            game_state->settings.animation_duration);
                    enqueue(&(game_state->queue_state), game_state, drawDealerCard, NULL, dealer_card_animation,
                            game_state->settings.animation_duration);
                    enqueue(&(game_state->queue_state), game_state, drawPlayerCard, NULL, player_card_animation,
                            game_state->settings.animation_duration);
                }
                save_settings(game_state->settings);
            }
            break;
        }
        case GameStateInsurance: {
            if (game_state->selectDirection == DirectionLeft ||
                game_state->selectDirection == DirectionRight ||
                game_state->selectDirection == DirectionUp ||
                game_state->selectDirection == DirectionDown) {
                game_state->insurance_selected ^= 1;
            }
            if (game_state->selectDirection == Select) {
                if (game_state->insurance_selected) {
                    game_state->insurance_bet = game_state->bet / 2;
                    game_state->player_score -= game_state->insurance_bet;
                }
                // Peek at dealer hole card
                if (is_natural_blackjack(game_state->dealer_cards, game_state->dealer_card_count)) {
                    game_state->state = GameStateDealer;
                    enqueue(&game_state->queue_state, game_state, dealer_blackjack_action, NULL,
                            to_dealer_blackjack_state, game_state->settings.message_duration);
                } else {
                    game_state->insurance_bet = 0; // lost
                    game_state->state = GameStatePlay;
                }
            }
            break;
        }
        case GameStatePlay:
            if (!queue_ran)
                player_tick(game_state);
            break;
        case GameStateDealer:
            if (!queue_ran)
                dealer_tick(game_state);
            break;
        case GameStateSettings:
            settings_tick(game_state);
            break;
        default:
            break;
    }

    game_state->selectDirection = None;
}

void start_round(GameState *game_state) {
    game_state->menu->current_menu = MENU_HIT;
    game_state->player_card_count = 0;
    game_state->dealer_card_count = 0;
    set_menu_state(game_state->menu, MENU_DOUBLE, true);
    set_menu_state(game_state->menu, MENU_SPLIT, false);
    game_state->menu->enabled = true;
    game_state->started = false;
    game_state->doubled = false;
    game_state->split = false;
    game_state->num_hands = 1;
    game_state->current_hand = 0;
    memset(game_state->split_hand_counts, 0, sizeof(game_state->split_hand_counts));
    memset(game_state->split_hand_bets, 0, sizeof(game_state->split_hand_bets));
    memset(game_state->hand_busted, 0, sizeof(game_state->hand_busted));
    memset(game_state->ace_split_hands, 0, sizeof(game_state->ace_split_hands));
    game_state->insurance_bet = 0;
    game_state->insurance_offered = false;
    game_state->insurance_selected = 0;
    int remaining = game_state->deck.card_count - game_state->deck.index;
    bool was_used = game_state->deck.card_count > 0;
    if (remaining < 20) {
        free(game_state->deck.cards);
        game_state->deck.cards = NULL;
        generate_deck(&game_state->deck, game_state->settings.num_decks);
        shuffle_deck(&game_state->deck);
        game_state->new_shoe = was_used;
    } else {
        game_state->new_shoe = false;
    }
    // Clamp last bet to what the player can actually afford
    if (game_state->settings.round_price > game_state->player_score)
        game_state->settings.round_price = game_state->player_score;
    if (game_state->player_score == 0) {
        game_state->state = GameStateGameOver;
    } else {
        game_state->state = GameStateBet;
    }
}

void init(GameState *game_state) {
    set_menu_state(game_state->menu, MENU_DOUBLE, true);
    game_state->menu->enabled = true;
    game_state->menu->current_menu = MENU_HIT;
    game_state->settings = load_settings();
    game_state->last_tick = 0;
    game_state->processing = true;
    game_state->selectedMenu = 0;
    game_state->settings_step = 500;
    game_state->player_score = game_state->settings.starting_money;
    game_state->deck.cards = NULL;
    game_state->deck.card_count = 0;
    game_state->deck.index = 0;
    game_state->new_shoe = false;
    start_round(game_state);
}

static void input_callback(InputEvent *input_event, void *ctx) {
    FuriMessageQueue *event_queue = ctx;
    furi_assert(event_queue);
    AppEvent event = {.type = EventTypeKey, .input = *input_event};
    furi_message_queue_put(event_queue, &event, FuriWaitForever);
}

static void update_timer_callback(void *ctx) {
    FuriMessageQueue *event_queue = ctx;
    furi_assert(event_queue);
    AppEvent event = {.type = EventTypeTick};
    furi_message_queue_put(event_queue, &event, 0);
}

void doubleAction(void *state) {
    GameState *game_state = state;
    if (!game_state->doubled && game_state->player_score >= game_state->settings.round_price) {
        game_state->player_score -= game_state->settings.round_price;
        game_state->bet += game_state->settings.round_price;
        game_state->doubled = true;
        enqueue(&(game_state->queue_state), game_state, drawPlayerCard, NULL, player_card_animation,
                game_state->settings.animation_duration);
        game_state->player_cards[game_state->player_card_count] = game_state->deck.cards[game_state->deck.index];
        uint8_t score = hand_count(game_state->player_cards, game_state->player_card_count + 1);
        if (score > 21) {
            enqueue(&(game_state->queue_state), game_state, lose, NULL, to_bust_state,
                    game_state->settings.message_duration);
        } else {
            enqueue(&(game_state->queue_state), game_state, dealerTurn, NULL, to_dealer_turn,
                    game_state->settings.message_duration);
        }
        set_menu_state(game_state->menu, MENU_DOUBLE, false);
    }
}

// Split the current two-card hand into two independent hands (up to 4 total).
void splitAction(void *state) {
    GameState *gs = state;

    Card *cards = curr_cards(gs);
    uint8_t *cnt = curr_count_ptr(gs);

    if (gs->num_hands >= 4 ||
        *cnt != 2 ||
        card_bj_value(cards[0].character) != card_bj_value(cards[1].character) ||
        gs->player_score < gs->settings.round_price) {
        return;
    }

    gs->player_score -= gs->settings.round_price;

    uint8_t new_idx = gs->num_hands - 1; // 0-indexed slot in split_hands
    gs->split_hand_bets[new_idx] = gs->settings.round_price;

    // Move second card of current hand to the new hand
    gs->split_hands[new_idx][0] = cards[1];
    gs->split_hand_counts[new_idx] = 1;
    (*cnt)--;

    bool splitting_aces = (cards[0].character == 12);
    gs->split = true;
    gs->num_hands++;

    if (splitting_aces) {
        gs->ace_split_hands[gs->current_hand] = true;
        gs->ace_split_hands[gs->num_hands - 1] = true;
    }

    set_menu_state(gs->menu, MENU_DOUBLE, false);
    set_menu_state(gs->menu, MENU_SPLIT, false);

    enqueue(&gs->queue_state, gs, drawPlayerCard, NULL, player_card_animation,
            gs->settings.animation_duration);
    enqueue(&gs->queue_state, gs, drawSplitInitCard, NULL, NULL, 0);
}

void hitAction(void *state) {
    GameState *gs = state;
    enqueue(&gs->queue_state, gs, drawPlayerCard, NULL, player_card_animation,
            gs->settings.animation_duration);
}

void stayAction(void *state) {
    GameState *gs = state;
    if (gs->split && gs->current_hand < gs->num_hands - 1) {
        enqueue(&gs->queue_state, gs, advanceToNextHand, NULL, to_handN_state,
                gs->settings.message_duration);
    } else {
        enqueue(&gs->queue_state, gs, dealerTurn, NULL, to_dealer_turn,
                gs->settings.message_duration);
    }
}

int32_t blackjack_app(void *p) {
    UNUSED(p);

    int32_t return_code = 0;

    FuriMessageQueue *event_queue = furi_message_queue_alloc(8, sizeof(AppEvent));
    dolphin_deed(DolphinDeedPluginGameStart);
    GameState *game_state = malloc(sizeof(GameState));
    game_state->menu = malloc(sizeof(Menu));
    game_state->menu->menu_width = 40;
    init(game_state);
    add_menu(game_state->menu, "Double", doubleAction); // MENU_DOUBLE = 0
    add_menu(game_state->menu, "Split", splitAction);   // MENU_SPLIT  = 1
    add_menu(game_state->menu, "Hit", hitAction);       // MENU_HIT    = 2
    add_menu(game_state->menu, "Stay", stayAction);     // MENU_STAY   = 3
    set_menu_state(game_state->menu, MENU_SPLIT, false); // Split disabled until valid deal
    set_card_graphics(&I_card_graphics);

    game_state->state = GameStateStart;

    game_state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    if (!game_state->mutex) {
        FURI_LOG_E(APP_NAME, "cannot create mutex\r\n");
        return_code = 255;
        goto free_and_exit;
    }

    ViewPort *view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, render_callback, game_state);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    FuriTimer *timer =
        furi_timer_alloc(update_timer_callback, FuriTimerTypePeriodic, event_queue);
    furi_timer_start(timer, furi_kernel_get_tick_frequency() / 25);

    Gui *gui = furi_record_open("gui");
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    AppEvent event;

    for (bool processing = true; processing;) {
        FuriStatus event_status = furi_message_queue_get(event_queue, &event, 100);
        furi_mutex_acquire(game_state->mutex, FuriWaitForever);
        if (event_status == FuriStatusOk) {
            if (event.type == EventTypeKey) {
                if (event.input.type == InputTypePress) {
                    switch (event.input.key) {
                        case InputKeyUp:
                            game_state->selectDirection = DirectionUp;
                            break;
                        case InputKeyDown:
                            game_state->selectDirection = DirectionDown;
                            break;
                        case InputKeyRight:
                            game_state->selectDirection = DirectionRight;
                            break;
                        case InputKeyLeft:
                            game_state->selectDirection = DirectionLeft;
                            break;
                        case InputKeyBack:
                            if (game_state->state == GameStateSettings) {
                                game_state->state = GameStateStart;
                                save_settings(game_state->settings);
                            } else
                                processing = false;
                            break;
                        case InputKeyOk:
                            game_state->selectDirection = Select;
                            break;
                        default:
                            break;
                    }
                }
            } else if (event.type == EventTypeTick) {
                tick(game_state);
                processing = game_state->processing;
            }
        } else {
            FURI_LOG_D(APP_NAME, "osMessageQueue: event timeout");
        }
        view_port_update(view_port);
        furi_mutex_release(game_state->mutex);
    }

    furi_timer_free(timer);
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_mutex_free(game_state->mutex);

    free_and_exit:
    free(game_state->deck.cards);
    free_menu(game_state->menu);
    queue_clear(&(game_state->queue_state));
    free(game_state);
    furi_message_queue_free(event_queue);

    return return_code;
}
