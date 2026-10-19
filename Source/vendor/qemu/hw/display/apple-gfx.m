/*
 * QEMU Apple ParavirtualizedGraphics.framework device
 *
 * Copyright © 2023 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ParavirtualizedGraphics.framework is a set of libraries that macOS provides
 * which implements 3d graphics passthrough to the host as well as a
 * proprietary guest communication channel to drive it. This device model
 * implements support to drive that library from within QEMU.
 */

#include "qemu/osdep.h"
#include "qemu/lockable.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/visitor.h"
#include "qapi/error.h"
#include "qemu/aio-wait.h"
#include "system/address-spaces.h"
#include "system/dma.h"
#include "migration/blocker.h"
#include "ui/console.h"
#include "apple-gfx.h"
#include "trace.h"

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>

#import <ParavirtualizedGraphics/ParavirtualizedGraphics.h>
#include "apple-gfx-paging-probe.h"
#include "apple-gfx-oop-ram.h"
#include "apple-gfx-oop-timing.h"
#include "system/system.h"
#include <Block.h>
#import <objc/message.h>
#include "apple-gfx-app.h"

typedef struct AppleGFXOOPClient {
    int fd;
    pid_t pid;
    void *frame;
    GArray *ranges;
    QemuMutex rpc_lock;
    dispatch_semaphore_t reply_ready;
    dispatch_queue_t control_queue;
    AppleGFXOOPCommand reply;
    uint64_t sequence;
    bool failed;
    uint32_t frame_width, frame_height;
    void (^irq)(uint32_t);
    Notifier exit_notifier;
    AppleGFXState *state;
    AppleGFXOOPTiming lock_time, read_time, write_time, other_time;
    bool timing_enabled;
} AppleGFXOOPClient;
static void apple_gfx_oop_frame_bh(void *opaque);
typedef struct {
    AppleGFXOOPClient *client;
    AppleGFXOOPCommand message;
    QEMUCursor *cursor;
} AppleGFXOOPCursorJob;

static void apple_gfx_oop_cursor_bh(void *opaque)
{
    AppleGFXOOPCursorJob *job = opaque;
    AppleGFXState *s = job->client->state;
    if (job->cursor) {
        if (s->cursor) { cursor_unref(s->cursor); }
        s->cursor = job->cursor;
        qemu_console_set_cursor(s->con, s->cursor);
    } else {
        qemu_console_set_mouse(s->con, (int32_t)job->message.offset,
            (int32_t)(job->message.offset >> 32), job->message.value != 0);
    }
    g_free(job);
}

static void apple_gfx_oop_exit(Notifier *n, void *unused)
{
    AppleGFXOOPClient *c = container_of(n, AppleGFXOOPClient, exit_notifier);
    shutdown(c->fd, SHUT_RDWR);
    kill(c->pid, SIGTERM);
    /* QEMU process exit owns the remaining client allocation and mappings. */
}

static void *apple_gfx_oop_receive(void *opaque)
{
    AppleGFXOOPClient *c = opaque;
    AppleGFXOOPCommand message;
    while (apple_gfx_oop_io(c->fd, &message, sizeof(message), false)) {
        if (message.magic != APPLE_GFX_OOP_MAGIC || message.reserved) { break; }
        if (message.operation == APPLE_GFX_OOP_IRQ) {
            if (message.value < 32 && c->irq) c->irq(message.value);
        } else if (message.operation == APPLE_GFX_OOP_FRAME) {
            if (message.offset < 320 || message.value < 200 ||
                message.offset > APPLE_GFX_OOP_MAX_DIMENSION ||
                message.value > APPLE_GFX_OOP_MAX_DIMENSION) { break; }
            /* Only one unacknowledged frame exists; its dimensions remain
             * stable until the presentation completion returns ownership. */
            c->frame_width = message.offset;
            c->frame_height = message.value;
            aio_bh_schedule_oneshot(qemu_get_aio_context(), apple_gfx_oop_frame_bh, c);
        } else if (message.operation == APPLE_GFX_OOP_CURSOR_STATE ||
                   message.operation == APPLE_GFX_OOP_CURSOR_GLYPH) {
            AppleGFXOOPCursorJob *job = g_new0(AppleGFXOOPCursorJob, 1);
            job->client = c;
            job->message = message;
            if (message.operation == APPLE_GFX_OOP_CURSOR_GLYPH) {
                uint32_t w = message.offset, h = message.offset >> 32;
                if (!w || !h || w > APPLE_GFX_OOP_CURSOR_MAX ||
                    h > APPLE_GFX_OOP_CURSOR_MAX ||
                    (uint32_t)message.value >= w || (message.value >> 32) >= h) {
                    g_free(job); break;
                }
                job->cursor = cursor_alloc(w, h);
                job->cursor->hot_x = (uint32_t)message.value;
                job->cursor->hot_y = message.value >> 32;
                if (!apple_gfx_oop_io(c->fd, job->cursor->data, w * h * 4, false)) {
                    cursor_unref(job->cursor); g_free(job); break;
                }
            }
            aio_bh_schedule_oneshot(qemu_get_aio_context(), apple_gfx_oop_cursor_bh, job);
        } else {
            c->reply = message;
            dispatch_semaphore_signal(c->reply_ready);
        }
    }
    qatomic_set(&c->failed, true);
    /* A helper may exit while the guest is waiting for a GPU interrupt and
     * therefore never issue another MMIO RPC. Report the lost transport here
     * rather than leaving a frozen frame with no diagnostic. */
    /* Do not reap here: the exit notifier still owns the child PID. */
    siginfo_t status = {0};
    int result = waitid(P_PID, c->pid, &status, WEXITED | WNOHANG | WNOWAIT);
    if (!result && status.si_pid == c->pid && status.si_code == CLD_EXITED) {
        error_report("PVG helper disconnected (exit status %d); GPU unavailable",
                     status.si_status);
    } else if (!result && status.si_pid == c->pid &&
               (status.si_code == CLD_KILLED || status.si_code == CLD_DUMPED)) {
        error_report("PVG helper disconnected (signal %d); GPU unavailable",
                     status.si_status);
    } else {
        error_report("PVG helper transport disconnected; GPU unavailable");
    }
    dispatch_semaphore_signal(c->reply_ready);
    return NULL;
}

