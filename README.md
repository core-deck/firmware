# CoreDeck Firmware

Firmware for the [CoreDeck](https://coredeck.sh) macropad. Built on top of
[vial-qmk](https://github.com/vial-kb/vial-qmk) (a fork of
[QMK](https://qmk.fm)) for Vial keymap support.

## Building

```sh
git clone --recurse-submodules git@github.com:core-deck/firmware.git
cd firmware
./build.sh           # default keymap
./build.sh vial      # vial keymap
```

Output: `core_deck_rev1_<keymap>.uf2` at the repo root.

The `build.sh` script:
- Applies patches from `patches/` to the vendored `vial-qmk`
- Symlinks `keyboards/core_deck/` into the QMK tree
- Runs `qmk compile`

You need [QMK CLI](https://docs.qmk.fm/#/newbs_getting_started) installed
(`pip install qmk` or `brew install qmk/qmk/qmk`) and a working ARM toolchain.

## Flashing

Hold the **Agent** (orange) button while plugging in the USB cable to enter
UF2 mode, then drag the `.uf2` file onto the mounted drive.

If the device is unresponsive and won't enter UF2 mode that way, the BOOT
button is the fallback — it's hidden inside but reachable through the hole
in the bottom plate. Hold it while plugging in.

You can also flash directly from the build script:

```sh
./build.sh default flash
```

## Repository Layout

```
keyboards/core_deck/      # CoreDeck-specific source (single source of truth)
patches/                  # Patches applied to vial-qmk at build time
vendor/vial-qmk/          # vial-qmk submodule (pinned)
build.sh                  # Build script
.github/workflows/        # CI builds UF2 artifacts on push
```

## Updating vial-qmk

```sh
git -C vendor/vial-qmk fetch origin vial
git -C vendor/vial-qmk checkout origin/vial
cd ../..
./build.sh   # verify build still passes
git add vendor/vial-qmk
git commit -m "Bump vial-qmk to <commit>"
```

## License

Firmware sources under `keyboards/core_deck/` are licensed under GPL-2.0-or-later
to match QMK. See individual source file headers.
