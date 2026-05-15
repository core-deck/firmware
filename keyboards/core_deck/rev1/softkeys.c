// Copyright 2024 vden
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "keymap_introspection.h"
#include "os_detection.h"
#include "softkeys.h"
#include "display.h"
#include "protocol.h"
#include <stdio.h>

#ifdef DYNAMIC_KEYMAP_ENABLE
#    include "dynamic_keymap.h"
#endif

/* Global config instance */
kb_config_t kb_config;

/* Soft key matrix positions: row 0, cols 0-2 */
#define SOFTKEY_ROW 0
#define SOFTKEY_COL_START 0

/* Hold-to-peek state machine */
typedef enum {
    HOLD_IDLE,
    HOLD_WAITING,
    HOLD_PEEKING,
} hold_state_t;

static hold_state_t hold_state = HOLD_IDLE;
static uint8_t hold_index = 0;
static uint32_t hold_start = 0;

/* Weak callbacks for keymap override */
__attribute__((weak)) bool softkeys_tap_default_user(uint8_t index) {
    return false;
}

__attribute__((weak)) bool softkeys_get_label_user(uint8_t index, char *buf, uint8_t buf_size) {
    return false;
}

__attribute__((weak)) void softkeys_apply_defaults_user(void) {
    /* No custom defaults — all keys use keymap keycodes */
}

void softkeys_init(void) {
    eeconfig_read_kb_datablock(&kb_config, 0, sizeof(kb_config));

    /* Detect virgin EEPROM: all softkey types are SOFTKEY_DEFAULT (0) */
    bool all_default = true;
    for (uint8_t i = 0; i < SOFTKEY_COUNT; i++) {
        if (kb_config.softkeys[i].type != SOFTKEY_DEFAULT) {
            all_default = false;
            break;
        }
    }
    if (all_default) {
        softkeys_apply_defaults_user();
        softkeys_save();
        dprintf("Softkeys: applied firmware defaults\n");
    }

    /* Theme migration: a dedicated version byte distinguishes a populated
     * theme block from both zero-filled and flash-erased EEPROM regions. */
    if (kb_config.theme_version != THEME_CONFIG_VERSION) {
        theme_apply_defaults();
        softkeys_save();
        dprintf("Theme: applied firmware defaults (version was 0x%02X)\n",
                kb_config.theme_version);
    }

    dprintf("Softkeys: loaded from EEPROM (bl=%d)\n", kb_config.backlight_level);
    for (uint8_t i = 0; i < SOFTKEY_COUNT; i++) {
        dprintf("  key[%d]: type=%d\n", i, kb_config.softkeys[i].type);
    }
}

void softkeys_save(void) {
    eeconfig_update_kb_datablock(&kb_config, 0, sizeof(kb_config));
    dprintf("Softkeys: saved to EEPROM\n");
}

/**
 * @brief Fire the tap action for a softkey
 *
 * When the companion app is connected, routes actions via Raw HID
 * (0x11 for strings, 0x12 for keycodes) so the host app can deliver
 * them to the correct terminal regardless of window focus or keyboard layout.
 */
static void softkey_fire_tap(uint8_t index) {
    softkey_entry_t *entry = &kb_config.softkeys[index];
    bool routed = display_is_connected();

    switch (entry->type) {
        case SOFTKEY_DEFAULT:
            /* Callback handles its own routing (sees display_is_connected()) */
            if (!softkeys_tap_default_user(index)) {
                uint16_t kc = softkeys_get_keymap_keycode(index);
                if (routed) {
                    send_key_event(kc);
                } else {
                    tap_code16(kc);
                }
            }
            break;

        case SOFTKEY_KEYCODE: {
            uint16_t kc = ((uint16_t)entry->data[0] << 8) | entry->data[1];
            if (routed) {
                send_key_event(kc);
            } else {
                tap_code16(kc);
            }
            break;
        }

        case SOFTKEY_STRING: {
            uint8_t flags = entry->data[0];
            if (routed) {
                send_type_string(flags & (uint8_t)~SOFTKEY_STRING_FLAG_ENTER, (const char *)&entry->data[1]);
                if (flags & SOFTKEY_STRING_FLAG_ENTER) {
                    send_key_event(KC_ENT);
                }
            } else {
                send_string((const char *)&entry->data[1]);
                if (flags & SOFTKEY_STRING_FLAG_ENTER) {
                    tap_code(KC_ENT);
                }
            }
            break;
        }

        case SOFTKEY_SEQUENCE: {
            uint8_t count = entry->data[0];
            if (count > SOFTKEY_SEQ_MAX_KEYS) {
                count = SOFTKEY_SEQ_MAX_KEYS;
            }
            for (uint8_t i = 0; i < count; i++) {
                uint16_t kc = ((uint16_t)entry->data[1 + i * 2] << 8) | entry->data[2 + i * 2];
                if (routed) {
                    send_key_event(kc);
                } else {
                    tap_code16(kc);
                    if (i < count - 1) {
                        wait_ms(SOFTKEY_SEQ_DELAY_MS);
                    }
                }
            }
            break;
        }
    }
}

