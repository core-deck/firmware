# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with
code in this repository.

## Repository Overview

This repository hosts the firmware for the [Core Deck](https://coredeck.sh)
macropad — a Claude Code-optimized 9-key device with a TFT display, RGB
lighting, rotary encoder, and a YOLO-mode toggle switch.

The firmware is built on top of [vial-qmk](https://github.com/vial-kb/vial-qmk),
which is vendored as a git submodule under `vendor/vial-qmk/`. Only the
keyboard-specific code lives here; nothing else is copied or forked from
upstream.

## Repository Layout

```
.
├── README.md                       Repo-level intro and build instructions
├── LICENSE                         GPL-2.0 (matches QMK's license)
├── build.sh                        Build script — symlinks core_deck into
│                                   vial-qmk, applies patches, runs qmk compile
├── .github/workflows/build.yml     CI: builds vial UF2 on push, attaches
│                                   to GitHub Releases on tags (v*)
├── patches/                        Patches applied to the vial-qmk submodule
│                                   at build time (e.g. Python 3.12 fixes)
├── vendor/vial-qmk/                vial-qmk submodule (pinned)
└── keyboards/core_deck/            CoreDeck firmware sources (single source
    ├── README.md                   of truth — symlinked into vial-qmk at
    ├── HARDWARE.md                 build time so QMK's build system finds it
    ├── PROTOCOL.md                 like a normal keyboard)
    └── rev1/
        ├── keyboard.json           DD config: matrix, pins, USB IDs, RGB layout
        ├── config.h                Display/PWM/I2C defines not in keyboard.json
        ├── halconf.h, mcuconf.h    ChibiOS HAL config (I2C, PWM)
        ├── rules.mk                Build feature flags
        ├── matrix.c                Custom matrix scan (PCF8574 + direct GPIO)
        ├── rev1.c                  Keyboard-level init & housekeeping
        ├── display.c/h             ST7789 TFT rendering, backlight, alerts
        ├── protocol.c/h            Raw HID command dispatch & chunked transport
        ├── softkeys.c/h            Runtime-reconfigurable keys + EEPROM
        ├── rgb_matrix_kb.inc       Custom GLOW_REACTIVE LED effect
        ├── graphics/               Embedded fonts (QFF) and logo (QGF)
        ├── tools/bdf_to_qmk_png.py BDF → QMK PNG glyph generator
        └── keymaps/
            ├── default/keymap.c    Vanilla keymap (compile sanity check)
            └── vial/               Shipping keymap with vial.json
```

## Build Flow

The build script handles the unusual flow where the keyboard source lives
outside the QMK tree:

```sh
./build.sh vial         # build vial keymap (recommended — what we ship)
./build.sh default      # build default keymap (sanity only — not shipped)
```

What `build.sh` does on each run:
1. Verifies the `vendor/vial-qmk` submodule is initialized.
2. Applies any unapplied patches from `patches/` to the submodule.
3. Symlinks `keyboards/core_deck/` into `vendor/vial-qmk/keyboards/core_deck/`
   (idempotent — created only if not already present).
4. Runs `qmk compile -kb core_deck/rev1 -km <keymap>` from inside the
   submodule.
5. Copies the resulting UF2 back to the repo root.

CI builds the `vial` keymap only. On tagged releases (`v*`), the UF2 is
attached to the GitHub Release.

## Testing on Hardware

To flash a built UF2 to a physical device:

1. Hold the **Agent (orange) button** while plugging in USB. The device
   mounts as a USB drive.
2. Drag `core_deck_rev1_vial.uf2` onto the drive.

Fallback for an unresponsive device: hold the **BOOT** button (hidden inside,
reachable through the bottom-plate hole) while plugging in.

Or: `./build.sh vial flash` invokes QMK's built-in flasher.

## Updating vial-qmk

Vial-qmk is pinned via the submodule. To bump:

```sh
git -C vendor/vial-qmk fetch origin vial
git -C vendor/vial-qmk checkout origin/vial
./build.sh vial          # verify the new HEAD still builds
git add vendor/vial-qmk
git commit -m "Bump vial-qmk to <short-sha>"
```

If the bump introduces breakage in `keyboards/core_deck/`, fix it; if the
breakage is in vial-qmk's tree, capture a patch in `patches/` rather than
modifying the submodule's working tree.

## Coding Conventions (CoreDeck firmware)

C code follows QMK conventions:
- 4 spaces indentation
- Modified One True Brace Style (opening brace on same line)
- Always include optional braces: `if (cond) { ... }`
- `#pragma once` instead of include guards
- C-style `/* */` comments for explanatory blocks
- Use `// clang-format off` / `// clang-format on` around the `LAYOUT()`
  macro in keymap.c so clang-format doesn't reflow the visual layout.

Python tooling (e.g. `tools/bdf_to_qmk_png.py`) follows vial-qmk's setup.cfg
(yapf + flake8, 256-char column limit).

## Versioning

Firmware version is exposed two places that must be kept in sync:
- `device_version` in `keyboards/core_deck/rev1/keyboard.json`
- `FW_VERSION` constant in `keyboards/core_deck/rev1/protocol.c`

Bump both when cutting a release. Companion apps query the current value via
HID command 0x09.

The companion-app HID protocol has two wire formats: standalone (default
keymap) and VIAL (vial keymap, prefixed with `0x80`). Companion apps detect
the variant via `CMD_GET_VERSION` and flip behavior — see PROTOCOL.md.

## Related Documentation

- `keyboards/core_deck/README.md` — device-level overview, button layout,
  HID command summary.
- `keyboards/core_deck/HARDWARE.md` — wiring, pin assignments, LED chain
  order, PCB recommendations.
- `keyboards/core_deck/PROTOCOL.md` — full HID protocol specification with
  Python and Node.js client examples.
