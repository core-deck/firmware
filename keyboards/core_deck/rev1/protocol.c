// Copyright 2024 vden
// SPDX-License-Identifier: GPL-2.0-or-later

#include "quantum.h"
#include "protocol.h"
#include "display.h"
#include "raw_hid.h"
#include "softkeys.h"
#include "theme.h"
#include <string.h>

/* Firmware version (update on each release) */
#define FW_VERSION "2.2.0"

/* Internal header size for parsing (flags + cmd, after prefix is stripped) */
#define PROTO_PKT_HEADER 2

/* Mode state */
static uint8_t current_mode_state = MODE_DEFAULT;

/* YOLO switch state */
static bool current_yolo_state = false;

void mode_state_set(uint8_t mode) {
    if (mode < MODE_COUNT) {
        current_mode_state = mode;
    }
}

uint8_t mode_state_get(void) {
    return current_mode_state;
}

void mode_state_cycle(void) {
    current_mode_state = (current_mode_state + 1) % MODE_COUNT;
}

void yolo_state_set(bool on) {
    current_yolo_state = on;
}

bool yolo_state_get(void) {
    return current_yolo_state;
}

void send_state_report(void) {
    uint8_t packet[32] = {0};
    uint8_t p = 0;
#ifdef VIA_ENABLE
    packet[p++] = PROTO_PREFIX;
#endif
    packet[p + 0] = PROTO_FLAG_START | PROTO_FLAG_END;  /* 0xC0 */
    packet[p + 1] = CMD_STATE_REPORT;                   /* 0x10 */
    packet[p + 2] = STATE_BYTE(mode_state_get(), yolo_state_get());
    raw_hid_send(packet, 32);
}

/**
 * @brief Send a key event to the host (single-packet, unsolicited)
 *
 * Payload: [kc_hi, kc_lo] — 16-bit QMK keycode.
 * Host decodes modifiers (bits 12-8) + base keycode (bits 7-0 = USB HID usage).
 */
void send_key_event(uint16_t keycode) {
    uint8_t packet[32] = {0};
    uint8_t p = 0;
#ifdef VIA_ENABLE
    packet[p++] = PROTO_PREFIX;
#endif
    packet[p + 0] = PROTO_FLAG_START | PROTO_FLAG_END;  /* 0xC0 */
    packet[p + 1] = CMD_KEY_EVENT;                      /* 0x12 */
    packet[p + 2] = (keycode >> 8) & 0xFF;
    packet[p + 3] = keycode & 0xFF;
    raw_hid_send(packet, 32);
}

/**
 * @brief Send a type-string command to the host (chunked, unsolicited)
 *
 * Reuses the same chunked framing as send_response() — the flags byte
 * occupies the position where the status byte normally sits.
 * Payload per message: [flags_byte, string_bytes...]
 *
 * @param flags  SOFTKEY_STRING flags (bit 0 = send Enter after string)
 * @param str    Null-terminated string to type
 */
void send_type_string(uint8_t flags, const char *str) {
    uint16_t str_len = strlen(str);
    uint16_t total = 1 + str_len;  /* flags byte + string */
    uint16_t sent = 0;
    const uint8_t p = PROTO_PREFIX_SIZE;

    while (sent < total) {
        uint8_t packet[32] = {0};
        uint8_t pkt_flags = 0;

        if (sent == 0) pkt_flags |= PROTO_FLAG_START;

        uint16_t remaining = total - sent;
        uint8_t chunk_len = remaining < PROTO_PAYLOAD_SIZE ? (uint8_t)remaining : PROTO_PAYLOAD_SIZE;

        if (sent + chunk_len >= total) pkt_flags |= PROTO_FLAG_END;

#ifdef VIA_ENABLE
        packet[0] = PROTO_PREFIX;
#endif
        packet[p + 0] = pkt_flags;
        packet[p + 1] = CMD_TYPE_STRING;

        if (sent == 0) {
            /* First chunk: [flags, string...] */
            packet[p + 2] = flags;
            if (str_len > 0 && chunk_len > 1) {
                memcpy(&packet[p + 3], str, chunk_len - 1);
            }
        } else {
            /* Continuation: string bytes at offset (sent - 1) */
            memcpy(&packet[p + 2], &str[sent - 1], chunk_len);
        }

        raw_hid_send(packet, 32);
        sent += chunk_len;
    }
}

