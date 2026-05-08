// Copyright 2024 vden
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "qp.h"
#include "qp_comms.h"
#include "qp_st77xx_opcodes.h"
#include "qp_st7789_opcodes.h"
#include "display.h"
#include "softkeys.h"
#include "protocol.h"
#include "config.h"
#include "qp_surface.h"
#include "graphics/terminus_bold_18.qff.h"
#include "graphics/terminus_reg_14.qff.h"
#include "graphics/logo.qgf.h"
#include <hal.h>
#include <string.h>

/* Alert/accent color tuning (for visibility through tinted cover)
 * Pure red HSV(0,255,255) only lights R sub-pixels — dim through tint.
 * Lower saturation mixes in white, activating all sub-pixels = much brighter.
 */
#define ALERT_HUE 0
#define ALERT_SAT 160
#define ALERT_VAL 255

/* Task text: cyan/blue — same treatment */
#define TASK_HUE 170
#define TASK_SAT 160
#define TASK_VAL 255

/* Context bar (bottom 2px gold line, matches logo accent) */
#define CTX_BAR_HUE  30
#define CTX_BAR_SAT 225
#define CTX_BAR_VAL 215

/* YOLO hazard stripe parameters */
#define YOLO_STRIPE_W   3   /* width of stripe column on each side */
#define YOLO_STRIPE_P   6   /* diagonal stripe period in pixels */
#define YOLO_HUE       20   /* orange */
#define YOLO_SAT      255
#define YOLO_VAL      255

/* Display handle */
static painter_device_t display = NULL;
static painter_font_handle_t font_large;
static painter_font_handle_t font_small;
static painter_image_handle_t logo;

/* Offscreen surface for flicker-free rendering */
static painter_device_t surface = NULL;
static uint8_t surface_buffer[SURFACE_REQUIRED_BUFFER_BYTE_SIZE(DISPLAY_WIDTH, DISPLAY_HEIGHT, 16)];

/* Backlight control */
static uint8_t backlight_level = DISPLAY_BL_DEFAULT_LEVEL;
static uint8_t target_level = DISPLAY_BL_DEFAULT_LEVEL;
static uint32_t last_ping_time = 0;
static bool connected = false;
static uint32_t last_content_change_time = 0;
static bool idle_dimmed = false;

static PWMConfig pwmCFG = {
    .frequency = 1000000,  // 1MHz clock
    .period = 256,         // 256 steps = ~3.9kHz PWM
};

/* Display data from companion app */
static display_data_t display_data = {
    .session = "",
    .task = "",
    .task2 = "",
    .tabs = {0},
    .tab_count = 0,
    .active_tab = -1,
    .context_percent = 0,
};

/* Overlay state */
static overlay_state_t overlay_state = OVERLAY_NONE;

/* Alert state */
static alert_entry_t alerts[DISPLAY_MAX_TABS];
static uint32_t alert_order_counter = 0;
static uint8_t  alert_count = 0;
static bool alert_details_active = false; /* true while Claude button held */

/* Deferred render flag — set by HID callbacks, consumed by display_task().
 * Keeps QP rendering out of the USB callback path so the main loop
 * (and RGB matrix animation) isn't starved during bursts of HID traffic. */
static bool render_pending = false;

/* Forward declarations */
static void display_render_alert_overlay(void);
static void draw_wordwrapped(painter_device_t target, painter_font_handle_t font,
                              const uint16_t *y_positions, uint8_t max_lines,
                              const char *text, uint16_t max_w,
                              uint8_t hue, uint8_t sat, uint8_t val);

/* Tab breathing animation state */
static uint32_t last_frame_time = 0;
static uint16_t anim_phase = 0;       // 0..1999 for 2s cycle
#define ANIM_FRAME_MS 50              // ~20fps
#define ANIM_CYCLE_MS 2000

/* HSV Colors */
// White: 0, 0, 255
// Blue: 170, 255, 255
// Green: 85, 255, 255
// Grey: 0, 0, 128
// Black: 0, 0, 0

/**
 * @brief Custom ST7789 init for 76x284 panel
 * EXACT sequence from supplier's Arduino example
 */
bool qp_st7789_init(painter_device_t device, painter_rotation_t rotation) {
    const uint8_t st7789_init_sequence[] = {
        // Command,                 Delay, N, Data[N]
        ST77XX_CMD_RESET,            120,  0,
        ST77XX_CMD_SLEEP_OFF,          5,  0,
        ST77XX_SET_PIX_FMT,            0,  1, 0x55,
        ST77XX_CMD_INVERT_OFF,          0,  0,
        ST77XX_CMD_NORMAL_ON,          0,  0,
        ST77XX_CMD_DISPLAY_ON,        20,  0
    };
    // clang-format on
    qp_comms_bulk_command_sequence(device, st7789_init_sequence, sizeof(st7789_init_sequence));
    // Set MADCTL for rotation (try WITHOUT BGR - colors were swapped)
    uint8_t madctl = 0x00;  // RGB color order (no BGR)
    switch (rotation) {
        case QP_ROTATION_0:   madctl |= 0x00; break;
        case QP_ROTATION_90:  madctl |= 0x60; break;
        case QP_ROTATION_180: madctl |= 0xC0; break;
        case QP_ROTATION_270: madctl |= 0xA0; break;
        default: break;
    }
    dprintf("Setting MADCTL=0x%02X for rotation\n", madctl);
    qp_comms_command_databyte(device, ST77XX_SET_MADCTL, madctl);

    dprintf("=== ST7789 init COMPLETE ===\n");
    return true;
}

/**
 * @brief Initialize the TFT display
 */
