/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Software scanout for installers and opt-in PassthroughOpenGL research. PVG does
 * not use this path. One shared frame is owned by the app until its GPU ACK. */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "qemu/cutils.h"
#include "qemu/timer.h"
#include "ui/console.h"
#include "ui/vim-display.h"
#include "hw/display/apple-gfx-app-wire.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>

#define VIM_FRAME_SIZE (8192u * 8192u * 4u)
typedef struct VIMDisplay {
    DisplayChangeListener dcl;
    uint8_t *frame;
    int socket;
    bool dirty, busy;
    bool event_driven, refreshing;
    QEMUBH *event_bh;
    uint64_t sequence;
    AppleGFXAppMessage input;
    size_t input_used;
    QEMUCursor *cursor;
    bool cursor_dirty, visibility_dirty, cursor_visible;
    uint8_t *output;
    size_t output_size, output_sent;
    /* Explicit numeric diagnosis only; no pixel capture or normal logging. */
    bool timing;
    uint64_t timing_start, last_refresh, frame_sent;
    uint64_t refreshes, frames, updates, update_bytes, busy_refreshes;
    uint64_t refresh_gaps, refresh_gap_ns, refresh_gap_max, refresh_work_ns, copy_ns;
    uint64_t acks, ack_ns, ack_max;
} VIMDisplay;
static void vim_read(void *opaque);
static void vim_kick(VIMDisplay *s)
{
    /* Coalesce notifications; an owned shared frame is never overwritten.
     * ACK or completion of a partial metadata write wakes pending work. */
    if (s->event_driven && !s->refreshing && !s->output &&
        (s->cursor_dirty || s->visibility_dirty || (s->dirty && !s->busy))) {
        qemu_bh_schedule(s->event_bh);
    }
}
static void vim_write(void *opaque)
{
    VIMDisplay *s = opaque;
    while (s->output_sent < s->output_size) {
        ssize_t n = write(s->socket, s->output + s->output_sent,
                          s->output_size - s->output_sent);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            qemu_set_fd_handler(s->socket, vim_read, vim_write, s);
            return;
        }
        if (n <= 0) {
            error_report("Virtual Intel Mac display connection closed");
            exit(1);
        }
        s->output_sent += n;
    }
    g_clear_pointer(&s->output, g_free);
    s->output_size = s->output_sent = 0;
    qemu_set_fd_handler(s->socket, vim_read, NULL, s);
    vim_kick(s);
}
static void vim_send(VIMDisplay *s, const AppleGFXAppMessage *message,
                     const void *pixels, size_t bytes)
{
    assert(!s->output);
    s->output_size = sizeof(*message) + bytes;
    s->output = g_malloc(s->output_size);
    memcpy(s->output, message, sizeof(*message));
    if (bytes) {
        memcpy(s->output + sizeof(*message), pixels, bytes);
    }
    vim_write(s);
}
static void vim_cursor_define(DisplayChangeListener *dcl, QEMUCursor *cursor)
{
    VIMDisplay *s = container_of(dcl, VIMDisplay, dcl);
    if (s->cursor) {
        cursor_unref(s->cursor);
    }
    s->cursor = cursor_ref(cursor);
    s->cursor_dirty = true;
    vim_kick(s);
}
static void vim_mouse_set(DisplayChangeListener *dcl, int x, int y, bool on)
{
    VIMDisplay *s = container_of(dcl, VIMDisplay, dcl);
    s->cursor_visible = on;
    s->visibility_dirty = true;
    vim_kick(s);
}

static G_NORETURN void vim_fail(const char *reason)
{
    error_report("Virtual Intel Mac installer display: %s", reason);
    exit(1);
}