static uint64_t apple_gfx_oop_rpc(AppleGFXOOPClient *c, uint32_t op,
                                 uint64_t offset, uint64_t value)
{
    bool timing = c->timing_enabled;
    uint64_t started = timing ? apple_gfx_oop_now() : 0;
    QEMU_LOCK_GUARD(&c->rpc_lock);
    if (timing) { apple_gfx_oop_timing(&c->lock_time, "rpc-lock", started); }
    started = timing ? apple_gfx_oop_now() : 0;
    if (qatomic_read(&c->failed)) { return UINT64_MAX; }
    AppleGFXOOPCommand request = {.magic=APPLE_GFX_OOP_MAGIC,
        .operation=op, .sequence=++c->sequence, .offset=offset, .value=value};
    bool ok = apple_gfx_oop_io(c->fd, &request, sizeof(request), true);
    if (ok) ok = !dispatch_semaphore_wait(c->reply_ready,
                            dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC));
    ok = ok && !qatomic_read(&c->failed) && c->reply.sequence == request.sequence &&
         c->reply.operation == op && !c->reply.status;
    if (!ok) {
        qatomic_set(&c->failed, true);
        shutdown(c->fd, SHUT_RDWR);
        error_report("PVG helper request failed; device transport stopped");
        return UINT64_MAX;
    }
    if (timing) {
        apple_gfx_oop_timing(op == APPLE_GFX_OOP_READ ? &c->read_time :
            op == APPLE_GFX_OOP_WRITE ? &c->write_time : &c->other_time,
            op == APPLE_GFX_OOP_READ ? "rpc-read" :
            op == APPLE_GFX_OOP_WRITE ? "rpc-write" : "rpc-other", started);
    }
    return c->reply.value;
}

static void apple_gfx_oop_frame_ack(void *opaque)
{
    AppleGFXOOPClient *c = opaque;
    dispatch_async(c->control_queue, ^{
        apple_gfx_oop_rpc(c, APPLE_GFX_OOP_FRAME_ACK, 0, 0);
    });
}

static void apple_gfx_oop_frame_bh(void *opaque)
{
    AppleGFXOOPClient *c = opaque;
    AppleGFXState *s = c->state;
    if (surface_width(s->surface) != c->frame_width ||
        surface_height(s->surface) != c->frame_height) {
        size_t stride = QEMU_ALIGN_UP((size_t)c->frame_width * 4, 256);
        MTLTextureDescriptor *td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
            width:c->frame_width height:c->frame_height mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> texture = [s->readback_buffer newTextureWithDescriptor:td
            offset:0 bytesPerRow:stride];
        if (!texture) { error_report("PVG resized scanout import failed");
            apple_gfx_oop_frame_ack(c); return; }
        [s->texture release]; s->texture = texture;
        s->surface = qemu_create_displaysurface_from(c->frame_width,
            c->frame_height, PIXMAN_x8r8g8b8, stride, c->frame);
        qemu_console_set_surface(s->con, s->surface);
    }
    /* Both Metal and software consumers reference the shared frame directly.
     * FRAME_ACK prevents reuse until the Metal presentation has completed. */
    if (qemu_console_has_metal(s->con)) {
        qemu_console_metal_scanout(s->con, s->texture, apple_gfx_oop_frame_ack, c);
    } else {
        qemu_console_update_full(s->con);
        apple_gfx_oop_frame_ack(c);
    }
}

/* macOS 27 SPI; runtime encoding and PGMemoryMap constructor verified.
 * Experimental fixed-RAM mapping: memory hotplug is not supported yet. */
typedef struct { uint64_t address, length; void *host; } AppleGFXGuestRange;
@interface NSObject (AppleGFXMemoryMap)
- (void)addRange:(AppleGFXGuestRange)range;
- (void)setMemoryMapDescriptor:(id)descriptor;
@end
typedef struct {
    MemoryListener listener;
    id descriptor;
} AppleGFXMapCollector;

static void apple_gfx_collect_ram(MemoryListener *listener,
                                  MemoryRegionSection *section)
{
    AppleGFXMapCollector *c = container_of(listener, AppleGFXMapCollector, listener);
    if (!memory_region_is_ram(section->mr) || section->readonly) {
        return;
    }
    AppleGFXGuestRange range = {
        section->offset_within_address_space, int128_get64(section->size),
        memory_region_get_ram_ptr(section->mr) + section->offset_within_region,
    };
    [c->descriptor addRange:range];
}

static bool apple_gfx_set_memory_map(PGDeviceDescriptor *desc)
{
    Class cls = NSClassFromString(@"PGMemoryMapDescriptor");
    if (!cls || ![desc respondsToSelector:@selector(setMemoryMapDescriptor:)]) {
        return false;
    }
    AppleGFXMapCollector c = {
        .listener = { .region_add = apple_gfx_collect_ram },
        .descriptor = [cls new],
    };
    memory_listener_register(&c.listener, &address_space_memory);
    memory_listener_unregister(&c.listener);
    [desc setMemoryMapDescriptor:c.descriptor];
    [c.descriptor release];
    return true;
}

static const AppleGFXDisplayMode apple_gfx_default_modes[] = {
    { 1920, 1080, 60 },
    { 1440, 1080, 60 },
    { 1280, 1024, 60 },
};

static Error *apple_gfx_mig_blocker;
static uint32_t next_pgdisplay_serial_num = 1;

static dispatch_queue_t get_background_queue(void)
{
    return dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0);
}

/* ------ PGTask and task operations: new/destroy/map/unmap ------ */

/*
 * This implements the type declared in <ParavirtualizedGraphics/PGDevice.h>
 * which is opaque from the framework's point of view. It is used in callbacks
 * in the form of its typedef PGTask_t, which also already exists in the
 * framework headers.
 *
 * A "task" in PVG terminology represents a host-virtual contiguous address
 * range which is reserved in a large chunk on task creation. The mapMemory
 * callback then requests ranges of guest system memory (identified by their
 * GPA) to be mapped into subranges of this reserved address space.
 * This type of operation isn't well-supported by QEMU's memory subsystem,
 * but it is fortunately trivial to achieve with Darwin's mach_vm_remap() call,
 * which allows us to refer to the same backing memory via multiple virtual
 * address ranges. The Mach VM APIs are therefore used throughout for managing
 * task memory.
 */
struct PGTask_s {
    QTAILQ_ENTRY(PGTask_s) node;
    AppleGFXState *s;
    mach_vm_address_t address;
    uint64_t len;
    /*
     * All unique MemoryRegions for which a mapping has been created in this
     * task, and on which we have thus called memory_region_ref(). There are
     * usually very few regions of system RAM in total, so we expect this array
     * to be very short. Therefore, no need for sorting or fancy search
     * algorithms, linear search will do.
     * Protected by AppleGFXState's task_mutex.
     */
    GPtrArray *mapped_regions;
};

