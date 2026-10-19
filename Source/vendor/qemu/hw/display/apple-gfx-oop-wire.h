/* Experimental local IPC ABI. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef APPLE_GFX_OOP_WIRE_H
#define APPLE_GFX_OOP_WIRE_H
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#define APPLE_GFX_OOP_MAGIC 0x50475632u
#define APPLE_GFX_OOP_MAX_RANGES 128
#define APPLE_GFX_OOP_WIDTH 1280
#define APPLE_GFX_OOP_HEIGHT 800
#define APPLE_GFX_OOP_STRIDE (APPLE_GFX_OOP_WIDTH * 4)
#define APPLE_GFX_OOP_MAX_DIMENSION 8192
#define APPLE_GFX_OOP_FRAME_SIZE (APPLE_GFX_OOP_MAX_DIMENSION * APPLE_GFX_OOP_MAX_DIMENSION * 4)
typedef struct {
    uint32_t magic, count;
    int32_t frame_fd;
    uint32_t reserved;
} AppleGFXOOPHello;
typedef struct {
    uint64_t guest, length, file_offset;
    int32_t inherited_fd; /* valid only in spawn bootstrap, explicitly inherited */
    uint32_t reserved;
} AppleGFXOOPRAMWire;
typedef struct {
    uint32_t magic, status;
    uint64_t mmio_length;
} AppleGFXOOPReady;
enum {
    APPLE_GFX_OOP_READ = 1,
    APPLE_GFX_OOP_WRITE = 2,
    APPLE_GFX_OOP_RESET = 3,
    APPLE_GFX_OOP_IRQ = 4,
    APPLE_GFX_OOP_FRAME = 5,
    APPLE_GFX_OOP_FRAME_ACK = 6,
    APPLE_GFX_OOP_CURSOR_STATE = 7,
    APPLE_GFX_OOP_CURSOR_GLYPH = 8,
    APPLE_GFX_OOP_SET_MODE = 9,
};
/* Glyph event: offset packs width/height, value packs hotspot x/y.
 * Followed by width*height native little-endian ARGB32 pixels. */
#define APPLE_GFX_OOP_CURSOR_MAX 256
typedef struct {
    uint32_t magic, operation;
    uint64_t sequence, offset, value;
    int32_t status;
    uint32_t reserved;
} AppleGFXOOPCommand;
_Static_assert(sizeof(AppleGFXOOPCommand) == 40, "PVG IPC layout");
static bool apple_gfx_oop_io(int fd, void *bytes, size_t length, bool send)
{
    uint8_t *p = bytes;
    while (length) {
        ssize_t n = send ? write(fd, p, length) : read(fd, p, length);
        if (n < 0 && errno == EINTR) { continue; }
        if (n <= 0) { return false; }
        length -= n;
        p += n;
    }
    return true;
}
#endif
