/* Local app integration; not an upstream contribution. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef APPLE_GFX_APP_H
#define APPLE_GFX_APP_H
#include "apple-gfx-app-wire.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>

typedef struct AppleGFXApp {
    int fd;
    void *memory;
    id<MTLBuffer> buffer;
    dispatch_queue_t writer;
    QemuMutex lock;
    uint64_t sequence;
    void (*done)(void *);
    void *opaque;
    AppleGFXState *state;
    uint32_t power_epoch;
    bool power_sleeping;
    bool framework_sleeping;
    bool service_sleeping;
} AppleGFXApp;

static void apple_gfx_ui_info(void *, uint32_t, QemuUIInfo *);
static void new_frame_handler_bh(void *opaque);

static void app_fail(void)
{
    error_report("Virtual Intel Mac direct display disconnected or invalid");
    _exit(1);
}

static void app_send(AppleGFXApp *a, AppleGFXAppMessage message, NSData *pixels)
{
    [pixels retain];
    dispatch_async(a->writer, ^{
        AppleGFXAppMessage m = message;
        if (!apple_gfx_oop_io(a->fd, &m, sizeof(m), true) ||
            (pixels && !apple_gfx_oop_io(a->fd, (void *)pixels.bytes,
                                       pixels.length, true))) {
            app_fail();
        }
        [pixels release];
    });
}

/* PGDisplayDescriptor's callback queue serializes transitions. Rendering only
 * samples the atomic epoch; no extra lock, IPC or copy per unchanged frame. */
static void app_update_power(AppleGFXApp *a)
{
    bool sleeping = a->framework_sleeping || a->service_sleeping;
    if (a->power_sleeping == sleeping) {
        return;
    }
    a->power_sleeping = sleeping;
    uint32_t epoch = qatomic_read(&a->power_epoch);
    if (epoch == UINT32_MAX) {
        app_fail();
    }
    qatomic_set(&a->power_epoch, ++epoch);
    AppleGFXAppMessage message = { .magic = APPLE_GFX_APP_MAGIC,
        .operation = APPLE_GFX_APP_DISPLAY_POWER,
        .sequence = epoch, .width = sleeping };
    app_send(a, message, nil);
}
static void app_display_power(AppleGFXApp *a, bool sleeping)
{
    if (!a) { return; }
    a->framework_sleeping = sleeping;
    app_update_power(a);
}

static AppleGFXApp *app_connect(AppleGFXState *s)
{
    const char *path = getenv("PVG_APP_DISPLAY_SOCKET");
    if (!path || !*path) {
        return NULL;
    }
    AppleGFXApp *a = g_new0(AppleGFXApp, 1);
    a->state = s;
    qemu_mutex_init(&a->lock);
    a->writer = dispatch_queue_create("pvg.direct.app", DISPATCH_QUEUE_SERIAL);
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    if (strlen(path) >= sizeof(address.sun_path)) {
        app_fail();
    }
    pstrcpy(address.sun_path, sizeof(address.sun_path), path);
    a->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    uid_t uid; gid_t gid;
    if (a->fd < 0 || connect(a->fd, (struct sockaddr *)&address, sizeof(address)) ||
        getpeereid(a->fd, &uid, &gid) || uid != getuid()) {
        app_fail();
    }
    int one = 1;
    setsockopt(a->fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    int memory_fd = qemu_shm_alloc(APPLE_GFX_OOP_FRAME_SIZE, &error_fatal);
    a->memory = mmap(NULL, APPLE_GFX_OOP_FRAME_SIZE, PROT_READ | PROT_WRITE,
                     MAP_SHARED, memory_fd, 0);
    if (a->memory == MAP_FAILED) {
        app_fail();
    }
    a->buffer = [s->mtl newBufferWithBytesNoCopy:a->memory
        length:APPLE_GFX_OOP_FRAME_SIZE options:MTLResourceStorageModeShared
        deallocator:nil];
    if (!a->buffer) {
        app_fail();
    }
    AppleGFXAppMessage hello = { .magic = APPLE_GFX_APP_MAGIC,
        .operation = APPLE_GFX_APP_HELLO };
    char control[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec iov = { &hello, sizeof(hello) };
    struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1,
        .msg_control = control, .msg_controllen = sizeof(control) };
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET; cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &memory_fd, sizeof(memory_fd));
    ssize_t n;
    do { n = sendmsg(a->fd, &msg, 0); } while (n < 0 && errno == EINTR);
    if (n <= 0 || ((size_t)n < sizeof(hello) &&
        !apple_gfx_oop_io(a->fd, (char *)&hello + n, sizeof(hello) - n, true))) {
        app_fail();
    }
    close(memory_fd);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        for (;;) {
            AppleGFXAppMessage m;
            if (!apple_gfx_oop_io(a->fd, &m, sizeof(m), false) ||
                m.magic != APPLE_GFX_APP_MAGIC || m.reserved) {
                app_fail();
            }
            if (m.operation == APPLE_GFX_APP_ACK) {
                qemu_mutex_lock(&a->lock);
                void (*done)(void *) = a->done;
                void *opaque = a->opaque;
                if (!done || m.sequence != a->sequence || m.width || m.height || m.stride) {
                    app_fail();
                }
                a->done = NULL;
                qemu_mutex_unlock(&a->lock);
                done(opaque);
            } else if (m.operation == APPLE_GFX_APP_GUEST_POWER) {
                if (m.sequence || m.width > 1 || m.height || m.stride) {
                    app_fail();
                }
                dispatch_async(dispatch_get_main_queue(), ^{
                    bool was_asleep = a->power_sleeping;
                    a->service_sleeping = m.width != 0;
                    app_update_power(a);
                    if (was_asleep && !a->power_sleeping) {
                        aio_bh_schedule_oneshot(qemu_get_aio_context(),
                                               new_frame_handler_bh, s);
                    }
                });
            } else if (m.operation == APPLE_GFX_APP_SET_MODE) {
                if (m.sequence || m.stride || m.width < 320 || m.height < 200 ||
                    m.width > APPLE_GFX_OOP_MAX_DIMENSION ||
                    m.height > APPLE_GFX_OOP_MAX_DIMENSION) {
                    app_fail();
                }
                QemuUIInfo info = { .width = m.width, .height = m.height };
                bql_lock();
                apple_gfx_ui_info(s, 0, &info);
                bql_unlock();
            } else {
                app_fail();
            }
        }
    });
    return a;
}

static void app_present(AppleGFXApp *a, uint32_t w, uint32_t h, uint32_t stride,
                        uint32_t power_epoch, void (*done)(void *), void *opaque)
{
    qemu_mutex_lock(&a->lock);
    assert(!a->done);
    a->done = done; a->opaque = opaque;
    AppleGFXAppMessage m = { .magic = APPLE_GFX_APP_MAGIC,
        .operation = APPLE_GFX_APP_POWER_FRAME, .sequence = ++a->sequence,
        .width = w, .height = h, .stride = stride, .reserved = power_epoch };
    qemu_mutex_unlock(&a->lock);
    app_send(a, m, nil);
}
#endif