static PGTask_t *apple_gfx_new_task(AppleGFXState *s, uint64_t len)
{
    mach_vm_address_t task_mem;
    PGTask_t *task;
    kern_return_t r;

    r = mach_vm_allocate(mach_task_self(), &task_mem, len, VM_FLAGS_ANYWHERE);
    if (r != KERN_SUCCESS) {
        return NULL;
    }

    task = g_new0(PGTask_t, 1);
    task->s = s;
    task->address = task_mem;
    task->len = len;
    task->mapped_regions = g_ptr_array_sized_new(2 /* Usually enough */);

    QEMU_LOCK_GUARD(&s->task_mutex);
    QTAILQ_INSERT_TAIL(&s->tasks, task, node);

    return task;
}

static void apple_gfx_destroy_task(AppleGFXState *s, PGTask_t *task)
{
    GPtrArray *regions = task->mapped_regions;
    MemoryRegion *region;
    size_t i;

    for (i = 0; i < regions->len; ++i) {
        region = g_ptr_array_index(regions, i);
        memory_region_unref(region);
    }
    g_ptr_array_unref(regions);

    mach_vm_deallocate(mach_task_self(), task->address, task->len);

    QEMU_LOCK_GUARD(&s->task_mutex);
    QTAILQ_REMOVE(&s->tasks, task, node);
    g_free(task);
}

void *apple_gfx_host_ptr_for_gpa_range(uint64_t guest_physical,
                                       uint64_t length, bool read_only,
                                       MemoryRegion **mapping_in_region)
{
    MemoryRegion *ram_region;
    char *host_ptr;
    hwaddr ram_region_offset = 0;
    hwaddr ram_region_length = length;

    ram_region = address_space_translate(&address_space_memory,
                                         guest_physical,
                                         &ram_region_offset,
                                         &ram_region_length, !read_only,
                                         MEMTXATTRS_UNSPECIFIED);

    if (!ram_region || ram_region_length < length ||
        !memory_access_is_direct(ram_region, !read_only,
				 MEMTXATTRS_UNSPECIFIED)) {
        return NULL;
    }

    host_ptr = memory_region_get_ram_ptr(ram_region);
    if (!host_ptr) {
        return NULL;
    }
    host_ptr += ram_region_offset;
    *mapping_in_region = ram_region;
    return host_ptr;
}

static bool apple_gfx_task_map_memory(AppleGFXState *s, PGTask_t *task,
                                      uint64_t virtual_offset,
                                      PGPhysicalMemoryRange_t *ranges,
                                      uint32_t range_count, bool read_only)
{
    kern_return_t r;
    void *source_ptr;
    mach_vm_address_t target;
    vm_prot_t cur_protection, max_protection;
    bool success = true;
    MemoryRegion *region;

    RCU_READ_LOCK_GUARD();
    QEMU_LOCK_GUARD(&s->task_mutex);

    trace_apple_gfx_map_memory(task, range_count, virtual_offset, read_only);
    for (int i = 0; i < range_count; i++) {
        PGPhysicalMemoryRange_t *range = &ranges[i];

        target = task->address + virtual_offset;
        virtual_offset += range->physicalLength;

        trace_apple_gfx_map_memory_range(i, range->physicalAddress,
                                         range->physicalLength);

        region = NULL;
        source_ptr = apple_gfx_host_ptr_for_gpa_range(range->physicalAddress,
                                                      range->physicalLength,
                                                      read_only, &region);
        if (!source_ptr) {
            success = false;
            continue;
        }

        if (!g_ptr_array_find(task->mapped_regions, region, NULL)) {
            g_ptr_array_add(task->mapped_regions, region);
            memory_region_ref(region);
        }

        cur_protection = 0;
        max_protection = 0;
        /* Map guest RAM at range->physicalAddress into PG task memory range */
        r = mach_vm_remap(mach_task_self(),
                          &target, range->physicalLength, vm_page_size - 1,
                          VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
                          mach_task_self(), (mach_vm_address_t)source_ptr,
                          false /* shared mapping, no copy */,
                          &cur_protection, &max_protection,
                          VM_INHERIT_COPY);
        trace_apple_gfx_remap(r, source_ptr, target);
        g_assert(r == KERN_SUCCESS);
    }

    return success;
}

static void apple_gfx_task_unmap_memory(AppleGFXState *s, PGTask_t *task,
                                        uint64_t virtual_offset, uint64_t length)
{
    kern_return_t r;
    mach_vm_address_t range_address;

    trace_apple_gfx_unmap_memory(task, virtual_offset, length);

    /*
     * Replace task memory range with fresh 0 pages, undoing the mapping
     * from guest RAM.
     */
    range_address = task->address + virtual_offset;
    r = mach_vm_allocate(mach_task_self(), &range_address, length,
                         VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE);
    g_assert(r == KERN_SUCCESS);
}

/* ------ Rendering and frame management ------ */

static void apple_gfx_render_frame_completed_bh(void *opaque);

typedef struct AppleGFXReadback {
    AppleGFXState *s;
    id<MTLBuffer> buffer;
    size_t stride;
    uint32_t width;
    uint32_t height;
    bool succeeded;
    bool readback;
    bool direct;
    bool presented;
    uint32_t power_epoch;
    id<MTLTexture> texture;
} AppleGFXReadback;

static void apple_gfx_present_done(void *opaque)
{
    aio_bh_schedule_oneshot(qemu_get_aio_context(),
                           apple_gfx_render_frame_completed_bh, opaque);
}