/* Chunked protocol reassembly state */
static uint8_t  reassembly_buf[PROTO_REASSEMBLY_SIZE];
static uint16_t reassembly_len = 0;
static uint8_t  reassembly_cmd = 0;
static bool     reassembly_active = false;

/**
 * @brief Build and send a (possibly multi-packet) response
 *
 * Wire format per packet: [flags, cmd, payload...]
 * The first chunk's payload starts with the status byte: [status, data...]
 * Continuation chunks carry only data bytes.
 * For responses that fit in one packet this produces the same 0xC0 frame
 * that was used before, so existing host code still works.
 */
static void send_response(uint8_t cmd, uint8_t status, const uint8_t *data, uint16_t data_len) {
    uint16_t total = 1 + data_len;  // status byte + data
    uint16_t sent = 0;
    const uint8_t p = PROTO_PREFIX_SIZE;

    while (sent < total) {
        uint8_t packet[32] = {0};
        uint8_t flags = 0;

        if (sent == 0) flags |= PROTO_FLAG_START;

        uint16_t remaining = total - sent;
        uint8_t chunk_len = remaining < PROTO_PAYLOAD_SIZE ? (uint8_t)remaining : PROTO_PAYLOAD_SIZE;

        if (sent + chunk_len >= total) flags |= PROTO_FLAG_END;

#ifdef VIA_ENABLE
        packet[0] = PROTO_PREFIX;
#endif
        packet[p + 0] = flags;
        packet[p + 1] = cmd;

        if (sent == 0) {
            /* First chunk: [status, data...] */
            packet[p + 2] = status;
            if (data && chunk_len > 1) {
                memcpy(&packet[p + 3], data, chunk_len - 1);
            }
        } else {
            /* Continuation: data starting at offset (sent - 1) */
            if (data) {
                memcpy(&packet[p + 2], &data[sent - 1], chunk_len);
            }
        }

        raw_hid_send(packet, 32);
        sent += chunk_len;
    }
}

/**
 * @brief Send an error response
 */
static void send_error(uint8_t error_code) {
    uint8_t response[32] = {0};
    uint8_t p = 0;
#ifdef VIA_ENABLE
    response[p++] = PROTO_PREFIX;
#endif
    response[p + 0] = PROTO_FLAG_START | PROTO_FLAG_END;  // 0xC0
    response[p + 1] = 0xFF;
    response[p + 2] = error_code;
    raw_hid_send(response, 32);
}

/**
 * @brief Process a complete reassembled message
 *
 * Commands:
 *   0x01: Update display data (JSON)
 *   0x02: Ping (responds with pong)
 *   0x03: Set brightness [brightness 0-255, save_flag 0/1]
 *   0x04: Set soft key [key_index, type, save_flag, data...]
 *   0x05: Get soft key [key_index]
 *   0x06: Reset all soft keys
 *   0x07: Set mode state [mode 0-2]
 *   0x08: Alert [JSON: tab, session, text]
 *   0x09: Get firmware version (responds with version string)
 *   0x0A: Disconnect (host signals it is going away — immediately go idle)
 *   0x0B: Set theme slot [slot, hue, sat, val, save_flag]
 *   0x0C: Get theme [slot, or 0xFF to dump all]
 *   0x0D: Reset theme (resets and returns dump-all response)
 */

/**
 * @brief Build a [count, h0,s0,v0, h1,s1,v1, ...] dump of all theme slots.
 * @return Number of bytes written to buf (1 + 3*THEME_SLOT_COUNT).
 */
