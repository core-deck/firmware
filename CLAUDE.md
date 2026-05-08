# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Overview

This is the QMK (Quantum Mechanical Keyboard) Firmware repository, a keyboard firmware based on tmk_keyboard for Atmel AVR and ARM controllers. QMK supports nearly 4000 keyboards with a unified codebase that allows users to customize keymaps, add features, and build firmware.

**Official Documentation**: https://docs.qmk.fm

## Architecture

### Core Components

- **`quantum/`**: Core QMK functionality including the main loop, action handling, keycodes, and features
  - Entry point: `quantum/main.c` contains `main()` which initializes hardware and runs the main loop
  - `quantum/keyboard.c` contains `keyboard_task()` which handles matrix scanning and keyboard functionality
  - Matrix scanning runs continuously to detect key presses and sends only state changes to the host

- **`keyboards/`**: Individual keyboard definitions (~4000 keyboards)
  - Each keyboard has its own directory with `keyboard.json` (data-driven config), optional `config.h`, and keymaps
  - Directory structure: `keyboards/<vendor>/<model>/<revision>/`
  - Each keyboard must have a `default` keymap

- **`tmk_core/`**: Low-level keyboard matrix and protocol code from TMK

- **`platforms/`**: Platform-specific code (AVR, ChibiOS/ARM, etc.)

- **`drivers/`**: Hardware drivers (RGB, I2C, SPI, audio, etc.)

- **`builddefs/`**: Build system makefiles and rules

### Data-Driven Configuration

QMK uses a data-driven configuration system centered around `keyboard.json` (formerly `info.json`):

- **`data/schemas/keyboards.jsonschema`**: JSON schema defining valid keyboard configuration
- **`data/mappings/info_config.hjson`**: Maps `config.h` defines to `keyboard.json` keys
- **`data/mappings/info_rules.hjson`**: Maps `rules.mk` variables to `keyboard.json` keys
- **Goal**: Single source of truth in `keyboard.json`, auto-generate `config.h` and `rules.mk` values

### Build System

The build system uses GNU Make with a custom parsing layer:

- **`Makefile`**: Top-level makefile that parses `<keyboard>:<keymap>:<target>` format
- **`builddefs/build_keyboard.mk`**: Compiles individual keyboard/keymap combinations
- Build artifacts go to `.build/` directory
- The format `make <keyboard>:<keymap>` is used to build specific configurations

### Matrix Scanning and Keymaps

1. Matrix scanning detects which physical switches are pressed (returns matrix state as 2D array)
2. `LAYOUT()` macro (generated from `keyboard.json`) maps matrix positions to physical key positions
3. Keymap uses `LAYOUT()` macro to assign keycodes to physical keys
4. Only state changes (key press/release events) are sent to the host

## Common Development Commands

### Building Firmware

```bash
# Compile a specific keyboard and keymap
qmk compile -kb <keyboard> -km <keymap>

# Compile using make (equivalent)
make <keyboard>:<keymap>

# Example: Build Clueboard 66 rev3 with default keymap
qmk compile -kb clueboard/66/rev3 -km default
make clueboard/66/rev3:default

# Compile and flash to keyboard
qmk flash -kb <keyboard> -km <keymap>
make <keyboard>:<keymap>:flash

# Compile from keyboard directory (auto-detects keyboard)
cd keyboards/planck/rev6
qmk compile -km default

# Parallel compilation (faster builds)
qmk compile -j 0 -kb <keyboard> -km <keymap>

# Clean build artifacts
make clean
```

### Testing

```bash
# Run all unit tests
make test:all

# Run specific test group
make test:tap_hold_configurations

# Run tests matching substring
make test:retro_shift:tap_hold_configurations

# Run tests with debug output
make test:all DEBUG=1

# Test executables are in .build/test/
```

Tests use Google Test framework and must be written in C++. Test files are in subdirectories like `tests/`, `quantum/*/tests/`.

### Linting and Formatting

```bash
# Format C code using clang-format
qmk format-c -a              # Format all core files
qmk format-c <file>          # Format specific file
qmk format-c --core-only -a  # Format only core files (not keyboards)
qmk format-c -n <file>       # Dry run (check without formatting)

# Format Python code
qmk format-python -a

# Lint keyboard/keymap
qmk lint -kb <keyboard>
qmk lint -kb <keyboard> -km <keymap>
qmk lint --strict            # Treat warnings as errors

# Run both format and pytest (requires Docker)
make format-and-pytest
```

**Formatting Notes**:
- Use `// clang-format off` and `// clang-format on` to protect LAYOUT macros from formatting
- Configuration is in `.clang-format` (4 spaces, modified One True Brace Style)

