# Core Deck

A Claude Code-optimized macropad with TFT display, RGB lighting, and rotary encoder.

![Core Deck](https://via.placeholder.com/600x400?text=Core+Deck)

## Features

### Hardware
- **9 Buttons**: 4 on PCF8574 I2C matrix + 4 direct GPIO buttons + 1 encoder button
- **Rotary Encoder**: EC11 with push button (Arrow Up/Down navigation)
- **TFT Display**: ST7789 (284×76) with live task info
- **RGB Lighting**: 9 WS2812B addressable LEDs with reactive effects
- **YOLO Mode**: Hardware toggle switch for risky operations
- **Controller**: RP2040-Tiny (compatible with limited GPIO)

### Software
- **Plug & Play**: Works as standard USB keyboard, no companion app required
- **Optional Companion App**: Display shows real-time Claude Code task info via Raw HID
- **Configurable Soft Keys**: 3 soft keys (Clear, Verbose, Model) reconfigurable at runtime via HID
- **Smart LEDs**: Reactive button lighting + Mode button (3-state: default/cyan/purple)
- **Low Power**: Current limited to 500mA (USB 2.0 compliant)

## Button Layout

### PCF8574 Matrix (1×4) - I2C Connected
```
┌──────────┬──────────┬──────────┬──────────┐
│ Claude   │ Clear*   │ Verbose* │ Model*   │
│   F20    │ Esc-Esc  │ Ctrl-O   │ /model   │
└──────────┴──────────┴──────────┴──────────┘
* = Soft key (reconfigurable via HID)
```

### Direct GPIO Buttons
```
┌──────────┬──────────┬──────────┬──────────┐
│  Stop    │ Accept   │ Reject   │   Mode   │
│ Ctrl-C   │  Enter   │   Esc    │Shift-Tab │
└──────────┴──────────┴──────────┴──────────┘

┌──────────┐
│Enc Button│  Rotary Encoder: ↑/↓ (rotate), Enter (press)
│  Enter   │
└──────────┘
```

### Hardware Switch
```
YOLO Mode Toggle: GP28 (sends text message on state change)
```

## Key Functions

| Key | Default Keycode | Purpose |
|-----|---------|---------|
| **Claude** | F20 | Trigger Claude (companion app) |
| **Clear** | Esc-Esc | Clear input (soft key) |
| **Verbose** | Ctrl-O | Toggle verbose mode (soft key) |
| **Model** | /model + Enter | Model select (soft key) |
| **Stop** | Ctrl-C | Interrupt execution |
| **Accept** | Enter | Confirm action |
| **Reject** | Esc | Cancel action |
| **Mode** | Shift-Tab | Switch Claude Code mode (plan/edit/etc) |
| **Encoder ↑↓** | Arrow Up/Down | Navigate output |
| **Encoder Press** | Enter | Select/Confirm |

## RGB LED Behaviors

### Standard Buttons (8 LEDs)
- **Idle**: Dim cyan glow
- **Pressed**: Bright flash
- **Effect**: Solid Reactive Simple

### Mode Button (LED 7)
- **Default**: Normal reactive effect (no override)
- **Plan mode**: Cyan — planning mode active
- **Accept mode**: Purple — accept changes mode active
- **Cycling**: Each press cycles through default → plan → accept → default
- **HID sync**: Companion app can set mode via command 0x07

**Note**: Brightness limited to 51% (130/255) to stay under 500mA USB limit.

## Display

### Without Companion App
Shows logo (idle mode)

### With Companion App (via Raw HID)
Shows real-time Claude Code information:
- Session name
- Current task description

**Display:** ST7789 284×76 landscape

## Building Firmware

### Prerequisites
```bash
# Install QMK CLI
python3 -m pip install qmk

# Setup QMK
qmk setup
```

### Compile
```bash
cd qmk_firmware
qmk compile -kb core_deck/rev1 -km default
```

### Flash
1. Hold BOOTSEL button on RP2040-Tiny
2. Plug in USB cable
3. Copy `.uf2` file to RPI-RP2 drive
4. Or use: `qmk flash -kb core_deck/rev1 -km default`

### Alternative: Double-Tap Reset
Firmware includes double-tap bootloader entry:
1. Press Reset button twice within 500ms
2. Device enters bootloader mode
3. Copy firmware as above

## Hardware Setup

### Bill of Materials

| Component | Quantity | Notes |
|-----------|----------|-------|
| RP2040-Tiny | 1 | Waveshare or compatible |
| PCF8574 | 1 | I2C GPIO expander (address 0x20) |
| ST7789 | 1 | TFT display module (284×76) |
| EC11 Rotary Encoder | 1 | With push button |
| WS2812B LEDs | 9 | Individual or strip |
| Mechanical Switches | 8 | Cherry MX or compatible |
| SPST Toggle Switch | 1 | YOLO mode |
| Keycaps | 8 | 1U size |

### Pin Assignments

See [HARDWARE.md](HARDWARE.md) for complete wiring guide.

**Quick Reference:**
- **I2C (PCF8574)**: GP4 (SDA), GP5 (SCL)
- **Display (SPI)**: GP6 (SCK), GP7 (MOSI), GP8 (CS), GP9 (DC), GP10 (RST)
- **Encoder**: GP11 (A), GP12 (B), GP27 (button)
- **Direct Buttons**: GP13-GP15, GP26
- **RGB LEDs**: GP2 (data)
- **YOLO Switch**: GP28

**Pins Used**: 18 of 20
**Pins Free**: GP1, GP29

## Companion App

The display can show real-time Claude Code information via Raw HID.

### USB HID Configuration
- **VID**: 0xFEED
- **PID**: 0x0803
- **Usage Page**: 0xFF60
- **Usage**: 0x61
- **Report Size**: 32 bytes

### Communication Protocol

| Command | Direction | Description |
|---------|-----------|-------------|
| 0x01 | Host->Device | Update display (JSON data) |
| 0x02 | Host<->Device | Ping/Pong keep-alive |
| 0x03 | Host->Device | Set display brightness |
| 0x04 | Host->Device | Set soft key assignment |
| 0x05 | Host->Device | Get soft key assignment |
| 0x06 | Host->Device | Reset all soft keys to defaults |
| 0x07 | Host->Device | Set mode state (default/plan/accept) |
| 0x10 | Device->Host | State report (mode + YOLO) |
| 0x11 | Device->Host | Type string (routed soft key string) |
| 0x12 | Device->Host | Key event (routed keycode) |

See [PROTOCOL.md](PROTOCOL.md) for full protocol details and Python/Node.js examples.

## Soft Keys

The 3 rightmost keys in Row 0 (Clear, Verbose, Model) are **soft keys** that can be reconfigured at runtime via the companion app's HID protocol, without reflashing firmware.

### Assignment Types

| Type | Description | Example |
|------|-------------|---------|
| Default | Use firmware default (Esc-Esc, Ctrl-O, /model) | Reset to factory |
| Keycode | Single key with optional modifiers, proper press/hold/release | `Ctrl-Z`, `F5` |
| String | Types out a string on press (max 127 chars) | `/gsd`, `/verbose` |

### Persistence

- Settings can be saved to EEPROM (survive reboots) or applied temporarily (runtime only)
- Use the `save_flag` in HID command 0x04 to control persistence
- HID command 0x06 resets all soft keys to defaults and saves

### HID Commands

Commands use the chunked protocol (see [PROTOCOL.md](PROTOCOL.md)):
- **0x04**: Set soft key (key_index, type, save_flag, data — supports strings up to 127 chars via chunking)
- **0x05**: Get soft key (key_index)
- **0x06**: Reset all soft keys to defaults

## Customization

### Changing Keymaps
Edit `keyboards/core_deck/rev1/keymaps/default/keymap.c`:

```c
[0] = LAYOUT(
    KC_F20,      KC_NO,       LCTL(KC_O),  KC_NO,        // Row 0: Claude, Clear(EEPROM), Verbose, Model(EEPROM)
    LCTL(KC_C),  KC_ENT,      KC_ESC,      LSFT(KC_TAB), // Row 1: Stop, Accept, Reject, Mode
    KC_ENT                                                // Row 2: Encoder button
)
```

### Adjusting RGB
Runtime controls (via QMK):
- **Toggle RGB**: RGB_TOG
- **Brightness**: RGB_VAI / RGB_VAD
- **Effect**: RGB_MOD
- **Speed**: RGB_SPI / RGB_SPD

Or edit `config.h`:
```c
#define RGB_MATRIX_DEFAULT_HUE 128  // Color (0-255)
#define RGB_MATRIX_DEFAULT_SAT 255  // Saturation
#define RGB_MATRIX_DEFAULT_VAL 100  // Brightness
```

## Troubleshooting

### Display Not Working
- Check SPI wiring (GP6-GP10)
- Verify display configuration in config.h
- Check `qmk console` for errors
- Ensure 3.3V power adequate

### Buttons Not Responding
- Verify PCF8574 I2C address (0x20 or 0x38)
- Check I2C wiring (GP4, GP5)
- Run `qmk console` to see matrix scan errors

### LEDs Not Lighting
- WS2812B requires 5V power (not 3.3V)
- Check data line connection (GP2)
- Verify 330Ω resistor between GP2 and first LED
- Ensure LEDs wired in correct sequence (0→1→2...→13)

### Encoder Skipping
- Add 100nF capacitors on encoder A/B pins
- Uncomment `ENCODER_DIRECTION_FLIP` in config.h if backwards
- Adjust `ENCODER_MAP_KEY_DELAY` if needed

### USB Not Recognized
- Check if RP2040-Tiny is in bootloader mode
- Try different USB cable/port
- Verify firmware compiled successfully

## Power Consumption

| Component | Current |
|-----------|---------|
| RP2040-Tiny | ~50mA |
| PCF8574 | ~5mA |
| Display | ~50mA |
| WS2812B (9×) | ~315mA |
| **Total** | ~420mA |

**Note**: Firmware limits LED brightness to keep total under 500mA USB limit. At full brightness LEDs would draw 540mA.

## Documentation

- [HARDWARE.md](HARDWARE.md) - Complete hardware guide, wiring, and PCB recommendations
- [PROTOCOL.md](PROTOCOL.md) - Chunked HID protocol, display setup, and companion app examples

## Contributing

This keyboard is designed for the QMK firmware repository.

## License

GPL-2.0-or-later (QMK standard)

## Credits

- **Designer**: vden
- **Firmware**: QMK Firmware
- **Display Driver**: QMK Quantum Painter
- **RGB Library**: QMK RGB Matrix

## Support

For issues or questions:
1. Check documentation files
2. Run `qmk console` for debug output
3. Open issue in QMK repository

---

**Current Configuration:**
- Display: ST7789 (284×76, landscape)
- Matrix: 1×4 PCF8574 + 4 direct GPIO + 1 encoder button
- RGB: 9 WS2812B LEDs @ 51% brightness
- Platform: RP2040-Tiny