static uint16_t theme_dump_response(uint8_t *buf) {
    buf[0] = (uint8_t)THEME_SLOT_COUNT;
    for (uint8_t i = 0; i < THEME_SLOT_COUNT; i++) {
        theme_get(i, &buf[1 + i * 3], &buf[2 + i * 3], &buf[3 + i * 3]);
    }
    return 1 + (uint16_t)THEME_SLOT_COUNT * 3;
}
static void process_message(uint8_t cmd, const uint8_t *payload, uint16_t len) {
    switch (cmd) {
        case 0x01: {  // Update display data
            display_ping_received();
            display_update_json((const char *)payload, len);
            send_response(0x01, 0x00, NULL, 0);
            break;
        }

        case 0x02:  // Ping
            display_ping_received();
            send_response(0x02, 0x00, NULL, 0);
            send_state_report();
            break;

        case 0x03:  // Set brightness
            display_ping_received();
            if (len >= 1) {
                display_backlight_set(payload[0]);
                if (len >= 2 && payload[1] == 0x01) {
                    display_backlight_save();
                }
                dprintf("Brightness set to %d\n", payload[0]);
            }
            {
                uint8_t brightness_data[1] = {display_backlight_get()};
                send_response(0x03, 0x00, brightness_data, 1);
            }
            break;

        case 0x04: {  // Set soft key
            display_ping_received();
            if (len >= 3) {
                uint8_t key_index = payload[0];
                uint8_t type = payload[1];
                bool save = payload[2] != 0;
                const uint8_t *sk_data = (len > 3) ? &payload[3] : NULL;
                uint8_t sk_data_len = (len > 3) ? (len - 3) : 0;

                uint8_t resp_data[3] = {0};
                if (softkeys_set(key_index, type, sk_data, sk_data_len, save)) {
                    resp_data[0] = 0x01;  // success
                    resp_data[1] = key_index;
                    resp_data[2] = type;
                }
                send_response(0x04, resp_data[0], &resp_data[1], 2);
            } else {
                send_response(0x04, 0x00, NULL, 0);
            }
            break;
        }

        case 0x05: {  // Get soft key
            display_ping_received();
            if (len >= 1) {
                uint8_t key_index = payload[0];
                const softkey_entry_t *entry = softkeys_get(key_index);
                if (entry) {
                    /* Build response: [key_index, type, entry_data...]
                     * DEFAULT is resolved to KEYCODE so the host never
                     * sees the internal "default" state. */
                    uint8_t resp_buf[2 + SOFTKEY_DATA_LEN];
                    resp_buf[0] = key_index;
                    uint16_t entry_data_len = 0;

                    if (entry->type == SOFTKEY_DEFAULT) {
                        resp_buf[1] = SOFTKEY_KEYCODE;
                        uint16_t kc = softkeys_get_keymap_keycode(key_index);
                        resp_buf[2] = (kc >> 8) & 0xFF;
                        resp_buf[3] = kc & 0xFF;
                        entry_data_len = 2;
                    } else if (entry->type == SOFTKEY_KEYCODE) {
                        resp_buf[1] = SOFTKEY_KEYCODE;
                        entry_data_len = 2;
                        memcpy(&resp_buf[2], entry->data, entry_data_len);
                    } else if (entry->type == SOFTKEY_STRING) {
                        resp_buf[1] = SOFTKEY_STRING;
                        entry_data_len = 1 + strlen((const char *)&entry->data[1]);
                        memcpy(&resp_buf[2], entry->data, entry_data_len);
                    } else if (entry->type == SOFTKEY_SEQUENCE) {
                        resp_buf[1] = SOFTKEY_SEQUENCE;
                        uint8_t count = entry->data[0];
                        entry_data_len = 1 + count * 2;
                        memcpy(&resp_buf[2], entry->data, entry_data_len);
                    }

                    send_response(0x05, 0x00, resp_buf, 2 + entry_data_len);
                } else {
                    send_response(0x05, 0x00, NULL, 0);
                }
            }
            break;
        }

        case 0x06:  // Reset all soft keys
            display_ping_received();
            softkeys_reset_all();
            {
                /* Return effective assignment for each key: [type, kc_hi, kc_lo] x3
                 * DEFAULT is resolved to KEYCODE for the host. */
                uint8_t resp_data[SOFTKEY_COUNT * 3];
                for (uint8_t i = 0; i < SOFTKEY_COUNT; i++) {
                    const softkey_entry_t *e = softkeys_get(i);
                    uint8_t etype = e->type;
                    uint16_t kc;
                    if (etype == SOFTKEY_DEFAULT) {
                        etype = SOFTKEY_KEYCODE;
                        kc = softkeys_get_keymap_keycode(i);
                    } else if (etype == SOFTKEY_KEYCODE) {
                        kc = ((uint16_t)e->data[0] << 8) | e->data[1];
                    } else {
                        kc = 0;
                    }
                    resp_data[i * 3] = etype;
                    resp_data[i * 3 + 1] = (kc >> 8) & 0xFF;
                    resp_data[i * 3 + 2] = kc & 0xFF;
                }
                send_response(0x06, 0x00, resp_data, sizeof(resp_data));
            }
            break;

        case 0x08:  // Alert
            display_ping_received();
            display_update_alert((const char *)payload, len);
            send_response(0x08, 0x00, NULL, 0);
            break;

        case 0x07:  // Set mode state
            display_ping_received();
            if (len >= 1 && payload[0] < MODE_COUNT) {
                mode_state_set(payload[0]);
                dprintf("Mode set to %d\n", payload[0]);
            }
            {
                uint8_t resp_data[1] = {mode_state_get()};
                send_response(0x07, 0x00, resp_data, 1);
            }
            send_state_report();
            break;

        case CMD_GET_VERSION:  // Get firmware version
            display_ping_received();
            send_response(CMD_GET_VERSION, 0x00,
                          (const uint8_t *)FW_VERSION, strlen(FW_VERSION));
            break;

        case CMD_DISCONNECT:  // Host signals it is disconnecting
            send_response(CMD_DISCONNECT, 0x00, NULL, 0);
            display_host_disconnected();
            break;

        case CMD_SET_THEME: {  // Set one theme slot [slot, h, s, v, save]
            display_ping_received();
            uint8_t resp[4] = {0};
            if (len >= 5 && payload[0] < THEME_SLOT_COUNT) {
                uint8_t slot = payload[0];
                theme_set(slot, payload[1], payload[2], payload[3], payload[4] != 0);
                resp[0] = slot;
                theme_get(slot, &resp[1], &resp[2], &resp[3]);
                display_render();  /* Refresh with new colour */
                dprintf("Theme: set slot %d = (%d,%d,%d) save=%d\n",
                        slot, payload[1], payload[2], payload[3], payload[4]);
            }
            send_response(CMD_SET_THEME, 0x00, resp, sizeof(resp));
            break;
        }

        case CMD_GET_THEME: {  // Get theme [slot, or 0xFF for dump all]
            display_ping_received();
            if (len >= 1 && payload[0] != THEME_GET_ALL) {
                uint8_t slot = payload[0];
                uint8_t resp[4] = {0};
                if (slot < THEME_SLOT_COUNT) {
                    resp[0] = slot;
                    theme_get(slot, &resp[1], &resp[2], &resp[3]);
                }
                send_response(CMD_GET_THEME, 0x00, resp, sizeof(resp));
            } else {
                uint8_t buf[1 + THEME_SLOT_COUNT * 3];
                uint16_t n = theme_dump_response(buf);
                send_response(CMD_GET_THEME, 0x00, buf, n);
            }
            break;
        }

        case CMD_RESET_THEME: {  // Reset theme to firmware defaults
            display_ping_received();
            theme_reset();
            display_render();
            uint8_t buf[1 + THEME_SLOT_COUNT * 3];
            uint16_t n = theme_dump_response(buf);
            send_response(CMD_RESET_THEME, 0x00, buf, n);
            dprintf("Theme: reset to defaults\n");
            break;
        }

        default:
            dprintf("Unknown command: 0x%02X\n", cmd);
            send_error(PROTO_ERR_UNKNOWN_CMD);
            break;
    }
}