static void apple_gfx_render_new_frame(AppleGFXState *s)
{
    uint32_t width = surface_width(s->surface);
    uint32_t height = surface_height(s->surface);
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    id<MTLCommandBuffer> command_buffer = [s->mtl_queue commandBuffer];
    id<MTLTexture> texture = s->texture;
    AppleGFXReadback *frame = g_new0(AppleGFXReadback, 1);

    frame->s = s;
    frame->buffer = [s->readback_buffer retain];
    frame->stride = s->readback_stride;
    frame->width = width;
    frame->height = height;
    frame->power_epoch = s->app ? qatomic_read(&s->app->power_epoch) : 0;
    frame->direct = qemu_console_has_metal(s->con);
    frame->readback = !frame->direct || s->gfx_update_requested;
    frame->texture = [texture retain];

    assert(bql_locked());
    [texture retain];
    [command_buffer retain];

    s->rendering_frame_width = width;
    s->rendering_frame_height = height;

    dispatch_async(get_background_queue(), ^{
        /*
         * This is not safe to call from the BQL/BH due to PVG-internal locks
         * causing deadlocks.
         */
        bool r = [s->pgdisp encodeCurrentFrameToCommandBuffer:command_buffer
                                                 texture:texture
                                                  region:region];
        if (!r) {
            [texture release];
            [command_buffer release];
            [frame->buffer release];
            [frame->texture release];
            g_free(frame);
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: encodeCurrentFrameToCommandBuffer:texture:region: "
                          "failed\n", __func__);
            bql_lock();
            --s->pending_frames;
            if (s->pending_frames > 0) {
                apple_gfx_render_new_frame(s);
            }
            bql_unlock();
            return;
        }

        /* Convert the tiled texture on the GPU, not in getBytes under BQL. */
        if (frame->readback) {
            id<MTLBlitCommandEncoder> blit = [command_buffer blitCommandEncoder];
            [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0
                     sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:MTLSizeMake(width, height, 1)
                         toBuffer:frame->buffer destinationOffset:0
           destinationBytesPerRow:frame->stride
         destinationBytesPerImage:frame->stride * height];
            if (frame->buffer.storageMode == MTLStorageModeManaged) {
                [blit synchronizeResource:frame->buffer];
            }
            [blit endEncoding];
        }
        [texture release];
        [command_buffer addCompletedHandler:
            ^(id<MTLCommandBuffer> cb)
            {
                frame->succeeded = cb.status == MTLCommandBufferStatusCompleted;
                aio_bh_schedule_oneshot(qemu_get_aio_context(),
                                        apple_gfx_render_frame_completed_bh, frame);
            }];
        [command_buffer commit];
        [command_buffer release];
    });
}

static void apple_gfx_render_frame_completed_bh(void *opaque)
{
    AppleGFXReadback *frame = opaque;
    AppleGFXState *s = frame->s;

    @autoreleasepool {
        if (s->app && frame->succeeded && !frame->presented) {
            frame->presented = true;
            app_present(s->app, frame->width, frame->height, frame->stride,
                        frame->power_epoch, apple_gfx_present_done, frame);
            return;
        }
        if (frame->direct && frame->succeeded && !frame->presented &&
            frame->buffer == s->readback_buffer) {
            frame->presented = true;
            qemu_console_metal_scanout(s->con, frame->texture,
                                      apple_gfx_present_done, frame);
            return;
        }
        --s->pending_frames;
        assert(s->pending_frames >= 0);

        /* Only update display if mode hasn't changed since we started rendering. */
        if (!s->app && frame->readback && frame->succeeded &&
            frame->buffer == s->readback_buffer &&
            frame->width == surface_width(s->surface) &&
            frame->height == surface_height(s->surface)) {
            const uint8_t *src = frame->buffer.contents;
            uint8_t *dst = surface_data(s->surface);
            for (uint32_t y = 0; y < frame->height; y++) {
                memcpy(dst + y * surface_stride(s->surface),
                       src + y * frame->stride, frame->width * 4);
            }
            if (s->gfx_update_requested) {
                s->gfx_update_requested = false;
                qemu_console_update_full(s->con);
                qemu_console_hw_update_done(s->con);
                s->new_frame_ready = false;
            } else {
                s->new_frame_ready = true;
            }
        }
        [frame->buffer release];
        [frame->texture release];
        g_free(frame);
        if (s->gfx_update_requested && s->pending_frames == 0) {
            s->pending_frames = 1;
        }
        if (s->pending_frames > 0) {
            apple_gfx_render_new_frame(s);
        }
    }
}

static bool apple_gfx_fb_update_display(void *opaque)
{
    AppleGFXState *s = opaque;
    bool done = true;

    assert(bql_locked());

    if (s->oop) { qemu_console_update_full(s->con); return true; }

    if (qemu_console_has_metal(s->con) && s->surface && s->texture) {
        s->gfx_update_requested = true;
        if (s->pending_frames == 0) {
            s->pending_frames = 1;
            apple_gfx_render_new_frame(s);
        }
        return false;
    }
    if (s->new_frame_ready) {
        qemu_console_update_full(s->con);
        s->new_frame_ready = false;
    } else if (s->pending_frames > 0) {
        s->gfx_update_requested = true;
        done = false;
    }

    return done;
}

static void apple_gfx_ui_info(void *opaque, uint32_t head, QemuUIInfo *info)
{
    AppleGFXState *s = opaque;
    uint32_t width = info->width;
    uint32_t height = info->height;

    if (s->oop) {
        if (width < 320 || height < 200 ||
            width > APPLE_GFX_OOP_MAX_DIMENSION || height > APPLE_GFX_OOP_MAX_DIMENSION ||
            (s->surface && width == surface_width(s->surface) &&
             height == surface_height(s->surface))) { return; }
        AppleGFXOOPClient *c = s->oop;
        dispatch_async(c->control_queue, ^{
            apple_gfx_oop_rpc(c, APPLE_GFX_OOP_SET_MODE, width, height);
        });
        return;
    }

    /* Ignore minimized/uninitialized views and bound GPU resource use. */
    if (!s->pgdisp || width < 320 || height < 200 ||
        width > 8192 || height > 8192 ||
        (s->surface && width == surface_width(s->surface) &&
         height == surface_height(s->surface))) {
        return;
    }
    /* Never hold BQL across framework calls that can invoke display callbacks. */
    dispatch_async(dispatch_get_main_queue(), ^{
        PGDisplayCoord_t size = { width, height };
        PGDisplayMode *mode = [[PGDisplayMode alloc]
            initWithSizeInPixels:size refreshRateInHz:60];
        s->pgdisp.modeList = @[mode];
        [mode release];
    });
}

static const GraphicHwOps apple_gfx_fb_ops = {
    .gfx_update = apple_gfx_fb_update_display,
    .ui_info = apple_gfx_ui_info,
};

/* ------ Mouse cursor and display mode setting ------ */

static void set_mode(AppleGFXState *s, uint32_t width, uint32_t height)
{
    MTLTextureDescriptor *textureDescriptor;

    if (s->app && (width > APPLE_GFX_OOP_MAX_DIMENSION ||
                   height > APPLE_GFX_OOP_MAX_DIMENSION)) {
        app_fail();
    }

    if (s->surface &&
        width == surface_width(s->surface) &&
        height == surface_height(s->surface)) {
        return;
    }

    [s->texture release];
    [s->readback_buffer release];

    s->surface = qemu_create_displaysurface(width, height);

    @autoreleasepool {
        textureDescriptor =
            [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                             width:width
                                            height:height
                                         mipmapped:NO];
        textureDescriptor.usage = s->pgdisp.minimumTextureUsage;
        s->texture = [s->mtl newTextureWithDescriptor:textureDescriptor];
        s->readback_stride = QEMU_ALIGN_UP((size_t)width * 4, 256);
        s->readback_buffer = s->app ? [s->app->buffer retain] : [s->mtl
            newBufferWithLength:s->readback_stride * height
                        options:MTLResourceStorageModeShared];
        g_assert(s->readback_buffer != nil);
        s->using_managed_texture_storage =
            (s->texture.storageMode == MTLStorageModeManaged);
    }

    qemu_console_set_surface(s->con, s->surface);
}

