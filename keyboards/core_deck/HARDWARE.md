# Core Deck Hardware Guide

## RP2040-Tiny Board

The Core Deck is designed for the **RP2040-Tiny** board, which has limited GPIO pins exposed:
- **Available pins:** GP0-GP15, GP26-GP29 (20 pins total)
- **Missing pins:** GP16-GP25 (not broken out on RP2040-Tiny)

## Complete Pin Assignment

### GPIO Pin Usage Summary

| RP2040 Pin | Function | Component | Notes |
|------------|----------|-----------|-------|
| **GP0** | PWM | Display BL | Backlight brightness control |
| **GP2** | WS2812 Data | RGB LEDs | Addressable LED data line (9 LEDs) |
| **GP3** | SPI MISO | Display | SPI data in (optional, can float if not needed) |
| **GP4** | I2C SDA | PCF8574 | Button matrix I2C data |
| **GP5** | I2C SCL | PCF8574 | Button matrix I2C clock |
| **GP6** | SPI SCK | Display | SPI clock |
| **GP7** | SPI MOSI | Display | SPI data out |
| **GP8** | CS | Display | Chip select |
| **GP9** | DC | Display | Data/Command select |
| **GP10** | RST | Display | Reset pin |
| **GP11** | Encoder A | EC11 | Rotary encoder phase A |
| **GP12** | Encoder B | EC11 | Rotary encoder phase B |
| **GP13** | Stop Button | Direct GPIO | Ctrl-C |
| **GP14** | Accept Button | Direct GPIO | Enter |
| **GP15** | Reject Button | Direct GPIO | Esc |
| **GP26** | Mode Button | Direct GPIO | Shift-Tab |
| **GP27** | Encoder Button | Direct GPIO | Enter |
| **GP28** | DIP Switch | YOLO Mode | Toggle switch input |

**Pins used:** 18 out of 20 available
**Pins remaining:** GP1, GP29 (2 pins free)

### Future Expansion Options

The following pins are still available for additional features:

| Available Pins | Suggested Use Cases |
|----------------|---------------------|
| GP1 | UART RX for debugging or serial communication |
| GP29 (ADC3) | Analog input (potentiometer, sensor) |

## Wiring Diagram

### PCF8574 I2C GPIO Expander (Button Matrix)

The 4-button matrix (1 row × 4 columns) is connected via PCF8574 I2C expander to save GPIO pins.

```
RP2040-Tiny          PCF8574
-----------          --------
GP4 (SDA) -------- SDA
GP5 (SCL) -------- SCL
3.3V ------------- VCC
GND -------------- GND
                   A0, A1, A2 = GND (address 0x20)

PCF8574 Pin Mapping (1x4 matrix):
  P2 = Row (active-low output) - reuses existing PCB row 2
  P3, P4, P5, P6 = Columns (inputs with internal pull-ups)
  P0, P1, P7 = Unused (set high)

Matrix Layout:
         Col0(P3)  Col1(P4)  Col2(P5)  Col3(P6)
Row0(P2)   [0,0]     [0,1]     [0,2]     [0,3]

Button Functions (from keymap):
         Col0       Col1       Col2       Col3
Row0   Claude     Clear*     Verbose*   Model*
* = Soft key (reconfigurable via HID command 0x04)
```

**Scanning Method:**
1. Set row LOW (P0), others HIGH
2. Read column pins (P3-P6) - pressed buttons read LOW

**Address Configuration:**
- PCF8574: A0=A1=A2=GND → Address 0x20 (7-bit)
- PCF8574A: A0=A1=A2=GND → Address 0x38 (7-bit)
- Change in config.h if using A variant

**QMK I2C Configuration (Important!):**

QMK's I2C driver has several gotchas for RP2040:

1. **Address format**: QMK uses 8-bit I2C addresses (7-bit address shifted left by 1):
   ```c
   #define PCF8574_I2C_ADDRESS (0x20 << 1)  // 0x40 for PCF8574
   #define PCF8574_I2C_ADDRESS (0x38 << 1)  // 0x70 for PCF8574A
   ```

