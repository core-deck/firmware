# HID Protocol & Display Guide

## Overview

The Core Deck features a TFT display showing real-time information from Claude Code via a companion desktop application.

- **ST7789** (284x76, landscape)

## Display Layouts

### ST7789 Layout (284x76, Landscape)

```
┌──────────────────────────────────────────────────────────────────────────┐
│ my-project                                                   (White)    │
│ Refactoring auth.ts                                          (Blue)     │
│                                                              (reserved) │
└──────────────────────────────────────────────────────────────────────────┘
```

Long text is truncated with ellipsis ("...") when it exceeds the display width.

### Line 1: Session
- **Color**: White (HSV: 0, 0, 255)
- **Content**: Current session name
- **Max Length**: 64 characters

### Line 2: Task
- **Color**: Blue (HSV: 170, 255, 255)
- **Content**: Current task description (nullable — empty when idle)
- **Max Length**: 64 characters

### Line 3: Reserved
- Reserved for future use

## Hardware Configuration

### Display Specifications

#### ST7789 (Final Hardware)
- **Controller**: ST7789
- **Resolution**: 284x76 pixels
- **Orientation**: Landscape (rotated 90°)
- **Interface**: SPI

### Wiring

```
Display Pin   RP2040 Pin    Function
-----------   ----------    --------
VCC           3.3V          Power
GND           GND           Ground
SCK           GP6           SPI Clock
MOSI          GP7           SPI MOSI
MISO          GP3           SPI MISO (not connected - display is write-only)
CS            GP8           Chip Select
DC            GP9           Data/Command
RST           GP10          Reset
```

### Pin Configuration

All pins are configurable in `keyboards/core_deck/rev1/config.h`:

```c
/* SPI Configuration for Display */
#define SPI_DRIVER SPID0
#define SPI_SCK_PIN GP6
#define SPI_MOSI_PIN GP7
#define SPI_MISO_PIN GP3

/* Display Control Pins */
#define DISPLAY_CS_PIN GP8
#define DISPLAY_DC_PIN GP9
#define DISPLAY_RST_PIN GP10
```

## Communication Protocol

### Raw HID Interface

The display receives data via Raw HID (USB HID) from a companion desktop application.

**USB Configuration**:
- **Vendor ID**: 0xFEED (from `keyboard.json`)
- **Product ID**: 0x0803 (from `keyboard.json`)
- **Usage Page**: 0xFF60
- **Usage ID**: 0x61
- **Report Size**: 32 bytes

### Chunked Packet Format

Messages use a chunked transport layer so any command can span multiple 32-byte packets.

There are two wire format variants depending on the firmware build:

#### Standalone build (default keymap)

**Packet layout** (32 bytes):
```
Byte 0:     Header flags
              Bit 7 (0x80): START — first packet of message
              Bit 6 (0x40): END   — last packet of message
              Bits 5-0: Reserved (must be 0)
Byte 1:     Command ID
Bytes 2-31: Payload (30 bytes per chunk)
```

#### VIAL build (vial keymap)

When VIA/VIAL is enabled, both VIA and the custom protocol share the same raw HID endpoint. All custom protocol packets are prefixed with `0x80` to distinguish them from VIA commands (0x01-0x15) and VIAL magic (0xFE). VIA routes unrecognised command IDs to `raw_hid_receive_kb()`.

**Packet layout** (32 bytes):
```
Byte 0:     Protocol prefix (always 0x80)
Byte 1:     Header flags
              Bit 7 (0x80): START — first packet of message
              Bit 6 (0x40): END   — last packet of message
              Bits 5-0: Reserved (must be 0)
Byte 2:     Command ID
Bytes 3-31: Payload (29 bytes per chunk)
```

**Companion app changes for VIAL builds**:
- Prepend `0x80` to all outgoing packets
- Filter incoming packets: discard any packet where `data[0] != 0x80` (VIA echo packets — VIA always echoes the data buffer back after `raw_hid_receive_kb` returns; the firmware zeros the buffer but the host should still filter)
- Chunk size changes from 30 to 29 bytes
- Detect firmware variant via `CMD_GET_VERSION` — version `"2.0.0"+` uses the prefixed format