bool softkeys_process_record(uint16_t keycode, keyrecord_t *record) {
    uint8_t row = record->event.key.row;
    uint8_t col = record->event.key.col;

    /* Only intercept row 0, cols 0-2 */
    if (row != SOFTKEY_ROW || col < SOFTKEY_COL_START || col > SOFTKEY_COL_START + SOFTKEY_COUNT - 1) {
        return true;
    }

    uint8_t index = col - SOFTKEY_COL_START;

#ifdef DYNAMIC_KEYMAP_ENABLE
    /* If the user remapped this key in VIAL, let QMK process it normally */
    {
        uint16_t compiled_kc = keycode_at_keymap_location_raw(0, row, col);
        uint16_t dynamic_kc  = dynamic_keymap_get_keycode(0, row, col);
        if (compiled_kc != dynamic_kc) {
            return true;
        }
    }
#endif

    if (record->event.pressed) {
        /* Cancel any existing hold state */
        if (hold_state == HOLD_PEEKING) {
            display_overlay_hide();
        }
        hold_state = HOLD_WAITING;
        hold_index = index;
        hold_start = timer_read32();
    } else {
        /* Only process release for the tracked key */
        if (index == hold_index) {
            if (hold_state == HOLD_WAITING) {
                softkey_fire_tap(index);
            } else if (hold_state == HOLD_PEEKING) {
                display_overlay_hide();
            }
            hold_state = HOLD_IDLE;
        }
    }

    return false;
}

void softkeys_task(void) {
    if (hold_state == HOLD_WAITING && timer_elapsed32(hold_start) >= SOFTKEY_HOLD_MS) {
        hold_state = HOLD_PEEKING;
        display_overlay_show();
    }
}

/**
 * @brief Convert a basic QMK keycode to a short human-readable label
 */