### QMK CLI Utilities

```bash
# List all keyboards
qmk list-keyboards

# List keymaps for a keyboard
qmk list-keymaps -kb <keyboard>

# Get keyboard info
qmk info -kb <keyboard>

# Create new keyboard
qmk new-keyboard

# Create new keymap
qmk new-keymap -kb <keyboard>

# Generate compilation database for IDE/LSP
qmk compile --compiledb -kb <keyboard> -km <keymap>

# Check QMK environment
qmk doctor

# Update git submodules
qmk git-submodule
```

### Working with Keyboard Configurations

```bash
# Convert keymap.c to keymap.json
qmk c2json -kb <keyboard> -km <keymap> <keymap.c>

# Convert keymap.json to keymap.c
qmk json2c <keymap.json>
```

## Development Workflow

### Adding a New Keyboard

1. Use `qmk new-keyboard` to generate template
2. Edit `keyboard.json` with keyboard specifications (matrix, pins, features)
3. Create `default` keymap in `keymaps/default/`
4. Write `readme.md` following the template in `data/templates/keyboard/readme.md`
5. Test: `qmk compile -kb <keyboard> -km default`
6. Lint: `qmk lint -kb <keyboard>`
7. Format: `qmk format-c -a` (if modifying core files)

### Adding a New Feature

1. Add config option to `docs/config_options.md`
2. Set default value in appropriate core file
3. Add to JSON schema: `data/schemas/keyboards.jsonschema`
4. Add mapping in `data/mappings/info_config.hjson` or `data/mappings/info_rules.hjson`
5. If needed, add extraction code to `lib/python/qmk/info.py`
6. Document in `docs/feature_*.md`
7. Write unit tests in `tests/` or `quantum/*/tests/`
8. Run `make test:all` to verify tests pass

### Coding Conventions

**C Code**:
- 4 spaces indentation (soft tabs)
- Modified One True Brace Style (opening brace on same line)
- Always include optional braces: `if (condition) { return false; }`
- Use `#pragma once` instead of include guards
- Use C-style comments `/* */` for explanatory comments
- No strict line wrapping (use readability judgment, ~76 columns if wrapping)

**Python Code**:
- Configured in `setup.cfg` (yapf and flake8)
- 256 character column limit
- Use `qmk format-python` to auto-format

**Preprocessor**:
- Keep `#` at start of line, indent between `#` and directive (e.g., `#    if defined(FOO)`)
- Both `#ifdef` and `#if defined()` are acceptable; prefer `#if defined()` for multi-condition checks

## Important Notes

- **Disabled by default**: New features should be opt-in (enabled via config), not opt-out, to preserve memory
- **Cross-platform**: Consider both AVR and ARM when adding features
- **Separate PRs**: Don't mix keyboard additions with core changes; submit core changes first
- **No binary files**: Do not commit compiled `.hex`, `.bin`, `.uf2` files to keyboards directory
- **User keymaps not accepted**: User-specific keymaps and userspace contributions are no longer accepted
- **Name files after parent folder**: C/H files should match immediate parent directory (e.g., `/keyboards/kb1/kb2/kb2.[ch]`)

## Testing Before Submitting PRs

```bash
# Ensure your fork is up to date
git fetch upstream
git rebase upstream/master

# Build the keyboard
qmk compile -kb <keyboard> -km default

# For keyboards, test all keymaps
make <keyboard>:all

# For core changes, test compilation
make all

# Run tests
make test:all

# Format code
qmk format-c -a
qmk format-python -a

# Lint
qmk lint -kb <keyboard>

# Check for whitespace issues
git diff --check
```

## Flashing and Debugging

```bash
# Flash firmware to keyboard
qmk flash -kb <keyboard> -km <keymap>

# Open console for debugging output (requires CONSOLE_ENABLE = yes)
qmk console

# Generate compilation database for better IDE support
qmk compile --compiledb -kb <keyboard> -km <keymap>
```

## Build Configuration Files

- **`keyboard.json`**: Primary keyboard configuration (replaces most `config.h` and `rules.mk` usage)
- **`config.h`**: Additional C preprocessor defines (being phased out in favor of `keyboard.json`)
- **`rules.mk`**: Build system rules and feature flags (being phased out in favor of `keyboard.json`)
- **`keymaps/<name>/keymap.c`**: Keymap definition
- **`keymaps/<name>/rules.mk`**: Keymap-specific features

## Git Workflow

- Main branch: `master`
- QMK uses git submodules - run `qmk git-submodule` to sync them
- Commit message format: Short summary (≤70 chars), blank line, detailed description
- Breaking changes follow a deprecation process documented in `docs/breaking_changes.md`