static void vim_read(void *opaque)
{
    VIMDisplay *s = opaque;
    for (;;) {
        ssize_t n = read(s->socket, (char *)&s->input + s->input_used,
                         sizeof(s->input) - s->input_used);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }
        if (n <= 0) {
            vim_fail("app connection closed");
        }
        s->input_used += n;
        if (s->input_used != sizeof(s->input)) {
            continue;
        }
        AppleGFXAppMessage *m = &s->input;
        if (m->magic != APPLE_GFX_APP_MAGIC || m->reserved) {
            vim_fail("invalid app message");
        }
        if (m->operation == APPLE_GFX_APP_ACK) {
            if (!s->busy || m->sequence != s->sequence ||
                m->width || m->height || m->stride) {
                vim_fail("invalid frame acknowledgement");
            }
            s->busy = false;
            if (s->timing && s->frame_sent) {
                uint64_t elapsed = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - s->frame_sent;
                s->acks++;
                s->ack_ns += elapsed;
                s->ack_max = MAX(s->ack_max, elapsed);
            }
            vim_kick(s);
        } else if (m->operation == APPLE_GFX_APP_SET_MODE) {
            /* Stock Bochs has no UI-info consumer and retains its boot mode.
             * The opt-in Mavericks device can notify its framebuffer driver. */
            if (m->sequence || m->stride > 2 || m->width > 8192 || m->height > 8192) {
                vim_fail("invalid mode request");
            }
            if (qemu_console_ui_info_supported(s->dcl.con)) {
                QemuUIInfo info = {.width = m->width, .height = m->height,
                                   .logical_scale = m->stride};
                qemu_console_set_ui_info(s->dcl.con, &info, false);
            }
        } else {
            vim_fail("unknown app message");
        }
        s->input_used = 0;
    }
}

static void vim_update(DisplayChangeListener *dcl, int x, int y, int w, int h)
{
    VIMDisplay *s = container_of(dcl, VIMDisplay, dcl);
    s->dirty = true;
    vim_kick(s);
    if (s->timing && w > 0 && h > 0) {
        s->updates++;
        s->update_bytes += (uint64_t)w * h * 4;
    }
}

static void vim_switch(DisplayChangeListener *dcl, DisplaySurface *surface)
{
    VIMDisplay *s = container_of(dcl, VIMDisplay, dcl);
    s->dirty = true;
    vim_kick(s);
}

static void vim_refresh_work_inner(DisplayChangeListener *dcl)
{
    VIMDisplay *s = container_of(dcl, VIMDisplay, dcl);
    qemu_console_hw_update(dcl->con);
    /* Latest cursor shape/state coalesce while the app is backpressured.
     * Complete any partial message before another header; never block TCG. */
    if (s->output) {
        return;
    }
    if (s->cursor_dirty && s->cursor) {
        QEMUCursor *c = s->cursor;
        if (c->width > 0 && c->width <= 256 && c->height > 0 && c->height <= 256) {
            AppleGFXAppMessage cursor = {.magic = APPLE_GFX_APP_MAGIC,
                .operation = APPLE_GFX_APP_CURSOR_GLYPH, .width = c->width,
                .height = c->height, .stride = c->hot_x | (c->hot_y << 16)};
            vim_send(s, &cursor, c->data, c->width * c->height * 4);
        }
        s->cursor_dirty = false;
        if (s->output) {
            return;
        }
    }
    if (s->visibility_dirty) {
        AppleGFXAppMessage state = {.magic = APPLE_GFX_APP_MAGIC,
            .operation = APPLE_GFX_APP_CURSOR_STATE, .width = s->cursor_visible};
        vim_send(s, &state, NULL, 0);
        s->visibility_dirty = false;
        if (s->output) {
            return;
        }
    }
    if (!s->dirty || s->busy) {
        if (s->timing && s->dirty && s->busy) {
            s->busy_refreshes++;
        }
        return;
    }
    DisplaySurface *surface = qemu_console_surface(dcl->con);
    if (!surface || surface_is_placeholder(surface)) {
        /* QEMU's text placeholder is not guest output. Keep AppKit black until
         * firmware/guest video is initialized; never flash that ASCII message. */
        return;
    }
    unsigned w = surface_width(surface), h = surface_height(surface);
    if (w < 320 || h < 200 || w > 8192 || h > 8192) {
        return;
    }
    unsigned stride = (w * 4 + 255) & ~255u;
    uint64_t started = s->timing ? qemu_clock_get_ns(QEMU_CLOCK_REALTIME) : 0;
    pixman_image_t *dest = pixman_image_create_bits(PIXMAN_a8r8g8b8, w, h,
                                                   (uint32_t *)s->frame, stride);
    if (!dest) {
        vim_fail("cannot map software framebuffer");
    }
    pixman_image_composite32(PIXMAN_OP_SRC, surface->image, NULL, dest,
                            0, 0, 0, 0, 0, 0, w, h);
    pixman_image_unref(dest);
    if (s->timing) {
        s->copy_ns += qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - started;
    }
    AppleGFXAppMessage message = {.magic = APPLE_GFX_APP_MAGIC,
        .operation = APPLE_GFX_APP_FRAME, .sequence = ++s->sequence,
        .width = w, .height = h, .stride = stride};
    /* Tiny metadata-only write. Never block the emulator on presentation. */
    vim_send(s, &message, NULL, 0);
    s->busy = true;
    s->dirty = false;
    if (s->timing) {
        s->frames++;
        s->frame_sent = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    }
}

