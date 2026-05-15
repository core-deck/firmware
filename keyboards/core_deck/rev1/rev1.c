// Copyright 2024 vden
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "protocol.h"
#include "display.h"
#include "softkeys.h"
#include "theme.h"

#ifdef DIP_SWITCH_ENABLE

/**
 * @brief Callback function for DIP switch state changes
 *
 * Updates YOLO state and sends a state report via HID.
 * No keystroke is sent — the host reads YOLO state from the 0x10 report.
 *
 * @param index The index of the DIP switch (0 for YOLO mode)
 * @param active Inverted polarity: false when switch is ON
 * @return true to continue processing
 */
bool dip_switch_update_kb(uint8_t index, bool active) {
    if (!dip_switch_update_user(index, active)) {
        return false;
    }
    /* YOLO state handled by yolo_switch_poll() — DIP switch scanner
     * only fires reliably for one edge on this hardware. */
    return true;
}

#endif // DIP_SWITCH_ENABLE

/**
 * @brief EEPROM reset callback - set safe defaults
 */
void eeconfig_init_kb(void) {
    memset(&kb_config, 0, sizeof(kb_config));
    kb_config.backlight_level = DISPLAY_BL_DEFAULT_LEVEL;
    theme_apply_defaults();
    eeconfig_update_kb_datablock(&kb_config, 0, sizeof(kb_config));
    dprintf("EEPROM: kb_config defaults written\n");

    eeconfig_init_user();
}

/* Claude button hold-to-peek alert details */
#define CLAUDE_HOLD_MS 250
typedef enum { CLAUDE_IDLE, CLAUDE_WAITING, CLAUDE_PEEKING } claude_hold_t;
static claude_hold_t claude_hold_state = CLAUDE_IDLE;
static uint32_t claude_hold_start = 0;

/* Tracks whether the Claude button (F20, [0,3]) is physically held
 * down right now. Used as a chord modifier: F20 + Stop emits KC_F24
 * to the daemon (interpreted as "open fresh claude in focused cwd")
 * instead of the usual LCTL(KC_C) abort signal. */
static bool f20_held = false;

/**
 * @brief Process key records at keyboard level (before user)
 *
 * When the companion app is connected, routes all key events via Raw HID
 * so the host app can deliver them to the correct terminal regardless of
 * window focus or keyboard layout.
 */
bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        display_activity();
    }

    /* When alert overlay is active and no host connected,
       Accept [1,1] and Reject [1,2] dismiss the current alert */
    if (display_has_alerts() && !display_is_connected()) {
        if (record->event.key.row == 1 &&
            (record->event.key.col == 1 || record->event.key.col == 2)) {
            if (record->event.pressed) {
                display_alert_dismiss();
            }
            return false;
        }
    }

    if (!softkeys_process_record(keycode, record)) {
        return false;  /* softkeys handle their own HID routing */
    }

    /* Track Claude-button held state regardless of which branch handles
     * the keypress below — the F20+Stop chord (open fresh claude in
     * focused cwd) needs to know whether F20 is physically down when
     * Stop fires. Updated on every press/release of [0,3]. */
    if (record->event.key.row == 0 && record->event.key.col == 3) {
        f20_held = record->event.pressed;
    }

    /* Stop button [1,0]: when Claude button is held, replace the
     * usual LCTL(KC_C) abort with KC_F24 so the daemon can route it
     * to its "open fresh claude" handler instead of injecting Ctrl-C
     * into the wrapper. Only swap on press; release of either key
     * doesn't need to send anything (the daemon doesn't track key
     * release events). */
    if (record->event.key.row == 1 && record->event.key.col == 0
        && record->event.pressed && f20_held && display_is_connected()) {
        send_key_event(KC_F24);
        return false;
    }

    /* Claude button [0,3]: tap sends F20 to host, hold peeks alert details */
    if (record->event.key.row == 0 && record->event.key.col == 3 && display_has_alerts()) {
        if (record->event.pressed) {
            if (claude_hold_state == CLAUDE_PEEKING) {
                display_alert_show_details(false);
            }
            claude_hold_state = CLAUDE_WAITING;
            claude_hold_start = timer_read32();
        } else {
            if (claude_hold_state == CLAUDE_WAITING) {
                /* Short press — fire tap: send key event to host */
                if (display_is_connected()) {
                    send_key_event(keycode);
                } else {
                    return process_record_user(keycode, record);
                }
            } else if (claude_hold_state == CLAUDE_PEEKING) {
                display_alert_show_details(false);
            }
            claude_hold_state = CLAUDE_IDLE;
        }
        return false;
    }

    /* Mode button [1,3]: cycle mode on tap only, let QMK handle layer on hold */
    if (record->event.key.row == 1 && record->event.key.col == 3) {
        if (display_is_connected()) {
            if (record->tap.count && record->event.pressed) {
                /* Tap: cycle mode; app sends Shift+Tab from state report */
                mode_state_cycle();
                send_state_report();
                return false;
            }
            /* Hold/release: pass through to QMK for Layer 2 activation */
            return process_record_user(keycode, record);
        }
        /* Not connected: skip mode cycling, just pass through */
        return process_record_user(keycode, record);
    }

    /* When companion connected: route button presses via HID,
       but let layer-tap and mouse keys pass through so QMK handles them natively */
    if (display_is_connected()) {
        if (IS_QK_LAYER_TAP(keycode) || IS_MOUSE_KEYCODE(keycode)) {
            return process_record_user(keycode, record);
        }
        if (record->event.pressed) {
            send_key_event(keycode);
        }
        return false;  /* suppress normal USB keycode */
    }

    return process_record_user(keycode, record);
}

