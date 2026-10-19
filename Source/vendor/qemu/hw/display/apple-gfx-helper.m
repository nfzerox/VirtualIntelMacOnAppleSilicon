/* Local Intel PVG helper bringup. SPDX-License-Identifier: GPL-2.0-or-later */
/* Build standalone for x86_64; do not link system PVG (load donor explicitly). */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <CoreGraphics/CoreGraphics.h>
#import <objc/runtime.h>
#import <objc/message.h>
#import <ParavirtualizedGraphics/ParavirtualizedGraphics.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "apple-gfx-oop-wire.h"
#include "apple-gfx-app-wire.h"
#include "apple-gfx-oop-timing.h"
void apple_gfx_legacy_shadow_install(void);
typedef struct {
    AppleGFXOOPRAMWire wire;
    uint8_t *host;
} HelperRAM;
struct PGTask_s {
    mach_vm_address_t address;
    uint64_t length;
    pthread_mutex_t lock;
};
static void *helper_resolve(HelperRAM *ram, uint32_t count,
                            uint64_t address, uint64_t length)
{
    for (uint32_t i = 0; i < count; i++) {
        AppleGFXOOPRAMWire r = ram[i].wire;
        if (address < r.guest) { continue; }
        uint64_t offset = address - r.guest;
        if (offset < r.length && length <= r.length - offset) {
            return ram[i].host + offset;
        }
    }
    return NULL;
}
static PGTask_t *helper_task_create(uint64_t length, void **base)
{
    *base = NULL;
    if (!length || (length & 4095) || vm_page_size != 4096) { return NULL; }
    PGTask_t *task = calloc(1, sizeof(*task));
    if (!task) { return NULL; }
    if (mach_vm_allocate(mach_task_self(), &task->address, length, VM_FLAGS_ANYWHERE)) {
        free(task); return NULL;
    }
    task->length = length;
    pthread_mutex_init(&task->lock, NULL);
    *base = (void *)task->address;
    return task;
}
static bool helper_task_map(PGTask_t *task, uint64_t offset, bool read_only,
                            PGPhysicalMemoryRange_t *ranges, uint32_t count,
                            HelperRAM *ram, uint32_t ram_count)
{
    if (!task || (offset & 4095) || offset > task->length || (count && !ranges)) {
        return false;
    }
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t address = ranges[i].physicalAddress, length = ranges[i].physicalLength;
        if (!length || ((address | length) & 4095) ||
            length > task->length - offset - total ||
            !helper_resolve(ram, ram_count, address, length)) { return false; }
        total += length;
    }
    pthread_mutex_lock(&task->lock);
    for (uint32_t i = 0; i < count; i++) {
        mach_vm_address_t target = task->address + offset;
        uint64_t length = ranges[i].physicalLength;
        vm_prot_t current, maximum;
        kern_return_t kr = mach_vm_remap(mach_task_self(), &target, length, 0,
            VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, mach_task_self(),
            (mach_vm_address_t)helper_resolve(ram, ram_count, ranges[i].physicalAddress, length),
            FALSE, &current, &maximum, VM_INHERIT_NONE);
        if (!kr && read_only) {
            kr = mach_vm_protect(mach_task_self(), target, length, FALSE, VM_PROT_READ);
        }
        if (kr) {
            /* Never continue serving a device with a partially changed task. */
            fprintf(stderr, "PVG helper mapping failed: %d\n", kr);
            _exit(30);
        }
        offset += length;
    }
    pthread_mutex_unlock(&task->lock);
    return true;
}
/* Optional app presentation endpoint. The app creates a private directory and
 * listener before launching QEMU. Fail closed if explicitly requested. */
