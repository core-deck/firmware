/*
Copyright 2023 QMK
Copyright 2024 vden

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "quantum.h"
#include "i2c_master.h"
#include "matrix.h"
#include "wait.h"
#include "print.h"
#include "gpio.h"

static matrix_row_t matrix[MATRIX_ROWS];
static const uint8_t row_pins[] = PCF8574_ROW_PINS;
static const uint8_t col_pins[] = PCF8574_COL_PINS;

/* Direct pin mapping for rows 1-2
 * Row 1: GP13, GP15, GP14, GP26 (Stop, Accept, Reject, Mode)
 * Row 2: GP27, NO_PIN, NO_PIN, NO_PIN (Encoder button)
 */
static const pin_t direct_pins[2][4] = {
    {GP13, GP15, GP14, GP26},  // Row 1
    {GP27, NO_PIN, NO_PIN, NO_PIN}  // Row 2
};

static bool pcf8574_initialized = false;
static uint8_t pcf8574_error_count = 0;
static uint16_t i2c_scan_count = 0;

/* Initialize PCF8574 */
static void init_pcf8574(void) {
    if (!pcf8574_initialized) {
        dprintf("PCF8574: Starting I2C init...\n");
        dprintf("PCF8574: Address=0x%02X, Timeout=%d\n", PCF8574_I2C_ADDRESS, PCF8574_I2C_TIMEOUT);
        dprintf("PCF8574: Row pins={%d}, Col pins={%d,%d,%d,%d}\n",
                row_pins[0], col_pins[0], col_pins[1], col_pins[2], col_pins[3]);

        i2c_init();
        wait_ms(100);

        /* Set initial state:
         * - All row pins high (inactive)
         * - All column pins high (enables pull-ups for inputs)
         * - Unused pins high
         */
        uint8_t data = 0xFF;
        dprintf("PCF8574: Writing initial state 0x%02X...\n", data);
        i2c_status_t status = i2c_transmit(PCF8574_I2C_ADDRESS, &data, 1, PCF8574_I2C_TIMEOUT);

        if (status == I2C_STATUS_SUCCESS) {
            pcf8574_initialized = true;
            pcf8574_error_count = 0;
            dprintf("PCF8574: Initialized successfully!\n");

            /* Test read */
            uint8_t test_read = 0;
            status = i2c_receive(PCF8574_I2C_ADDRESS, &test_read, 1, PCF8574_I2C_TIMEOUT);
            if (status == I2C_STATUS_SUCCESS) {
                dprintf("PCF8574: Test read OK, state=0x%02X\n", test_read);
            } else {
                dprintf("PCF8574: Test read FAILED, status=%d\n", status);
            }
        } else {
            dprintf("PCF8574: Init FAILED! status=%d (0=OK, 1=ADDR_NACK, 2=DATA_NACK, 3=ERROR)\n", status);
        }
    }
}

/* Read columns for a given row */
static matrix_row_t read_cols(uint8_t row) {
    matrix_row_t cols = 0;

    if (!pcf8574_initialized) {
        return 0;
    }

    /* Select row by setting it LOW, others HIGH
     * Also keep column pins HIGH (for pull-ups) and unused pins HIGH
     */
    uint8_t data = 0xFF;
    data &= ~(1 << row_pins[row]);  // Set selected row LOW

    /* Write row selection to PCF8574 */
    i2c_status_t status = i2c_transmit(PCF8574_I2C_ADDRESS, &data, 1, PCF8574_I2C_TIMEOUT);

    if (status != I2C_STATUS_SUCCESS) {
        pcf8574_error_count++;
        if (pcf8574_error_count == 1 || pcf8574_error_count % 100 == 0) {
            dprintf("PCF8574: Write FAILED! status=%d, data=0x%02X, errors=%d\n",
                    status, data, pcf8574_error_count);
        }
        return 0;
    }

    /* Wait for signal to stabilize */
    wait_us(30);

    /* Read back the state */
    uint8_t pin_state = 0;
    status = i2c_receive(PCF8574_I2C_ADDRESS, &pin_state, 1, PCF8574_I2C_TIMEOUT);

    if (status != I2C_STATUS_SUCCESS) {
        pcf8574_error_count++;
        if (pcf8574_error_count == 1 || pcf8574_error_count % 100 == 0) {
            dprintf("PCF8574: Read FAILED! status=%d, errors=%d\n", status, pcf8574_error_count);
        }
        return 0;
    }

    /* Reset error count on successful read */
    pcf8574_error_count = 0;

    /* Deselect row (set all rows HIGH) */
    data = 0xFF;
    i2c_transmit(PCF8574_I2C_ADDRESS, &data, 1, PCF8574_I2C_TIMEOUT);

    /* Parse column states
     * A pressed key will read as LOW (0), unpressed as HIGH (1)
     * We invert the logic for the matrix
     */
    for (uint8_t col = 0; col < MATRIX_COLS; col++) {
        if (!(pin_state & (1 << col_pins[col]))) {
            cols |= (1 << col);
        }
    }

    /* Periodic debug logging (every ~10000 scans, roughly every 10 seconds) */
    i2c_scan_count++;
    if (i2c_scan_count % 10000 == 0) {
        dprintf("PCF8574: Scan #%u OK, cols=0x%02X\n", i2c_scan_count, cols);
    }

    return cols;
}

