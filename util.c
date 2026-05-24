#include <storage/storage.h>
#include "util.h"

const char *CONFIG_FILE_PATH = EXT_PATH(".blackjack.settings");

void save_settings_file(FlipperFormat *file, Settings *settings) {
    flipper_format_write_header_cstr(file, CONFIG_FILE_HEADER, CONFIG_FILE_VERSION);

    flipper_format_write_comment_cstr(file, "Card animation duration in ms");
    flipper_format_write_uint32(file, CONF_ANIMATION_DURATION, &(settings->animation_duration), 1);
    flipper_format_write_comment_cstr(file, "Popup message duration in ms");
    flipper_format_write_uint32(file, CONF_MESSAGE_DURATION, &(settings->message_duration), 1);
    flipper_format_write_comment_cstr(file, "Player's starting money");
    flipper_format_write_uint32(file, CONF_STARTING_MONEY, &(settings->starting_money), 1);
    flipper_format_write_comment_cstr(file, "Round price");
    flipper_format_write_uint32(file, CONF_ROUND_PRICE, &(settings->round_price), 1);
    flipper_format_write_comment_cstr(file, "Enable sound effects");
    flipper_format_write_bool(file, CONF_SOUND_EFFECTS, &(settings->sound_effects), 1);

    uint32_t v;
    flipper_format_write_comment_cstr(file, "Number of decks (1-8)");
    v = settings->num_decks;
    flipper_format_write_uint32(file, CONF_NUM_DECKS, &v, 1);
    flipper_format_write_comment_cstr(file, "Blackjack payout: 0=3:2, 1=6:5, 2=1:1");
    v = settings->blackjack_payout;
    flipper_format_write_uint32(file, CONF_BJ_PAYOUT, &v, 1);
    flipper_format_write_comment_cstr(file, "Dealer hits soft 17");
    flipper_format_write_bool(file, CONF_DEALER_HITS_SOFT17, &(settings->dealer_hits_soft17), 1);
    flipper_format_write_comment_cstr(file, "Allow re-splitting aces");
    flipper_format_write_bool(file, CONF_RESPLIT_ACES, &(settings->resplit_aces), 1);
    flipper_format_write_comment_cstr(file, "Allow hitting after ace split");
    flipper_format_write_bool(file, CONF_HIT_AFTER_ACE_SPLIT, &(settings->hit_after_ace_split), 1);
}

void save_settings(Settings settings) {
    Storage *storage = furi_record_open(RECORD_STORAGE);
    storage_simply_remove(storage, CONFIG_FILE_PATH);
    FlipperFormat *file = flipper_format_file_alloc(storage);
    if (flipper_format_file_open_new(file, CONFIG_FILE_PATH)) {
        save_settings_file(file, &settings);
    } else {
        FURI_LOG_E(APP_NAME, "Save error");
    }
    flipper_format_file_close(file);
    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);
}

Settings load_settings() {
    Settings settings;

    settings.animation_duration  = 800;
    settings.message_duration    = 1500;
    settings.starting_money      = 200;
    settings.round_price         = 10;
    settings.sound_effects       = true;
    settings.num_decks           = 6;
    settings.blackjack_payout    = 0;
    settings.dealer_hits_soft17  = false;
    settings.resplit_aces        = true;
    settings.hit_after_ace_split = true;

    Storage *storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat *file = flipper_format_file_alloc(storage);
    FuriString *string_value = furi_string_alloc();

    if (storage_common_stat(storage, CONFIG_FILE_PATH, NULL) != FSE_OK) {
        FURI_LOG_D(APP_NAME, "Config not found, creating...");
        if (flipper_format_file_open_new(file, CONFIG_FILE_PATH)) {
            save_settings_file(file, &settings);
        }
    } else {
        if (flipper_format_file_open_existing(file, CONFIG_FILE_PATH)) {
            uint32_t value;
            bool valueBool;
            if (flipper_format_read_header(file, string_value, &value)) {
                if (flipper_format_read_uint32(file, CONF_ANIMATION_DURATION, &value, 1))
                    settings.animation_duration = value;
                if (flipper_format_read_uint32(file, CONF_MESSAGE_DURATION, &value, 1))
                    settings.message_duration = value;
                if (flipper_format_read_uint32(file, CONF_STARTING_MONEY, &value, 1))
                    settings.starting_money = value;
                if (flipper_format_read_uint32(file, CONF_ROUND_PRICE, &value, 1))
                    settings.round_price = value;
                if (flipper_format_read_bool(file, CONF_SOUND_EFFECTS, &valueBool, 1))
                    settings.sound_effects = valueBool;
                if (flipper_format_read_uint32(file, CONF_NUM_DECKS, &value, 1))
                    settings.num_decks = (uint8_t)value;
                if (flipper_format_read_uint32(file, CONF_BJ_PAYOUT, &value, 1))
                    settings.blackjack_payout = (uint8_t)value;
                if (flipper_format_read_bool(file, CONF_DEALER_HITS_SOFT17, &valueBool, 1))
                    settings.dealer_hits_soft17 = valueBool;
                if (flipper_format_read_bool(file, CONF_RESPLIT_ACES, &valueBool, 1))
                    settings.resplit_aces = valueBool;
                if (flipper_format_read_bool(file, CONF_HIT_AFTER_ACE_SPLIT, &valueBool, 1))
                    settings.hit_after_ace_split = valueBool;
            }
            flipper_format_file_close(file);
        }
    }

    furi_string_free(string_value);
    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);
    return settings;
}