| Header | Meaning |
|--------|---------|
| `0xC0` | Single-packet message (START+END) |
| `0x80` | First chunk of multi-packet message |
| `0x00` | Middle chunk |
| `0x40` | Final chunk |

**Reassembly rules**:
- **START**: Reset reassembly buffer, store command ID, copy payload
- **Middle**: Verify command ID matches, append payload
- **END**: Append payload, process complete message
- **Buffer overflow** (>512 bytes): Send error response, reset state
- **Middle/END without prior START**: Send error response, ignore
- A new START always resets state; no reassembly timeout needed

### Response Format

Responses use the same chunked framing as requests. Most responses fit in a single packet, but commands that return variable-length data (e.g. 0x05 Get Soft Key) may span multiple packets.

**Single-packet response** (most commands):
```
Byte 0:     0xC0 (START|END)
Byte 1:     Command ID (echoed)
Byte 2:     Status (0x00 = success)
Bytes 3-31: Response data (up to 29 bytes)
```

**Multi-packet response** (same START/END flags as requests):
```
First packet:
  Byte 0:     0x80 (START)
  Byte 1:     Command ID
  Byte 2:     Status
  Bytes 3-31: First 29 bytes of response data

Continuation packets:
  Byte 0:     0x00 (middle) or 0x40 (END)
  Byte 1:     Command ID
  Bytes 2-31: Next 30 bytes of response data
```

**Host reassembly**: Collect packets until END flag. The status byte is always byte 2 of the first packet. Concatenate data bytes (bytes 3-31 of first packet, bytes 2-31 of continuation packets) to reconstruct the full response data.

### Error Response

```
Byte 0:     0xC0
Byte 1:     0xFF
Byte 2:     Error code
Bytes 3-31: 0x00
```

| Error Code | Meaning |
|------------|---------|
| `0x01` | Buffer overflow (message too large) |
| `0x02` | Bad sequence (middle/end without start, or command ID mismatch) |
| `0x03` | Unknown command |

### Command Reference

All payload byte offsets below are relative to byte 2 of the packet (after flags and command ID).

#### Command 0x01: Update Display

**Payload** (Host → Keyboard): JSON string (up to 512 bytes, chunked as needed)

**JSON Format**:
```json
{"session":"my-project","task":"Refactoring auth.ts"}
{"session":"my-project","task":null}
```

**Response**: `status=0x00`, no additional data

#### Command 0x02: Ping

**Payload**: None (single packet)

**Response**: `status=0x00`, no additional data

#### Command 0x03: Set Brightness

**Payload** (single packet):
```
Byte 0: Brightness level (0-255)
Byte 1: Save flag (0x00 = temporary, 0x01 = save to EEPROM)
```

**Response**: `status=0x00`, data: `[current_brightness]`

#### Command 0x04: Set Soft Key

Assigns a new function to one of the 3 soft keys (Clear, Verbose, Model).

**Payload** (may be chunked for long strings):
```
Byte 0: Key index (0 = Clear, 1 = Verbose, 2 = Model)
Byte 1: Type (0x00 = default, 0x01 = keycode, 0x02 = string, 0x03 = sequence)
Byte 2: Save flag (0x00 = runtime only, 0x01 = persist to EEPROM)
Byte 3+: Data (depends on type):
           type 0x00: no data needed
           type 0x01: [keycode_hi, keycode_lo] (QMK 16-bit keycode)
           type 0x02: [flags, string_bytes...] (max 126 chars)
                      flags bit 0: send Enter after string (0 = no, 1 = yes)
                      flags bits 1-7: reserved (0)
           type 0x03: [count, kc1_hi, kc1_lo, kc2_hi, kc2_lo, ...]
                      count: number of keycodes (1-63)
                      Each keycode is a 16-bit QMK keycode (big-endian)
```

**Response**: `status=success/failure`, data: `[key_index, type]`

**Assignment types:**

| Type | Value | Description | Example |
|------|-------|-------------|---------|
| Default | 0x00 | Use firmware default (Esc-Esc, Ctrl-O, /model) | Reset to factory |
| Keycode | 0x01 | Single QMK keycode with modifiers. Proper press/hold/release. | `LCTL(KC_Z)`, `KC_F5` |
| String | 0x02 | Types out a string on key press (max 126 chars). Flags byte controls whether Enter is sent after. | `/gsd`, `/verbose` |
| Sequence | 0x03 | Taps a sequence of keycodes on press with 10ms delay between each (max 63 keycodes) | Esc-Esc, Ctrl-Z Ctrl-Z |