static void vim_refresh_work(DisplayChangeListener *dcl)
{
    VIMDisplay *s = container_of(dcl, VIMDisplay, dcl);
    /* Device refresh can synchronously notify this listener. It is already
     * being drained here; never turn that notification into a busy BH loop. */
    s->refreshing = true;
    vim_refresh_work_inner(dcl);
    s->refreshing = false;
}

static void vim_refresh(DisplayChangeListener *dcl)
{
    VIMDisplay *s = container_of(dcl, VIMDisplay, dcl);
    if (!s->timing) {
        vim_refresh_work(dcl);
        return;
    }
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    if (!s->timing_start) {
        s->timing_start = now;
    }
    if (s->last_refresh) {
        uint64_t gap = now - s->last_refresh;
        s->refresh_gaps++;
        s->refresh_gap_ns += gap;
        s->refresh_gap_max = MAX(s->refresh_gap_max, gap);
    }
    s->last_refresh = now;
    s->refreshes++;
    vim_refresh_work(dcl);
    uint64_t end = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    s->refresh_work_ns += end - now;
    if (end - s->timing_start >= 2 * NANOSECONDS_PER_SECOND) {
        /* Aggregate at most once per two seconds. Includes actual callback
         * gaps, not an assumed refresh rate. ACK time also includes host
         * scheduling and QEMU servicing delays; it is not GPU time alone. */
        fprintf(stderr, "VIM display timing: seconds=%.3f refreshes=%" PRIu64
                " frames=%" PRIu64 " updates=%" PRIu64 " update-MiB=%.3f"
                " busy=%" PRIu64 " gap-ms=%.3f max-gap-ms=%.3f"
                " work-ms=%.3f copy-ms=%.3f acks=%" PRIu64
                " ack-ms=%.3f max-ack-ms=%.3f\n",
                (end - s->timing_start) / 1e9, s->refreshes, s->frames,
                s->updates, s->update_bytes / 1048576.0, s->busy_refreshes,
                s->refresh_gap_ns / (1e6 * MAX(s->refresh_gaps, 1)),
                s->refresh_gap_max / 1e6,
                s->refresh_work_ns / (1e6 * MAX(s->refreshes, 1)),
                s->copy_ns / (1e6 * MAX(s->frames, 1)), s->acks,
                s->ack_ns / (1e6 * MAX(s->acks, 1)), s->ack_max / 1e6);
        s->timing_start = end;
        s->refreshes = s->frames = s->updates = s->update_bytes = s->busy_refreshes = 0;
        s->refresh_gaps = s->refresh_gap_ns = s->refresh_gap_max = s->refresh_work_ns = s->copy_ns = 0;
        s->acks = s->ack_ns = s->ack_max = 0;
    }
}

