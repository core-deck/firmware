// Copyright 2024 vden
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdint.h>

/* Protocol prefix byte — distinguishes custom protocol from VIA/VIAL commands.
 * When VIA_ENABLE is set, all custom packets are prefixed with 0x80 so VIA
 * routes unrecognised command IDs to raw_hid_receive_kb(). */
#ifdef VIA_ENABLE
#    define PROTO_PREFIX          0x80
#    define PROTO_PREFIX_SIZE     1
#else
#    define PROTO_PREFIX_SIZE     0
#endif

/* Chunked protocol constants */
#define PROTO_FLAG_START       0x80
#define PROTO_FLAG_END         0x40
#define PROTO_HEADER_SIZE      (2 + PROTO_PREFIX_SIZE) /* [prefix] + flags + command */
#define PROTO_PAYLOAD_SIZE     (32 - PROTO_HEADER_SIZE)
#define PROTO_REASSEMBLY_SIZE  512

/* Protocol error codes */
#define PROTO_ERR_OVERFLOW     0x01
#define PROTO_ERR_BAD_SEQ      0x02
#define PROTO_ERR_UNKNOWN_CMD  0x03

/* Command IDs.
 * These live at byte 2 of every packet (after the 0x80 prefix and the
 * flags byte) so they cannot collide with VIA command IDs, which VIA reads
 * from byte 0. Stay below 0xF0 to leave room for unsolicited keyboard→host
 * commands at 0x10+. */
#define CMD_ALERT         0x08  /* Host → Keyboard: set/clear tab alert */
#define CMD_GET_VERSION   0x09  /* Host → Keyboard: query firmware version */
#define CMD_DISCONNECT    0x0A  /* Host → Keyboard: signal host is disconnecting */
#define CMD_SET_THEME     0x0B  /* Host → Keyboard: set one theme slot */
#define CMD_GET_THEME     0x0C  /* Host → Keyboard: read one slot or dump all */
#define CMD_RESET_THEME   0x0D  /* Host → Keyboard: reset theme to defaults */
#define CMD_STATE_REPORT  0x10  /* Keyboard → Host: unsolicited state report */
#define CMD_TYPE_STRING   0x11  /* Keyboard → Host: type string (for SOFTKEY_STRING) */
#define CMD_KEY_EVENT     0x12  /* Keyboard → Host: key event (16-bit QMK keycode) */

/* CMD_GET_THEME byte-0 sentinel meaning "dump all slots". */
#define THEME_GET_ALL     0xFF

/* Mode button states — cycle order: default → accept → plan → auto → default.
 * Mirrors Claude Code's cyclable terminal modes (default, acceptEdits,
 * plan, auto). All four fit in the 2 mode bits below. */
#define MODE_DEFAULT  0
#define MODE_ACCEPT   1
#define MODE_PLAN     2
#define MODE_AUTO     3
#define MODE_COUNT    4

/* Pack mode + yolo into a single state byte:
 *   Bit 1-0: Mode (0=default, 1=accept, 2=plan, 3=auto)
 *   Bit 2:   YOLO (0=off, 1=on)
 *   Bit 7-3: Reserved (0) */
#define STATE_BYTE(mode, yolo) ((uint8_t)((mode) & 0x03) | ((yolo) ? 0x04 : 0x00))

void mode_state_set(uint8_t mode);
uint8_t mode_state_get(void);
void mode_state_cycle(void);

void yolo_state_set(bool on);
bool yolo_state_get(void);

void send_state_report(void);
void send_type_string(uint8_t flags, const char *str);
void send_key_event(uint16_t keycode);