#### Command 0x05: Get Soft Key

**Payload** (single packet): `[key_index]`

**Response** (may be multi-packet for long strings/sequences): `status=0x00`, data: `[key_index, type, data...]`

Note: DEFAULT (0x00) is resolved to its effective type before sending — the host never receives type 0x00.
- type 0x01: `[keycode_hi, keycode_lo]`
- type 0x02: `[flags, string_bytes...]` (flags byte + up to 126 chars, chunked across packets). Flags bit 0 = send Enter.
- type 0x03: `[count, kc1_hi, kc1_lo, ...]` (full sequence, up to 63 keycodes, chunked across packets)

#### Command 0x06: Reset All Soft Keys

Resets all 3 soft keys to their keymap defaults and saves to EEPROM.

**Payload**: None (single packet)

**Response**: `status=0x00`, data: 9 bytes — 3 entries of `[type, kc_hi, kc_lo]`

```
Bytes 0-2:  Key 0 — [type, keycode_hi, keycode_lo]
Bytes 3-5:  Key 1 — [type, keycode_hi, keycode_lo]
Bytes 6-8:  Key 2 — [type, keycode_hi, keycode_lo]
```

For keys with type `0x00` (default), the keycode is the compiled-in keymap default for that position. For type `0x01` (keycode), it is the override keycode. For types `0x02`/`0x03` (string/sequence), the keycode is `0x0000`.

#### Command 0x07: Set Mode

Sets the Mode button LED state to reflect the current Claude Code mode. Use this when the mode is changed externally (e.g., from the main keyboard).

**Payload** (single packet):
```
Byte 0: Mode (0x00 = default, 0x01 = accept, 0x02 = plan)
```

**Response**: `status=0x00`, data: `[current_mode]`

**Mode values** (cycle order: default → accept → plan → default):

| Value | Mode | LED Color |
|-------|------|-----------|
| 0x00 | Default | Normal (reactive effect) |
| 0x01 | Accept | Purple |
| 0x02 | Plan | Cyan |

#### Command 0x09: Get Firmware Version

Queries the firmware version string from the device.

**Payload**: None (single packet)

**Response**: `status=0x00`, data: UTF-8 version string (e.g. `"1.1.0"`)

**Notes**:
- Firmware that does not implement this command will return error `0x03` (Unknown command). The host should treat this as version `"unknown"`.
- The version string matches the `FW_VERSION` constant defined in `protocol.c` and should be kept in sync with `device_version` in `keyboard.json`.

#### Command 0x08: Set/Clear Alert

Sets or clears an alert for a specific tab. Alerts attract the user's attention with a breathing red frame overlay on the display. The screen wakes from dim/idle when an alert arrives.

**Direction**: Host → Keyboard

**Payload** (JSON string, chunked if needed):

Set alert:
```json
{"tab":0,"session":"my-feature","text":"Build failed!","details":"exit code 1 in tests/auth.rs"}
```

Clear alert:
```json
{"tab":0,"text":null}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `tab` | integer | Yes | Tab index (0-15) |
| `session` | string | When setting | Session name shown on overlay |
| `text` | string or null | Yes | Alert text (null or absent = clear) |
| `details` | string | No | Extended details shown when user holds Claude button |

**Response**: `status=0x00`, no data

**Behavior**:
- One tab can have at most one active alert. Setting a new alert on a tab replaces the previous one.
- Alerts are queued: the oldest (least recently set) alert is displayed on the overlay.
- When an alert is cleared, the next oldest alert (if any) is shown. When all are cleared, the overlay is dismissed.
- Tabs with active alerts show a red filled square indicator (in both normal view and alert overlay).
- The overlay shows a breathing red gradient frame, the session name, and the alert text.
- If `details` is provided, the user can hold the **Claude button** (F20, row 0 col 0) to temporarily view the details text. The red frame remains; session, alert text, and tab indicators are replaced by the details. Releasing the button restores the normal alert view.
- All alerts are automatically cleared on disconnect (ping timeout).

#### Command 0x11: Type String (Keyboard → Host)

Unsolicited command sent when a soft key with STRING type is pressed while the companion app is connected. The host should insert the string via OS text input API into the Claude Code terminal.

**Direction**: Keyboard → Host (unsolicited, no response expected)

**Packet format** (chunked — same framing as responses):
```
First packet:
  Byte 0:     0x80 (START) or 0xC0 (START|END for short strings)
  Byte 1:     0x11 (CMD_TYPE_STRING)
  Byte 2:     Flags byte (bit 0 = send Enter after string)
  Bytes 3-31: First 29 bytes of string

