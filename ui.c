#include <math.h>
#include <notification/notification_messages.h>

#include "ui.h"

#define LINE_HEIGHT 16
#define ITEM_PADDING 4

const char MoneyMul[4] = {
        'K', 'B', 'T', 'S'
};

void draw_player_scene(Canvas *const canvas, const GameState *game_state) {
    uint8_t h = game_state->current_hand;
    const Card *cards = (h == 0) ? game_state->player_cards : game_state->split_hands[h - 1];
    uint8_t count = (h == 0) ? game_state->player_card_count : game_state->split_hand_counts[h - 1];
    if (count > 0)
        draw_deck(cards, count, canvas);

    if (game_state->dealer_card_count > 0)
        draw_card_back_at(13, 5, canvas);

    if (game_state->dealer_card_count > 1) {
        draw_card_at(2, 2, game_state->dealer_cards[1].pip, game_state->dealer_cards[1].character,
                   canvas);
    }
}

void draw_dealer_scene(Canvas *const canvas, const GameState *game_state) {
    uint8_t max_card = game_state->dealer_card_count;
    draw_deck((game_state->dealer_cards), max_card, canvas);
}

void popup_frame(Canvas *const canvas) {
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 32, 15, 66, 13);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 32, 15, 66, 13);
    canvas_set_font(canvas, FontSecondary);
}


void draw_play_menu(Canvas *const canvas, const GameState *game_state) {
    const char *menus[3] = {"Double", "Hit", "Stay"};
    for (uint8_t m = 0; m < 3; m++) {
        if (m == 0 && (game_state->doubled || game_state->player_score < game_state->settings.round_price)) continue;
        int y = m * 13 + 25;
        canvas_set_color(canvas, ColorBlack);

        if (game_state->selectedMenu == m) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 1, y, 31, 12);
        } else {
            canvas_set_color(canvas, ColorWhite);
            canvas_draw_box(canvas, 1, y, 31, 12);
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_frame(canvas, 1, y, 31, 12);
        }

        if (game_state->selectedMenu == m)
            canvas_set_color(canvas, ColorWhite);
        else
            canvas_set_color(canvas, ColorBlack);
        canvas_draw_str_aligned(canvas, 16, y + 6, AlignCenter, AlignCenter, menus[m]);
    }
}

void draw_screen(Canvas *const canvas, const bool *points) {
    for (uint8_t x = 0; x < 128; x++) {
        for (uint8_t y = 0; y < 64; y++) {
            if (points[y * 128 + x])
                canvas_draw_dot(canvas, x, y);
        }
    }
}

void draw_score(Canvas *const canvas, bool top, uint8_t amount, bool soft) {
    char drawChar[22];
    if (soft)
        snprintf(drawChar, sizeof(drawChar), "Player score: S%i", amount);
    else
        snprintf(drawChar, sizeof(drawChar), "Player score: %i", amount);
    if (top)
        canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, drawChar);
    else
        canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, drawChar);
}

void draw_money(Canvas *const canvas, uint32_t score) {
    canvas_set_font(canvas, FontSecondary);
    char drawChar[10];
    if (score < 1000) {
        snprintf(drawChar, sizeof(drawChar), "$%lu", (unsigned long)score);
    } else if (score < 1000000) {
        uint32_t whole = score / 1000;
        uint32_t frac  = (score % 1000) / 100;
        if (frac == 0) snprintf(drawChar, sizeof(drawChar), "$%luk", (unsigned long)whole);
        else           snprintf(drawChar, sizeof(drawChar), "$%lu.%luk", (unsigned long)whole, (unsigned long)frac);
    } else {
        uint32_t whole = score / 1000000;
        uint32_t frac  = (score % 1000000) / 100000;
        if (frac == 0) snprintf(drawChar, sizeof(drawChar), "$%lum", (unsigned long)whole);
        else           snprintf(drawChar, sizeof(drawChar), "$%lu.%lum", (unsigned long)whole, (unsigned long)frac);
    }
    canvas_draw_str_aligned(canvas, 126, 2, AlignRight, AlignTop, drawChar);
}


void draw_menu(Canvas *const canvas, const char *text, const char *value, int8_t y, bool left_caret, bool right_caret,
               bool selected) {
    UNUSED(selected);
    if (y < 0 || y >= 64) return;

    if (selected) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, y, 122, LINE_HEIGHT);
        canvas_set_color(canvas, ColorWhite);
    }

    canvas_draw_str_aligned(canvas, 4, y + ITEM_PADDING, AlignLeft, AlignTop, text);
    if (left_caret)
        canvas_draw_str_aligned(canvas, 80, y + ITEM_PADDING, AlignLeft, AlignTop, "<");
    canvas_draw_str_aligned(canvas, 100, y + ITEM_PADDING, AlignCenter, AlignTop, value);
    if (right_caret)
        canvas_draw_str_aligned(canvas, 120, y + ITEM_PADDING, AlignRight, AlignTop, ">");

    canvas_set_color(canvas, ColorBlack);
}