/**
 * @brief Chunked protocol handler — shared receive logic
 *
 * @param data    Points to [flags, cmd, payload...] (prefix already stripped)
 * @param length  Number of bytes (32 for standalone, 31 for VIAL after prefix)
 *
 * Packet format (after prefix):
 *   Byte 0: Flags (bit 7 = START, bit 6 = END)
 *   Byte 1: Command ID
 *   Bytes 2+: Payload
 */
static void proto_handle_packet(uint8_t *data, uint8_t length) {
    if (length < PROTO_PKT_HEADER) return;

    uint8_t flags = data[0];
    uint8_t cmd   = data[1];
    const uint8_t *chunk = &data[PROTO_PKT_HEADER];
    uint8_t chunk_len = length - PROTO_PKT_HEADER;

    bool is_start = (flags & PROTO_FLAG_START) != 0;
    bool is_end   = (flags & PROTO_FLAG_END) != 0;

    if (is_start && is_end) {
        // Single-packet message — process immediately
        process_message(cmd, chunk, chunk_len);
        return;
    }

    if (is_start) {
        // First chunk of multi-packet message
        reassembly_active = true;
        reassembly_cmd = cmd;
        reassembly_len = 0;

        if (chunk_len > PROTO_REASSEMBLY_SIZE) {
            send_error(PROTO_ERR_OVERFLOW);
            reassembly_active = false;
            return;
        }
        memcpy(reassembly_buf, chunk, chunk_len);
        reassembly_len = chunk_len;
        return;
    }

    // Middle or END chunk — must have an active reassembly
    if (!reassembly_active) {
        send_error(PROTO_ERR_BAD_SEQ);
        return;
    }

    // Verify command matches
    if (cmd != reassembly_cmd) {
        send_error(PROTO_ERR_BAD_SEQ);
        reassembly_active = false;
        return;
    }

    // Check for overflow
    if (reassembly_len + chunk_len > PROTO_REASSEMBLY_SIZE) {
        send_error(PROTO_ERR_OVERFLOW);
        reassembly_active = false;
        return;
    }

    // Append chunk
    memcpy(&reassembly_buf[reassembly_len], chunk, chunk_len);
    reassembly_len += chunk_len;

    if (is_end) {
        // Complete message — process it
        reassembly_active = false;
        process_message(reassembly_cmd, reassembly_buf, reassembly_len);
    }
}