2. **Pin names**: QMK always uses `I2C1_SDA_PIN` and `I2C1_SCL_PIN` regardless of which I2C peripheral you use. The "1" is historical naming, not tied to I2CD0/I2CD1:
   ```c
   // config.h - CORRECT
   #define I2C_DRIVER I2CD0
   #define I2C1_SDA_PIN GP4    // Note: I2C1_, not I2C0_!
   #define I2C1_SCL_PIN GP5

   // WRONG - these are ignored!
   #define I2C0_SDA_PIN GP4
   #define I2C0_SCL_PIN GP5
   ```

3. **Required configuration files**:
   - `halconf.h`: Must have `#define HAL_USE_I2C TRUE`
   - `mcuconf.h`: Must have `#define RP_I2C_USE_I2C0 TRUE` (or I2C1)
   - `config.h`: Define `I2C_DRIVER`, `I2C1_SDA_PIN`, `I2C1_SCL_PIN`

### ST7789 TFT Display (SPI)

```
RP2040-Tiny       Display
-----------       --------
GP6 (SCK)  ------ SCK/SCLK
GP7 (MOSI) ------ SDA/MOSI
GP3 (MISO) ------ (not connected - display is write-only)
GP8 (CS)   ------ CS
GP9 (DC)   ------ DC
GP10 (RST) ------ RST/RESET
3.3V ------------ VCC
GND ------------- GND
```

**Display:** ST7789 (284x76), landscape orientation

### Display Backlight (PWM)

```
RP2040-Tiny       Display
-----------       --------
GP0 (PWM0) ------ BL/LED (Backlight control)
```

**Configuration:**
- **Pin:** GP0 (PWM slice 0, channel A)
- **PWM Frequency:** ~3.9kHz (1MHz clock / 256 period)
- **Brightness Range:** 0-255

**Behavior:**
- **Startup:** Display starts dimmed (5/255) until companion app connects
- **Connected:** Brightness at user-set level (default 127)
- **Timeout:** If no ping/data for 5 seconds, dims to 5/255
- **Reconnect:** Restores full brightness on next ping
- **Persistence:** Brightness stored in `kb_config_t` EEPROM datablock (shared with soft key settings)

### EC11 Rotary Encoder

```
RP2040-Tiny      EC11 Encoder
-----------      -------------
GP11 ----------- A (phase A)
GP12 ----------- B (phase B)
GND ------------ C (common/ground)
(GP24) --------- SW (push button - optional, not yet configured)
```

**Specifications:**
- Type: Incremental quadrature encoder
- Resolution: 4 pulses per detent (standard EC11)
- Built-in debouncing in firmware

**Optional Push Button:**
The EC11 has a built-in momentary switch (SW pin). Currently not configured, but can be added to any available GPIO (e.g., GP13, GP14, or GP15).

### YOLO Mode Toggle Switch

```
RP2040-Tiny      SPST Switch
-----------      ------------
GP28 ----------- Terminal 1
GND ------------ Terminal 2
```

**Type:** SPST (Single Pole Single Throw) toggle switch
**Behavior:**
- Switch ON (GP28 → GND): Sends "YOLO MODE ACTIVATED!"
- Switch OFF (GP28 → floating/high): Sends "YOLO mode deactivated."

### WS2812B RGB LEDs (9 LEDs)

```
RP2040-Tiny      WS2812B LED Strip
-----------      ------------------
GP2 ------------ DIN (Data In)
5V ------------- VDD (Power)
GND ------------ GND (Ground)
```

**LED Configuration:**
- **Type:** WS2812B individually addressable RGB LEDs
- **Count:** 9 LEDs (one under each button)
- **Data Pin:** GP2
- **Wiring:** Daisy-chain (DOUT → DIN for each LED in sequence)

**LED Mapping** (chain order from `keyboard.json` `rgb_matrix.layout`):
```
LED Index    Matrix Position    Button Function
---------    ---------------    ---------------
0            [0,3]              Claude
1            [0,2]              Model
2            [0,1]              Verbose
3            [0,0]              Clear

4            [1,3]              Mode  (cycles default/plan/accept)
5            [1,1]              Accept
6            [1,2]              Reject
7            [1,0]              Stop

8            [2,0]              Encoder button
```

Note: row 1 is wired so matrix `[1,3]` is the **leftmost** direct-GPIO button
(Mode) and `[1,0]` is the rightmost (Stop). Row 1 visual order, left-to-right:
**Mode | Accept | Reject | Stop**.

