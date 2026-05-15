// Copyright 2023 QMK
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/* Matrix configuration
 * Row 0: PCF8574 matrix (1x4 = 4 buttons: Clear, Verbose, Model, Claude)
 * Row 1: Direct GPIO buttons (4 buttons: Stop, Accept, Reject, Mode)
 * Row 2: Encoder button (1 button: Enter)
 */
#define MATRIX_ROWS 3
#define MATRIX_COLS 4

#define BOOTMAGIC_ROW 0
#define BOOTMAGIC_COLUMN 3

/* I2C Configuration for RP2040 */
#define I2C_DRIVER I2CD0
#define I2C1_SDA_PIN GP4
#define I2C1_SCL_PIN GP5

/* PCF8574 I2C Configuration */
// Note: QMK uses 8-bit I2C address format (7-bit address << 1)
// PCF8574 7-bit address: 0x20 -> 8-bit: 0x40
// PCF8574A 7-bit address: 0x38 -> 8-bit: 0x70
#define PCF8574_I2C_ADDRESS (0x20 << 1)  // 0x40 - PCF8574 with A0-A2 grounded
#define PCF8574_I2C_TIMEOUT 100

/* PCF8574 Pin Mapping for 1x4 matrix
 * P2: Row (output) - uses existing PCB row 2 (soft buttons line)
 * P3, P4, P5, P6: Columns (inputs with pull-ups)
 * P0, P1, P7: Unused (set high)
 */
#define PCF8574_ROW_PINS {2}
#define PCF8574_COL_PINS {3, 4, 5, 6}

/* Debounce reduces chatter (unintended double-presses) */
#define DEBOUNCE 5

/* Direct GPIO Button Pins (handled in custom matrix.c)
 * Row 1: GP13, GP15, GP14, GP26 (Stop, Accept, Reject, Mode)
 * Row 2: GP27 (Encoder button)
 */

/* YOLO Mode Switch - SPST toggle switch connected to GPIO */
#define DIP_SWITCH_PINS { GP28 }  // Change to your desired GPIO pin
// #define QUANTUM_PAINTER_DEBUG

/* TFT Display Configuration */
#define QUANTUM_PAINTER_SUPPORTS_NATIVE_COLORS TRUE
#define ST7789_NUM_DEVICES 1
#define DISPLAY_WIDTH 284
#define DISPLAY_HEIGHT 76
#define ST7789_NO_AUTOMATIC_VIEWPORT_OFFSETS

/* SPI Configuration for Display - RP2040-Tiny Compatible (GP0-GP15, GP26-GP29) */
#define SPI_DRIVER SPID0
#define SPI_SCK_PIN GP6    // Changed from GP18 for RP2040-Tiny
#define SPI_MOSI_PIN GP7   // Changed from GP19 for RP2040-Tiny
#define SPI_MISO_PIN GP3   // Changed from GP16 for RP2040-Tiny

/* Display Control Pins - RP2040-Tiny Compatible */
#define DISPLAY_CS_PIN GP8   // Changed from GP17 for RP2040-Tiny
#define DISPLAY_DC_PIN GP9   // Changed from GP20 for RP2040-Tiny
#define DISPLAY_RST_PIN GP10 // Changed from GP21 for RP2040-Tiny

/* Quantum Painter Configuration */
#define QUANTUM_PAINTER_DISPLAY_TIMEOUT 0  // Never timeout (0 = infinite)

/* RGB Matrix Configuration - WS2812B LEDs */
#define WS2812_PIO_USE_PIO1  // Use PIO1 to avoid conflicts with other peripherals

/* Current Limiting for 500mA total:
 * 9 LEDs × 35mA average = 315mA (safe margin)
 * Maximum brightness limited to 130/255 (~51%) in keyboard.json
 */
#define RGB_MATRIX_LED_COUNT 9
#define RGB_MATRIX_KEYPRESSES  // Enable reactive effects
#define RGB_MATRIX_FRAMEBUFFER_EFFECTS  // Enable frame buffer effects

/* RGB Matrix defaults */
#define RGB_MATRIX_DEFAULT_MODE RGB_MATRIX_CUSTOM_GLOW_REACTIVE  // Constant dim glow + flash on keypress
#define RGB_MATRIX_DEFAULT_HUE 17    // Orange base color (#FF6600)
#define RGB_MATRIX_DEFAULT_SAT 255   // Full saturation
#define RGB_MATRIX_DEFAULT_VAL 104   // Medium brightness
#define RGB_MATRIX_DEFAULT_SPD 128   // Medium speed

/* Debug Configuration - Maximum Verbosity */
// Note: Other debug options controlled via rules.mk and console feature

/* Bootloader Configuration */
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET           // Enable double-tap to enter bootloader
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 500U  // Time window for double-tap (ms)
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK 0U   // No LED indicator

/* Rotary Encoder Configuration */
// EC11 encoders can be noisy - these settings provide robust debouncing
#define ENCODER_MAP_ENABLE                           // Enable encoder map in keymap
#define ENCODER_DEFAULT_POS 0x3                      // Default position bitmask (both pins high)

// Debouncing for noisy EC11 encoders
// QMK reads encoder every matrix scan (default ~1ms). These settings filter noise:
// #define ENCODER_DIRECTION_FLIP                       // Uncomment if rotation is backwards
// #define ENCODER_RESOLUTIONS { 4 }                 // Already set in keyboard.json (4 pulses per detent)

// Additional filtering for very noisy encoders (optional, uncomment if needed):
// #define ENCODER_MAP_KEY_DELAY 10                  // Delay between encoder actions (ms)

/* EEPROM Datablock Configuration
 * kb_config_t = 3 softkeys × 129 bytes + 1 backlight + 1 theme_version + 10 theme colors × 3 bytes */
#define EECONFIG_KB_DATA_SIZE 419

/* Display Backlight Configuration */
#define DISPLAY_BL_PIN GP0
#define DISPLAY_BL_PWM_DRIVER PWMD0
#define DISPLAY_BL_PWM_CHANNEL 0
#define DISPLAY_BL_DEFAULT_LEVEL 178      // Default brightness (reduced for high-VLT cover)
#define DISPLAY_BL_DIM_LEVEL 125         // Dimmed brightness when disconnected
#define DISPLAY_BL_PING_TIMEOUT_MS 30000  // Dim after 30s without host activity
#define DISPLAY_BL_IDLE_TIMEOUT_MS (15 * 60 * 1000)  // Dim after 15min without content change