static int helper_app_connect(int frame_fd)
{
    const char *path = getenv("PVG_APP_DISPLAY_SOCKET");
    if (!path || !*path) { return -1; }
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(path) >= sizeof(address.sun_path)) { _exit(44); }
    strlcpy(address.sun_path, path, sizeof(address.sun_path));
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    uid_t uid; gid_t gid;
    if (fd < 0 || connect(fd, (struct sockaddr *)&address, sizeof(address)) ||
        getpeereid(fd, &uid, &gid) || uid != getuid()) { _exit(44); }
    AppleGFXAppMessage hello = {.magic=APPLE_GFX_APP_MAGIC,
        .operation=APPLE_GFX_APP_HELLO};
    char control[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec iov = {&hello, sizeof(hello)};
    struct msghdr msg = {.msg_iov=&iov, .msg_iovlen=1,
        .msg_control=control, .msg_controllen=sizeof(control)};
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET; cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &frame_fd, sizeof(frame_fd));
    ssize_t sent;
    do { sent = sendmsg(fd, &msg, 0); } while (sent < 0 && errno == EINTR);
    if (sent <= 0 || ((size_t)sent < sizeof(hello) &&
        !apple_gfx_oop_io(fd, (char *)&hello + sent, sizeof(hello) - sent, true))) {
        fprintf(stderr, "PVG helper: app display connection failed\n");
        _exit(44);
    }
    return fd;
}
static int helper_main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc != 3) { return 64; }
        int channel = atoi(argv[1]);
        signal(SIGPIPE, SIG_IGN);
        AppleGFXOOPHello hello;
        if (!apple_gfx_oop_io(channel, &hello, sizeof(hello), false) ||
            hello.magic != APPLE_GFX_OOP_MAGIC || !hello.count ||
            hello.count > APPLE_GFX_OOP_MAX_RANGES || hello.reserved) { return 1; }
        struct stat frame_stat;
        if (hello.frame_fd < 0 || fstat(hello.frame_fd, &frame_stat) ||
            frame_stat.st_size != APPLE_GFX_OOP_FRAME_SIZE) { return 35; }
        void *frame = mmap(NULL, APPLE_GFX_OOP_FRAME_SIZE, PROT_READ | PROT_WRITE,
                           MAP_SHARED, hello.frame_fd, 0);
        int app_channel = helper_app_connect(hello.frame_fd);
        close(hello.frame_fd);
        if (frame == MAP_FAILED) { return 36; }
        HelperRAM *ram = calloc(hello.count, sizeof(*ram));
        if (!ram) { return 2; }
        for (uint32_t i = 0; i < hello.count; i++) {
            AppleGFXOOPRAMWire *r = &ram[i].wire;
            struct stat st;
            if (!apple_gfx_oop_io(channel, r, sizeof(*r), false) || r->reserved ||
                !r->length || r->inherited_fd < 0 ||
                ((r->file_offset | r->length) & (getpagesize() - 1)) ||
                r->guest > UINT64_MAX - r->length ||
                fstat(r->inherited_fd, &st) || st.st_size < 0 ||
                r->file_offset > (uint64_t)st.st_size ||
                r->length > (uint64_t)st.st_size - r->file_offset) { return 3; }
            ram[i].host = mmap(NULL, r->length, PROT_READ | PROT_WRITE,
                              MAP_SHARED, r->inherited_fd, r->file_offset);
            close(r->inherited_fd);
            if (ram[i].host == MAP_FAILED) { return 4; }
        }
        void *image = dlopen(argv[2], RTLD_NOW | RTLD_LOCAL);
        if (!image) { fprintf(stderr, "PVG helper: %s\n", dlerror()); return 5; }
        if (!getenv("PVG_LEGACY_SHADOW")) setenv("PVG_LEGACY_SHADOW", "1", 1);
        if (!getenv("PVG_SHADOW_PRIVATE_PAGING")) setenv("PVG_SHADOW_PRIVATE_PAGING", "1", 1);
        /* Install after donor classes exist and before any PVG resources. */
        apple_gfx_legacy_shadow_install();
        id<PGDevice> (*create)(PGDeviceDescriptor *) =
            dlsym(image, "PGCreateDeviceWithDescriptor");
        /* Older frameworks (macOS 13) export only the public constructor. */
        if (!create) { create = dlsym(image, "PGNewDeviceWithDescriptor"); }
        if (!create) { return 6; }
        PGDeviceDescriptor *desc = [NSClassFromString(@"PGDeviceDescriptor") new];
        desc.device = MTLCreateSystemDefaultDevice();
        id<MTLBuffer> frame_buffer = [desc.device newBufferWithBytesNoCopy:frame
            length:APPLE_GFX_OOP_FRAME_SIZE options:MTLResourceStorageModeShared deallocator:nil];
        if (!frame_buffer) { return 37; }
        desc.readMemory = ^bool(uint64_t address, uint64_t size, void *out) {
            void *source = helper_resolve(ram, hello.count, address, size);
            if (!source) { return false; }
            memcpy(out, source, size); return true;
        };
        desc.createTask = ^PGTask_t *(uint64_t length, void **base) {
            return helper_task_create(length, base);
        };
        desc.destroyTask = ^(PGTask_t *task) {
            if (!task) { return; }
            mach_vm_deallocate(mach_task_self(), task->address, task->length);
            pthread_mutex_destroy(&task->lock);
            free(task);
        };
        desc.mapMemory = ^bool(PGTask_t *task, uint32_t count, uint64_t off,
                              bool ro, PGPhysicalMemoryRange_t *ranges) {
            return helper_task_map(task, off, ro, ranges, count, ram, hello.count);
        };
        desc.unmapMemory = ^bool(PGTask_t *task, uint64_t off, uint64_t len) {
            if (!task || !len || ((off | len) & 4095) || off > task->length ||
                len > task->length - off) { return false; }
            pthread_mutex_lock(&task->lock);
            mach_vm_address_t target = task->address + off;
            kern_return_t kr = mach_vm_allocate(mach_task_self(), &target, len,
                                               VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE);
            pthread_mutex_unlock(&task->lock);
            return kr == KERN_SUCCESS;
        };
        dispatch_queue_t output = dispatch_queue_create("pvg.helper.output", DISPATCH_QUEUE_SERIAL);
        dispatch_suspend(output); /* Readiness header must precede all events. */
        desc.raiseInterrupt = ^(uint32_t vector) {
            dispatch_async(output, ^{
                AppleGFXOOPCommand irq = {.magic=APPLE_GFX_OOP_MAGIC,
                    .operation=APPLE_GFX_OOP_IRQ, .value=vector};
                if (!apple_gfx_oop_io(channel, &irq, sizeof(irq), true)) { _exit(32); }
            });
        };
        if (getenv("PVG_OOP_CHECK_MAPPINGS")) {
            for (uint32_t i = 0; i < hello.count; i++) {
                if (ram[i].wire.length < 8192) { continue; }
                void *base;
                PGTask_t *task = desc.createTask(8192, &base);
                PGPhysicalMemoryRange_t ranges[] = {
                    {ram[i].wire.guest + 4096, 4096}, {ram[i].wire.guest, 4096}
                };
                if (!task || !desc.mapMemory(task, 2, 0, true, ranges) ||
                    memcmp(base, ram[i].host + 4096, 4096) ||
                    memcmp((char *)base + 4096, ram[i].host, 4096) ||
                    !desc.unmapMemory(task, 0, 8192)) { return 31; }
                desc.destroyTask(task);
            }
            fprintf(stderr, "PVG helper mapping callback checks passed\n");
        }
        id<PGDevice> device = create(desc);
        dispatch_queue_t frames = dispatch_queue_create("pvg.helper.frames", DISPATCH_QUEUE_SERIAL);
        dispatch_queue_t app_output = dispatch_queue_create("pvg.helper.app", DISPATCH_QUEUE_SERIAL);
        __block uint64_t app_sequence = 0;
        __block uint32_t power_epoch = 0;
        __block BOOL power_sleeping = NO;
        __block BOOL frame_busy = NO;
        __block BOOL frame_dirty = NO;
        BOOL timing = getenv("PVG_OOP_TIMING") != NULL;
        __block AppleGFXOOPTiming render_time = {}, ack_time = {};
        __block uint64_t frame_sent = 0;
        __block id<PGDisplay> display = nil;
        __block id<MTLTexture> texture = nil;
        __block uint32_t frame_width = APPLE_GFX_OOP_WIDTH;
        __block uint32_t frame_height = APPLE_GFX_OOP_HEIGHT;
        id<MTLCommandQueue> metal_queue = [desc.device newCommandQueue];
        PGDisplayDescriptor *display_desc = [NSClassFromString(@"PGDisplayDescriptor") new];
        display_desc.name = @"QEMU Intel PVG helper";
        display_desc.queue = frames;
        display_desc.sizeInMillimeters = NSMakeSize(340, 212);
        CGDirectDisplayID host_display = CGMainDisplayID();
        CGDisplayModeRef host_mode = CGDisplayCopyDisplayMode(host_display);
        CGSize host_mm = CGDisplayScreenSize(host_display);
        size_t host_pixels = host_mode ? CGDisplayModeGetPixelWidth(host_mode) : 0;
        size_t host_points = host_mode ? CGDisplayModeGetWidth(host_mode) : 0;
        if (host_points && host_pixels > host_points) {
            frame_width = 2560; frame_height = 1600;
        }
        if (host_pixels && host_mm.width > 0) {
            double mm_per_pixel = host_mm.width / host_pixels;
            display_desc.sizeInMillimeters = NSMakeSize(frame_width * mm_per_pixel,
                                                       frame_height * mm_per_pixel);
        }
        if (host_mode) { CGDisplayModeRelease(host_mode); }
        /* Donor26 private descriptor property, verified byte-valued getter
         * and setter in the paired architecture audit. Opt-in until guest
         * mode changes prove that this supplies persistent HiDPI policy. */
        if (getenv("PVG_OOP_SCALE_FACTOR")) {
            const char *setting = getenv("PVG_OOP_SCALE_FACTOR");
            if (strcmp(setting, "1") && strcmp(setting, "2")) { return 43; }
            SEL setter = sel_registerName("setScaleFactor:");
            SEL getter = sel_registerName("scaleFactor");
            if (![display_desc respondsToSelector:setter] ||
                ![display_desc respondsToSelector:getter]) {
                /* Older frameworks (macOS 13) have no descriptor scale factor;
                 * the display keeps its default scale there. */
                fprintf(stderr, "PVG helper: descriptor scaleFactor unsupported; using default\n");
            } else {
                uint8_t scale = setting[0] - '0';
                ((void (*)(id, SEL, uint8_t))objc_msgSend)(display_desc, setter, scale);
                uint8_t actual = ((uint8_t (*)(id, SEL))objc_msgSend)(display_desc, getter);
                if (actual != scale) { return 45; }
                fprintf(stderr, "PVG helper descriptor scaleFactor=%u\n", actual);
            }
        }
        display_desc.modeChangeHandler = ^(PGDisplayCoord_t size, OSType format) {
            if (size.x < 320 || size.y < 200 ||
                size.x > APPLE_GFX_OOP_MAX_DIMENSION || size.y > APPLE_GFX_OOP_MAX_DIMENSION) { return; }
            frame_width = size.x; frame_height = size.y;
            /* Existing submitted commands retain their old texture. Shared
             * buffer writes remain barred by frame_busy until consumer ACK. */
            [texture release]; texture = nil;
        };
        __block BOOL cursor_visible = YES;
        __block int app_cursor_visibility = -1;
        void (^send_cursor_state)(void) = ^{
            if (app_channel >= 0) {
                /* Position follows the native pointer, not an IPC round trip. */
                if (app_cursor_visibility == cursor_visible) { return; }
                app_cursor_visibility = cursor_visible;
                BOOL visible = cursor_visible;
                dispatch_async(app_output, ^{
                    AppleGFXAppMessage event = {.magic=APPLE_GFX_APP_MAGIC,
                        .operation=APPLE_GFX_APP_CURSOR_STATE, .width=visible};
                    if (!apple_gfx_oop_io(app_channel, &event, sizeof(event), true)) { _exit(46); }
                });
                return;
            }
            PGDisplayCoord_t position = display.cursorPosition;
            AppleGFXOOPCommand event = {.magic=APPLE_GFX_OOP_MAGIC,
                .operation=APPLE_GFX_OOP_CURSOR_STATE,
                .offset=(uint32_t)position.x | ((uint64_t)(uint32_t)position.y << 32),
                .value=cursor_visible};
            dispatch_async(output, ^{
                AppleGFXOOPCommand message = event;
                if (!apple_gfx_oop_io(channel, &message, sizeof(message), true)) { _exit(41); }
            });
        };
        display_desc.cursorShowHandler = ^(BOOL show) {
            cursor_visible = show;
            send_cursor_state();
        };
        if (@available(macOS 11.1, *)) {
            display_desc.cursorMoveHandler = ^{ send_cursor_state(); };
        }
        display_desc.cursorGlyphHandler = ^(NSBitmapImageRep *glyph, PGDisplayCoord_t hotspot) {
            NSInteger w = glyph.pixelsWide, h = glyph.pixelsHigh;
            if (glyph.bitsPerPixel != 32 || w <= 0 || h <= 0 ||
                w > APPLE_GFX_OOP_CURSOR_MAX || h > APPLE_GFX_OOP_CURSOR_MAX ||
                hotspot.x >= w || hotspot.y >= h || glyph.bytesPerRow < w * 4) { return; }
            NSMutableData *pixels = [NSMutableData dataWithLength:w * h * 4];
            uint32_t *dst = pixels.mutableBytes;
            for (NSInteger y = 0; y < h; y++) {
                const uint8_t *src = glyph.bitmapData + y * glyph.bytesPerRow;
                for (NSInteger x = 0; x < w; x++, src += 4) {
                    *dst++ = (uint32_t)src[0] << 16 | (uint32_t)src[1] << 8 |
                        (uint32_t)src[2] | (uint32_t)src[3] << 24;
                }
            }
            if (app_channel >= 0) {
                dispatch_async(app_output, ^{
                    AppleGFXAppMessage event = {.magic=APPLE_GFX_APP_MAGIC,
                        .operation=APPLE_GFX_APP_CURSOR_GLYPH, .width=(uint32_t)w, .height=(uint32_t)h,
                        .stride=(uint32_t)hotspot.x | ((uint32_t)hotspot.y << 16)};
                    if (!apple_gfx_oop_io(app_channel, &event, sizeof(event), true) ||
                        !apple_gfx_oop_io(app_channel, pixels.mutableBytes, pixels.length, true)) { _exit(47); }
                });
                return;
            }
            dispatch_async(output, ^{
                AppleGFXOOPCommand event = {.magic=APPLE_GFX_OOP_MAGIC,
                    .operation=APPLE_GFX_OOP_CURSOR_GLYPH,
                    .offset=(uint64_t)w | ((uint64_t)h << 32),
                    .value=(uint32_t)hotspot.x | ((uint64_t)(uint32_t)hotspot.y << 32)};
                if (!apple_gfx_oop_io(channel, &event, sizeof(event), true) ||
                    !apple_gfx_oop_io(channel, pixels.mutableBytes, pixels.length, true)) { _exit(42); }
            });
        };
        // Same Intel donor callback ABI as the in-process path. Keep power
        // events on the existing helper-to-app channel, not through QEMU.
        __block BOOL framework_sleeping = NO, service_sleeping = NO;
        void (^display_power)(BOOL, BOOL) = ^(BOOL value, BOOL service) {
            if (service) { service_sleeping = value; }
            else { framework_sleeping = value; }
            BOOL sleeping = framework_sleeping || service_sleeping;
            if (app_channel < 0 || power_sleeping == sleeping) { return; }
            power_sleeping = sleeping;
            if (power_epoch == UINT32_MAX) { _exit(50); }
            uint32_t epoch = ++power_epoch;
            dispatch_async(app_output, ^{
                AppleGFXAppMessage event = {.magic=APPLE_GFX_APP_MAGIC,
                    .operation=APPLE_GFX_APP_DISPLAY_POWER,
                    .sequence=epoch, .width=sleeping};
                if (!apple_gfx_oop_io(app_channel, &event, sizeof(event), true)) { _exit(50); }
            });
        };
        SEL sleep_setter = sel_registerName("setSleepHandler:");
        if (app_channel >= 0 && [display_desc respondsToSelector:sleep_setter]) {
            ((void (*)(id, SEL, id))objc_msgSend)(display_desc, sleep_setter,
                ^{ display_power(YES, NO); });
        }
        display_desc.newFrameEventHandler = ^{
            display_power(NO, NO);
            if (frame_busy) { frame_dirty = YES; return; }
            if (!display) { return; }
            if (!texture) {
                MTLTextureDescriptor *td = [MTLTextureDescriptor
                    texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                    width:frame_width height:frame_height mipmapped:NO];
                td.usage = display.minimumTextureUsage;
                texture = [desc.device newTextureWithDescriptor:td];
                if (!texture) { _exit(38); }
            }
            frame_busy = YES;
            uint32_t width = frame_width, height = frame_height;
            uint32_t frame_power_epoch = power_epoch;
            size_t stride = ((size_t)width * 4 + 255) & ~(size_t)255;
            uint64_t started = timing ? apple_gfx_oop_now() : 0;
            id<MTLCommandBuffer> command = [metal_queue commandBuffer];
            if (![display encodeCurrentFrameToCommandBuffer:command texture:texture
                  region:MTLRegionMake2D(0, 0, width, height)]) {
                frame_busy = NO; return;
            }
            id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
            [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0
                sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(width,height,1)
                toBuffer:frame_buffer destinationOffset:0
                destinationBytesPerRow:stride
                destinationBytesPerImage:stride * height];
            [blit endEncoding];
            /* Keep the display callback queue available while the GPU runs.
             * frame_busy still owns the shared storage until presentation ACK. */
            [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
                if (completed.status != MTLCommandBufferStatusCompleted) { _exit(39); }
                dispatch_async(frames, ^{
                    if (timing) { apple_gfx_oop_timing(&render_time, "helper-render-and-wait", started); }
                    frame_sent = timing ? apple_gfx_oop_now() : 0;
                    if (app_channel >= 0) {
                        uint64_t sequence = ++app_sequence;
                        dispatch_async(app_output, ^{
                            AppleGFXAppMessage event = {.magic=APPLE_GFX_APP_MAGIC,
                                .operation=APPLE_GFX_APP_POWER_FRAME, .sequence=sequence,
                                .width=width, .height=height, .stride=(uint32_t)stride,
                                .reserved=frame_power_epoch};
                            if (!apple_gfx_oop_io(app_channel, &event, sizeof(event), true)) { _exit(45); }
                        });
                        return;
                    }
                    dispatch_async(output, ^{
                        AppleGFXOOPCommand event = {.magic=APPLE_GFX_OOP_MAGIC,
                            .operation=APPLE_GFX_OOP_FRAME, .offset=width, .value=height};
                        if (!apple_gfx_oop_io(channel, &event, sizeof(event), true)) { _exit(40); }
                    });
                });
            }];
            [command commit];
        };
        display = [device newDisplayWithDescriptor:display_desc port:0 serialNum:1];
        PGDisplayCoord_t resolution = {frame_width, frame_height};
        PGDisplayMode *mode = [[NSClassFromString(@"PGDisplayMode") alloc]
            initWithSizeInPixels:resolution refreshRateInHz:60];
        display.modeList = @[mode];
        AppleGFXOOPReady ready = {APPLE_GFX_OOP_MAGIC, device ? 0 : 1, desc.mmioLength};
        if (!apple_gfx_oop_io(channel, &ready, sizeof(ready), true)) { return 7; }
        if (!device) { return 8; }
        dispatch_resume(output);
        if (app_channel >= 0) {
            /* Independent input reader: resize must work while scanout is idle,
             * and cursor delivery must not wait for presentation completion. */
            // A lifetime-long blocking socket reader must not consume the
            // global worker pool used by PVG, presentation and Metal callbacks.
            [NSThread detachNewThreadWithBlock:^{
                NSThread.currentThread.name = @"pvg-app-input";
                AppleGFXAppMessage incoming;
                while (apple_gfx_oop_io(app_channel, &incoming, sizeof(incoming), false)) {
                    if (incoming.magic != APPLE_GFX_APP_MAGIC || incoming.reserved || incoming.stride) { _exit(48); }
                    AppleGFXAppMessage message = incoming;
                    if (message.operation == APPLE_GFX_APP_ACK && !message.width && !message.height) {
                        dispatch_async(frames, ^{
                            if (!frame_busy || message.sequence != app_sequence) { _exit(49); }
                            frame_busy = NO;
                            if (frame_dirty) { frame_dirty = NO; display_desc.newFrameEventHandler(); }
                        });
                    } else if (message.operation == APPLE_GFX_APP_GUEST_POWER &&
                               !message.sequence && message.width <= 1 && !message.height) {
                        dispatch_async(frames, ^{
                            BOOL was_asleep = power_sleeping;
                            display_power(message.width != 0, YES);
                            if (was_asleep && !power_sleeping) {
                                display_desc.newFrameEventHandler();
                            }
                        });
                    } else if (message.operation == APPLE_GFX_APP_SET_MODE && !message.sequence &&
                        message.width >= 320 && message.width <= APPLE_GFX_OOP_MAX_DIMENSION &&
                        message.height >= 200 && message.height <= APPLE_GFX_OOP_MAX_DIMENSION) {
                        dispatch_async(frames, ^{
                            PGDisplayCoord_t size = {message.width, message.height};
                            PGDisplayMode *new_mode = [[NSClassFromString(@"PGDisplayMode") alloc]
                                initWithSizeInPixels:size refreshRateInHz:60];
                            display.modeList = @[new_mode];
                            [new_mode release];
                        });
                    } else { _exit(48); }
                }
                _exit(50);
            }];
        }
        uint64_t last_sequence = 0;
        AppleGFXOOPCommand request;
        while (apple_gfx_oop_io(channel, &request, sizeof(request), false)) {
            if (request.magic != APPLE_GFX_OOP_MAGIC || request.reserved ||
                request.status || request.sequence <= last_sequence) { return 33; }
            last_sequence = request.sequence;
            if (request.operation == APPLE_GFX_OOP_FRAME_ACK) {
                if (app_channel >= 0) { continue; }
                dispatch_async(frames, ^{
                    if (timing && frame_sent) {
                        apple_gfx_oop_timing(&ack_time, "helper-frame-to-ack", frame_sent);
                        frame_sent = 0;
                    }
                    frame_busy = NO;
                    if (frame_dirty) {
                        frame_dirty = NO;
                        display_desc.newFrameEventHandler();
                    }
                });
            } else if (request.operation == APPLE_GFX_OOP_SET_MODE) {
                if (request.offset < 320 || request.value < 200 ||
                    request.offset > APPLE_GFX_OOP_MAX_DIMENSION ||
                    request.value > APPLE_GFX_OOP_MAX_DIMENSION) { request.status = EINVAL; }
                else {
                    PGDisplayCoord_t size = {request.offset, request.value};
                    dispatch_async(frames, ^{
                        PGDisplayMode *mode = [[NSClassFromString(@"PGDisplayMode") alloc]
                            initWithSizeInPixels:size refreshRateInHz:60];
                        display.modeList = @[mode];
                        [mode release];
                    });
                }
            } else if (request.operation == APPLE_GFX_OOP_RESET) {
                if ([device respondsToSelector:@selector(reset)]) {
                    ((void (*)(id, SEL))objc_msgSend)(device, @selector(reset));
                }
            } else if ((request.offset & 3) || request.offset >= desc.mmioLength) {
                request.status = EINVAL;
            } else if (request.operation == APPLE_GFX_OOP_READ) {
                request.value = [device mmioReadAtOffset:request.offset];
            } else if (request.operation == APPLE_GFX_OOP_WRITE && request.value <= UINT32_MAX) {
                [device mmioWriteAtOffset:request.offset value:(uint32_t)request.value];
            } else {
                request.status = ENOTSUP;
            }
            AppleGFXOOPCommand reply = request;
            dispatch_async(output, ^{
                AppleGFXOOPCommand message = reply;
                if (!apple_gfx_oop_io(channel, &message, sizeof(message), true)) { _exit(34); }
            });
        }
        /* Process exit owns teardown, including any in-flight device callbacks. */
        _exit(0);
    }
}

/* Each startup check has its own status; report it, since QEMU only sees EOF. */
int main(int argc, char **argv)
{
    int status = helper_main(argc, argv);
    if (status) fprintf(stderr, "PVG helper: startup failed with status %d\n", status);
    return status;
}