Continuation packets (if needed):
  Byte 0:     0x00 (middle) or 0x40 (END)
  Byte 1:     0x11
  Bytes 2-31: Next 30 bytes of string
```

**Host reassembly**: Collect packets until END flag. Byte 2 of the first packet is the flags byte. Concatenate string bytes (bytes 3-31 of first packet, bytes 2-31 of continuations). Insert the string into the Claude Code terminal, followed by Enter if flags bit 0 is set.

#### Command 0x12: Key Event (Keyboard → Host)

Unsolicited command sent when any key is pressed while the companion app is connected. The host should simulate the keystroke in the Claude Code terminal.

**Direction**: Keyboard → Host (unsolicited, no response expected)

**Packet format** (always single packet):
```
Byte 0:     0xC0 (START|END)
Byte 1:     0x12 (CMD_KEY_EVENT)
Byte 2:     Keycode high byte
Byte 3:     Keycode low byte
Bytes 4-31: 0x00
```

**Keycode encoding**: 16-bit QMK keycode. For basic keys (0x0000-0x00FF), the low byte is the USB HID usage code. For modified keys (0x0100-0x1FFF), bits 12-8 encode modifiers:

| Bit | Modifier |
|-----|----------|
| 8 | Left Ctrl |
| 9 | Left Shift |
| 10 | Left Alt |
| 11 | Left GUI |
| 12 | Right Ctrl |

Examples: `KC_ESC` = `0x0029`, `LCTL(KC_C)` = `0x0106`, `KC_ENT` = `0x0028`, `KC_F20` = `0x006F`.

**Host handling**: Decode the modifier bits and base keycode, then simulate the keystroke in the Claude Code terminal using OS-level key simulation (e.g., AppleScript, xdotool, or similar).

#### Command 0x10: State Report (Keyboard → Host)

Unsolicited notification sent by the keyboard whenever internal state changes. The host does **not** send this command — it only receives it.

**Direction**: Keyboard → Host (unsolicited)

**Packet format** (single packet, always 0xC0):
```
Byte 0:     0xC0 (START|END)
Byte 1:     0x10 (CMD_STATE_REPORT)
Byte 2:     State byte (see encoding below)
Bytes 3-31: 0x00
```

**State byte encoding**:

| Bit(s) | Field | Values |
|--------|-------|--------|
| 1-0 | Mode | 0 = default, 1 = accept, 2 = plan |
| 2 | YOLO | 0 = off, 1 = on |
| 7-3 | Reserved | 0 |

**Triggers** — the keyboard sends a state report when:

1. **Mode button pressed** — user cycles mode on the keyboard
2. **YOLO switch toggled** — user flips the DIP switch
3. **Set Mode command (0x07)** — after host sets mode, keyboard confirms new state
4. **Ping (0x02)** — after pong response, keyboard sends current state
5. **Boot** — best-effort report at keyboard init (USB may not be ready)

**Host handling notes**:
- State reports may arrive at any time, including between a command and its response
- When reading a response after sending a command, if byte 1 is `0x10`, consume the packet as a state report and read again for the actual command response
- After a ping, the host will receive two packets: the pong (0x02) followed by a state report (0x10)

### JSON Fields (Command 0x01: Update Display)

| Field      | Type           | Description                     | Max Length |
|------------|----------------|---------------------------------|------------|
| `session`  | string         | Current session name            | 64 chars   |
| `task`     | string or null | Current task description        | 64 chars   |

### JSON Fields (Command 0x08: Alert)

| Field      | Type           | Description                     | Max Length |
|------------|----------------|---------------------------------|------------|
| `tab`      | integer        | Tab index (0-15)                | —          |
| `session`  | string         | Session name for this alert     | 64 chars   |
| `text`     | string or null | Alert text (null/absent = clear)| 64 chars   |
| `details`  | string         | Extended details (optional)     | 64 chars   |

## Companion App Example

### Python Example

```python
import hid
import json
import time