/**
 * @brief Keyboard post-init callback
 */
void keyboard_post_init_kb(void) {
    // Call user function
    keyboard_post_init_user();

    // Load soft key config from EEPROM (must be before display for backlight)
    softkeys_init();

    // Initialize display (reads backlight from kb_config)
    keyboard_post_init_display();

    // Best-effort initial state report (USB may not be ready yet)
    send_state_report();
}

/* Deferred state report — avoids blocking main loop with raw_hid_send() */
static bool state_report_pending = false;
static uint32_t state_report_time = 0;
#define STATE_REPORT_DELAY_MS 50

/**
 * @brief Poll YOLO switch GPIO directly (GP28)
 *
 * QMK's DIP switch scanner misses the OFF transition on this hardware.
 * Read the pin ourselves with simple debounce.
 */
static void yolo_switch_poll(void) {
    static uint8_t stable_count = 0;
    static bool last_pin = false;
    static bool initialized = false;

    /* GP28 is an ADC pin — re-assert digital input + pull-up every read
     * because the ADC subsystem may clear the pad's Input Enable bit. */
    gpio_set_pin_input_low(GP28);
    bool pin = gpio_read_pin(GP28);

    if (!initialized) {
        last_pin = pin;
        yolo_state_set(pin);  /* pin HIGH = switch ON = YOLO */
        initialized = true;
        return;
    }

    if (pin == last_pin) {
        stable_count = 0;
        return;
    }

    /* Require 5 consecutive different reads (~5 scan cycles) */
    if (++stable_count < 5) return;

    last_pin = pin;
    stable_count = 0;

    bool yolo = pin;  /* pin HIGH when switch ON, LOW when OFF */
    if (yolo != yolo_state_get()) {
        yolo_state_set(yolo);
        display_render();
        state_report_pending = true;
        state_report_time = timer_read32();
    }
}

/**
 * @brief Housekeeping task for periodic updates
 */
void housekeeping_task_kb(void) {
    // Direct YOLO switch poll (DIP switch scanner misses OFF edge)
    yolo_switch_poll();

    // Send deferred state report after debounce settles
    if (state_report_pending && timer_elapsed32(state_report_time) >= STATE_REPORT_DELAY_MS) {
        state_report_pending = false;
        send_state_report();
    }

    // Claude button hold-to-peek alert details
    if (claude_hold_state == CLAUDE_WAITING && timer_elapsed32(claude_hold_start) >= CLAUDE_HOLD_MS) {
        claude_hold_state = CLAUDE_PEEKING;
        display_alert_show_details(true);
    }

    // Poll softkey hold timer for peek overlay
    softkeys_task();

    // Check for ping timeout and auto-dim display
    display_task();

    // Call user function
    housekeeping_task_user();
}