void matrix_init_custom(void) {
    /* Initialize matrix state */
    for (uint8_t i = 0; i < MATRIX_ROWS; i++) {
        matrix[i] = 0;
    }

    dprintf("\n");
    dprintf("========================================\n");
    dprintf("Matrix Init: Starting...\n");
    dprintf("========================================\n");
    dprintf("Matrix: %d rows x %d cols\n", MATRIX_ROWS, MATRIX_COLS);
    dprintf("  Row 0: PCF8574 I2C (Clear, Verbose, Model, Claude)\n");
    dprintf("  Row 1: Direct GPIO (Stop, Accept, Reject, Mode)\n");
    dprintf("  Row 2: Encoder button\n");
    dprintf("----------------------------------------\n");

    /* Initialize PCF8574 */
    init_pcf8574();

    dprintf("----------------------------------------\n");

    /* Initialize direct GPIO pins (rows 1-2) as inputs with pull-ups */
    for (uint8_t row = 0; row < 2; row++) {
        for (uint8_t col = 0; col < 4; col++) {
            pin_t pin = direct_pins[row][col];
            if (pin != NO_PIN) {
                gpio_set_pin_input_high(pin);
                dprintf("Direct GPIO: row %d col %d = GP%lu OK\n", row + 1, col, (unsigned long)pin);
            }
        }
    }

    dprintf("========================================\n");
    dprintf("Matrix Init: Complete!\n");
    dprintf("========================================\n\n");
}

static uint16_t not_init_warn_count = 0;

bool matrix_scan_custom(matrix_row_t current_matrix[]) {
    bool changed = false;

    /* Try to reinitialize if PCF8574 is not responding */
    if (!pcf8574_initialized) {
        not_init_warn_count++;
        if (not_init_warn_count == 1 || not_init_warn_count % 1000 == 0) {
            dprintf("PCF8574: NOT INITIALIZED! Attempt #%u\n", not_init_warn_count);
        }
        init_pcf8574();
        if (!pcf8574_initialized) {
            /* Still not initialized, try again next scan */
            return false;
        }
    }

    /* Scan PCF8574 matrix (row 0 only) */
    matrix_row_t cols = read_cols(0);

    if (current_matrix[0] != cols) {
        dprintf("PCF8574: Row 0 CHANGED: 0x%02X -> 0x%02X\n", current_matrix[0], cols);
        current_matrix[0] = cols;
        changed = true;
    }

    /* Scan direct GPIO pins (rows 1-2) */
    for (uint8_t row = 0; row < 2; row++) {
        matrix_row_t direct_cols = 0;

        for (uint8_t col = 0; col < 4; col++) {
            pin_t pin = direct_pins[row][col];
            if (pin != NO_PIN) {
                /* Read pin state - active LOW (pressed = 0, released = 1) */
                if (!gpio_read_pin(pin)) {
                    direct_cols |= (1 << col);
                }
            }
        }

        uint8_t matrix_row = row + 1;  // Rows 1-2
        if (current_matrix[matrix_row] != direct_cols) {
            dprintf("Matrix Scan: Direct GPIO row %d changed: 0x%02X -> 0x%02X\n", matrix_row, current_matrix[matrix_row], direct_cols);
            current_matrix[matrix_row] = direct_cols;
            changed = true;
        }
    }

    return changed;
}