static void update_cursor(AppleGFXState *s)
{
    assert(bql_locked());
    if (s->app) {
        AppleGFXAppMessage m = { .magic = APPLE_GFX_APP_MAGIC,
            .operation = APPLE_GFX_APP_CURSOR_STATE,
            .width = qatomic_read(&s->cursor_show) };
        app_send(s->app, m, nil);
    }
    qemu_console_set_mouse(s->con, s->pgdisp.cursorPosition.x,
                           s->pgdisp.cursorPosition.y, qatomic_read(&s->cursor_show));
}

static void update_cursor_bh(void *opaque)
{
    AppleGFXState *s = opaque;
    update_cursor(s);
}

typedef struct AppleGFXSetCursorGlyphJob {
    AppleGFXState *s;
    NSBitmapImageRep *glyph;
    PGDisplayCoord_t hotspot;
} AppleGFXSetCursorGlyphJob;

static void set_cursor_glyph(void *opaque)
{
    AppleGFXSetCursorGlyphJob *job = opaque;
    AppleGFXState *s = job->s;
    NSBitmapImageRep *glyph = job->glyph;
    uint32_t bpp = glyph.bitsPerPixel;
    size_t width = glyph.pixelsWide;
    size_t height = glyph.pixelsHigh;
    size_t padding_bytes_per_row = glyph.bytesPerRow - width * 4;
    const uint8_t* px_data = glyph.bitmapData;

    trace_apple_gfx_cursor_set(bpp, width, height);

    if (s->cursor) {
        cursor_unref(s->cursor);
        s->cursor = NULL;
    }

    if (bpp == 32) { /* Shouldn't be anything else, but just to be safe... */
        s->cursor = cursor_alloc(width, height);
        s->cursor->hot_x = job->hotspot.x;
        s->cursor->hot_y = job->hotspot.y;

        uint32_t *dest_px = s->cursor->data;

        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                /*
                 * NSBitmapImageRep's red & blue channels are swapped
                 * compared to QEMUCursor's.
                 */
                *dest_px =
                    (px_data[0] << 16u) |
                    (px_data[1] <<  8u) |
                    (px_data[2] <<  0u) |
                    (px_data[3] << 24u);
                ++dest_px;
                px_data += 4;
            }
            px_data += padding_bytes_per_row;
        }
        qemu_console_set_cursor(s->con, s->cursor);
        if (s->app && width <= 256 && height <= 256) {
            AppleGFXAppMessage m = { .magic = APPLE_GFX_APP_MAGIC,
                .operation = APPLE_GFX_APP_CURSOR_GLYPH,
                .width = width, .height = height,
                .stride = s->cursor->hot_x | (s->cursor->hot_y << 16) };
            app_send(s->app, m, [NSData dataWithBytes:s->cursor->data length:width * height * 4]);
        }
        update_cursor(s);
    }
    [glyph release];

    g_free(job);
}

/* ------ DMA (device reading system memory) ------ */

typedef struct AppleGFXReadMemoryJob {
    QemuEvent event;
    hwaddr physical_address;
    uint64_t length;
    void *dst;
    bool success;
} AppleGFXReadMemoryJob;

static void apple_gfx_do_read_memory(void *opaque)
{
    AppleGFXReadMemoryJob *job = opaque;
    MemTxResult r;

    r = dma_memory_read(&address_space_memory, job->physical_address,
                        job->dst, job->length, MEMTXATTRS_UNSPECIFIED);
    job->success = (r == MEMTX_OK);

    qemu_event_set(&job->event);
}

static bool apple_gfx_read_memory(AppleGFXState *s, hwaddr physical_address,
                                  uint64_t length, void *dst)
{
    AppleGFXReadMemoryJob job = {
        .physical_address = physical_address, .length = length, .dst = dst
    };

    trace_apple_gfx_read_memory(physical_address, length, dst);

    /* Performing DMA requires BQL, so do it in a BH. */
    qemu_event_init(&job.event, 0);
    aio_bh_schedule_oneshot(qemu_get_aio_context(),
                            apple_gfx_do_read_memory, &job);
    qemu_event_wait(&job.event);
    qemu_event_destroy(&job.event);
    return job.success;
}

/* ------ Memory-mapped device I/O operations ------ */

typedef struct AppleGFXIOJob {
    AppleGFXState *state;
    uint64_t offset;
    uint64_t value;
    bool completed;
} AppleGFXIOJob;

static void apple_gfx_do_read(void *opaque)
{
    AppleGFXIOJob *job = opaque;
    job->value = job->state->oop ?
        apple_gfx_oop_rpc(job->state->oop, APPLE_GFX_OOP_READ, job->offset, 0) :
        [job->state->pgdev mmioReadAtOffset:job->offset];
    qatomic_set(&job->completed, true);
    aio_wait_kick();
}

static uint64_t apple_gfx_read(void *opaque, hwaddr offset, unsigned size)
{
    AppleGFXIOJob job = {
        .state = opaque,
        .offset = offset,
        .completed = false,
    };
    AppleGFXState *s = opaque;
    dispatch_queue_t queue = s->oop ? s->oop->control_queue : get_background_queue();

    dispatch_async_f(queue, &job, apple_gfx_do_read);
    AIO_WAIT_WHILE(NULL, !qatomic_read(&job.completed));

    trace_apple_gfx_read(offset, job.value);
    return job.value;
}

static void apple_gfx_do_write(void *opaque)
{
    AppleGFXIOJob *job = opaque;
    if (job->state->oop) {
        apple_gfx_oop_rpc(job->state->oop, APPLE_GFX_OOP_WRITE, job->offset, job->value);
    } else {
        [job->state->pgdev mmioWriteAtOffset:job->offset value:job->value];
    }
    qatomic_set(&job->completed, true);
    aio_wait_kick();
}

