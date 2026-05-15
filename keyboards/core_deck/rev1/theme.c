// Copyright 2024 vden
// SPDX-License-Identifier: GPL-2.0-or-later

#include "theme.h"
#include "softkeys.h"

/* Built-in defaults — match the HSV literals originally compiled into
 * display.c. Indexed by theme_slot_t. Keep in lockstep with the enum. */
static const theme_color_t theme_defaults[THEME_SLOT_COUNT] = {
    [THEME_SESSION]       = {   0,   0, 255 },  /* white */
    [THEME_TASK]          = { 170, 105, 255 },  /* cyan/blue, desat for tinted cover */
    [THEME_TASK_EMPTY]    = {   0,   0, 180 },  /* dim white */
    [THEME_ALERT]         = {   0, 160, 255 },  /* red, desat for tinted cover */
    [THEME_CTX_BAR]       = {  30, 225, 215 },  /* amber/gold */
    [THEME_YOLO]          = {  20, 255, 255 },  /* orange */
    [THEME_TAB_ACTIVE]    = {   9, 156, 222 },  /* peach */
    [THEME_TAB_INACTIVE]  = {   0,   0, 255 },  /* white */
    [THEME_SOFTKEY_LABEL] = {   0,   0, 255 },  /* white */
    [THEME_SOFTKEY_SEP]   = {   0,   0,  60 },  /* very dim white */
};

void theme_apply_defaults(void) {
    for (uint8_t i = 0; i < THEME_SLOT_COUNT; i++) {
        kb_config.theme[i] = theme_defaults[i];
    }
    kb_config.theme_version = THEME_CONFIG_VERSION;
}

bool theme_set(uint8_t slot, uint8_t hue, uint8_t sat, uint8_t val, bool save) {
    if (slot >= THEME_SLOT_COUNT) {
        return false;
    }
    kb_config.theme[slot].hue = hue;
    kb_config.theme[slot].sat = sat;
    kb_config.theme[slot].val = val;
    if (save) {
        softkeys_save();
    }
    return true;
}

void theme_get(uint8_t slot, uint8_t *hue, uint8_t *sat, uint8_t *val) {
    if (slot >= THEME_SLOT_COUNT) {
        *hue = 0; *sat = 0; *val = 0;
        return;
    }
    *hue = kb_config.theme[slot].hue;
    *sat = kb_config.theme[slot].sat;
    *val = kb_config.theme[slot].val;
}

void theme_reset(void) {
    theme_apply_defaults();
    softkeys_save();
}

uint8_t theme_hue(uint8_t slot) {
    return slot < THEME_SLOT_COUNT ? kb_config.theme[slot].hue : 0;
}

uint8_t theme_sat(uint8_t slot) {
    return slot < THEME_SLOT_COUNT ? kb_config.theme[slot].sat : 0;
}

uint8_t theme_val(uint8_t slot) {
    return slot < THEME_SLOT_COUNT ? kb_config.theme[slot].val : 0;
}