void display_init(void) {
    // Use actual panel dimensions with correct offsets
    dprintf("Initializing ST7789 with %dx%d panel...\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    display = qp_st7789_make_spi_device(DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                        DISPLAY_CS_PIN, DISPLAY_DC_PIN, DISPLAY_RST_PIN,
                                        8, 3);

    if (!display) {
        dprintf("Failed to create display device\n");
        return;
    }

    // Set offsets based on boundary test: panel at X=82, Y=18 in the 240x320 buffer
    qp_set_viewport_offsets(display, 18, 82);

    if (!qp_init(display, QP_ROTATION_90)) {
        dprintf("Failed to initialize display\n");
        return;
    }

    if (!qp_power(display, true)) {
        dprintf("Failed to power on display\n");
        return;
    }

    // Simple color test on actual panel dimensions
    dprintf("Drawing color test on %dx%d panel...\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);

    // Fill with BLACK
    qp_rect(display, 0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1, 0, 0, 0, true);
    qp_flush(display);

    font_large = qp_load_font_mem(font_terminus_bold_18);
    font_small = qp_load_font_mem(font_terminus_reg_14);
    logo = qp_load_image_mem(gfx_logo);

    // Create offscreen surface for flicker-free rendering
    surface = qp_make_rgb565_surface(DISPLAY_WIDTH, DISPLAY_HEIGHT, surface_buffer);
    if (!surface || !qp_init(surface, QP_ROTATION_0)) {
        dprintf("Failed to create offscreen surface\n");
        return;
    }

    dprintf("Display initialized successfully\n");

    // Initial render
    display_show_logo();
}

/**
 * @brief Show logo for idle mode
 */
void display_show_logo(void) {
    if (logo != NULL && surface != NULL) {
        qp_drawimage(surface, 0, 0, logo);
        qp_surface_draw(surface, display, 0, 0, true);
    }
}

/**
 * @brief Set PWM duty cycle directly (internal use for dim/restore)
 */
static void backlight_set_pwm(uint8_t level) {
    backlight_level = level;
    pwmEnableChannel(&DISPLAY_BL_PWM_DRIVER, DISPLAY_BL_PWM_CHANNEL, level);
}

/**
 * @brief Initialize display backlight PWM
 */
void display_backlight_init(void) {
    // Load saved brightness from kb_config (populated by softkeys_init)
    if (kb_config.backlight_level > 0) {
        target_level = kb_config.backlight_level;
    }

    // Configure PWM pin (RP2040 PWM function is alternate 4)
    palSetPadMode(PAL_PORT(DISPLAY_BL_PIN), PAL_PAD(DISPLAY_BL_PIN),
                  PAL_MODE_ALTERNATE(4));

    // Set channel mode before starting PWM driver
    pwmCFG.channels[DISPLAY_BL_PWM_CHANNEL].mode = PWM_OUTPUT_ACTIVE_LOW;

    // Start PWM driver
    pwmStart(&DISPLAY_BL_PWM_DRIVER, &pwmCFG);

    // Start dimmed until first ping
    backlight_set_pwm(DISPLAY_BL_DIM_LEVEL);

    dprintf("Backlight initialized, saved level: %d\n", target_level);
}

/**
 * @brief Set display backlight level (updates both target and PWM)
 */
void display_backlight_set(uint8_t level) {
    target_level = level;
    backlight_set_pwm(level);
}

/**
 * @brief Get current display backlight target level
 */
uint8_t display_backlight_get(void) {
    return target_level;
}

/**
 * @brief Save current brightness to EEPROM
 */
void display_backlight_save(void) {
    kb_config.backlight_level = target_level;
    softkeys_save();
    dprintf("Backlight level %d saved to EEPROM\n", target_level);
}

/**
 * @brief Wake display from idle dim on user activity (key press, encoder)
 */
void display_activity(void) {
    if (idle_dimmed) {
        idle_dimmed = false;
        last_content_change_time = timer_read32();
        backlight_set_pwm(target_level);
    }
}

/**
 * @brief Called when ping/pong or data is received from companion app
 */
void display_ping_received(void) {
    last_ping_time = timer_read32();
    if (!connected) {
        connected = true;
        idle_dimmed = false;
        last_content_change_time = timer_read32();
        backlight_set_pwm(target_level);  // Restore full brightness
        dprintf("Companion connected, brightness restored to %d\n", target_level);
    }
}

/**
 * @brief Transition to disconnected/idle state — dim, show logo, clear overlays.
 *
 * Called from ping timeout and from the explicit CMD_DISCONNECT command.
 */
void display_host_disconnected(void) {
    if (!connected) return;
    connected = false;
    idle_dimmed = false;
    alert_details_active = false;

    if (alert_count > 0) {
        /* Alerts persist — keep overlay visible at full brightness so the
         * user can acknowledge them via Accept/Reject buttons. */
        overlay_state = OVERLAY_ALERT;
        backlight_set_pwm(target_level);
        render_pending = true;
        dprintf("Host disconnected, %d alert(s) persist\n", alert_count);
    } else {
        overlay_state = OVERLAY_NONE;
        alert_order_counter = 0;
        backlight_set_pwm(DISPLAY_BL_DIM_LEVEL);
        display_show_logo();
        dprintf("Host disconnected, display idle\n");
    }
}

/**
 * @brief Display housekeeping task - check for ping timeout and drive animation
 */
void display_task(void) {
    if (connected && timer_elapsed32(last_ping_time) > DISPLAY_BL_PING_TIMEOUT_MS) {
        display_host_disconnected();
    }

    if (connected && !idle_dimmed && timer_elapsed32(last_content_change_time) > DISPLAY_BL_IDLE_TIMEOUT_MS) {
        idle_dimmed = true;
        backlight_set_pwm(DISPLAY_BL_DIM_LEVEL);
        dprintf("Idle timeout, display dimmed\n");
    }

    /* Determine if animation is needed */
    bool needs_anim = false;

    if (overlay_state == OVERLAY_ALERT) {
        /* Breathing alert frame */
        needs_anim = true;
    } else if (overlay_state == OVERLAY_NONE) {
        /* Breathing tab dots for working (state=2) tabs */
        for (uint8_t i = 0; i < display_data.tab_count; i++) {
            if (display_data.tabs[i] == 2) { needs_anim = true; break; }
        }
    }

    if (needs_anim && connected) {
        uint32_t now = timer_read32();
        if (timer_elapsed32(last_frame_time) >= ANIM_FRAME_MS) {
            last_frame_time = now;
            anim_phase = (anim_phase + ANIM_FRAME_MS) % ANIM_CYCLE_MS;
            render_pending = true;
        }
    } else {
        anim_phase = 0;
    }

    /* Consume deferred render (from HID callbacks or animation tick) */
    if (render_pending) {
        render_pending = false;
        if (overlay_state == OVERLAY_ALERT) {
            display_render_alert_overlay();
        } else {
            display_render();
        }
    }
}

/**
 * @brief Decode one UTF-8 codepoint, return number of bytes consumed (0 on error)
 */
static uint8_t utf8_decode(const char *s, uint32_t *cp) {
    uint8_t c = (uint8_t)s[0];
    if (c < 0x80) { *cp = c; return 1; }
    if ((c & 0xE0) == 0xC0) { *cp = ((c & 0x1F) << 6) | (s[1] & 0x3F); return 2; }
    if ((c & 0xF0) == 0xE0) { *cp = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); return 3; }
    if ((c & 0xF8) == 0xF0) { *cp = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); return 4; }
    return 0;
}