#ifdef VIA_ENABLE
/**
 * @brief VIA/VIAL raw HID callback — receives unrecognised command IDs
 *
 * In vial-qmk, VIA owns raw_hid_receive(). Unrecognised command IDs
 * (anything not 0x01-0x15 or 0xFE) fall through to raw_hid_receive_kb().
 * Our custom protocol uses a 0x80 prefix byte which VIA doesn't claim.
 *
 * After processing, we memset the buffer to zero to neutralise VIA's
 * mandatory echo (it always calls raw_hid_send(data, length) after
 * raw_hid_receive_kb returns).
 */
bool raw_hid_receive_kb(uint8_t *data, uint8_t length) {
    if (length < 1 || data[0] != PROTO_PREFIX) {
        return false;  // Not our protocol — ignore
    }

    proto_handle_packet(&data[1], length - 1);

    /* Neutralise VIA's mandatory echo — zero the buffer so the host
     * receives an all-zero packet instead of our raw data. The companion
     * app filters these out (data[0] != PROTO_PREFIX). */
    memset(data, 0, length);
    return true;
}

#else
/**
 * @brief Standalone raw HID callback — no VIA, no prefix
 *
 * Packet format (32 bytes):
 *   Byte 0: Flags (bit 7 = START, bit 6 = END)
 *   Byte 1: Command ID
 *   Bytes 2-31: Payload (30 bytes per chunk)
 */
void raw_hid_receive(uint8_t *data, uint8_t length) {
    proto_handle_packet(data, length);
}

#endif /* VIA_ENABLE */