#define SETTINGS_COUNT 10

void settings_page(Canvas *const canvas, const GameState *gameState) {
    char drawChar[10];
    int startY = 0;
    if (gameState->selectedMenu <= 1) {
        startY = 10;
    } else if (LINE_HEIGHT * (gameState->selectedMenu + 1) >= 64) {
        startY -= (LINE_HEIGHT * (gameState->selectedMenu + 1)) - 64;
    }

    int scrollHeight = (int)round(64.0 / SETTINGS_COUNT) + ITEM_PADDING * 2;
    int scrollPos    = (int)(64.0 * (gameState->selectedMenu + 1) / SETTINGS_COUNT) - ITEM_PADDING * 2;

    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 123, scrollPos, 4, scrollHeight);
    canvas_draw_box(canvas, 125, 0, 1, 64);

    if (gameState->selectedMenu <= 1) {
        char stepBuf[10];
        snprintf(stepBuf, sizeof(stepBuf), "OK:%lu", gameState->settings_step);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 121, 1, AlignRight, AlignTop, stepBuf);
    }

    // Row 0 — Start money
    snprintf(drawChar, sizeof(drawChar), "%li", gameState->settings.starting_money);
    draw_menu(canvas, "Start money", drawChar,
              0 * LINE_HEIGHT + startY,
              gameState->settings.starting_money > gameState->settings.round_price,
              gameState->settings.starting_money < 10000,
              gameState->selectedMenu == 0);

    // Row 1 — Round price
    snprintf(drawChar, sizeof(drawChar), "%li", gameState->settings.round_price);
    draw_menu(canvas, "Round price", drawChar,
              1 * LINE_HEIGHT + startY,
              gameState->settings.round_price > 10,
              gameState->settings.round_price < gameState->settings.starting_money,
              gameState->selectedMenu == 1);

    // Row 2 — Num decks
    snprintf(drawChar, sizeof(drawChar), "%d", gameState->settings.num_decks);
    draw_menu(canvas, "Num decks", drawChar,
              2 * LINE_HEIGHT + startY,
              gameState->settings.num_decks > 1,
              gameState->settings.num_decks < 8,
              gameState->selectedMenu == 2);

    // Row 3 — BJ payout
    const char *payout_str = (gameState->settings.blackjack_payout == 1) ? "6:5" :
                             (gameState->settings.blackjack_payout == 2) ? "1:1" : "3:2";
    draw_menu(canvas, "BJ pays", payout_str,
              3 * LINE_HEIGHT + startY,
              true, true,
              gameState->selectedMenu == 3);

    // Row 4 — Anim length
    snprintf(drawChar, sizeof(drawChar), "%li", gameState->settings.animation_duration);
    draw_menu(canvas, "Anim. length", drawChar,
              4 * LINE_HEIGHT + startY,
              gameState->settings.animation_duration > 0,
              gameState->settings.animation_duration < 2000,
              gameState->selectedMenu == 4);

    // Row 5 — Popup time
    snprintf(drawChar, sizeof(drawChar), "%li", gameState->settings.message_duration);
    draw_menu(canvas, "Popup time", drawChar,
              5 * LINE_HEIGHT + startY,
              gameState->settings.message_duration > 0,
              gameState->settings.message_duration < 2000,
              gameState->selectedMenu == 5);

    // Row 6 — Dealer hits soft 17
    draw_menu(canvas, "Soft 17", gameState->settings.dealer_hits_soft17 ? "Hit" : "Std",
              6 * LINE_HEIGHT + startY,
              true, true,
              gameState->selectedMenu == 6);

    // Row 7 — Re-split aces
    draw_menu(canvas, "Resplit aces", gameState->settings.resplit_aces ? "Yes" : "No",
              7 * LINE_HEIGHT + startY,
              true, true,
              gameState->selectedMenu == 7);

    // Row 8 — Hit after ace split
    draw_menu(canvas, "Hit on ace", gameState->settings.hit_after_ace_split ? "Yes" : "No",
              8 * LINE_HEIGHT + startY,
              true, true,
              gameState->selectedMenu == 8);

    // Row 9 — Sound effects
    draw_menu(canvas, "Sound FX", gameState->settings.sound_effects ? "On" : "Off",
              9 * LINE_HEIGHT + startY,
              true, true,
              gameState->selectedMenu == 9);
}