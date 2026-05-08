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

Direct GPIO Buttons (Row 1) — visual order, left-to-right:
┌─────────┬─────────┬─────────┬─────────┐
│  Mode   │ Accept  │ Reject  │  Stop   │
│Shift-Tab│ Enter   │  Esc    │ Ctrl-C  │
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

4 buttons connected directly to RP2040 GPIO. Wiring is mirrored relative to
the visual layout — matrix col 0 (`GP13`) is the **rightmost** button on the
device, col 3 (`GP26`) is the **leftmost**.

* **GP13**: Stop (Ctrl-C) — rightmost
* **GP14**: Accept (Enter)
* **GP15**: Reject (Esc)
* **GP26**: Mode (Shift-Tab tap, Layer 2 hold) — leftmost
* **GP27**: Encoder button (Enter tap, Layer 1 hold)

### YOLO Mode Switch

The YOLO mode toggle switch connects directly to the RP2040:

* **GPIO Pin**: GP28 (configurable in `config.h`)
* **Wiring**: One side to GP28, other side to GND
* **Internal pull-up**: Enabled automatically by QMK

## Building

Use the top-level `build.sh` (which manages the vial-qmk submodule, applies
patches, and runs `qmk compile`):

    ./build.sh vial         # build vial keymap (recommended)
    ./build.sh default      # build default keymap

Output: `core_deck_rev1_<keymap>.uf2` at the repo root.

Pre-built UF2 files for tagged releases are available at
[github.com/core-deck/firmware/releases](https://github.com/core-deck/firmware/releases).

See the [top-level README](../../../README.md) for full instructions.

## Flashing / Bootloader

Hold the **Agent (orange) button** while plugging in USB. The device mounts
as a USB drive — drag the `.uf2` onto it.

Fallback (if device is unresponsive): the **BOOT** button is hidden inside
the case and reachable through the hole in the bottom plate. Hold it while
plugging in.

You can also flash directly: `./build.sh vial flash`.
