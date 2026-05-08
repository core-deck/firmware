# Core Deck (Rev 1)

A custom 9-key macropad specifically designed for Claude Code, featuring dedicated buttons for common actions and navigation.

## Hardware

* Keyboard Maintainer: [Denis Voskvitsov](https://github.com/vden)
* Hardware Supported: RP2040-based controller with PCF8574 I2C GPIO expander
* Hardware Availability: Custom build

## Layout

The Core Deck features a 2-row button layout plus encoder and YOLO mode toggle switch:

```
PCF8574 I2C Matrix (Row 0):
┌─────────┬─────────┬─────────┬─────────┐
│ Claude  │ Clear*  │Verbose* │ Model*  │
│  F20    │ Esc-Esc │ Ctrl-O  │ /model  │
└─────────┴─────────┴─────────┴─────────┘
* = Soft key (reconfigurable via HID)

Direct GPIO Buttons (Row 1):
┌─────────┬─────────┬─────────┬─────────┐
│  Stop   │ Accept  │ Reject  │  Mode   │
│ Ctrl-C  │ Enter   │  Esc    │Shift-Tab│
└─────────┴─────────┴─────────┴─────────┘

Encoder (Row 2):
┌─────────┐
│  Enter  │  + Rotate: ↑/↓
└─────────┘

[YOLO MODE] ← SPST Toggle Switch (GP28)
```

### YOLO Mode Switch

The YOLO mode toggle switch is a hardware SPST (Single Pole Single Throw) switch that sends keystroke events when toggled:
- **Switch ON**: Sends "YOLO MODE ACTIVATED!" + Enter
- **Switch OFF**: Sends "YOLO mode deactivated." + Enter

This allows you to enable/disable YOLO mode and have the state change automatically communicated to Claude Code.

## Wiring

### Matrix Buttons (PCF8574)

The keyboard uses a PCF8574 I2C GPIO expander for 4 buttons:

* **I2C Address**: 0x20 (default PCF8574) or 0x38 (PCF8574A)
* **SDA/SCL**: Connect to your RP2040's I2C pins (default: GP4=SDA, GP5=SCL)
* **Matrix Configuration**: 1 row × 4 columns
  * Row: P2 (reuses existing PCB row 2)
  * Columns: P3, P4, P5, P6

### Direct GPIO Buttons

4 buttons connected directly to RP2040 GPIO:
* **GP13**: Stop (Ctrl-C)
* **GP14**: Accept (Enter)
* **GP15**: Reject (Esc)
* **GP26**: Mode (Shift-Tab)
* **GP27**: Encoder button (Enter)

### YOLO Mode Switch

The YOLO mode toggle switch connects directly to the RP2040:

* **GPIO Pin**: GP28 (configurable in `config.h`)
* **Wiring**: One side to GP28, other side to GND
* **Internal pull-up**: Enabled automatically by QMK

## Building

Make example for this keyboard (after setting up your build environment):

    qmk compile -kb core_deck/rev1 -km default

Or using make:

    make core_deck/rev1:default

Flashing example for this keyboard:

    qmk flash -kb core_deck/rev1 -km default

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).

## Bootloader

Enter the bootloader in 3 ways:

* **Bootmagic reset**: Hold down the key at (0,0) in the matrix (top left key) and plug in the keyboard
* **Physical reset button**: Briefly press the button on the back of the PCB - some may have pads you must short instead
* **Keycode in layout**: Press the key mapped to `QK_BOOT` if it is available