/* Unicode codepoints included in our fonts.
 * ○ ● ✓ are substitutes for ☐ ☑ ◎ (not in Terminus).
 * ↦ is a remote-session marker. */
static const uint32_t allowed_unicode[] = {
    0x00B7, 0x2009, 0x2026, 0x21A6, 0x2191, 0x2193,
    0x25CB, 0x25CF, 0x2713,
};
#define ALLOWED_UNICODE_COUNT (sizeof(allowed_unicode) / sizeof(allowed_unicode[0]))

/**
 * @brief Strip characters not present in the font
 *
 * Keeps printable ASCII (0x20-0x7E) and specifically included Unicode glyphs.
 * Result is written to `out` (must be at least `out_size` bytes).
 */
static void sanitize_text(char *out, size_t out_size, const char *in) {
    size_t j = 0;
    size_t i = 0;
    while (in[i] != '\0' && j < out_size - 4) {
        uint32_t cp;
        uint8_t len = utf8_decode(&in[i], &cp);
        if (len == 0) { i++; continue; }

        bool keep = (cp >= 0x20 && cp <= 0x7E);
        if (!keep) {
            for (uint8_t k = 0; k < ALLOWED_UNICODE_COUNT; k++) {
                if (cp == allowed_unicode[k]) { keep = true; break; }
            }
        }

        if (keep) {
            for (uint8_t b = 0; b < len; b++) {
                out[j++] = in[i + b];
            }
        }
        i += len;
    }
    out[j] = '\0';
}

/**
 * @brief Draw text centered horizontally, with ellipsis truncation
 *
 * Text is sanitized for font safety only — all content-aware compacting
 * (thin spaces, token removal, arrow-digit spacing) is done by the app.
 */
static void draw_text_centered(painter_device_t target, painter_font_handle_t font,
                                uint16_t y, const char *text,
                                uint16_t max_width, uint8_t hue, uint8_t sat, uint8_t val) {
    static char sanitized[DISPLAY_MAX_TEXT_LEN];
    sanitize_text(sanitized, sizeof(sanitized), text);

    int16_t tw = qp_textwidth(font, sanitized);
    if (tw <= max_width) {
        uint16_t x = (DISPLAY_WIDTH - tw) / 2;
        qp_drawtext_recolor(target, x, y, font, sanitized, hue, sat, val, 0, 0, 0);
        return;
    }

    /* Truncate with ellipsis, then center the result.
     * Walk backward on UTF-8 character boundaries to avoid splitting
     * multi-byte sequences (which makes qp_textwidth return 0). */
    static char buf[DISPLAY_MAX_TEXT_LEN];
    size_t len = strlen(sanitized);
    int16_t ellipsis_w = qp_textwidth(font, "\xe2\x80\xa6");

    for (size_t i = len; i > 0; ) {
        i--;
        /* Skip UTF-8 continuation bytes (10xxxxxx) to land on a char boundary */
        while (i > 0 && ((uint8_t)sanitized[i] & 0xC0) == 0x80) {
            i--;
        }
        memcpy(buf, sanitized, i);
        buf[i] = '\0';
        int16_t w = qp_textwidth(font, buf) + ellipsis_w;
        if (w <= max_width) {
            buf[i]     = '\xe2';
            buf[i + 1] = '\x80';
            buf[i + 2] = '\xa6';
            buf[i + 3] = '\0';
            uint16_t x = (DISPLAY_WIDTH - w) / 2;
            qp_drawtext_recolor(target, x, y, font, buf, hue, sat, val, 0, 0, 0);
            return;
        }
    }

    uint16_t x = (DISPLAY_WIDTH - qp_textwidth(font, "\xe2\x80\xa6")) / 2;
    qp_drawtext_recolor(target, x, y, font, "\xe2\x80\xa6", hue, sat, val, 0, 0, 0);
}

/**
 * @brief Calculate breathing radius from animation phase (triangle wave 3..6..3)
 */
static uint8_t calc_breathing_radius(uint16_t phase) {
    /* phase 0..999 -> radius 3..6, phase 1000..1999 -> radius 6..3 */
    uint16_t half = ANIM_CYCLE_MS / 2;
    uint16_t t = (phase < half) ? phase : (ANIM_CYCLE_MS - 1 - phase);
    return 3 + (t * 3 / (half - 1));
}

/**
 * @brief Draw tab indicator circles on the bottom line
 */