static void keycode_to_label(uint16_t keycode, char *buf, uint8_t buf_size) {
    /* QK_MODS range: modifier + basic keycode */
    if (keycode >= 0x0100 && keycode <= 0x1FFF) {
        uint8_t mods = (keycode >> 8) & 0x1F;
        uint16_t base = keycode & 0xFF;
        char prefix[24] = "";
        uint8_t pos = 0;
        os_variant_t os = detected_host_os();
        bool is_mac = (os == OS_MACOS || os == OS_IOS || os == OS_UNSURE);
        const char *alt_name = is_mac ? "Opt-" : "Alt-";
        const char *gui_name = is_mac ? "Cmd-" : "Win-";
        const char *mod_names[] = {"Ctrl-", "Shift-", alt_name, gui_name};
        for (uint8_t m = 0; m < 4; m++) {
            if (mods & (1 << m)) {
                const char *mn = mod_names[m];
                while (*mn) prefix[pos++] = *mn++;
            }
        }
        prefix[pos] = '\0';

        char base_name[16];
        keycode_to_label(base, base_name, sizeof(base_name));
        snprintf(buf, buf_size, "%s%s", prefix, base_name);
        return;
    }

    /* Letters */
    if (keycode >= KC_A && keycode <= KC_Z) {
        buf[0] = 'A' + (keycode - KC_A);
        buf[1] = '\0';
        return;
    }

    /* Digits */
    if (keycode >= KC_1 && keycode <= KC_9) {
        buf[0] = '1' + (keycode - KC_1);
        buf[1] = '\0';
        return;
    }
    if (keycode == KC_0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    /* F-keys */
    if (keycode >= KC_F1 && keycode <= KC_F12) {
        snprintf(buf, buf_size, "F%d", keycode - KC_F1 + 1);
        return;
    }
    if (keycode >= KC_F13 && keycode <= KC_F24) {
        snprintf(buf, buf_size, "F%d", keycode - KC_F13 + 13);
        return;
    }

    /* Common keys */
    switch (keycode) {
        case KC_ENT:  snprintf(buf, buf_size, "Enter"); return;
        case KC_ESC:  snprintf(buf, buf_size, "Esc"); return;
        case KC_BSPC: snprintf(buf, buf_size, "Bksp"); return;
        case KC_TAB:  snprintf(buf, buf_size, "Tab"); return;
        case KC_SPC:  snprintf(buf, buf_size, "Space"); return;
        case KC_DEL:  snprintf(buf, buf_size, "Del"); return;
        case KC_INS:  snprintf(buf, buf_size, "Ins"); return;
        case KC_HOME: snprintf(buf, buf_size, "Home"); return;
        case KC_END:  snprintf(buf, buf_size, "End"); return;
        case KC_PGUP: snprintf(buf, buf_size, "PgUp"); return;
        case KC_PGDN: snprintf(buf, buf_size, "PgDn"); return;
        case KC_UP:   snprintf(buf, buf_size, "Up"); return;
        case KC_DOWN: snprintf(buf, buf_size, "Down"); return;
        case KC_LEFT: snprintf(buf, buf_size, "Left"); return;
        case KC_RGHT: snprintf(buf, buf_size, "Right"); return;
        case KC_MINS: buf[0] = '-'; buf[1] = '\0'; return;
        case KC_EQL:  buf[0] = '='; buf[1] = '\0'; return;
        case KC_LBRC: buf[0] = '['; buf[1] = '\0'; return;
        case KC_RBRC: buf[0] = ']'; buf[1] = '\0'; return;
        case KC_BSLS: buf[0] = '\\'; buf[1] = '\0'; return;
        case KC_SCLN: buf[0] = ';'; buf[1] = '\0'; return;
        case KC_QUOT: buf[0] = '\''; buf[1] = '\0'; return;
        case KC_GRV:  buf[0] = '`'; buf[1] = '\0'; return;
        case KC_COMM: buf[0] = ','; buf[1] = '\0'; return;
        case KC_DOT:  buf[0] = '.'; buf[1] = '\0'; return;
        case KC_SLSH: buf[0] = '/'; buf[1] = '\0'; return;
        case KC_NO:   snprintf(buf, buf_size, "---"); return;
    }

    /* Unknown: hex fallback */
    snprintf(buf, buf_size, "0x%04X", keycode);
}

void softkeys_get_label(uint8_t index, char *buf, uint8_t buf_size) {
    if (index >= SOFTKEY_COUNT || buf_size == 0) {
        if (buf_size > 0) buf[0] = '\0';
        return;
    }

#ifdef DYNAMIC_KEYMAP_ENABLE
    /* If the user remapped this key in VIAL, show the VIAL keycode label
     * instead of any softkey-configured label. */
    {
        uint16_t compiled_kc = keycode_at_keymap_location_raw(0, SOFTKEY_ROW, SOFTKEY_COL_START + index);
        uint16_t dynamic_kc  = dynamic_keymap_get_keycode(0, SOFTKEY_ROW, SOFTKEY_COL_START + index);
        if (compiled_kc != dynamic_kc) {
            keycode_to_label(dynamic_kc, buf, buf_size);
            return;
        }
    }
#endif

    softkey_entry_t *entry = &kb_config.softkeys[index];

    switch (entry->type) {
        case SOFTKEY_DEFAULT:
            if (!softkeys_get_label_user(index, buf, buf_size)) {
                keycode_to_label(softkeys_get_keymap_keycode(index), buf, buf_size);
            }
            break;

        case SOFTKEY_KEYCODE: {
            uint16_t kc = ((uint16_t)entry->data[0] << 8) | entry->data[1];
            keycode_to_label(kc, buf, buf_size);
            break;
        }

        case SOFTKEY_STRING: {
            const char *str = (const char *)&entry->data[1];
            snprintf(buf, buf_size, "%s", str);
            break;
        }

        case SOFTKEY_SEQUENCE: {
            uint8_t count = entry->data[0];
            if (count > SOFTKEY_SEQ_MAX_KEYS) count = SOFTKEY_SEQ_MAX_KEYS;
            uint8_t pos = 0;
            for (uint8_t i = 0; i < count && pos < buf_size - 1; i++) {
                uint16_t kc = ((uint16_t)entry->data[1 + i * 2] << 8) | entry->data[2 + i * 2];
                char kn[16];
                keycode_to_label(kc, kn, sizeof(kn));
                uint8_t kn_len = strlen(kn);
                uint8_t need = (i > 0 ? 1 : 0) + kn_len;
                if (pos + need >= buf_size - 1) {
                    /* Truncate with "..." */
                    if (pos + 3 < buf_size) {
                        buf[pos++] = '.'; buf[pos++] = '.'; buf[pos++] = '.';
                    }
                    break;
                }
                if (i > 0) buf[pos++] = ' ';
                memcpy(&buf[pos], kn, kn_len);
                pos += kn_len;
            }
            buf[pos] = '\0';
            break;
        }

        default:
            buf[0] = '?';
            buf[1] = '\0';
            break;
    }
}

bool softkeys_set(uint8_t index, uint8_t type, const uint8_t *data, uint8_t data_len, bool save) {
    if (index >= SOFTKEY_COUNT) {
        return false;
    }

    /* Reset hold state if this key is being tracked */
    if (hold_state != HOLD_IDLE && hold_index == index) {
        if (hold_state == HOLD_PEEKING) {
            display_overlay_hide();
        }
        hold_state = HOLD_IDLE;
    }

    softkey_entry_t *entry = &kb_config.softkeys[index];

    /* Clear entry */
    memset(entry, 0, sizeof(softkey_entry_t));
    entry->type = type;

    if (type == SOFTKEY_KEYCODE && data && data_len >= 2) {
        entry->data[0] = data[0];
        entry->data[1] = data[1];
    } else if (type == SOFTKEY_STRING && data && data_len > 0) {
        uint8_t copy_len = data_len < SOFTKEY_DATA_LEN ? data_len : SOFTKEY_DATA_LEN - 1;
        memcpy(entry->data, data, copy_len);
        entry->data[copy_len] = '\0';
    } else if (type == SOFTKEY_SEQUENCE && data && data_len >= 3) {
        uint8_t count = data[0];
        if (count == 0 || count > SOFTKEY_SEQ_MAX_KEYS || data_len < 1 + count * 2) {
            /* Validation failed — reset entry to default */
            memset(entry, 0, sizeof(softkey_entry_t));
            return false;
        }
        uint8_t copy_len = 1 + count * 2;
        memcpy(entry->data, data, copy_len);
    }

    dprintf("Softkeys: set key[%d] type=%d\n", index, type);

    if (save) {
        softkeys_save();
    }

    return true;
}

const softkey_entry_t *softkeys_get(uint8_t index) {
    if (index >= SOFTKEY_COUNT) {
        return NULL;
    }
    return &kb_config.softkeys[index];
}

uint16_t softkeys_get_keymap_keycode(uint8_t index) {
    if (index >= SOFTKEY_COUNT) {
        return KC_NO;
    }
#ifdef DYNAMIC_KEYMAP_ENABLE
    /* With VIAL/VIA: read from the dynamic keymap (EEPROM) so VIAL
     * remaps are reflected in labels and companion app responses. */
    return dynamic_keymap_get_keycode(0, SOFTKEY_ROW, SOFTKEY_COL_START + index);
#else
    return keycode_at_keymap_location_raw(0, SOFTKEY_ROW, SOFTKEY_COL_START + index);
#endif
}

void softkeys_reset_all(void) {
    /* Reset hold state */
    if (hold_state == HOLD_PEEKING) {
        display_overlay_hide();
    }
    hold_state = HOLD_IDLE;

    /* Zero all soft key entries, then apply firmware defaults */
    memset(kb_config.softkeys, 0, sizeof(kb_config.softkeys));
    softkeys_apply_defaults_user();
    softkeys_save();
    dprintf("Softkeys: all reset to firmware defaults\n");
}
