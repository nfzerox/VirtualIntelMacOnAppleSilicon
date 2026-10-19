/* Local experimental PVG helper RAM export. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef APPLE_GFX_OOP_RAM_H
#define APPLE_GFX_OOP_RAM_H
#include "system/ramblock.h"
#include "apple-gfx-oop-wire.h"
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <crt_externs.h>
#include <sys/mman.h>

typedef struct AppleGFXOOPRange {
    uint64_t guest, length, file_offset;
    int fd; /* owned duplicate; transfer out-of-band, never as a wire integer */
    MemoryRegion *mr; /* retained until export is freed */
} AppleGFXOOPRange;

typedef struct AppleGFXOOPCollector {
    MemoryListener listener;
    GArray *ranges;
    Error *error;
} AppleGFXOOPCollector;

static void apple_gfx_oop_collect(MemoryListener *listener,
                                  MemoryRegionSection *section)
{
    AppleGFXOOPCollector *c = container_of(listener, AppleGFXOOPCollector, listener);
    if (c->error || !memory_region_is_ram(section->mr) || section->readonly) {
        return;
    }
    void *host = memory_region_get_ram_ptr(section->mr) + section->offset_within_region;
    ram_addr_t offset;
    RAMBlock *rb = qemu_ram_block_from_host(host, false, &offset);
    uint64_t length = int128_get64(section->size);
    if (!rb || rb->fd < 0 || !(rb->flags & RAM_SHARED) ||
        offset > rb->used_length || length > rb->used_length - offset ||
        rb->fd_offset > UINT64_MAX - offset) {
        error_setg(&c->error, "PVG helper requires shared file-backed RAM: %s",
                   memory_region_name(section->mr));
        return;
    }
    AppleGFXOOPRange r = {
        .guest = section->offset_within_address_space,
        .length = length,
        .file_offset = rb->fd_offset + offset,
        .fd = fcntl(rb->fd, F_DUPFD_CLOEXEC, 0),
        .mr = section->mr,
    };
    if (r.fd < 0) {
        error_setg_errno(&c->error, errno, "Cannot duplicate PVG RAM backing");
        return;
    }
    memory_region_ref(r.mr);
    g_array_append_val(c->ranges, r);
}

static void apple_gfx_oop_free_ranges(GArray *ranges)
{
    for (unsigned i = 0; i < ranges->len; i++) {
        AppleGFXOOPRange *r = &g_array_index(ranges, AppleGFXOOPRange, i);
        close(r->fd);
        memory_region_unref(r->mr);
    }
    g_array_free(ranges, true);
}

/* Caller holds BQL. Snapshot only: hotplug is not supported by this experiment. */
static GArray *apple_gfx_oop_ram_ranges(Error **errp)
{
    AppleGFXOOPCollector c = {
        .listener = {.region_add = apple_gfx_oop_collect},
        .ranges = g_array_new(false, false, sizeof(AppleGFXOOPRange)),
    };
    memory_listener_register(&c.listener, &address_space_memory);
    memory_listener_unregister(&c.listener);
    if (c.error) {
        error_propagate(errp, c.error);
        apple_gfx_oop_free_ranges(c.ranges);
        return NULL;
    }
    return c.ranges;
}