static void vim_event_refresh(void *opaque)
{
    VIMDisplay *s = opaque;
    vim_refresh(&s->dcl);
}

static const DisplayChangeListenerOps vim_ops = {
    .dpy_name = "virtual-intel-mac-installer",
    .dpy_gfx_update = vim_update,
    .dpy_gfx_switch = vim_switch,
    .dpy_refresh = vim_refresh,
    .dpy_cursor_define = vim_cursor_define,
    .dpy_mouse_set = vim_mouse_set,
    .dpy_gfx_check_format = qemu_pixman_check_format,
};

void vim_display_init(void)
{
    const char *path = getenv("VIM_INSTALLER_DISPLAY_SOCKET");
    if (!path || !*path) {
        return;
    }
    VIMDisplay *s = g_new0(VIMDisplay, 1);
    s->timing = g_strcmp0(getenv("VIM_DISPLAY_TIMING"), "1") == 0;
    s->event_driven = g_strcmp0(getenv("VIM_DISPLAY_EVENT_DRIVEN"), "1") == 0;
    if (s->event_driven) {
        s->event_bh = qemu_bh_new(vim_event_refresh, s);
    }
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(path) >= sizeof(address.sun_path)) {
        vim_fail("socket path is too long");
    }
    pstrcpy(address.sun_path, sizeof(address.sun_path), path);
    s->socket = socket(AF_UNIX, SOCK_STREAM, 0);
    uid_t uid;
    gid_t gid;
    if (s->socket < 0 || connect(s->socket, (struct sockaddr *)&address,
                              sizeof(address)) ||
        getpeereid(s->socket, &uid, &gid) || uid != getuid()) {
        vim_fail("cannot connect to app");
    }
    int one = 1;
    setsockopt(s->socket, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    int frame_fd = qemu_shm_alloc(VIM_FRAME_SIZE, &error_fatal);
    s->frame = mmap(NULL, VIM_FRAME_SIZE, PROT_READ | PROT_WRITE,
                    MAP_SHARED, frame_fd, 0);
    if (s->frame == MAP_FAILED) {
        vim_fail("cannot allocate shared frame");
    }
    AppleGFXAppMessage hello = {.magic = APPLE_GFX_APP_MAGIC,
        .operation = APPLE_GFX_APP_HELLO};
    char control[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec iov = {&hello, sizeof(hello)};
    struct msghdr message = {.msg_iov = &iov, .msg_iovlen = 1,
        .msg_control = control, .msg_controllen = sizeof(control)};
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&message);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &frame_fd, sizeof(frame_fd));
    if (sendmsg(s->socket, &message, 0) != sizeof(hello)) {
        vim_fail("cannot share frame descriptor");
    }
    close(frame_fd);
    fcntl(s->socket, F_SETFL, O_NONBLOCK);
    s->dcl.ops = &vim_ops;
    /* The opt-in PassthroughOpenGL research display needs a 60-Hz-capable delivery
     * cadence. Keep normal installer behavior unchanged. ACK backpressure
     * still owns the shared frame; a faster poll must never reuse it early. */
    const char *refresh = getenv("VIM_DISPLAY_REFRESH_MS");
    unsigned interval;
    if (refresh && !qemu_strtoui(refresh, NULL, 10, &interval) &&
        interval >= 5 && interval <= 1000) {
        s->dcl.update_interval = interval;
    }
    s->dcl.con = qemu_console_lookup_default();
    if (!s->dcl.con) {
        vim_fail("no graphical console");
    }
    qemu_set_fd_handler(s->socket, vim_read, NULL, s);
    qemu_console_register_listener(s->dcl.con, &s->dcl, &vim_ops);
}