# USB configuration
VENDOR_ID = 0xFEED
PRODUCT_ID = 0x0803
USAGE_PAGE = 0xFF60
USAGE = 0x61

PROTO_PREFIX = 0x80  # Protocol prefix for VIAL builds

def find_core_deck():
    """Find the Core Deck device"""
    devices = hid.enumerate(VENDOR_ID, PRODUCT_ID)
    for device in devices:
        if device['usage_page'] == USAGE_PAGE and device['usage'] == USAGE:
            return hid.Device(path=device['path'])
    return None

def detect_protocol(device):
    """Detect protocol version — returns True if VIAL (prefixed) protocol.

    Sends a version query without prefix first. If the response has
    prefix 0x80 and version >= 2.0.0, use prefixed protocol.
    """
    # Try standalone format first
    packet = bytes([0xC0, 0x09]).ljust(32, b'\x00')
    device.write(packet)
    response = device.read(32, timeout=1000)
    if response and response[0] == 0xC0 and response[2] == 0x00:
        version = bytes(response[3:]).rstrip(b'\x00').decode('utf-8', errors='replace')
        return False, version  # Standalone protocol

    # VIAL build: the standalone query was treated as VIA command 0xC0
    # (unknown), so we get an echo back. Try prefixed format.
    packet = bytes([PROTO_PREFIX, 0xC0, 0x09]).ljust(32, b'\x00')
    device.write(packet)
    # Read until we get our prefixed response (skip VIA echo)
    for _ in range(3):
        response = device.read(32, timeout=1000)
        if response and response[0] == PROTO_PREFIX:
            if response[1] == 0xC0 and response[3] == 0x00:
                version = bytes(response[4:]).rstrip(b'\x00').decode('utf-8', errors='replace')
                return True, version
    return True, "unknown"  # Assume VIAL if detection fails

class CoreDeck:
    """Core Deck HID client — auto-detects standalone vs VIAL protocol."""

    def __init__(self, device):
        self.device = device
        self.use_prefix, self.version = detect_protocol(device)
        self.chunk_size = 29 if self.use_prefix else 30

    def send_message(self, command, payload=b''):
        """Send a message using the chunked protocol.

        Splits payload into chunks with START/END flags.
        Returns the response packet from the keyboard.
        """
        offset = 0
        total = len(payload)

        while offset < total or offset == 0:
            chunk = payload[offset:offset + self.chunk_size]
            flags = 0x00
            if offset == 0:
                flags |= 0x80  # START
            if offset + len(chunk) >= total:
                flags |= 0x40  # END

            if self.use_prefix:
                packet = bytes([PROTO_PREFIX, flags, command]) + chunk
            else:
                packet = bytes([flags, command]) + chunk
            packet = packet.ljust(32, b'\x00')
            self.device.write(packet)
            offset += len(chunk)

            if flags & 0x40:  # END — read response
                return self._read_response()

        return None

    def _read_response(self):
        """Read response, filtering VIA echo packets in VIAL mode."""
        for _ in range(5):
            data = self.device.read(32, timeout=1000)
            if data is None:
                return None
            if self.use_prefix:
                if data[0] == PROTO_PREFIX:
                    return data  # Our protocol response
                # else: VIA echo or state report — skip
            else:
                return data
        return None

    def send_display_update(self, session, task=None):
        """Send display update to Core Deck"""
        data = {"session": session, "task": task}
        payload = json.dumps(data).encode('utf-8')
        response = self.send_message(0x01, payload)
        p = 1 if self.use_prefix else 0
        return response and response[p] == 0xC0 and response[p + 2] == 0x00

    def send_ping(self):
        """Send ping, returns True if pong received"""
        response = self.send_message(0x02)
        p = 1 if self.use_prefix else 0
        return response and response[p] == 0xC0 and response[p + 1] == 0x02

    def set_brightness(self, level, save=False):
        """Set display brightness (0-255)"""
        payload = bytes([level, 0x01 if save else 0x00])
        response = self.send_message(0x03, payload)
        p = 1 if self.use_prefix else 0
        return response and response[p] == 0xC0

    def get_version(self):
        """Query firmware version string"""
        return self.version

    def set_soft_key(self, index, key_type, data=b'', save=False):
        """Set a soft key assignment"""
        payload = bytes([index, key_type, 0x01 if save else 0x00]) + data
        response = self.send_message(0x04, payload)
        p = 1 if self.use_prefix else 0
        return response and response[p] == 0xC0