static void draw_tabs(painter_device_t target) {
    if (display_data.tab_count == 0) return;

    const uint16_t center_y = 65;
    const uint16_t margin = yolo_state_get() ? (10 + YOLO_STRIPE_W + 1) : 10;
    const uint16_t spacing = 20;
    const uint8_t base_radius = 4;
    uint8_t max_visible = (DISPLAY_WIDTH - 2 * margin) / spacing + 1;
    bool overflow = display_data.tab_count > max_visible;
    uint8_t visible = overflow ? (max_visible - 1) : display_data.tab_count;

    /* Wider margins for few tabs so they don't stretch edge-to-edge */
    uint16_t eff_margin = margin;
    if (visible == 2) {
        eff_margin = DISPLAY_WIDTH / 4;       /* 71px — quarter width each side */
    } else if (visible == 3) {
        eff_margin = DISPLAY_WIDTH / 6;       /* 47px — sixth width each side */
    }

    for (uint8_t i = 0; i < visible; i++) {
        uint16_t cx;
        if (visible == 1) {
            cx = DISPLAY_WIDTH / 2;
        } else {
            cx = eff_margin + (uint32_t)i * (DISPLAY_WIDTH - 2 * eff_margin) / (visible - 1);
        }

        /* Alert indicator: red filled square overrides normal circle */
        if (i < DISPLAY_MAX_TABS && alerts[i].text[0] != '\0') {
            uint8_t half = base_radius;
            qp_rect(target, cx - half, center_y - half, cx + half, center_y + half,
                    ALERT_HUE, ALERT_SAT, ALERT_VAL, true);
            continue;
        }

        uint8_t state = display_data.tabs[i];
        bool is_active = (display_data.active_tab == i);
        bool is_working = (state == 2);
        uint8_t r = is_working ? calc_breathing_radius(anim_phase) : base_radius;
        if (is_active) r++;  /* +1px compensates lower-contrast peach color */

        if (state == 0) {
            /* Inactive: outline circle, white */
            qp_circle(target, cx, center_y, r, 0, 0, 255, false);
        } else if (state == 1) {
            /* Loaded: filled white, or peach if active tab */
            if (is_active) {
                qp_circle(target, cx, center_y, r, 9, 156, 222, true);
            } else {
                qp_circle(target, cx, center_y, r, 0, 0, 255, true);
            }
        } else {
            /* Working: breathing filled peach, or breathing white if also active */
            if (is_active) {
                qp_circle(target, cx, center_y, r, 9, 156, 222, true);
            } else {
                qp_circle(target, cx, center_y, r, 0, 0, 255, true);
            }
        }
    }

    if (overflow) {
        /* Draw ">" overflow indicator at right edge */
        uint16_t x = DISPLAY_WIDTH - margin - qp_textwidth(font_small, ">");
        qp_drawtext_recolor(target, x, center_y - 7, font_small, ">", 0, 0, 128, 0, 0, 0);
    }
}

/**
 * @brief Draw YOLO mode hazard stripes (diagonal orange/black on left & right edges)
 *
 * 3px-wide columns on each side with 45-degree diagonal stripes.
 * Left and right patterns are mirrored (both use distance-from-edge + y).
 */
static void draw_yolo_stripes(painter_device_t target) {
    for (uint16_t y = 0; y < DISPLAY_HEIGHT; y++) {
        for (uint8_t i = 0; i < YOLO_STRIPE_W; i++) {
            if ((i + y) % YOLO_STRIPE_P < YOLO_STRIPE_P / 2) {
                qp_setpixel(target, i, y, YOLO_HUE, YOLO_SAT, YOLO_VAL);
                qp_setpixel(target, DISPLAY_WIDTH - 1 - i, y, YOLO_HUE, YOLO_SAT, YOLO_VAL);
            }
        }
    }
}

/**
 * @brief Draw context usage bar (2px amber line at bottom of display)
 */
static void draw_context_bar(painter_device_t target) {
    if (display_data.context_percent == 0) return;

    uint16_t bar_w = (uint16_t)((uint32_t)display_data.context_percent * DISPLAY_WIDTH / 100);
    if (bar_w == 0) bar_w = 1;
    if (bar_w > DISPLAY_WIDTH) bar_w = DISPLAY_WIDTH;

    qp_rect(target, 0, DISPLAY_HEIGHT - 2, bar_w - 1, DISPLAY_HEIGHT - 1,
            CTX_BAR_HUE, CTX_BAR_SAT, CTX_BAR_VAL, true);
}

void display_render(void) {
    if (!display || !surface) return;
    if (overlay_state != OVERLAY_NONE) return;  /* Don't clobber overlay */

    /* If not connected and no display data, show idle logo */
    if (!connected && display_data.session[0] == '\0') {
        display_show_logo();
        return;
    }

    bool yolo = yolo_state_get();
    uint16_t max_text_width = DISPLAY_WIDTH - (yolo ? 2 * (YOLO_STRIPE_W + 1) : 4);

    // Draw to offscreen surface
    qp_rect(surface, 0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1, 0, 0, 0, true);

    if (yolo) {
        draw_yolo_stripes(surface);
    }

    // Line 1: SESSION (White, 18px bold) - at y=2, centered
    draw_text_centered(surface, font_large, 2, display_data.session, max_text_width, 0, 0, 255);

    // Lines 2-3: TASK (Blue, 14px) — pre-formatted by app
    if (display_data.task[0] != '\0') {
        if (display_data.task2[0] != '\0') {
            /* Two pre-formatted lines from app */
            draw_text_centered(surface, font_small, 25, display_data.task, max_text_width, TASK_HUE, TASK_SAT, TASK_VAL);
            draw_text_centered(surface, font_small, 42, display_data.task2, max_text_width, TASK_HUE, TASK_SAT, TASK_VAL);
        } else {
            /* Single line centered */
            draw_text_centered(surface, font_small, 33, display_data.task, max_text_width, TASK_HUE, TASK_SAT, TASK_VAL);
        }
    } else {
        /* No active task — show dimmed placeholder */
        draw_text_centered(surface, font_small, 33, "No active task", max_text_width, 0, 0, 180);
    }

    // Draw tab indicators on bottom line
    draw_tabs(surface);

    // Draw context usage bar at very bottom
    draw_context_bar(surface);

    // Blit to physical display in one shot
    qp_surface_draw(surface, display, 0, 0, true);
}

