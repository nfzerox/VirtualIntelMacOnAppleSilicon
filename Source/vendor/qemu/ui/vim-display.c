/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Unaccelerated installer display for Virtual Intel Mac. PVG does not use this
 * path. One anonymous shared frame is owned by the app until its GPU ACK. */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qemu/main-loop.h"
#include "qemu/cutils.h"
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
    uint64_t sequence;
    AppleGFXAppMessage input;
    size_t input_used;
} VIMDisplay;

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
        } else if (m->operation == APPLE_GFX_APP_SET_MODE) {
            /* The stock Bochs driver owns its mode. Host window scaling remains
             * available; arbitrary guest resizing requires the later PVG path. */
            if (m->sequence || m->stride || m->width > 8192 || m->height > 8192) {
                vim_fail("invalid mode request");
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
}

static void vim_switch(DisplayChangeListener *dcl, DisplaySurface *surface)
{
    VIMDisplay *s = container_of(dcl, VIMDisplay, dcl);
    s->dirty = true;
}

static void vim_refresh(DisplayChangeListener *dcl)
{
    VIMDisplay *s = container_of(dcl, VIMDisplay, dcl);
    qemu_console_hw_update(dcl->con);
    if (!s->dirty || s->busy) {
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
    pixman_image_t *dest = pixman_image_create_bits(PIXMAN_a8r8g8b8, w, h,
                                                   (uint32_t *)s->frame, stride);
    if (!dest) {
        vim_fail("cannot map software framebuffer");
    }
    pixman_image_composite32(PIXMAN_OP_SRC, surface->image, NULL, dest,
                            0, 0, 0, 0, 0, 0, w, h);
    pixman_image_unref(dest);
    AppleGFXAppMessage message = {.magic = APPLE_GFX_APP_MAGIC,
        .operation = APPLE_GFX_APP_FRAME, .sequence = ++s->sequence,
        .width = w, .height = h, .stride = stride};
    /* Tiny metadata-only write. Never block the emulator on presentation. */
    if (send(s->socket, &message, sizeof(message), 0) != sizeof(message)) {
        vim_fail("cannot submit frame metadata");
    }
    s->busy = true;
    s->dirty = false;
}

static const DisplayChangeListenerOps vim_ops = {
    .dpy_name = "virtual-intel-mac-installer",
    .dpy_gfx_update = vim_update,
    .dpy_gfx_switch = vim_switch,
    .dpy_refresh = vim_refresh,
    .dpy_gfx_check_format = qemu_pixman_check_format,
};

void vim_display_init(void)
{
    const char *path = getenv("VIM_INSTALLER_DISPLAY_SOCKET");
    if (!path || !*path) {
        return;
    }
    VIMDisplay *s = g_new0(VIMDisplay, 1);
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
    s->dcl.con = qemu_console_lookup_default();
    if (!s->dcl.con) {
        vim_fail("no graphical console");
    }
    qemu_set_fd_handler(s->socket, vim_read, NULL, s);
    qemu_console_register_listener(s->dcl.con, &s->dcl, &vim_ops);
}