def main():
    device = find_core_deck()
    if not device:
        print("Core Deck not found!")
        return

    print(f"Connected to Core Deck")
    print(f"Manufacturer: {device.manufacturer}")
    print(f"Product: {device.product}")

    deck = CoreDeck(device)
    print(f"Firmware: {deck.get_version()}")
    print(f"Protocol: {'VIAL (prefixed)' if deck.use_prefix else 'standalone'}")

    try:
        # Example: Send display update with session and task
        deck.send_display_update(session="my-project", task="Refactoring auth.ts")

        # Example: Clear task (idle)
        deck.send_display_update(session="my-project")

        # Example: Set a soft key to type a long string
        long_string = "/review --detailed --include-tests"
        deck.set_soft_key(0, 0x02, long_string.encode('utf-8'), save=True)

    finally:
        device.close()

if __name__ == '__main__':
    main()
```

### Node.js Example

```javascript
const HID = require('node-hid');

const VENDOR_ID = 0xFEED;
const PRODUCT_ID = 0x0803;
const USAGE_PAGE = 0xFF60;
const USAGE = 0x61;

const PROTO_PREFIX = 0x80; // Protocol prefix for VIAL builds

function findCoreDeck() {
    const devices = HID.devices();
    const device = devices.find(d =>
        d.vendorId === VENDOR_ID &&
        d.productId === PRODUCT_ID &&
        d.usagePage === USAGE_PAGE &&
        d.usage === USAGE
    );

    return device ? new HID.HID(device.path) : null;
}

function sendMessage(device, command, payload = Buffer.alloc(0), usePrefix = false) {
    const chunkSize = usePrefix ? 29 : 30;
    return new Promise((resolve) => {
        let offset = 0;
        const total = payload.length;

        function sendChunk() {
            const chunk = payload.subarray(offset, offset + chunkSize);
            let flags = 0x00;
            if (offset === 0) flags |= 0x80; // START
            if (offset + chunk.length >= total) flags |= 0x40; // END

            const packet = Buffer.alloc(32);
            if (usePrefix) {
                packet[0] = PROTO_PREFIX;
                packet[1] = flags;
                packet[2] = command;
                chunk.copy(packet, 3);
            } else {
                packet[0] = flags;
                packet[1] = command;
                chunk.copy(packet, 2);
            }
            device.write(Array.from(packet));

            offset += chunk.length;

            if (flags & 0x40) {
                // END — read response
                device.read((err, data) => {
                    resolve(err ? null : data);
                });
            } else {
                // More chunks to send
                sendChunk();
            }
        }

        sendChunk();
    });
}

async function sendDisplayUpdate(device, data, usePrefix = false) {
    const payload = Buffer.from(JSON.stringify(data), 'utf8');
    const response = await sendMessage(device, 0x01, payload, usePrefix);
    const p = usePrefix ? 1 : 0;
    return response && response[p] === 0xC0 && response[p + 2] === 0x00;
}

async function main() {
    const device = findCoreDeck();

    if (!device) {
        console.log('Core Deck not found!');
        return;
    }

    console.log('Connected to Core Deck');

    // Detect protocol: try version query to determine if VIAL build
    // For simplicity, set usePrefix based on your firmware build:
    const usePrefix = false; // Set to true for VIAL firmware

    // Example update — JSON is automatically chunked if needed
    await sendDisplayUpdate(device, {
        session: 'my-project',
        task: 'Refactoring auth.ts'
    }, usePrefix);

    device.close();
}