/**
 * @brief Simple JSON parser to extract values
 *
 * Format: {"session":"value","task":"value"} or {"session":"value","task":null}
 */
static void parse_json(const char* json) {
    char* ptr;
    char* end;

    // Parse "session"
    ptr = strstr(json, "\"session\":\"");
    if (ptr) {
        ptr += 11;  // Skip "session":"
        end = strchr(ptr, '"');
        if (end) {
            size_t slen = end - ptr;
            if (slen >= DISPLAY_MAX_TEXT_LEN) slen = DISPLAY_MAX_TEXT_LEN - 1;
            memcpy(display_data.session, ptr, slen);
            display_data.session[slen] = '\0';
        }
    }

    // Parse "task" (string or null)
    ptr = strstr(json, "\"task\":");
    if (ptr) {
        ptr += 7;  // Skip "task":
        if (strncmp(ptr, "null", 4) == 0) {
            display_data.task[0] = '\0';
        } else if (*ptr == '"') {
            ptr++;  // Skip opening quote
            end = strchr(ptr, '"');
            if (end) {
                size_t slen = end - ptr;
                if (slen >= DISPLAY_MAX_TEXT_LEN) slen = DISPLAY_MAX_TEXT_LEN - 1;
                memcpy(display_data.task, ptr, slen);
                display_data.task[slen] = '\0';
            }
        }
    }

    // Parse "task2" (string or null, optional)
    display_data.task2[0] = '\0';
    ptr = strstr(json, "\"task2\":");
    if (ptr) {
        ptr += 8;  // Skip "task2":
        if (*ptr == '"') {
            ptr++;  // Skip opening quote
            end = strchr(ptr, '"');
            if (end) {
                size_t slen = end - ptr;
                if (slen >= DISPLAY_MAX_TEXT_LEN) slen = DISPLAY_MAX_TEXT_LEN - 1;
                memcpy(display_data.task2, ptr, slen);
                display_data.task2[slen] = '\0';
            }
        }
    }

    // Parse "tabs" array
    display_data.tab_count = 0;
    memset(display_data.tabs, 0, sizeof(display_data.tabs));
    ptr = strstr(json, "\"tabs\":");
    if (ptr) {
        ptr += 7;  // Skip "tabs":
        while (*ptr == ' ') ptr++;
        if (*ptr == '[') {
            ptr++;  // Skip '['
            while (*ptr != '\0' && *ptr != ']' && display_data.tab_count < DISPLAY_MAX_TABS) {
                while (*ptr == ' ' || *ptr == ',') ptr++;
                if (*ptr >= '0' && *ptr <= '9') {
                    display_data.tabs[display_data.tab_count++] = (*ptr - '0') & 0x03;
                    ptr++;
                } else {
                    break;
                }
            }
        }
    }

    // Parse "active" index
    display_data.active_tab = -1;
    ptr = strstr(json, "\"active\":");
    if (ptr) {
        ptr += 9;  // Skip "active":
        while (*ptr == ' ') ptr++;
        if (*ptr == 'n') {
            /* null */
            display_data.active_tab = -1;
        } else if (*ptr >= '0' && *ptr <= '9') {
            int val = 0;
            while (*ptr >= '0' && *ptr <= '9') {
                val = val * 10 + (*ptr - '0');
                ptr++;
            }
            if (val < display_data.tab_count) {
                display_data.active_tab = (int8_t)val;
            }
        }
    }

    // Parse "context_percent" (float 0-100, stored as uint8_t integer part)
    display_data.context_percent = 0;
    ptr = strstr(json, "\"context_percent\":");
    if (ptr) {
        ptr += 18;  // Skip "context_percent":
        while (*ptr == ' ') ptr++;
        if (*ptr >= '0' && *ptr <= '9') {
            int val = 0;
            while (*ptr >= '0' && *ptr <= '9') {
                val = val * 10 + (*ptr - '0');
                ptr++;
            }
            // Skip decimal part (e.g., ".75")
            if (val > 100) val = 100;
            display_data.context_percent = (uint8_t)val;
        }
    }

    dprintf("Parsed JSON: session='%s', task='%s', task2='%s', tabs=%d, active=%d, ctx=%d%%\n",
            display_data.session, display_data.task, display_data.task2, display_data.tab_count, display_data.active_tab, display_data.context_percent);
}

/**
 * @brief Update display with JSON data from companion app
 */
void display_update_json(const char *data, uint16_t len) {
    static char json_buf[512];
    uint16_t json_len = len < (sizeof(json_buf) - 1) ? len : (sizeof(json_buf) - 1);
    memcpy(json_buf, data, json_len);
    json_buf[json_len] = '\0';

    parse_json(json_buf);

    last_content_change_time = timer_read32();
    if (idle_dimmed) {
        idle_dimmed = false;
        backlight_set_pwm(target_level);
        dprintf("Content changed, idle dim canceled\n");
    }

    render_pending = true;  /* Defer render to display_task() */
}

/**
 * @brief Find the oldest active alert (smallest order value)
 * @return Tab index of oldest alert, or -1 if none
 */
static int8_t find_oldest_alert(void) {
    int8_t oldest = -1;
    uint32_t min_order = UINT32_MAX;
    for (uint8_t i = 0; i < DISPLAY_MAX_TABS; i++) {
        if (alerts[i].text[0] != '\0' && alerts[i].order < min_order) {
            min_order = alerts[i].order;
            oldest = i;
        }
    }
    return oldest;
}

