// Copyright 2024 vden
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "action.h"
#include "theme.h"

#define SOFTKEY_COUNT     3
#define SOFTKEY_DATA_LEN 128  // 127 usable chars + null, or 2 bytes for keycode
#define SOFTKEY_HOLD_MS  250  // Hold duration to trigger peek overlay

/* Soft key assignment types */
typedef enum {
    SOFTKEY_DEFAULT  = 0x00,  // Use keymap default
    SOFTKEY_KEYCODE  = 0x01,  // Single keycode (with modifiers)
    SOFTKEY_STRING   = 0x02,  // Type a string on press
    SOFTKEY_SEQUENCE = 0x03,  // Tap a sequence of keycodes on press
} softkey_type_t;

#define SOFTKEY_SEQ_MAX_KEYS  63  // Max keycodes in a sequence (data[0] + 63*2 = 127 bytes)
#define SOFTKEY_SEQ_DELAY_MS  10  // Delay between keycodes in a sequence

/* String type flags (stored in data[0], string starts at data[1]) */
#define SOFTKEY_STRING_FLAG_ENTER  0x01  // Send Enter after typing the string

/* Per-key EEPROM entry */
typedef struct __attribute__((packed)) {
    uint8_t type;                    // softkey_type_t
    uint8_t data[SOFTKEY_DATA_LEN];  // keycode: [hi, lo], string: null-terminated
} softkey_entry_t;  // 129 bytes

/* Full keyboard EEPROM config */
typedef struct __attribute__((packed)) {
    softkey_entry_t softkeys[SOFTKEY_COUNT];     // 387 bytes
    uint8_t backlight_level;                     //   1 byte (migrated from raw addr 32)
    uint8_t theme_version;                       //   1 byte (THEME_CONFIG_VERSION sentinel)
    theme_color_t theme[THEME_SLOT_COUNT];       //  30 bytes (10 slots × 3 bytes)
} kb_config_t;  // 419 bytes

/* Global config instance */
extern kb_config_t kb_config;

/**
 * @brief Initialize soft keys - load config from EEPROM datablock
 */
void softkeys_init(void);

/**
 * @brief Save full kb_config to EEPROM datablock
 */
void softkeys_save(void);

/**
 * @brief Process a key record, overriding soft keys if configured
 * @return true if the event should continue to keymap processing, false if handled
 */
bool softkeys_process_record(uint16_t keycode, keyrecord_t *record);

/**
 * @brief Set a soft key assignment
 * @param index Key index (0-2)
 * @param type Assignment type
 * @param data Pointer to data (keycode bytes or string)
 * @param data_len Length of data
 * @param save If true, persist to EEPROM
 * @return true on success
 */
bool softkeys_set(uint8_t index, uint8_t type, const uint8_t *data, uint8_t data_len, bool save);

/**
 * @brief Get a soft key assignment
 * @param index Key index (0-2)
 * @return Pointer to the entry, or NULL if index invalid
 */
const softkey_entry_t *softkeys_get(uint8_t index);

/**
 * @brief Get the keymap-default keycode for a soft key index
 * @param index Key index (0-2)
 * @return The 16-bit keycode from layer 0 of the keymap
 */
uint16_t softkeys_get_keymap_keycode(uint8_t index);

/**
 * @brief Reset all soft keys to defaults and save to EEPROM
 */
void softkeys_reset_all(void);

/**
 * @brief Poll hold timer - call from housekeeping_task_kb
 */
void softkeys_task(void);

/**
 * @brief Generate a human-readable label for a softkey
 * @param index Key index (0-2)
 * @param buf Output buffer
 * @param buf_size Size of output buffer
 */
void softkeys_get_label(uint8_t index, char *buf, uint8_t buf_size);

/**
 * @brief Weak callback: fire tap action for DEFAULT softkey with custom keycode
 * @param index Key index (0-2)
 * @return true if handled, false to fall back to tap_code16(keymap keycode)
 */
bool softkeys_tap_default_user(uint8_t index);

/**
 * @brief Weak callback: provide label for DEFAULT softkey with custom keycode
 * @param index Key index (0-2)
 * @param buf Output buffer
 * @param buf_size Size of output buffer
 * @return true if label was written, false to fall back to keycode_to_label()
 */
bool softkeys_get_label_user(uint8_t index, char *buf, uint8_t buf_size);

/**
 * @brief Weak callback: populate firmware defaults for softkeys
 *
 * Called on first boot (virgin EEPROM) and after reset-all.
 * Modify kb_config.softkeys[] entries directly; entries not touched
 * remain as SOFTKEY_DEFAULT (uses keymap keycode).
 */
void softkeys_apply_defaults_user(void);