main();
```

## Implementation Details

### Quantum Painter

The display uses QMK's Quantum Painter framework:
- **Driver**: `st7789_spi`
- **Color Format**: HSV (Hue, Saturation, Value)
- **Primitives**: `qp_rect()`, `qp_drawtext_recolor()`

### Source File Organization

The firmware is split into two modules:

- **`protocol.c/h`** — Raw HID handling, chunked packet reassembly, command dispatch, and mode state. This is the entry point for all companion app communication.
- **`display.c/h`** — TFT display rendering, backlight/PWM control, connection tracking, and JSON parsing. Exposes `display_update_json()` for protocol to push data.

### Raw HID Callback

The chunked protocol handler lives in `protocol.c`:

1. Entry point strips the protocol prefix (if VIA_ENABLE) and calls `proto_handle_packet()`
2. `proto_handle_packet()` extracts START/END flags and reassembles multi-packet messages into a 512-byte buffer
3. Once complete, `process_message()` dispatches to command handlers (0x01–0x09)
4. Display commands call into `display.c` via public functions (`display_update_json()`, `display_ping_received()`, `display_backlight_set()`, etc.)
5. All responses use `send_response()` with prefix + `[0xC0, cmd, status, data...]` format

```c
// protocol.c — two entry points depending on build
#ifdef VIA_ENABLE
// VIAL build: VIA owns raw_hid_receive(), unrecognised IDs fall through here
bool raw_hid_receive_kb(uint8_t *data, uint8_t length) {
    if (data[0] != PROTO_PREFIX) return false;
    proto_handle_packet(&data[1], length - 1);
    memset(data, 0, length);  // neutralise VIA's mandatory echo
    return true;
}
#else
// Standalone build: we own the raw HID endpoint
void raw_hid_receive(uint8_t *data, uint8_t length) {
    proto_handle_packet(data, length);
}
#endif
```

### JSON Parsing

Simple string parsing in `display.c` without external libraries:
- Uses `strstr()` to find field names
- Uses `strchr()` to find closing quotes
- Handles `null` values for nullable fields
- Validates lengths to prevent buffer overflows

## Debugging

### Enable Console Output

1. Console is already enabled in `keyboard.json`
2. Run `qmk console` to view debug messages
3. Display driver prints:
   - Initialization status
   - Parsed JSON data
   - Rendering operations

### Common Issues

**Display not initializing**:
- Check SPI wiring (SCK, MOSI, CS, DC, RST)
- Verify power supply (3.3V, sufficient current)
- Check console for error messages
- Verify display configuration in config.h

**HID device not found**:
- Verify USB VID/PID match firmware
- Check usage page/usage ID (0xFF60/0x61)
- Try different HID library

**Display shows garbled text**:
- Check display orientation (QP_ROTATION_90)
- Verify display resolution (284x76)
- Check viewport offsets if needed

**JSON not parsing**:
- Verify JSON format (no spaces around colons for simple parser)
- Check string lengths (max 64 chars for session/task)
- Ensure null termination

## Customization

### Change Colors

Edit HSV values in `keyboards/core_deck/rev1/display.c`:

```c
// White: 0, 0, 255
// Blue: 170, 255, 255
// Green: 85, 255, 255
// Red: 0, 255, 255
// Yellow: 43, 255, 255
// Cyan: 128, 255, 255
// Magenta: 213, 255, 255
```

### Change Layout

Modify `display_render()` to adjust:
- Y positions of each line
- Text alignment

### Add More Fields

1. Add fields to `display_data_t` struct in `display.h`
2. Add parsing in `parse_json()` in `display.c` (called via `display_update_json()`)
3. Add rendering in `display_render()` in `display.c`
4. Update JSON format in companion app

### Add New HID Commands

1. Define the command ID in `protocol.c` `process_message()` switch
2. If the command needs display interaction, add a public function in `display.h/c`
3. Update companion app and this protocol documentation

## Performance

- **Display Refresh**: On-demand (only when data received)
- **SPI Speed**: Divisor 16 (fast updates)
- **HID Latency**: <10ms typical
- **CPU Impact**: Minimal (no continuous polling)

## Future Enhancements

- Custom fonts for better readability
- Icons/graphics for visual indicators
- Multiple screens/pages
- Animations for progress updates
- Error/warning indicators
- Touch screen integration (if hardware supports it)