/**
 * @brief Render the alert overlay (full-screen, shows oldest alert)
 */
/**
 * @brief Draw sanitized text centered, with ellipsis truncation but NO compact_text.
 *
 * Used for alert text where thin-space substitution and token removal are unwanted.
 * Input is raw (unsanitized); sanitize_text is applied internally.
 */
static void draw_sanitized_centered(painter_device_t target, painter_font_handle_t font,
                                     uint16_t y, const char *text,
                                     uint16_t max_width, uint8_t hue, uint8_t sat, uint8_t val) {
    static char safe[DISPLAY_MAX_TEXT_LEN];
    sanitize_text(safe, sizeof(safe), text);

    int16_t tw = qp_textwidth(font, safe);
    if (tw <= (int16_t)max_width) {
        uint16_t x = (DISPLAY_WIDTH - tw) / 2;
        qp_drawtext_recolor(target, x, y, font, safe, hue, sat, val, 0, 0, 0);
        return;
    }

    /* Truncate with ellipsis — walk backward on UTF-8 char boundaries */
    static char buf[DISPLAY_MAX_TEXT_LEN];
    size_t len = strlen(safe);
    int16_t ellipsis_w = qp_textwidth(font, "\xe2\x80\xa6");

    for (size_t i = len; i > 0; ) {
        i--;
        while (i > 0 && ((uint8_t)safe[i] & 0xC0) == 0x80) {
            i--;
        }
        memcpy(buf, safe, i);
        buf[i] = '\0';
        int16_t w = qp_textwidth(font, buf) + ellipsis_w;
        if (w <= (int16_t)max_width) {
            buf[i]     = '\xe2';
            buf[i + 1] = '\x80';
            buf[i + 2] = '\xa6';
            buf[i + 3] = '\0';
            uint16_t x = (DISPLAY_WIDTH - w) / 2;
            qp_drawtext_recolor(target, x, y, font, buf, hue, sat, val, 0, 0, 0);
            return;
        }
    }
}

/**
 * @brief Draw breathing alert frame (5 concentric red rectangles)
 *
 * Brightness oscillates with anim_phase using a triangle wave (same
 * cycle as tab breathing).  At peak the outer ring is full red (V=255),
 * at trough it fades to dim (V=40).
 */
static void draw_alert_frame(painter_device_t target) {
    /* Triangle wave 0‥255 from anim_phase (0‥ANIM_CYCLE_MS-1) */
    uint16_t half = ANIM_CYCLE_MS / 2;
    uint8_t breath;  /* 0..255 */
    if (anim_phase < half) {
        breath = (uint8_t)((uint32_t)anim_phase * 255 / half);
    } else {
        breath = (uint8_t)((uint32_t)(ANIM_CYCLE_MS - 1 - anim_phase) * 255 / half);
    }

    /* Base V values for the 5 rings (outer → inner) */
    static const uint8_t base_v[] = { 255, 180, 120, 70, 30 };

    for (uint8_t i = 0; i < 5; i++) {
        /* Scale V by breath (60..base_v), keep S from ALERT_SAT */
        uint8_t v = 60 + (uint8_t)((uint16_t)(base_v[i] - 60) * breath / 255);
        qp_rect(target, i, i, DISPLAY_WIDTH - 1 - i, DISPLAY_HEIGHT - 1 - i,
                ALERT_HUE, ALERT_SAT, v, false);
    }
}

/**
 * @brief Word-wrap and draw text across 1-3 lines (sanitized, no compact_text)
 *
 * Draws at the given y positions (up to max_lines).  Each line is centered
 * and truncated with ellipsis if still too wide.
 */
static void draw_wordwrapped(painter_device_t target, painter_font_handle_t font,
                              const uint16_t *y_positions, uint8_t max_lines,
                              const char *text, uint16_t max_w,
                              uint8_t hue, uint8_t sat, uint8_t val) {
    static char safe[DISPLAY_MAX_TEXT_LEN];
    sanitize_text(safe, sizeof(safe), text);

    /* Collect line break points */
    size_t breaks[3];  /* start indices for each line */
    uint8_t nlines = 0;
    size_t start = 0;
    size_t len = strlen(safe);

    while (start < len && nlines < max_lines) {
        breaks[nlines] = start;

        /* Check if remainder fits on one line */
        if (qp_textwidth(font, &safe[start]) <= (int16_t)max_w) {
            nlines++;
            break;
        }

        if (nlines == max_lines - 1) {
            /* Last available line — just truncate with ellipsis */
            nlines++;
            break;
        }

        /* Find last space that keeps this line within max_w */
        size_t best = 0;
        static char probe[DISPLAY_MAX_TEXT_LEN];
        for (size_t i = start; i < len; i++) {
            if (safe[i] == ' ') {
                size_t seg_len = i - start;
                memcpy(probe, &safe[start], seg_len);
                probe[seg_len] = '\0';
                if (qp_textwidth(font, probe) <= (int16_t)max_w) {
                    best = i;
                } else {
                    break;
                }
            }
        }

        if (best > start) {
            nlines++;
            start = best + 1; /* skip the space */
        } else {
            /* No space — force truncation on this line */
            nlines++;
            break;
        }
    }

    /* Draw each line */
    for (uint8_t l = 0; l < nlines; l++) {
        const char *line_start = &safe[breaks[l]];
        if (l < nlines - 1) {
            /* Not the last line — draw exact substring */
            size_t seg_len = breaks[l + 1] - breaks[l];
            if (seg_len > 0 && safe[breaks[l + 1] - 1] == ' ') seg_len--;
            static char seg[DISPLAY_MAX_TEXT_LEN];
            memcpy(seg, line_start, seg_len);
            seg[seg_len] = '\0';
            draw_sanitized_centered(target, font, y_positions[l], seg, max_w, hue, sat, val);
        } else {
            /* Last line — may need ellipsis truncation */
            draw_sanitized_centered(target, font, y_positions[l], line_start, max_w, hue, sat, val);
        }
    }
}

