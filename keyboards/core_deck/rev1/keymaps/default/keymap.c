// Copyright 2023 QMK
// SPDX-License-Identifier: GPL-2.0-or-later

#include QMK_KEYBOARD_H
#include "protocol.h"
#include "display.h"
#include "softkeys.h"
#include <stdio.h>

/* No custom keycodes needed — softkey defaults are in EEPROM */

/* Keyboard initialization callback for debug */
void keyboard_post_init_user(void) {
    // Enable debug output
    debug_enable = true;
    // debug_matrix = true;
    debug_keyboard = true;

    dprintf("\n\n");
    dprintf("========================================\n");
    dprintf("Core Deck Keyboard Initialized\n");
    dprintf("========================================\n");
    dprintf("Firmware: QMK\n");
    dprintf("Keyboard: core_deck/rev1\n");
    dprintf("Matrix: 3 rows x 4 cols (9 keys total)\n");
    dprintf("  - Row 0: PCF8574 I2C (Clear, Verbose, Model, Claude)\n");
    dprintf("  - Row 1: Direct GPIO (Stop, Accept, Reject, Mode)\n");
    dprintf("  - Row 2: Encoder button (1 key)\n");
    dprintf("Features:\n");
#ifdef RGB_MATRIX_ENABLE
    dprintf("  - RGB Matrix: 9 LEDs\n");
#endif
#ifdef ENCODER_ENABLE
    dprintf("  - Encoder: GP11/GP12\n");
#endif
#ifdef RAW_ENABLE
    dprintf("  - Raw HID: Enabled\n");
#endif
#ifdef QUANTUM_PAINTER_ENABLE
    dprintf("  - Display: Enabled\n");
#endif
    dprintf("Debug: All output enabled\n");
    dprintf("========================================\n\n");
}

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    /*
     * PCF8574 Matrix (1x4):
     * ┌─────────┬─────────┬─────────┬─────────┐
     * │  Clear  │ Verbose │ Model   │ Claude  │  Row 0 (I2C)
     * │ Esc-Esc │ Ctrl-O  │ /model  │  F20    │
     * └─────────┴─────────┴─────────┴─────────┘
     *
     * Direct GPIO Buttons:
     * ┌─────────┬─────────┬─────────┬─────────┐
     * │  Stop   │ Accept  │ Reject  │  Mode   │  Row 1 (GP13, GP15, GP14, GP26)
     * │ Ctrl-C  │ Enter   │  Esc    │Tap:S-Tab│  Hold: Layer 2 (encoder sends mouse scroll)
     * └─────────┴─────────┴─────────┴─────────┘
     * ┌─────────┐
     * │Enc Btn  │  Row 2 (GP27)
     * │Tap:Enter│  Hold: Layer 1 (encoder sends Ctrl+Tab / Ctrl+Shift+Tab)
     * └─────────┘
     */
    [0] = LAYOUT(
        KC_NO,          LCTL(KC_O),     KC_NO,          KC_F20,       // Row 0: Clear(EEPROM), Verbose, Model(EEPROM), Claude
        LCTL(KC_C),      KC_ENT,         KC_ESC,      LT(2, KC_NO),   // Row 1: Stop, Accept, Reject, Mode (tap=Shift+Tab, hold=Layer 2)
        LT(1, KC_ENT)                                                  // Row 2: Encoder button (tap=Enter, hold=Layer 1)
    ),
    [1] = LAYOUT(
        _______,        _______,        _______,        _______,
        _______,        _______,        _______,        _______,
        _______
    ),
    [2] = LAYOUT(
        _______,        _______,        _______,        _______,
        _______,        _______,        _______,        _______,
        _______
    )
};

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    // Debug: Log all key events
    dprintf("Key: row=%d col=%d keycode=0x%04X %s\n",
            record->event.key.row, record->event.key.col, keycode,
            record->event.pressed ? "pressed" : "released");

    /* LT(2, KC_NO) tap: send Shift+Tab for Mode button */
    if (keycode == LT(2, KC_NO) && record->tap.count && record->event.pressed) {
        tap_code16(LSFT(KC_TAB));
        return false;
    }

    return true;
}

/**
 * @brief Populate firmware defaults for softkeys (called on virgin EEPROM and reset)
 */
void softkeys_apply_defaults_user(void) {
    /* Key 0: Clear input (Esc-Esc sequence) */
    kb_config.softkeys[0].type = SOFTKEY_SEQUENCE;
    kb_config.softkeys[0].data[0] = 2;  /* count */
    kb_config.softkeys[0].data[1] = (KC_ESC >> 8) & 0xFF;
    kb_config.softkeys[0].data[2] = KC_ESC & 0xFF;
    kb_config.softkeys[0].data[3] = (KC_ESC >> 8) & 0xFF;
    kb_config.softkeys[0].data[4] = KC_ESC & 0xFF;

    /* Key 1: stays SOFTKEY_DEFAULT (uses keymap Ctrl+O) */

    /* Key 2: Model select (/model + Enter) */
    kb_config.softkeys[2].type = SOFTKEY_STRING;
    kb_config.softkeys[2].data[0] = SOFTKEY_STRING_FLAG_ENTER;
    memcpy(&kb_config.softkeys[2].data[1], "/model", 7);  /* includes null */
}

#ifdef ENCODER_MAP_ENABLE
/**
 * @brief Encoder map for rotary encoder actions
 *
 * Maps encoder rotation to keycodes per layer:
 * - Layer 0: Arrow Up (CCW) / Arrow Down (CW)
 * - Layer 1 (encoder hold): Ctrl+Shift+Tab (CCW) / Ctrl+Tab (CW) — tab switching
 * - Layer 2 (Mode hold): Mouse wheel up (CCW) / Mouse wheel down (CW)
 */
const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][NUM_DIRECTIONS] = {
    [0] = { ENCODER_CCW_CW(KC_UP, KC_DOWN) },                          // Scroll
    [1] = { ENCODER_CCW_CW(LCTL(LSFT(KC_TAB)), LCTL(KC_TAB)) },       // Tab switch
    [2] = { ENCODER_CCW_CW(MS_WHLU, MS_WHLD) }                         // Mouse scroll
};
#endif

#ifdef RGB_MATRIX_ENABLE
/**
 * @brief Custom LED indicator for Thinking button
 *
 * LED Layout (9 LEDs total, indices 0-8):
 * Index 0-3: Row 0 (Clear, Verbose, Model, Claude)
 * Index 4-7: Row 1 (Stop, Accept, Reject, Mode)
 * Index 8:   Row 2 (Encoder button)
 *
 * Mode button is at index 4 - cycles through default/plan/accept
 */
bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    // Mode button LED (index 4) — use HSV→RGB for consistent color rendering
    if (led_min <= 4 && led_max > 4) {
        uint8_t v = rgb_matrix_config.hsv.v * 7 / 10;  // 70%, same as glow base
        hsv_t mode_hsv = {0, 255, v};
        switch (mode_state_get()) {
            case MODE_PLAN:
                mode_hsv.h = 128;  // Cyan
                break;
            case MODE_ACCEPT:
                mode_hsv.h = 191;  // Purple
                break;
            default:
                return false;  // No override, uses normal reactive effect
        }
        rgb_t rgb = hsv_to_rgb(mode_hsv);
        rgb_matrix_set_color(4, rgb.r, rgb.g, rgb.b);
    }
    return false;
}
#endif