**LED Behaviors:**
- **Reactive glow (all LEDs):** Constant dim base + brief brighter flash on press, fading back to base. Custom `GLOW_REACTIVE` effect (`rgb_matrix_kb.inc`).
- **Mode button (LED 4):** Overrides the base color to indicate Claude Code mode:
  - Default: no override (uses normal reactive effect)
  - Plan mode: cyan (HSV hue 128)
  - Accept mode: purple (HSV hue 191)
  - Cycles default → accept → plan → default on each press
  - Companion app can also set the mode via HID command 0x07
- **Alert mode:** When a host alert is active, all LEDs override to red.

**Power Consumption:**
- Maximum brightness limited to 130/255 (~51%) to stay under 500mA total
- 9 LEDs × ~35mA average = ~315mA (safe under USB 500mA limit)
- At full brightness: 9 LEDs × 60mA = 540mA (would exceed limit, hence brightness cap)

## RAW_HID Protocol

The companion app communicates with the keyboard via RAW_HID using a chunked protocol (32-byte packets with a 2-byte header). Messages can span multiple packets for payloads larger than 30 bytes.

The protocol handler lives in `protocol.c`, which dispatches commands to `display.c` (rendering, backlight) and `softkeys.c` (key configuration) as needed.

See [PROTOCOL.md](PROTOCOL.md) for the full protocol specification, packet format, command reference, and companion app examples.

**Quick reference:**

| Command | Description |
|---------|-------------|
| 0x01 | Update display (JSON, chunked) |
| 0x02 | Ping / keep-alive |
| 0x03 | Set display brightness |
| 0x04 | Set soft key assignment |
| 0x05 | Get soft key assignment |
| 0x06 | Reset all soft keys |
| 0x07 | Set mode state (default/plan/accept) |

**Connection Keep-Alive:**
The companion app should send pings (0x02) or data updates (0x01) at least every 5 seconds
to prevent the display from auto-dimming.

### EEPROM Storage

All persistent settings (soft keys + backlight) are stored in a single EEPROM datablock (388 bytes) managed by QMK's `EECONFIG_KB_DATA_SIZE` API. This avoids raw EEPROM address conflicts with QMK core.

## PCB Design Recommendations

If designing a custom PCB:

1. **Pull-up/Pull-down Resistors:**
   - GP28 (YOLO switch): Add 10kΩ pull-up to 3.3V
   - Encoder pins (GP11, GP12): Add 10kΩ pull-ups to 3.3V
   - Direct GPIO buttons (GP13-GP15, GP26-GP27): Internal pull-ups enabled in firmware
   - Optional: 100nF capacitors on encoder A/B pins to GND for hardware debouncing

2. **Decoupling Capacitors:**
   - 100nF ceramic capacitor near each IC's VCC pin
   - 10µF electrolytic capacitor on RP2040-Tiny VCC
   - 100µF electrolytic near WS2812B strip for LED current smoothing

3. **Display Power:**
   - ST7789 can draw 20-50mA during display updates
   - Ensure adequate 3.3V supply (RP2040-Tiny provides ~300mA from USB)

4. **I2C Pull-ups:**
   - 4.7kΩ pull-ups on SDA/SCL (usually already present on RP2040-Tiny)
   - If using long cables, reduce to 2.2kΩ

5. **WS2812B LED Wiring:**
   - **Data line:** GP2 → 330Ω resistor → first LED DIN
   - **Power:** 5V from USB (not 3.3V!) - WS2812B needs 4.5-5.5V
   - **Decoupling:** 100nF capacitor across VDD/GND at each LED
   - **Bulk capacitance:** 100-470µF electrolytic at power entry point
   - **Current handling:** Ensure PCB traces can handle 500mA (use at least 10mil/0.25mm width)
   - **Data signal:** Keep data line short, use series resistor (220-330Ω) near GP2
   - **Level shifting (optional):** GP2 outputs 3.3V, WS2812B expects 5V logic
     - Most WS2812B work with 3.3V signals when powered at 5V
     - For reliability, consider 74AHCT125 level shifter (3.3V → 5V)
   - **LED order:** Wire in sequence matching the LED index map (0→1→2...→13)