static void display_render_alert_overlay(void) {
    if (!display || !surface) return;

    /* Clear to black */
    qp_rect(surface, 0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1, 0, 0, 0, true);

    /* Red gradient frame */
    draw_alert_frame(surface);

    int8_t idx = find_oldest_alert();
    if (idx >= 0) {
        uint16_t max_w = DISPLAY_WIDTH - 14; /* 5px frame + 2px pad each side */

        if (alert_details_active && alerts[idx].details[0] != '\0') {
            /* Details view: up to 3 lines of details text, white, small font */
            static const uint16_t y3[] = { 10, 28, 46 };
            draw_wordwrapped(surface, font_small, y3, 3,
                             alerts[idx].details, max_w, 0, 0, 255);
        } else {
            /* Normal alert view: session + text + tabs */

            /* Line 1: Session name (white, large font, centered) */
            draw_text_centered(surface, font_large, 2, alerts[idx].session,
                               max_w, 0, 0, 255);

            /* Lines 2-3: Alert text (red, small font, word-wrapped) */
            static const uint16_t y2[] = { 25, 42 };
            draw_wordwrapped(surface, font_small, y2, 2,
                             alerts[idx].text, max_w, ALERT_HUE, ALERT_SAT, ALERT_VAL);

            /* Tab indicators (red squares for alerted tabs) */
            draw_tabs(surface);
        }
    }

    /* Blit to display */
    qp_surface_draw(surface, display, 0, 0, true);
}

/**
 * @brief Parse alert JSON and update alert state
 *
 * Set: {"tab":0,"session":"my-feat","text":"Build failed!","details":"..."}
 * Clear: {"tab":0,"text":null} or {"tab":0} (absent text = clear)
 */
static void parse_alert_json(const char *json, uint16_t len) {
    static char buf[512];  /* static to reduce stack pressure */
    uint16_t buf_len = len < (sizeof(buf) - 1) ? len : (sizeof(buf) - 1);
    memcpy(buf, json, buf_len);
    buf[buf_len] = '\0';

    /* Parse "tab" (required) */
    char *ptr = strstr(buf, "\"tab\":");
    if (!ptr) return;
    ptr += 6;
    while (*ptr == ' ') ptr++;
    if (*ptr < '0' || *ptr > '9') return;
    int tab = 0;
    while (*ptr >= '0' && *ptr <= '9') {
        tab = tab * 10 + (*ptr - '0');
        ptr++;
    }
    if (tab >= DISPLAY_MAX_TABS) return;

    /* Parse "text" (string, null, or absent = clear) */
    ptr = strstr(buf, "\"text\":");
    bool is_clear;
    static char text_val[DISPLAY_MAX_TEXT_LEN];
    text_val[0] = '\0';

    if (!ptr) {
        is_clear = true;  /* absent text = clear */
    } else {
        ptr += 7;
        while (*ptr == ' ') ptr++;
        if (strncmp(ptr, "null", 4) == 0) {
            is_clear = true;
        } else if (*ptr == '"') {
            is_clear = false;
            ptr++;  /* skip opening quote */
            char *end = strchr(ptr, '"');
            if (end) {
                is_clear = false;
                size_t slen = end - ptr;
                if (slen >= DISPLAY_MAX_TEXT_LEN) slen = DISPLAY_MAX_TEXT_LEN - 1;
                memcpy(text_val, ptr, slen);
                text_val[slen] = '\0';
            }
        } else {
            is_clear = true;
        }
    }

    if (is_clear) {
        /* Clear alert for this tab */
        if (alerts[tab].text[0] != '\0') {
            alerts[tab].text[0] = '\0';
            alerts[tab].session[0] = '\0';
            alerts[tab].details[0] = '\0';
            alert_count--;
            alert_details_active = false;
            dprintf("Alert cleared for tab %d, remaining=%d\n", tab, alert_count);
        }
    } else {
        /* Set alert — parse session (required when setting) */
        static char session_val[DISPLAY_MAX_TEXT_LEN];
        session_val[0] = '\0';
        char *sptr = strstr(buf, "\"session\":\"");
        if (sptr) {
            sptr += 11;
            char *end = strchr(sptr, '"');
            if (end) {
                size_t slen = end - sptr;
                if (slen >= DISPLAY_MAX_TEXT_LEN) slen = DISPLAY_MAX_TEXT_LEN - 1;
                memcpy(session_val, sptr, slen);
                session_val[slen] = '\0';
            }
        }

        /* Parse "details" (optional) */
        static char details_val[DISPLAY_MAX_TEXT_LEN];
        details_val[0] = '\0';
        char *dptr = strstr(buf, "\"details\":\"");
        if (dptr) {
            dptr += 11;
            char *end = strchr(dptr, '"');
            if (end) {
                size_t slen = end - dptr;
                if (slen >= DISPLAY_MAX_TEXT_LEN) slen = DISPLAY_MAX_TEXT_LEN - 1;
                memcpy(details_val, dptr, slen);
                details_val[slen] = '\0';
            }
        }

        bool was_empty = (alerts[tab].text[0] == '\0');
        strncpy(alerts[tab].text, text_val, DISPLAY_MAX_TEXT_LEN - 1);
        alerts[tab].text[DISPLAY_MAX_TEXT_LEN - 1] = '\0';
        strncpy(alerts[tab].session, session_val, DISPLAY_MAX_TEXT_LEN - 1);
        alerts[tab].session[DISPLAY_MAX_TEXT_LEN - 1] = '\0';
        strncpy(alerts[tab].details, details_val, DISPLAY_MAX_TEXT_LEN - 1);
        alerts[tab].details[DISPLAY_MAX_TEXT_LEN - 1] = '\0';
        alerts[tab].order = alert_order_counter++;
        if (was_empty) alert_count++;
        dprintf("Alert set for tab %d: '%s' (session='%s', details='%s'), total=%d\n",
                tab, text_val, session_val, details_val, alert_count);
    }
}