static void apple_gfx_write(void *opaque, hwaddr offset, uint64_t val,
                            unsigned size)
{
    /*
     * The methods mmioReadAtOffset: and especially mmioWriteAtOffset: can
     * trigger synchronous operations on other dispatch queues, which in turn
     * may call back out on one or more of the callback blocks. For this reason,
     * and as we are holding the BQL, we invoke the I/O methods on a pool
     * thread and handle AIO tasks while we wait. Any work in the callbacks
     * requiring the BQL will in turn schedule BHs which this thread will
     * process while waiting.
     */
    AppleGFXIOJob job = {
        .state = opaque,
        .offset = offset,
        .value = val,
        .completed = false,
    };
    AppleGFXState *s = opaque;
    dispatch_queue_t queue = s->oop ? s->oop->control_queue : get_background_queue();

    dispatch_async_f(queue, &job, apple_gfx_do_write);
    AIO_WAIT_WHILE(NULL, !qatomic_read(&job.completed));

    trace_apple_gfx_write(offset, val);
}

static const MemoryRegionOps apple_gfx_ops = {
    .read = apple_gfx_read,
    .write = apple_gfx_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

void apple_gfx_reset(AppleGFXState *s)
{
    if (s->oop) {
        /* Current reset RPC has no callbacks requiring BQL in the helper. */
        apple_gfx_oop_rpc(s->oop, APPLE_GFX_OOP_RESET, 0, 0);
    } else if ([s->pgdev respondsToSelector:@selector(reset)]) {
        ((void (*)(id, SEL))objc_msgSend)(s->pgdev, @selector(reset));
    }
}

static size_t apple_gfx_get_default_mmio_range_size(void)
{
    /* Qualified Intel 26 donor ABI; bootstrap rejects a differing size. */
    if (getenv("PVG_OOP_HELPER")) { return 16384; }
    size_t mmio_range_size;
    @autoreleasepool {
        PGDeviceDescriptor *desc = [PGDeviceDescriptor new];
        mmio_range_size = desc.mmioLength;
        [desc release];
    }
    return mmio_range_size;
}

/* ------ Initialisation and startup ------ */

void apple_gfx_common_init(Object *obj, AppleGFXState *s, const char* obj_name)
{
    size_t mmio_range_size = apple_gfx_get_default_mmio_range_size();

    trace_apple_gfx_common_init(obj_name, mmio_range_size);
    memory_region_init_io(&s->iomem_gfx, obj, &apple_gfx_ops, s, obj_name,
                          mmio_range_size);

    /* TODO: PVG framework supports serialising device state: integrate it! */
}

static void apple_gfx_register_task_mapping_handlers(AppleGFXState *s,
                                                     PGDeviceDescriptor *desc)
{
    desc.createTask = ^(uint64_t vmSize, void * _Nullable * _Nonnull baseAddress) {
        PGTask_t *task = apple_gfx_new_task(s, vmSize);
        *baseAddress = (void *)task->address;
        trace_apple_gfx_create_task(vmSize, *baseAddress);
        return task;
    };

    desc.destroyTask = ^(PGTask_t * _Nonnull task) {
        trace_apple_gfx_destroy_task(task, task->mapped_regions->len);

        apple_gfx_destroy_task(s, task);
    };

    desc.mapMemory = ^bool(PGTask_t * _Nonnull task, uint32_t range_count,
                           uint64_t virtual_offset, bool read_only,
                           PGPhysicalMemoryRange_t * _Nonnull ranges) {
        return apple_gfx_task_map_memory(s, task, virtual_offset,
                                         ranges, range_count, read_only);
    };

    desc.unmapMemory = ^bool(PGTask_t * _Nonnull task, uint64_t virtual_offset,
                             uint64_t length) {
        apple_gfx_task_unmap_memory(s, task, virtual_offset, length);
        return true;
    };

    desc.readMemory = ^bool(uint64_t physical_address, uint64_t length,
                            void * _Nonnull dst) {
        return apple_gfx_read_memory(s, physical_address, length, dst);
    };
}

static void new_frame_handler_bh(void *opaque)
{
    AppleGFXState *s = opaque;

    /* Drop frames if guest gets too far ahead. */
    if (s->pending_frames >= 2) {
        return;
    }
    ++s->pending_frames;
    if (s->pending_frames > 1) {
        return;
    }

    @autoreleasepool {
        apple_gfx_render_new_frame(s);
    }
}

static PGDisplayDescriptor *apple_gfx_prepare_display_descriptor(AppleGFXState *s)
{
    PGDisplayDescriptor *disp_desc = [PGDisplayDescriptor new];

    disp_desc.name = @"QEMU display";
    /* Convey the host's pixel density instead of always claiming a 20-inch
     * low-DPI panel. Cocoa already reports backing-pixel resize dimensions. */
    CGDirectDisplayID hostDisplay = CGMainDisplayID();
    CGSize hostMM = CGDisplayScreenSize(hostDisplay);
    CGDisplayModeRef hostMode = CGDisplayCopyDisplayMode(hostDisplay);
    size_t hostPixelsWide = hostMode ? CGDisplayModeGetPixelWidth(hostMode) : 0;
    size_t hostPixelsHigh = hostMode ? CGDisplayModeGetPixelHeight(hostMode) : 0;
    size_t hostPointsWide = hostMode ? CGDisplayModeGetWidth(hostMode) : 0;
    const char *app_scale = getenv("PVG_APP_DISPLAY_SCALE");
    bool retina = app_scale ? !strcmp(app_scale, "2") :
                  hostPointsWide && hostPixelsWide > hostPointsWide;
    if (s->app &&
        [disp_desc respondsToSelector:sel_registerName("setScaleFactor:")]) {
        ((void (*)(id, SEL, uint8_t))objc_msgSend)(disp_desc,
            sel_registerName("setScaleFactor:"), retina ? 2 : 1);
    }
    if (hostMM.width > 0 && hostMM.height > 0 && hostPixelsWide && hostPixelsHigh) {
        const AppleGFXDisplayMode *preferred = s->num_display_modes ?
            &s->display_modes[0] : &apple_gfx_default_modes[0];
        disp_desc.sizeInMillimeters = NSMakeSize(
            preferred->width_px * hostMM.width / hostPixelsWide,
            preferred->height_px * hostMM.height / hostPixelsHigh);
    } else {
        disp_desc.sizeInMillimeters = NSMakeSize(400., 300.);
    }
    if (hostMode) CGDisplayModeRelease(hostMode);
    disp_desc.queue = dispatch_get_main_queue();
    disp_desc.newFrameEventHandler = ^(void) {
        app_display_power(s->app, false);
        trace_apple_gfx_new_frame();
        aio_bh_schedule_oneshot(qemu_get_aio_context(), new_frame_handler_bh, s);
    };
    // Runtime metadata and the donor's _PGDisplayNub::_sleepState: show a
    // void(void) callback, not a Boolean argument. Wake requests a new frame.
    SEL sleep_setter = sel_registerName("setSleepHandler:");
    if (s->app && [disp_desc respondsToSelector:sleep_setter]) {
        ((void (*)(id, SEL, id))objc_msgSend)(disp_desc, sleep_setter,
            ^{ app_display_power(s->app, true); });
    }
    disp_desc.modeChangeHandler = ^(PGDisplayCoord_t sizeInPixels,
                                    OSType pixelFormat) {
        trace_apple_gfx_mode_change(sizeInPixels.x, sizeInPixels.y);

        BQL_LOCK_GUARD();
        set_mode(s, sizeInPixels.x, sizeInPixels.y);
    };
    disp_desc.cursorGlyphHandler = ^(NSBitmapImageRep *glyph,
                                     PGDisplayCoord_t hotspot) {
        AppleGFXSetCursorGlyphJob *job = g_malloc0(sizeof(*job));
        job->s = s;
        job->glyph = glyph;
        job->hotspot = hotspot;
        [glyph retain];
        aio_bh_schedule_oneshot(qemu_get_aio_context(),
                                set_cursor_glyph, job);
    };
    disp_desc.cursorShowHandler = ^(BOOL show) {
        trace_apple_gfx_cursor_show(show);
        qatomic_set(&s->cursor_show, show);
        aio_bh_schedule_oneshot(qemu_get_aio_context(),
                                update_cursor_bh, s);
    };
    if (@available(macOS 11.1, *)) {
      disp_desc.cursorMoveHandler = ^(void) {
        trace_apple_gfx_cursor_move();
        aio_bh_schedule_oneshot(qemu_get_aio_context(),
                                update_cursor_bh, s);
      };
    }

    return disp_desc;
}

static NSArray<PGDisplayMode *> *apple_gfx_create_display_mode_array(
    const AppleGFXDisplayMode display_modes[], uint32_t display_mode_count)
{
    PGDisplayMode *mode_obj;
    NSMutableArray<PGDisplayMode *> *mode_array =
        [[NSMutableArray alloc] initWithCapacity:display_mode_count];

    for (unsigned i = 0; i < display_mode_count; i++) {
        const AppleGFXDisplayMode *mode = &display_modes[i];
        trace_apple_gfx_display_mode(i, mode->width_px, mode->height_px);
        PGDisplayCoord_t mode_size = { mode->width_px, mode->height_px };

        mode_obj =
            [[PGDisplayMode alloc] initWithSizeInPixels:mode_size
                                        refreshRateInHz:mode->refresh_rate_hz];
        [mode_array addObject:mode_obj];
        [mode_obj release];
    }

    return mode_array;
}

static id<MTLDevice> copy_suitable_metal_device(void)
{
    id<MTLDevice> dev = nil;
    NSArray<id<MTLDevice>> *devs = MTLCopyAllDevices();

    /* Prefer a unified memory GPU. Failing that, pick a non-removable GPU. */
    for (size_t i = 0; i < devs.count; ++i) {
        if (devs[i].hasUnifiedMemory) {
            dev = devs[i];
            break;
        }
        if (!devs[i].removable) {
            dev = devs[i];
        }
    }

    if (dev != nil) {
        [dev retain];
    } else {
        dev = MTLCreateSystemDefaultDevice();
    }
    [devs release];

    return dev;
}

bool apple_gfx_common_realize(AppleGFXState *s, DeviceState *dev,
                              PGDeviceDescriptor *desc, Error **errp)
{
    PGDisplayDescriptor *disp_desc;
    const AppleGFXDisplayMode *display_modes = apple_gfx_default_modes;
    uint32_t num_display_modes = ARRAY_SIZE(apple_gfx_default_modes);
    NSArray<PGDisplayMode *> *mode_array;

    if (getenv("PVG_OOP_CHECK_RAM")) {
        GArray *ranges = apple_gfx_oop_ram_ranges(errp);
        if (!ranges) {
            return false;
        }
        for (unsigned i = 0; i < ranges->len; i++) {
            AppleGFXOOPRange *r = &g_array_index(ranges, AppleGFXOOPRange, i);
            fprintf(stderr, "PVG helper RAM gpa=0x%llx length=0x%llx file=0x%llx\n",
                    (unsigned long long)r->guest, (unsigned long long)r->length,
                    (unsigned long long)r->file_offset);
        }
        const char *helper = getenv("PVG_OOP_CHECK_HELPER");
        bool helper_ok = !helper || apple_gfx_oop_bootstrap(ranges, helper,
                                              getenv("PVG_OOP_DONOR"), NULL, NULL, NULL, errp);
        apple_gfx_oop_free_ranges(ranges);
        if (!helper_ok) { return false; }
    }
    const char *oop_helper = getenv("PVG_OOP_HELPER");
    if (oop_helper) {
        if (apple_gfx_mig_blocker == NULL) {
            error_setg(&apple_gfx_mig_blocker, "Migration blocked by experimental PVG helper");
            if (migrate_add_blocker(&apple_gfx_mig_blocker, errp) < 0) { return false; }
        }
        GArray *ranges = apple_gfx_oop_ram_ranges(errp);
        if (!ranges) { return false; }
        AppleGFXOOPClient *c = g_new0(AppleGFXOOPClient, 1);
        c->timing_enabled = getenv("PVG_OOP_TIMING") != NULL;
        if (!apple_gfx_oop_bootstrap(ranges, oop_helper, getenv("PVG_OOP_DONOR"),
                                      &c->fd, &c->pid, &c->frame, errp)) {
            apple_gfx_oop_free_ranges(ranges); g_free(c); return false;
        }
        c->ranges = ranges;
        c->state = s;
        c->irq = Block_copy(desc.raiseInterrupt);
        qemu_mutex_init(&c->rpc_lock);
        c->reply_ready = dispatch_semaphore_create(0);
        c->control_queue = dispatch_queue_create("pvg.oop.control", DISPATCH_QUEUE_SERIAL);
        c->exit_notifier.notify = apple_gfx_oop_exit;
        qemu_add_exit_notifier(&c->exit_notifier);
        s->oop = c;
        s->mtl = copy_suitable_metal_device();
        s->readback_buffer = [s->mtl newBufferWithBytesNoCopy:c->frame
            length:APPLE_GFX_OOP_FRAME_SIZE options:MTLResourceStorageModeShared deallocator:nil];
        MTLTextureDescriptor *td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
            width:APPLE_GFX_OOP_WIDTH height:APPLE_GFX_OOP_HEIGHT mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        s->texture = [s->readback_buffer newTextureWithDescriptor:td offset:0
                                         bytesPerRow:APPLE_GFX_OOP_STRIDE];
        if (!s->texture) {
            error_setg(errp, "PVG shared scanout texture import failed");
            apple_gfx_oop_exit(&c->exit_notifier, NULL); return false;
        }
        s->con = qemu_graphic_console_create(dev, 0, &apple_gfx_fb_ops, s);
        s->surface = qemu_create_displaysurface_from(APPLE_GFX_OOP_WIDTH,
            APPLE_GFX_OOP_HEIGHT, PIXMAN_x8r8g8b8, APPLE_GFX_OOP_STRIDE, c->frame);
        qemu_console_set_surface(s->con, s->surface);
        /* This blocks for the lifetime of the VM. Keeping it on a global GCD
         * pool can starve the MMIO jobs that supply the very replies it reads,
         * freezing every vCPU behind BQL. A dedicated blocking reader also
         * leaves the serial control queue free to issue RPCs. */
        QemuThread receiver;
        qemu_thread_create(&receiver, "pvg-receive", apple_gfx_oop_receive,
                           c, QEMU_THREAD_DETACHED);
        /* Subsequent frame events carry the selected guest dimensions. */
        return true;
    }

    if (apple_gfx_mig_blocker == NULL) {
        error_setg(&apple_gfx_mig_blocker,
                  "Migration state blocked by apple-gfx display device");
        if (migrate_add_blocker(&apple_gfx_mig_blocker, errp) < 0) {
            return false;
        }
    }

    qemu_mutex_init(&s->task_mutex);
    QTAILQ_INIT(&s->tasks);
    s->mtl = copy_suitable_metal_device();
    s->mtl_queue = [s->mtl newCommandQueue];
    s->app = app_connect(s);

    desc.device = s->mtl;

    if (getenv("PVG_NATIVE_INTEL_GEOMETRY")) {
        /* Local ARM26 donor experiment. QEMU must itself have 4K VM pages
         * before legacy guest aliases can be represented without rounding. */
        if (getpagesize() != 4096) {
            error_setg(errp, "Native Intel-geometry PVG requires a 4K-page process");
            return false;
        }
        apple_gfx_register_task_mapping_handlers(s, desc);
    } else if (@available(macOS 27.0, *)) {
        if (!apple_gfx_set_memory_map(desc)) {
            error_setg(errp, "PVG guest memory-map interface unavailable");
            return false;
        }
    } else {
        apple_gfx_register_task_mapping_handlers(s, desc);
    }

    s->cursor_show = true;

    /* The deprecated symbol is absent from the macOS 27 link stub. */
    id<PGDevice> (*create_device)(PGDeviceDescriptor *) =
        dlsym(RTLD_DEFAULT, "PGCreateDeviceWithDescriptor");
    if (!create_device) {
        create_device = dlsym(RTLD_DEFAULT, "PGNewDeviceWithDescriptor");
    }
    s->pgdev = create_device ? create_device(desc) : nil;
    if (!s->pgdev) {
        error_setg(errp, "Apple PVG refused device creation");
        return false;
    }
#if defined(__x86_64__)
    apple_gfx_install_paging_probe(s->mtl);
#endif

    disp_desc = apple_gfx_prepare_display_descriptor(s);
    /*
     * Although the framework does, this integration currently does not support
     * multiple virtual displays connected to a single PV graphics device.
     * It is however possible to create
     * more than one instance of the device, each with one display. The macOS
     * guest will ignore these displays if they share the same serial number,
     * so ensure each instance gets a unique one.
     */
    s->pgdisp = [s->pgdev newDisplayWithDescriptor:disp_desc
                                              port:0
                                         serialNum:next_pgdisplay_serial_num++];
    [disp_desc release];

    if (s->display_modes != NULL && s->num_display_modes > 0) {
        trace_apple_gfx_common_realize_modes_property(s->num_display_modes);
        display_modes = s->display_modes;
        num_display_modes = s->num_display_modes;
    }
    s->pgdisp.modeList = mode_array =
        apple_gfx_create_display_mode_array(display_modes, num_display_modes);
    [mode_array release];

    s->con = qemu_graphic_console_create(dev, 0, &apple_gfx_fb_ops, s);
    return true;
}

/* ------ Display mode list device property ------ */

static void apple_gfx_get_display_mode(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    Property *prop = opaque;
    AppleGFXDisplayMode *mode = object_field_prop_ptr(obj, prop);
    /* 3 uint16s (max 5 digits) + 2 separator characters + nul. */
    char buffer[5 * 3 + 2 + 1];
    char *pos = buffer;

    int rc = snprintf(buffer, sizeof(buffer),
                      "%"PRIu16"x%"PRIu16"@%"PRIu16,
                      mode->width_px, mode->height_px,
                      mode->refresh_rate_hz);
    assert(rc < sizeof(buffer));

    visit_type_str(v, name, &pos, errp);
}

static void apple_gfx_set_display_mode(Object *obj, Visitor *v,
                                       const char *name, void *opaque,
                                       Error **errp)
{
    Property *prop = opaque;
    AppleGFXDisplayMode *mode = object_field_prop_ptr(obj, prop);
    const char *endptr;
    g_autofree char *str = NULL;
    int ret;
    int val;

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }

    endptr = str;

    ret = qemu_strtoi(endptr, &endptr, 10, &val);
    if (ret || val > UINT16_MAX || val <= 0) {
        error_setg(errp, "width in '%s' must be a decimal integer number"
                         " of pixels in the range 1..65535", name);
        return;
    }
    mode->width_px = val;
    if (*endptr != 'x') {
        goto separator_error;
    }

    ret = qemu_strtoi(endptr + 1, &endptr, 10, &val);
    if (ret || val > UINT16_MAX || val <= 0) {
        error_setg(errp, "height in '%s' must be a decimal integer number"
                         " of pixels in the range 1..65535", name);
        return;
    }
    mode->height_px = val;
    if (*endptr != '@') {
        goto separator_error;
    }

    ret = qemu_strtoi(endptr + 1, &endptr, 10, &val);
    if (ret || val > UINT16_MAX || val <= 0) {
        error_setg(errp, "refresh rate in '%s'"
                         " must be a positive decimal integer (Hertz)", name);
        return;
    }
    mode->refresh_rate_hz = val;
    return;

separator_error:
    error_setg(errp,
               "Each display mode takes the format '<width>x<height>@<rate>'");
}

const PropertyInfo qdev_prop_apple_gfx_display_mode = {
    .type  = "display_mode",
    .description =
        "Display mode in pixels and Hertz, as <width>x<height>@<refresh-rate> "
        "Example: 3840x2160@60",
    .get   = apple_gfx_get_display_mode,
    .set   = apple_gfx_set_display_mode,
};
