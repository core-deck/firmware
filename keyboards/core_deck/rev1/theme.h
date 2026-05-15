// Copyright 2024 vden
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Display color palette — tunable at runtime via HID commands 0x0B/0x0C/0x0D
 * and persisted to EEPROM alongside softkeys (see kb_config_t).
 *
 * Each slot is one HSV triplet matching Quantum Painter's color API.
 * Defaults match the hardcoded values originally compiled into display.c. */

typedef enum {
    THEME_SESSION       = 0,  /* Line 1 session text + alert session header + details */
    THEME_TASK          = 1,  /* Lines 2-3 task text */
    THEME_TASK_EMPTY    = 2,  /* "No active task" placeholder */
    THEME_ALERT         = 3,  /* Alert frame, alert text, alert tab indicator */
    THEME_CTX_BAR       = 4,  /* Context usage bar at bottom */
    THEME_YOLO          = 5,  /* YOLO diagonal hazard stripes */
    THEME_TAB_ACTIVE    = 6,  /* Active tab circle (peach) */
    THEME_TAB_INACTIVE  = 7,  /* Inactive/loaded/working tab circles + overflow indicator */
    THEME_SOFTKEY_LABEL = 8,  /* Softkey overlay label text and dot identifiers */
    THEME_SOFTKEY_SEP   = 9,  /* Softkey overlay horizontal separators */
    THEME_SLOT_COUNT
} theme_slot_t;

typedef struct __attribute__((packed)) {
    uint8_t hue;
    uint8_t sat;
    uint8_t val;
} theme_color_t;

/* Stored at kb_config.theme_version. Bump if the slot layout changes in a
 * way that invalidates persisted data; softkeys_init re-applies defaults
 * on mismatch. Picked to be distinct from common flash sentinels
 * (0x00 zero-fill, 0xFF flash-erased). */
#define THEME_CONFIG_VERSION 0xA1

/**
 * @brief Fill kb_config.theme[] with built-in defaults
 *
 * Called by softkeys_init when the loaded EEPROM theme block looks
 * uninitialized (any slot with val==0), and by theme_reset.
 */
void theme_apply_defaults(void);

/**
 * @brief Set one slot
 * @param save  If true, persist kb_config to EEPROM after writing
 * @return false if slot is out of range
 */
bool theme_set(uint8_t slot, uint8_t hue, uint8_t sat, uint8_t val, bool save);

/**
 * @brief Read one slot into out params. Out-of-range slots return zeros.
 */
void theme_get(uint8_t slot, uint8_t *hue, uint8_t *sat, uint8_t *val);

/**
 * @brief Reset all slots to firmware defaults and persist to EEPROM
 */
void theme_reset(void);

/**
 * @brief Per-component accessors for inline use in qp_* draw calls.
 *
 * Out-of-range slots return 0. Slot is normally a compile-time enum
 * constant so the THEME_HSV(slot) macro expands cleanly.
 */
uint8_t theme_hue(uint8_t slot);
uint8_t theme_sat(uint8_t slot);
uint8_t theme_val(uint8_t slot);

/* Spreads one slot into the three arguments a qp_* call expects.
 * Slot must be side-effect-free since it is evaluated three times. */
#define THEME_HSV(slot) theme_hue(slot), theme_sat(slot), theme_val(slot)
