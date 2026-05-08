// Copyright 2024 vden
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>
#include <stdbool.h>

#define DISPLAY_MAX_TEXT_LEN 128
#define DISPLAY_MAX_TABS 16

/**
 * @brief Structure to hold display data
 */
typedef struct {
    char session[DISPLAY_MAX_TEXT_LEN];
    char task[DISPLAY_MAX_TEXT_LEN];
    char task2[DISPLAY_MAX_TEXT_LEN];  // second task line (pre-split by app)
    uint8_t tabs[DISPLAY_MAX_TABS];   // 0=inactive, 1=loaded, 2=working
    uint8_t tab_count;                // 0-16
    int8_t  active_tab;               // -1 = none
    uint8_t context_percent;          // 0-100, context window usage
} display_data_t;

/**
 * @brief Alert entry for a single tab
 */
typedef struct {
    char session[DISPLAY_MAX_TEXT_LEN]; // session name for this alert's tab
    char text[DISPLAY_MAX_TEXT_LEN];    // empty = no alert
    char details[DISPLAY_MAX_TEXT_LEN]; // optional extended details (shown on hold)
    uint32_t order;                     // monotonic insertion counter
} alert_entry_t;

/**
 * @brief Overlay state machine
 */
typedef enum {
    OVERLAY_NONE,
    OVERLAY_SOFTKEY,
    OVERLAY_ALERT,     // highest priority
} overlay_state_t;

/**
 * @brief Initialize the display
 */
void display_init(void);

/**
 * @brief Render the display with current data
 */
void display_render(void);


/**
 * @brief Show logo for idle mode
 */
void display_show_logo(void);

/**
 * @brief Keyboard post-init hook for display
 */
void keyboard_post_init_display(void);

/**
 * @brief Initialize display backlight PWM
 */
void display_backlight_init(void);

/**
 * @brief Set display backlight level
 * @param level Brightness level (0-255)
 */
void display_backlight_set(uint8_t level);

/**
 * @brief Get current display backlight level
 * @return Current brightness level (0-255)
 */
uint8_t display_backlight_get(void);

/**
 * @brief Save current brightness to EEPROM
 */
void display_backlight_save(void);

/**
 * @brief Update display with JSON data from companion app
 * @param data Raw payload bytes (will be null-terminated internally)
 * @param len Length of payload
 */
void display_update_json(const char *data, uint16_t len);

/**
 * @brief Called when ping/pong or data is received from companion app
 */
void display_ping_received(void);

/**
 * @brief Transition to disconnected/idle state (dim, show logo, clear overlays)
 */
void display_host_disconnected(void);

/**
 * @brief Display housekeeping task - check for ping timeout
 */
void display_task(void);

/**
 * @brief Show softkey label overlay (3 columns with labels)
 */
void display_overlay_show(void);

/**
 * @brief Hide overlay and restore normal display content
 */
void display_overlay_hide(void);

/**
 * @brief Update alert state from JSON payload (host command 0x08)
 * @param data Raw payload bytes (will be null-terminated internally)
 * @param len Length of payload
 */
void display_update_alert(const char *data, uint16_t len);

/**
 * @brief Dismiss the currently displayed alert (oldest)
 *
 * Used for firmware-side dismissal when no host app is connected.
 * Shows the next alert if any remain, otherwise transitions to idle.
 */
void display_alert_dismiss(void);

/**
 * @brief Check if any alerts are active
 * @return true if at least one alert exists
 */
bool display_has_alerts(void);

/**
 * @brief Check if a specific tab has an active alert
 * @param tab Tab index
 * @return true if the tab has an alert
 */
bool display_tab_has_alert(uint8_t tab);

/**
 * @brief Show or hide alert details view (hold-to-peek on Claude button)
 * @param show true to show details, false to restore normal alert
 */
void display_alert_show_details(bool show);

/**
 * @brief Wake display from idle dim (call on user activity like key press)
 */
void display_activity(void);

/**
 * @brief Check if companion app is connected (ping within timeout)
 * @return true if connected
 */
bool display_is_connected(void);
