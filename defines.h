#pragma once

#include <furi.h>
#include <input/input.h>
#include <gui/elements.h>
#include <flipper_format/flipper_format.h>
#include <flipper_format/flipper_format_i.h>
#include "common/card.h"
#include "common/queue.h"
#include "common/menu.h"

#define APP_NAME "Blackjack"

#define CONF_ANIMATION_DURATION "AnimationDuration"
#define CONF_MESSAGE_DURATION "MessageDuration"
#define CONF_STARTING_MONEY "StartingMoney"
#define CONF_ROUND_PRICE "RoundPrice"
#define CONF_SOUND_EFFECTS "SoundEffects"
#define CONF_NUM_DECKS "NumDecks"
#define CONF_BJ_PAYOUT "BjPayout"
#define CONF_DEALER_HITS_SOFT17 "DealerHitsSoft17"
#define CONF_RESPLIT_ACES "ResplitAces"
#define CONF_HIT_AFTER_ACE_SPLIT "HitAfterAceSplit"

typedef enum {
    EventTypeTick,
    EventTypeKey,
} EventType;

typedef struct{
    uint32_t animation_duration;
    uint32_t message_duration;
    uint32_t starting_money;
    uint32_t round_price;
    bool sound_effects;
    uint8_t num_decks;          // 1–8
    uint8_t blackjack_payout;   // 0=3:2, 1=6:5, 2=1:1
    bool dealer_hits_soft17;
    bool resplit_aces;
    bool hit_after_ace_split;
} Settings;

typedef struct {
    EventType type;
    InputEvent input;
} AppEvent;

typedef enum {
    GameStateGameOver,
    GameStateStart,
    GameStateBet,
    GameStatePlay,
    GameStateInsurance,
    GameStateSettings,
    GameStateDealer,
} PlayState;

typedef enum {
    DirectionUp,
    DirectionDown,
    DirectionRight,
    DirectionLeft,
    Select,
    Back,
    None
} Direction;

typedef struct {
    Card player_cards[21];
    Card dealer_cards[21];
    uint8_t player_card_count;
    uint8_t dealer_card_count;

    Direction selectDirection;
    Settings settings;

    uint32_t player_score;
    uint32_t bet;
    uint8_t selectedMenu;
    bool doubled;
    bool started;
    bool processing;

    uint32_t settings_step;         // money setting increment: 10, 100, or 500
    uint32_t last_win_amount;       // total profit stored by splitPayout for result popup
    bool split;
    bool ace_split_hands[4];        // true if hand[i] was created by splitting aces
    bool new_shoe;                  // true when a fresh shoe was started this round
    uint8_t num_hands;              // 1–4: total hands in play
    uint8_t current_hand;           // 0 = player_cards, 1–3 = split_hands[0..2]
    Card split_hands[3][21];
    uint8_t split_hand_counts[3];
    uint32_t split_hand_bets[3];
    bool hand_busted[4];            // bust flag per hand
    uint32_t insurance_bet;       // amount of insurance taken (0 = none)
    bool insurance_offered;       // true once the insurance check has run
    uint8_t insurance_selected;   // 0 = No, 1 = Yes
    Deck deck;
    PlayState state;
    QueueState queue_state;
    Menu *menu;
    unsigned int last_tick;
    FuriMutex* mutex;
} GameState;