/**
 * @brief Update alert state from JSON payload (host command 0x08)
 */
void display_update_alert(const char *data, uint16_t len) {
    parse_alert_json(data, len);

    /* Cancel idle dim and restore brightness (same as display_update_json) */
    last_content_change_time = timer_read32();
    if (idle_dimmed) {
        idle_dimmed = false;
        backlight_set_pwm(target_level);
    }

    if (alert_count > 0) {
        overlay_state = OVERLAY_ALERT;
    } else if (overlay_state == OVERLAY_ALERT) {
        overlay_state = OVERLAY_NONE;
    }

    render_pending = true;  /* Defer render to display_task() */
}

/**
 * @brief Dismiss the currently displayed alert (oldest by order)
 *
 * Called from button handler when no host is connected.
 * If more alerts remain the next one is shown; otherwise display goes idle.
 */
void display_alert_dismiss(void) {
    int8_t idx = find_oldest_alert();
    if (idx < 0) return;

    alerts[idx].text[0] = '\0';
    alerts[idx].session[0] = '\0';
    alerts[idx].details[0] = '\0';
    alert_count--;
    alert_details_active = false;
    dprintf("Alert dismissed for tab %d, remaining=%d\n", idx, alert_count);

    if (alert_count > 0) {
        render_pending = true;
    } else {
        overlay_state = OVERLAY_NONE;
        alert_order_counter = 0;
        backlight_set_pwm(DISPLAY_BL_DIM_LEVEL);
        display_show_logo();
    }
}

/**
 * @brief Check if any alerts are active
 */
bool display_has_alerts(void) {
    return alert_count > 0;
}

/**
 * @brief Check if a specific tab has an active alert
 */
bool display_tab_has_alert(uint8_t tab) {
    if (tab >= DISPLAY_MAX_TABS) return false;
    return alerts[tab].text[0] != '\0';
}

/**
 * @brief Toggle alert details view (Claude button hold-to-peek)
 */
void display_alert_show_details(bool show) {
    if (overlay_state != OVERLAY_ALERT) return;
    alert_details_active = show;
    display_render_alert_overlay();
}

/**
 * @brief Show softkey label overlay (3 columns with centered labels)
 */
void display_overlay_show(void) {
    if (!display || !surface) return;
    if (overlay_state == OVERLAY_ALERT) return;  /* Alert has priority */
    overlay_state = OVERLAY_SOFTKEY;

    /* Clear surface to black */
    qp_rect(surface, 0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1, 0, 0, 0, true);

    bool yolo = yolo_state_get();
    if (yolo) {
        draw_yolo_stripes(surface);
    }

    /* 3 horizontal lines, one per softkey */
    uint16_t line_h = DISPLAY_HEIGHT / 3;  /* 25 px */
    uint16_t font_h = 20;  /* Terminus Bold 10x20 */

    /* Dot identifiers: · ·· ··· (U+00B7, matching panel markings) */
    static const char *dots[] = {"\xc2\xb7", "\xc2\xb7\xc2\xb7", "\xc2\xb7\xc2\xb7\xc2\xb7"};
    uint16_t dot_x = yolo ? (YOLO_STRIPE_W + 2) : 4;
    uint16_t dot_margin = dot_x + qp_textwidth(font_large, dots[2]) + 8;

    for (uint8_t i = 0; i < SOFTKEY_COUNT; i++) {
        char label[48];
        softkeys_get_label(i, label, sizeof(label));

        /* Sanitize for font-available glyphs */
        char safe[48];
        sanitize_text(safe, sizeof(safe), label);

        uint16_t y = i * line_h + (line_h - font_h) / 2 + 2;

        /* Dot identifier (white, left-aligned) */
        qp_drawtext_recolor(surface, dot_x, y, font_large, dots[i], 0, 0, 255, 0, 0, 0);

        /* Label (white, centered in remaining space) */
        int16_t tw = qp_textwidth(font_large, safe);
        int16_t right_margin = yolo ? (YOLO_STRIPE_W + 2) : 0;
        int16_t label_area = DISPLAY_WIDTH - dot_margin - right_margin;
        int16_t x = dot_margin + (label_area - tw) / 2;
        if (x < (int16_t)dot_margin) x = dot_margin;

        qp_drawtext_recolor(surface, x, y, font_large, safe, 0, 0, 255, 0, 0, 0);
    }

    /* Thin horizontal separators (90% width, centered, subtle) */
    uint16_t sep_margin = DISPLAY_WIDTH / 20;  /* 5% each side */
    qp_line(surface, sep_margin, line_h, DISPLAY_WIDTH - 1 - sep_margin, line_h, 0, 0, 60);
    qp_line(surface, sep_margin, line_h * 2, DISPLAY_WIDTH - 1 - sep_margin, line_h * 2, 0, 0, 60);

    /* Blit to display */
    qp_surface_draw(surface, display, 0, 0, true);
}

/**
 * @brief Hide overlay and restore normal display content
 */
void display_overlay_hide(void) {
    if (overlay_state != OVERLAY_SOFTKEY) return;  /* Only dismiss softkey overlay */
    if (alert_count > 0) {
        overlay_state = OVERLAY_ALERT;
        display_render_alert_overlay();
    } else {
        overlay_state = OVERLAY_NONE;
        display_render();
    }
}

/**
 * @brief Check if companion app is connected
 */
bool display_is_connected(void) {
    return connected;
}

/**
 * @brief Keyboard post-init hook for display
 */
void keyboard_post_init_display(void) {
    display_init();
    display_backlight_init();
}