## Assembly Tips

### Testing Order

1. **I2C Communication (PCF8574):**
   - Flash firmware
   - Connect I2C (GP4, GP5)
   - Run `qmk console` and check for PCF8574 errors
   - Test button matrix

2. **Display:**
   - Connect SPI pins (GP6, GP7, GP8, GP9, GP10)
   - Power on - display should initialize
   - Check `qmk console` for display init messages

3. **Encoder:**
   - Connect GP11, GP12 to encoder A, B
   - Rotate and observe volume/keypress events

4. **YOLO Switch:**
   - Connect GP28 and GND
   - Toggle switch and observe messages

### Common Issues

**PCF8574 not responding (I2C timeout -2):**
- Verify QMK I2C pin config uses `I2C1_SDA_PIN`/`I2C1_SCL_PIN` (not `I2C0_*`)
- Check `halconf.h` has `HAL_USE_I2C TRUE`
- Check `mcuconf.h` has `RP_I2C_USE_I2C0 TRUE`
- Verify I2C address is shifted: `(0x20 << 1)` not just `0x20`
- Verify SDA/SCL not swapped
- Check pull-ups present (measure 3.3V on idle SDA/SCL)
- Check PCF8574 is powered (VCC = 3.3V)

**PCF8574 I2C address NACK (status 1):**
- Wrong I2C address (PCF8574 = 0x20, PCF8574A = 0x38)
- Address pins A0/A1/A2 not grounded properly
- PCF8574 not powered or damaged

**Display shows nothing:**
- Verify correct display type selected (ST7789)
- Check SPI wiring (SCK, MOSI, CS, DC, RST)
- Ensure 3.3V power supply adequate
- Try different SPI clock speed (change divisor in display.c)

**Encoder skipping/double-counting:**
- Add hardware capacitors (100nF on A and B pins)
- Uncomment `ENCODER_DIRECTION_FLIP` if rotation backwards
- Adjust `ENCODER_MAP_KEY_DELAY` in config.h

**YOLO switch not registering:**
- Add 10kΩ pull-up resistor to GP28
- Check switch orientation (GP28 should be HIGH when open)

## Power Requirements

| Component | Typical Current | Peak Current |
|-----------|----------------|--------------|
| RP2040-Tiny | 20mA | 50mA |
| PCF8574 | 1mA | 5mA |
| ST7789 | 10mA (idle) | 50mA (full white) |
| EC11 Encoder | 0mA | 0mA (passive) |
| WS2812B LEDs (9×) | 225mA (avg at 51% brightness) | 315mA (capped by firmware) |
| **Total** | ~260mA | ~420mA |

**Important:** USB 2.0 provides 500mA maximum. The firmware limits LED brightness to 130/255 (~51%) to keep total current under 500mA. At full brightness, LEDs alone would draw 540mA, which could exceed USB limits.

**Current Safety:**
- Firmware enforces `RGB_MATRIX_MAXIMUM_BRIGHTNESS = 130`
- 9 LEDs × 35mA (at 51% brightness) = 315mA for LEDs
- Total system: 315mA + 50mA (other components) = 365mA nominal
- Reactive effects briefly increase brightness but stay within safe limits

## Pin Change History

**Original Configuration (for full RP2040 boards):**
- Used GP16-GP23 which are NOT available on RP2040-Tiny
- Required 10 GPIO pins

**Current Configuration (RP2040-Tiny compatible):**
- All pins moved to GP0-GP15 and GP26-GP29 range
- Uses only 11 of 20 available pins
- Leaves 9 pins free for expansion

### Migration from Full RP2040

If migrating from a full RP2040 board (e.g., Raspberry Pi Pico) to RP2040-Tiny, update wiring:

| Old Pin | New Pin | Function |
|---------|---------|----------|
| GP16 | GP3 | SPI MISO |
| GP17 | GP8 | Display CS |
| GP18 | GP6 | SPI SCK |
| GP19 | GP7 | SPI MOSI |
| GP20 | GP9 | Display DC |
| GP21 | GP10 | Display RST |
| GP22 | GP11 | Encoder A |
| GP23 | GP12 | Encoder B |

No firmware changes needed - config.h and keyboard.json already updated.
