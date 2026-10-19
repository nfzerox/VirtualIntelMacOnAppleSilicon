/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef APPLE_GFX_APP_WIRE_H
#define APPLE_GFX_APP_WIRE_H
#include <stdint.h>
/* Private same-host ABI, little-endian on both supported architectures.
 * HELLO carries one SCM_RIGHTS frame descriptor. FRAME owns that memory until
 * the matching ACK. No frame pixels travel over this socket. */
#define APPLE_GFX_APP_MAGIC 0x56494d31u
enum { APPLE_GFX_APP_HELLO = 1, APPLE_GFX_APP_FRAME = 2,
       APPLE_GFX_APP_ACK = 3, APPLE_GFX_APP_CURSOR_STATE = 4,
       APPLE_GFX_APP_CURSOR_GLYPH = 5, APPLE_GFX_APP_SET_MODE = 6,
       APPLE_GFX_APP_DISPLAY_POWER = 7, APPLE_GFX_APP_POWER_FRAME = 8,
       APPLE_GFX_APP_GUEST_POWER = 9 };
/* GUEST_POWER: width=asleep (0/1), remaining fields zero. Producer serializes
 * it with native power events and tags frames with the resulting epoch. */
/* DISPLAY_POWER: width=asleep (0/1), sequence=nonzero uint32 power epoch,
 * other fields zero; no ACK. POWER_FRAME is FRAME with reserved=power epoch.
 * ACK still uses the frame sequence, not its power epoch. Legacy FRAME has
 * reserved=0 and is accepted only before the first power transition. */
/* SET_MODE: width/height are requested backing pixels; remaining fields zero. */
/* CURSOR_STATE: width is visibility (0/1), all other fields zero.
 * CURSOR_GLYPH: width/height <=256, stride packs hotspot x(low16)/y(high16),
 * followed by width*height BGRA bytes. Neither cursor event expects an ACK. */
typedef struct {
    uint32_t magic, operation;
    uint64_t sequence;
    uint32_t width, height, stride, reserved;
} AppleGFXAppMessage;
_Static_assert(sizeof(AppleGFXAppMessage) == 32, "App display wire ABI");
#endif
