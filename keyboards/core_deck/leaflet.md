# Core Deck — Leaflet Content

> A5 (148 × 210 mm), double-sided, landscape fold optional.
> Placeholder marks: `[PHOTO]`, `[CE]`, `[WEEE]`.

---

## SIDE A — Front

### Header

**CORE DECK**
Tactile fire-control for the agentic coding workflow.

[PHOTO — device at ~45° angle, display lit, keys visible]

### Button Layout

```
keyboard.png
```

° °° °°° — Soft keys: reconfigurable via companion app.

### Default Key Bindings

| Key            | Action    | What it does                         |
| -------------- | --------- | ------------------------------------ |
| **Agent**      | F20       | Trigger Companion app                |
| **Clear** °    | Esc × 2   | Clear input line                     |
| **Verbose** °° | Ctrl+O    | Toggle verbose output                |
| **Model** °°°  | /model ↵  | Open model selector                  |
| **Stop**       | Ctrl+C    | Interrupt execution                  |
| **Accept**     | Enter     | Confirm / approve                    |
| **Reject**     | Esc       | Cancel / dismiss                     |
| **Mode**       | Shift+Tab | Cycle mode (default → plan → accept) |

### Encoder & Combos

| Input              | Action                   |
| ------------------ | ------------------------ |
| Rotate             | ↑ / ↓ scroll             |
| Press              | Enter                    |
| Hold + rotate      | Switch tabs (Ctrl+Tab)   |
| Mode hold + rotate | Mouse scroll             |
| ARM switch         | Toggle auto-approve mode |

---

## SIDE B — Back

### Technical Specifications

|            |                                    |
| ---------- | ---------------------------------- |
| Controller | RP2040 (ARM Cortex-M0+, dual core) |
| Keys       | 8 × mechanical (Cherry MX)         |
| Encoder    | EC11 rotary with push button       |
| Display    | 1.3″ IPS TFT, 284 × 76 px          |
| Lighting   | 8 × WS2812B addressable RGB LEDs   |
| Connection | USB 2.0 (Type-C)                   |
| Firmware   | QMK / VIAL (open source)           |
| Power      | < 500 mA (USB bus powered)         |

### Requirements

- Any computer with a USB port (USB-C cable included)
- Works as a standard USB keyboard — no drivers required
- Optional: companion app for display and soft key features
  https://github.com/core-deck/core-deck

### Getting Started

1. Plug in via USB-C. The display shows the Core Deck logo.
2. All buttons work immediately as a USB keyboard.
3. Install the companion app for live display and soft key configuration.
4. Hold any soft key (° °° °°°) to peek at its current assignment.

### Firmware Updates

1. Unplug the device.
2. Hold the **Agent** button (orange) while plugging in USB-C.
3. A USB drive named **RPI-RP2** appears.
4. Drag the new `.uf2` firmware file onto the drive.

If the Agent button method doesn't work, double-press the reset
button on the bottom plate (within 500 ms) while connected.

### Regulatory

[CE]  [WEEE]

**Manufacturer**
Core Deck
Calle Gil-Vernet 54/55
Poligono Les Tapies 1 #1118
43890 Hospitalet de l'Infant, Tarragona
Spain

**Product:** Core Deck Macropad, Rev 1

This device complies with:

- EMC Directive 2014/30/EU
- RoHS Directive 2011/65/EU

Full EU Declaration of Conformity available at:
https://coredeck.sh/doc

Do not dispose of this product with household waste.
Return to a designated WEEE collection point.

### Support

Source & docs: https://github.com/core-deck