/* Bounded bootstrap check only; not the eventual device I/O wait path. */
static bool apple_gfx_oop_bootstrap(GArray *ranges, const char *helper,
                                   const char *donor, int *live_fd,
                                   pid_t *live_pid, void **live_frame, Error **errp)
{
    int sockets[2];
    if (!donor || !ranges->len || ranges->len > APPLE_GFX_OOP_MAX_RANGES) {
        error_setg(errp, "PVG helper needs donor and 1..128 shared RAM ranges");
        return false;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) {
        error_setg_errno(errp, errno, "PVG helper socketpair"); return false;
    }
    int yes = 1;
    setsockopt(sockets[0], SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
    struct timeval timeout = {.tv_sec = 20};
    setsockopt(sockets[0], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockets[0], SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    posix_spawnattr_t attr;
    posix_spawn_file_actions_t actions;
    int frame_fd = qemu_shm_alloc(APPLE_GFX_OOP_FRAME_SIZE, errp);
    if (frame_fd < 0) {
        close(sockets[0]); close(sockets[1]);
        return false;
    }
    void *frame = mmap(NULL, APPLE_GFX_OOP_FRAME_SIZE, PROT_READ | PROT_WRITE,
                       MAP_SHARED, frame_fd, 0);
    if (frame == MAP_FAILED) {
        close(frame_fd); close(sockets[0]); close(sockets[1]);
        error_setg(errp, "PVG shared frame mapping failed"); return false;
    }
    posix_spawnattr_init(&attr);
    posix_spawn_file_actions_init(&actions);
    int rc = posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT);
    if (!rc) rc = posix_spawn_file_actions_addinherit_np(&actions, frame_fd);
    if (!rc) rc = posix_spawn_file_actions_addinherit_np(&actions, sockets[1]);
    if (!rc) rc = posix_spawn_file_actions_addinherit_np(&actions, STDERR_FILENO);
    for (unsigned i = 0; !rc && i < ranges->len; i++) {
        rc = posix_spawn_file_actions_addinherit_np(&actions,
                g_array_index(ranges, AppleGFXOOPRange, i).fd);
    }
    char channel[32];
    snprintf(channel, sizeof(channel), "%d", sockets[1]);
    char *argv[] = {(char *)helper, channel, (char *)donor, NULL};
    pid_t pid;
    if (!rc) rc = posix_spawn(&pid, helper, &actions, &attr, argv, *_NSGetEnviron());
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    close(sockets[1]);
    if (rc) {
        close(frame_fd); munmap(frame, APPLE_GFX_OOP_FRAME_SIZE);
        close(sockets[0]); error_setg_errno(errp, rc, "PVG helper spawn"); return false;
    }
    AppleGFXOOPHello hello = {APPLE_GFX_OOP_MAGIC, ranges->len, frame_fd, 0};
    bool ok = apple_gfx_oop_io(sockets[0], &hello, sizeof(hello), true);
    close(frame_fd);
    for (unsigned i = 0; ok && i < ranges->len; i++) {
        AppleGFXOOPRange *r = &g_array_index(ranges, AppleGFXOOPRange, i);
        AppleGFXOOPRAMWire wire = {r->guest, r->length, r->file_offset, r->fd, 0};
        ok = apple_gfx_oop_io(sockets[0], &wire, sizeof(wire), true);
    }
    AppleGFXOOPReady ready;
    ok = ok && apple_gfx_oop_io(sockets[0], &ready, sizeof(ready), false) &&
         ready.magic == APPLE_GFX_OOP_MAGIC && !ready.status && ready.mmio_length == 16384;
    if (ok && !live_fd) {
        AppleGFXOOPCommand request = {.magic=APPLE_GFX_OOP_MAGIC,
            .operation=APPLE_GFX_OOP_READ, .sequence=1};
        ok = apple_gfx_oop_io(sockets[0], &request, sizeof(request), true);
        while (ok) {
            AppleGFXOOPCommand reply;
            ok = apple_gfx_oop_io(sockets[0], &reply, sizeof(reply), false) &&
                 reply.magic == APPLE_GFX_OOP_MAGIC && !reply.reserved;
            if (!ok) { break; }
            if (reply.operation == APPLE_GFX_OOP_IRQ) { continue; }
            ok = reply.operation == request.operation && reply.sequence == 1 && !reply.status;
            if (ok) fprintf(stderr, "PVG helper register[0]=0x%llx\n",
                            (unsigned long long)reply.value);
            break;
        }
    }
    if (ok) {
        fprintf(stderr, "PVG Intel helper ready: ranges=%u MMIO=%llu\n",
                ranges->len, (unsigned long long)ready.mmio_length);
    } else {
        error_setg(errp, "PVG helper bootstrap failed or timed out");
    }
    if (ok && live_fd) {
        struct timeval unlimited = {0};
        setsockopt(sockets[0], SOL_SOCKET, SO_RCVTIMEO, &unlimited, sizeof(unlimited));
        *live_fd = sockets[0];
        *live_pid = pid;
        *live_frame = frame;
        return true;
    }
    munmap(frame, APPLE_GFX_OOP_FRAME_SIZE);
    close(sockets[0]);
    /* This is a disposable qualification process, never an active VM helper. */
    if (!ok) kill(pid, SIGKILL);
    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return ok;
}
#endif
