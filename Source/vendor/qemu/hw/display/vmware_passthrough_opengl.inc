/* Local research only: a bounded stream across reserved VRAM, replacing the
 * emulated guest network. The same renderer can run externally for comparison
 * or inside QEMU through its bounded memory-stream API.
 * Deliberately opt-in; not an upstream VMware protocol extension. */
#define VIM_GL_REGISTER 0x1000
#define VIM_GL_CAPABILITIES_REGISTER 0x1001
#define VIM_GL_WRITE_CAPABILITIES_REGISTER 0x1002
#define VIM_GL_WRITE_MAGIC 0x56475731u
#define VIM_GL_SCATTER_CAPABILITIES_REGISTER 0x1003
#define VIM_GL_SCATTER_MAGIC 0x56475331u
#define VIM_GL_RAM_SCATTER_CAPABILITIES_REGISTER 0x1004
#define VIM_GL_RAM_SCATTER_MAGIC 0x56475231u
#define VIM_GL_FRAMEBUFFER_BLIT_CAPABILITIES_REGISTER 0x1005
#define VIM_GL_FRAMEBUFFER_BLIT_MAGIC 0x56474631u
#define VIM_GL_RAM_UPLOAD_CAPABILITIES_REGISTER 0x1006
#define VIM_GL_RAM_UPLOAD_MAGIC 0x56475531u
#define VIM_GL_MASKED_SCATTER_CAPABILITIES_REGISTER 0x1011
#define VIM_GL_MASKED_SCATTER_MAGIC 0x56474d31u
#define VIM_GL_EXCHANGE_CAPABILITIES_REGISTER 0x1012u
#define VIM_GL_EXCHANGE_MAGIC 0x56475831u
#define VIM_GL_EXCHANGE_LIMIT 4096u
#define VIM_GL_LARGE_EXCHANGE_CAPABILITIES_REGISTER 0x1016u
#define VIM_GL_LARGE_EXCHANGE_MAGIC 0x56475832u
#define VIM_GL_LARGE_EXCHANGE_LIMIT 65536u
#define VIM_GL_BULK_UPLOAD_CAPABILITIES_REGISTER 0x1013u
#define VIM_GL_BULK_UPLOAD_MAGIC 0x56474231u
#define VIM_GL_DOORBELL_CAPABILITIES_REGISTER 0x1014u
#define VIM_GL_DOORBELL_MAGIC 0x56474442u
#define VIM_GL_DOORBELL_PORT 12u
#define VIM_GL_NOTIFY_CAPABILITIES_REGISTER 0x1015u
#define VIM_GL_NOTIFY_MAGIC 0x56474e31u
#define VIM_GL_NOTIFY_STATUS_PORT 16u
#define VIM_GL_NOTIFY_ENABLE_PORT 20u
#define VIM_PANEL_CAPABILITIES_REGISTER 0x1007
#define VIM_PANEL_MAGIC 0x56474431u
#define VIM_PANEL_WIDTH_REGISTER 0x1008
#define VIM_PANEL_HEIGHT_REGISTER 0x1009
#define VIM_PANEL_SCALE_REGISTER 0x100a
#define VIM_PANEL_IRQ_CAPABILITIES_REGISTER 0x100b
#define VIM_PANEL_IRQ_MAGIC 0x56474932u
#define VIM_PANEL_IRQ_STATUS_PORT 8u
#define VIM_PANEL_GENERATION_REGISTER 0x100c
#define VIM_PANEL_IRQ_ENABLE_REGISTER 0x100d
#define VIM_PANEL_ACK_REGISTER 0x100e
#define VIM_PANEL_REQUEST_WIDTH_REGISTER 0x100f
#define VIM_PANEL_REQUEST_HEIGHT_REGISTER 0x1010
#define VIM_PANEL_LARGE_SCANOUT_REGISTER 0x1017
#define VIM_PANEL_LARGE_SCANOUT_MAGIC 0x56494c31u
#define VIM_PANEL_REQUEST_SCALE_REGISTER 0x1018u
#define VIM_GL_SCATTER_MAX_RANGES 2048u
#define VIM_GL_RAM_SCATTER_MAX_RANGES 16384u
#define VIM_GL_SCATTER_MAX_BYTES (64u * 1024u * 1024u)
#define VIM_GL_CAP_DIRECT_READ 1u
#define VIM_GL_RESULT_CAP_DRAINED_CLOSE 256u /* Matches private guest ABI. */
#define VIM_GL_MAGIC 0x56474c31u
#define VIM_GL_CHUNK (256 * 1024)
#include <sys/uio.h>
#include "qemu/main-loop.h"
#include "system/address-spaces.h"
#include "vmware_passthrough_opengl_notify.inc"
#ifdef __APPLE__
struct VIMMainRAMExport {
    struct vmsvga_state_s *device;
    void *base;
    uint64_t bytes;
    unsigned count;
    struct VIMSharedRAMRange ranges[VIM_SHARED_RAM_REGIONS];
};
static bool vmsvga_legacy_find_main_ram(Int128 start, Int128 length,
                                      const MemoryRegion *mr, hwaddr offset, void *opaque)
{
    struct VIMMainRAMExport *m = opaque;
    if (memory_region_is_ram(mr) && !memory_region_is_ram_device(mr) && !mr->readonly &&
        mr != &m->device->vga.vram && memory_region_size(mr) > m->bytes) {
        m->base = memory_region_get_ram_ptr((MemoryRegion *)mr);
        m->bytes = memory_region_size(mr);
    }
    return false;
}
static bool vmsvga_legacy_main_ram_ranges(Int128 start, Int128 length,
                                         const MemoryRegion *mr, hwaddr offset, void *opaque)
{
    struct VIMMainRAMExport *m = opaque;
    if (!memory_region_is_ram(mr) || memory_region_is_ram_device(mr) || mr->readonly ||
        memory_region_get_ram_ptr((MemoryRegion *)mr) != m->base) {
        return false;
    }
    uint64_t guest = int128_get64(start), bytes = int128_get64(length);
    if (m->count == VIM_SHARED_RAM_REGIONS || ((guest | offset | bytes) & 4095) ||
        !bytes || offset > m->bytes || bytes > m->bytes - offset) {
        m->count = VIM_SHARED_RAM_REGIONS + 1;
        return true;
    }
    m->ranges[m->count++] = (struct VIMSharedRAMRange){guest, offset, bytes};
    return false;
}
static void vmsvga_legacy_export_ram(struct vmsvga_state_s *s, const char *service, pid_t renderer)
{
    struct VIMMainRAMExport m = {.device = s};
    RCU_READ_LOCK_GUARD();
    FlatView *view = address_space_to_flatview(&address_space_memory);
    flatview_for_each_range(view, vmsvga_legacy_find_main_ram, &m);
    if (m.base && m.bytes && m.bytes <= VIM_SHARED_RAM_MEMORY_LIMIT) {
        flatview_for_each_range(view, vmsvga_legacy_main_ram_ranges, &m);
        char *name = g_strdup_printf("%s.ram", service);
        if (m.count && m.count <= VIM_SHARED_RAM_REGIONS) {
            s->legacy_ram_memory = VIMVRAMMemoryServerStartWithRanges(name, renderer,
                m.base, m.bytes, m.ranges, m.count);
        }
        g_free(name);
    }
    if (!s->legacy_ram_memory) {
        error_report("Research main RAM capability could not be exported (bytes=%" PRIu64 ", regions=%u)", m.bytes, m.count);
        if (getenv("VIM_PASSTHROUGH_OPENGL_INPROCESS_LIBRARY")) {
            exit(EXIT_FAILURE);
        }
    }
}
static void vmsvga_legacy_ram_ready(Notifier *notifier, void *unused)
{
    struct vmsvga_state_s *s = container_of(notifier, struct vmsvga_state_s, legacy_ram_ready);
    const char *service = getenv("VIM_OPENGL_VRAM_SERVICE");
    if (service && s->legacy_vram_memory) {
        vmsvga_legacy_export_ram(s, service, s->legacy_vram_memory->consumer);
    }
}
#endif
struct vmsvga_gl_segment {
    uint8_t *pointer;
    uint32_t offset, bytes, written;
    bool mapped, discard;
};
struct vmsvga_gl_wait {
    uint64_t epoch;
    uint32_t token;
    struct VIMPassthroughOpenGLStream stream;
    bool guarded;
};
static void vmsvga_passthrough_opengl_free_exchange(struct vmsvga_state_s *s,
                                           unsigned slot)
{
    g_free(s->passthrough_opengl_clients[slot].exchange_request);
    s->passthrough_opengl_clients[slot].exchange_request = NULL;
}
static struct vmsvga_gl_wait vmsvga_passthrough_opengl_wait_begin(
    struct vmsvga_state_s *s, unsigned slot)
{
    DeviceState *device = DEVICE(s->legacy_panel_pci);
    struct vmsvga_gl_wait wait = {
        .epoch = s->passthrough_opengl_epoch,
        .token = s->passthrough_opengl_clients[slot].token,
        .stream = s->passthrough_opengl_clients[slot].stream,
        .guarded = device->mem_reentrancy_guard.engaged_in_io,
    };
    g_assert(bql_locked() && !s->passthrough_opengl_clients[slot].busy);
    s->passthrough_opengl_clients[slot].busy = true;
    memory_region_ref(&s->vga.vram);
    /* Only native transport I/O may run unlocked. Other vCPUs' legitimate
     * device accesses are not recursive calls from that native wait. */
    device->mem_reentrancy_guard.engaged_in_io = false;
    bql_unlock();
    return wait;
}
static bool vmsvga_passthrough_opengl_wait_end(struct vmsvga_state_s *s,
                                    unsigned slot, struct vmsvga_gl_wait wait)
{
    bql_lock();
    DeviceState *device = DEVICE(s->legacy_panel_pci);
    g_assert(!device->mem_reentrancy_guard.engaged_in_io);
    device->mem_reentrancy_guard.engaged_in_io = wait.guarded;
    bool current = wait.epoch == s->passthrough_opengl_epoch &&
        s->passthrough_opengl_clients[slot].token == wait.token;
    s->passthrough_opengl_clients[slot].busy = false;
    if (!current) {
        vmsvga_gl_stream_close(wait.stream);
        s->passthrough_opengl_clients[slot].stream = (struct VIMPassthroughOpenGLStream){ .fd = -1 };
        vmsvga_passthrough_opengl_free_exchange(s, slot);
    }
    memory_region_unref(&s->vga.vram);
    return current;
}
static void *vmsvga_passthrough_opengl_map_ram(struct vmsvga_state_s *s,
                                   uint64_t address, uint32_t bytes, bool write)
{
    /* This device's register callback owns the BQL. Keep mapping/region
     * identity stable, and never turn an MMIO/ROM access into a bounce copy. */
    if (!bql_locked() || address > UINT64_MAX - bytes) {
        return NULL;
    }
    MemoryRegionSection section = memory_region_find(get_system_memory(), address, bytes);
    if (!section.mr) {
        return NULL;
    }
    void *pointer = NULL;
    if (section.offset_within_address_space == address &&
        int128_eq(section.size, int128_make64(bytes)) && !section.readonly &&
        memory_region_is_ram(section.mr) && !memory_region_is_ram_device(section.mr) &&
        section.mr != &s->vga.vram &&
        memory_access_is_direct(section.mr, write, MEMTXATTRS_UNSPECIFIED)) {
        hwaddr length = bytes, offset = 0;
        pointer = address_space_map(&address_space_memory, address, &length,
                                    write, MEMTXATTRS_UNSPECIFIED);
        if (pointer && (length != bytes ||
            memory_region_from_host(pointer, &offset) != section.mr ||
            offset != section.offset_within_region)) {
            /* Nothing was written; release without dirtying/invalidation
             * of a zero-length range, even on this rejected mapping. */
            address_space_unmap(&address_space_memory, pointer, length, false, 0);
            pointer = NULL;
        }
    }
    memory_region_unref(section.mr);
    return pointer;
}
/* Local owned-process A/B only, not a guest option or protocol change. */
static volatile bool vmsvga_legacy_ram_dirty_coalesce = true;
static uint64_t vmsvga_legacy_ram_dirty_requests;
static uint64_t vmsvga_legacy_ram_dirty_pages;
static uint64_t vmsvga_legacy_ram_dirty_segments;
static int vmsvga_legacy_page_compare(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *)left, b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}
static bool vmsvga_passthrough_opengl_dirty_ram(struct vmsvga_state_s *s,
                                     const uint8_t *list, unsigned count)
{
    struct VIMDirtySegment {
        void *pointer;
        uint32_t bytes;
    };
    uint64_t *pages = g_new(uint64_t, count);
    struct VIMDirtySegment *mapped = g_new0(struct VIMDirtySegment, count);
    unsigned used = 0;
    bool valid = true, ordered = true;
    /* Capture each address once. No guest descriptor is reread after its
     * validation, and nothing is dirtied until ALL ranges are accepted. */
    for (unsigned i = 0; i < count; i++) {
        pages[i] = ldq_le_p(list + i * 8);
        if (pages[i] & 4095) {
            valid = false;
            goto out;
        }
        if (i && pages[i] < pages[i - 1]) {
            ordered = false;
        }
    }
    unsigned unique = count;
    if (vmsvga_legacy_ram_dirty_coalesce) {
        /* This is an order-independent DIRTY SET, NOT the GPU alias-page
         * sequence or an ordered pixel copy. Sorting our captured addresses
         * lets permuted guest pages describe the same physical runs; neither
         * the guest descriptor nor any pixel/alias mapping is rewritten. */
        if (!ordered) {
            qsort(pages, count, sizeof(*pages), vmsvga_legacy_page_compare);
        }
        unique = 0;
        for (unsigned i = 0; i < count; i++) {
            if (!unique || pages[i] != pages[unique - 1]) {
                pages[unique++] = pages[i];
            }
        }
    }
    for (unsigned i = 0; i < unique; ) {
        unsigned end = i + 1;
        while (vmsvga_legacy_ram_dirty_coalesce && end < unique &&
               pages[end - 1] <= UINT64_MAX - 4096 &&
               pages[end] == pages[end - 1] + 4096) {
            end++;
        }
        uint32_t bytes = (end - i) * 4096;
        void *pointer = vmsvga_passthrough_opengl_map_ram(s, pages[i], bytes, true);
        if (pointer) {
            mapped[used++] = (struct VIMDirtySegment){pointer, bytes};
        } else {
            if (end == i + 1) {
                valid = false;
                goto out;
            }
            /* Adjacent guest pages can belong to different RAM sections.
             * A failed whole-range lookup does not authorize a hole/ROM or
             * bounce mapping. Preserve the original per-page acceptance. */
            for (unsigned j = i; j < end; j++) {
                pointer = vmsvga_passthrough_opengl_map_ram(s, pages[j], 4096, true);
                if (!pointer) {
                    valid = false;
                    goto out;
                }
                mapped[used++] = (struct VIMDirtySegment){pointer, 4096};
            }
        }
        i = end;
    }
out:
    for (unsigned i = 0; i < used; i++) {
        address_space_unmap(&address_space_memory, mapped[i].pointer,
                            mapped[i].bytes, valid, valid ? mapped[i].bytes : 0);
    }
    if (valid) {
        vmsvga_legacy_ram_dirty_requests++;
        vmsvga_legacy_ram_dirty_pages += count;
        vmsvga_legacy_ram_dirty_segments += used;
    }
    g_free(mapped);
    g_free(pages);
    return valid;
}
static int32_t vmsvga_passthrough_opengl_scatter(struct vmsvga_state_s *s, unsigned slot,
                                      uint32_t command, uint32_t bytes, uint32_t count,
                                      bool ram, bool upload, bool masked,
                                      bool *current)
{
    struct VIMPassthroughOpenGLStream stream = s->passthrough_opengl_clients[slot].stream;
    bool unlocked = s->passthrough_opengl_release_bql && bql_locked();
    struct vmsvga_gl_wait wait = {0};
    *current = true;
    uint32_t maximum = ram ? VIM_GL_RAM_SCATTER_MAX_RANGES : VIM_GL_SCATTER_MAX_RANGES;
    if (!count || count > maximum || count > VIM_GL_CHUNK / (ram ? 16u : 8u) || !bytes ||
        bytes > VIM_GL_SCATTER_MAX_BYTES ||
        command > s->vga.vram_size ||
        24u + VIM_GL_CHUNK > s->vga.vram_size - command) {
        return -1;
    }
    /* For RAM uploads the staging range contains only this bounded descriptor
     * table. It does not contain `bytes` of pixels; the complete prepared image
     * can be gathered in one call without growing VRAM staging. Old guests'
     * 256-KB calls remain valid. Validate every range before sending anything.
     * Copy validated metadata out of guest-writable VRAM before transferring.
     * A second guest CPU must not change an offset after bounds checking. */
    struct vmsvga_gl_segment *segments = g_new0(struct vmsvga_gl_segment, count);
    uint64_t total = 0;
    int32_t result = -1;
    uint8_t *discard = NULL;
    for (unsigned i = 0; i < count; i++) {
        uint8_t *entry = s->vga.vram_ptr + command + 24 + i * (ram ? 16 : 8);
        uint64_t address = ram ? ldq_le_p(entry) : ldl_le_p(entry);
        uint32_t length = ldl_le_p(entry + (ram ? 8 : 4));
        uint32_t kind = ram ? ldl_le_p(entry + 12) : 0;
        if (!length || kind > (masked ? 2u : 1u) || total + length > bytes ||
            (kind == 2 && (address || upload))) {
            goto out;
        }
        segments[i].bytes = length;
        if (kind == 2) {
            /* Only action 11 may consume a hole. Never map/dirty guest
             * storage for it; its address must be exactly zero. */
            segments[i].discard = true;
        } else if (kind == 1) {
            segments[i].pointer = vmsvga_passthrough_opengl_map_ram(s, address, length, !upload);
            if (!segments[i].pointer) {
                goto out;
            }
            segments[i].mapped = true;
        } else {
            if (address > s->vga.vram_size || length > s->vga.vram_size - address ||
                (address < (uint64_t)command + 24 + VIM_GL_CHUNK &&
                 address + length > command)) {
                goto out;
            }
            segments[i].offset = address;
            segments[i].pointer = s->vga.vram_ptr + address;
        }
        total += length;
    }
    if (total != bytes) {
        goto out;
    }
    if (masked) {
        discard = g_malloc(64u * 1024u);
    }
    unsigned first = 0;
    uint32_t within = 0, received = 0;
    if (unlocked) {
        /* Every vector points to validated VRAM retained by wait_begin, a
         * direct RAM mapping retained by address_space_map, or own scratch.
         * No guest descriptor/header is consulted until BQL is reacquired.
         * This is direct DMA, not a second frame-sized staging copy. */
        wait = vmsvga_passthrough_opengl_wait_begin(s, slot);
    }
    while (first < count) {
        struct iovec vectors[64];
        unsigned nvec = 0;
        for (unsigned i = 0; i < MIN(count - first, ARRAY_SIZE(vectors)); i++) {
            uint32_t skip = i ? 0 : within;
            struct vmsvga_gl_segment *segment = &segments[first + i];
            uint32_t remaining = segment->bytes - skip;
            vectors[i].iov_base = segment->discard ? discard : segment->pointer + skip;
            vectors[i].iov_len = segment->discard ? MIN(remaining, 64u * 1024u) : remaining;
            nvec++;
            /* A large gap uses bounded reusable scratch, not an allocation
             * proportional to a frame. Finish it before the following span. */
            if (vectors[i].iov_len < remaining) {
                break;
            }
        }
        ssize_t n;
        do {
            n = upload ? vmsvga_gl_stream_writev(stream, vectors, nvec) :
                vmsvga_gl_stream_readv(stream, vectors, nvec);
        } while (n < 0 && errno == EINTR);
        if (n <= 0) {
            break;
        }
        received += n;
        size_t left = n;
        while (left) {
            uint32_t amount = MIN(left, segments[first].bytes - within);
            if (!upload) {
                segments[first].written += amount;
            }
            left -= amount;
            within += amount;
            if (within == segments[first].bytes) {
                first++;
                within = 0;
            }
        }
    }
    if (unlocked) {
        *current = vmsvga_passthrough_opengl_wait_end(s, slot, wait);
    }
    /* Report a prefix on failure too; the kernel rejects incomplete reads
     * and the caller commits possible writes, then abandons this stream. */
    result = received ? received : -1;
out:
    for (unsigned i = 0; i < count; i++) {
        if (segments[i].written && !segments[i].mapped && !segments[i].discard) {
            memory_region_set_dirty(&s->vga.vram, segments[i].offset, segments[i].written);
        }
        if (segments[i].mapped) {
            /* The DMA API retains the region, marks the actual written
             * prefix dirty AND invalidates TCG code on written RAM pages. */
            address_space_unmap(&address_space_memory, segments[i].pointer,
                                segments[i].bytes, segments[i].written != 0,
                                segments[i].written);
        }
    }
    g_free(segments);
    g_free(discard);
    return result;
}
static int32_t vmsvga_legacy_framebuffer_blit(struct vmsvga_state_s *s,
                                            uint32_t command, uint32_t bytes,
                                            uint32_t count)
{
    /* Local experiment: the guest's existing CPU blit, executed natively.
     * Preflight the WHOLE request, retaining direct source maps. Failure
     * therefore means no writes, permitting the unchanged guest fallback. */
    uint64_t visible = (uint64_t)vmsvga_framebuffer_pitch(s) * s->new_height;
    if (s->new_depth != 32 || !s->new_width || !s->new_height ||
        visible > s->vga.vram_size || !count || count > VIM_GL_CHUNK / 24u ||
        !bytes || bytes > VIM_GL_SCATTER_MAX_BYTES ||
        command > s->vga.vram_size ||
        24u + VIM_GL_CHUNK > s->vga.vram_size - command) {
        return -2;
    }
    struct vmsvga_gl_segment *segments = g_new0(struct vmsvga_gl_segment, count);
    uint64_t total = 0;
    int32_t result = -1;
    for (unsigned i = 0; i < count; i++) {
        const uint8_t *entry = s->vga.vram_ptr + command + 24 + i * 24;
        uint64_t source = ldq_le_p(entry);
        uint32_t destination = ldl_le_p(entry + 8);
        uint32_t length = ldl_le_p(entry + 12);
        uint32_t kind = ldl_le_p(entry + 16);
        if (!length || kind > 1 || ldl_le_p(entry + 20) ||
            total + length > bytes || destination > visible ||
            length > visible - destination ||
            (destination < (uint64_t)command + 24 + VIM_GL_CHUNK &&
             (uint64_t)destination + length > command)) {
            result = -3;
            goto out;
        }
        segments[i].bytes = length;
        segments[i].offset = destination;
        if (kind) {
            segments[i].pointer = vmsvga_passthrough_opengl_map_ram(s, source, length, false);
            if (!segments[i].pointer) {
                result = -4;
                goto out;
            }
            segments[i].mapped = true;
        } else {
            if (source > s->vga.vram_size || length > s->vga.vram_size - source ||
                source < visible ||
                (source < (uint64_t)command + 24 + VIM_GL_CHUNK &&
                 source + length > command)) {
                result = -5;
                goto out;
            }
            segments[i].pointer = s->vga.vram_ptr + source;
        }
        total += length;
    }
    if (total != bytes) {
        result = -6;
        goto out;
    }
    for (unsigned i = 0; i < count; i++) {
        memcpy(s->vga.vram_ptr + segments[i].offset,
               segments[i].pointer, segments[i].bytes);
        memory_region_set_dirty(&s->vga.vram, segments[i].offset, segments[i].bytes);
    }
    result = bytes;
out:
    for (unsigned i = 0; i < count; i++) {
        if (segments[i].mapped) {
            address_space_unmap(&address_space_memory, segments[i].pointer,
                                segments[i].bytes, false, 0);
        }
    }
    g_free(segments);
    return result;
}
static void vmsvga_passthrough_opengl_reset(struct vmsvga_state_s *s)
{
    s->passthrough_opengl_epoch++;
    vmsvga_passthrough_opengl_notify_reset(s);
    for (unsigned i = 0; i < ARRAY_SIZE(s->passthrough_opengl_clients); i++) {
        if (s->passthrough_opengl_clients[i].token) {
            if (s->passthrough_opengl_clients[i].busy) {
                /* The unlocked exchange owns this descriptor until it
                 * rejoins the BQL. Wake it; never close/reuse its fd here. */
                vmsvga_gl_stream_shutdown(s->passthrough_opengl_clients[i].stream);
            } else {
                vmsvga_gl_stream_close(s->passthrough_opengl_clients[i].stream);
                s->passthrough_opengl_clients[i].stream = (struct VIMPassthroughOpenGLStream){ .fd = -1 };
            }
            s->passthrough_opengl_clients[i].token = 0;
        }
        if (!s->passthrough_opengl_clients[i].busy) {
            vmsvga_passthrough_opengl_free_exchange(s, i);
        }
    }
}
static void vmsvga_passthrough_opengl_exchange_unlocked(struct vmsvga_state_s *s,
    unsigned slot, uint32_t offset, uint32_t bytes, uint32_t receive)
{
    /* Small metadata only. No guest pointer, mapping or mutable header is
     * consulted while unlocked. The guest still waits for its own reply,
     * but other vCPUs, input, display delivery and the main loop can run. */
    uint8_t small_request[VIM_GL_EXCHANGE_LIMIT], response[VIM_GL_EXCHANGE_LIMIT];
    uint8_t *request = small_request;
    if (bytes > sizeof(small_request)) {
        if (!s->passthrough_opengl_clients[slot].exchange_request) {
            s->passthrough_opengl_clients[slot].exchange_request =
                g_try_malloc(VIM_GL_LARGE_EXCHANGE_LIMIT - 16);
            if (!s->passthrough_opengl_clients[slot].exchange_request) {
                return; /* No stream bytes were consumed. No automatic replay. */
            }
        }
        request = s->passthrough_opengl_clients[slot].exchange_request;
    }
    struct VIMPassthroughOpenGLStream stream = s->passthrough_opengl_clients[slot].stream;
    memcpy(request, s->vga.vram_ptr + offset + 24, bytes);
    struct vmsvga_gl_wait wait = vmsvga_passthrough_opengl_wait_begin(s, slot);
    uint32_t sent = 0, got = 0;
    while (sent < bytes) {
        ssize_t n = vmsvga_gl_stream_write(stream, request + sent, bytes - sent);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        sent += n;
    }
    while (sent == bytes && got < receive) {
        ssize_t n = vmsvga_gl_stream_read(stream, response + got, receive - got);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        got += n;
    }
    bool current = vmsvga_passthrough_opengl_wait_end(s, slot, wait);
    if (current && got == receive) {
        memcpy(s->vga.vram_ptr + offset + 24, response, receive);
        stl_le_p(s->vga.vram_ptr + offset + 16, got);
    } else {
        /* An incomplete command is never replayed. Close/reset cancellation
         * owns no result in the new guest generation and cannot publish into
         * its control buffer. The descriptor was retained while busy. */
        if (current) {
            vmsvga_gl_stream_close(stream);
            s->passthrough_opengl_clients[slot].stream = (struct VIMPassthroughOpenGLStream){ .fd = -1 };
            s->passthrough_opengl_clients[slot].token = 0;
            vmsvga_passthrough_opengl_free_exchange(s, slot);
        }
    }
}
static void vmsvga_passthrough_opengl(struct vmsvga_state_s *s, uint32_t offset)
{
    if (!s->passthrough_opengl_bridge || offset > s->vga.vram_size ||
        s->vga.vram_size - offset < 24) {
        return;
    }
    uint8_t *p = s->vga.vram_ptr + offset;
    uint32_t op = ldl_le_p(p + 4), token = ldl_le_p(p + 8);
    uint32_t bytes = ldl_le_p(p + 12);
    stl_le_p(p + 16, (uint32_t)-1);
    if (ldl_le_p(p) != VIM_GL_MAGIC ||
        (op != 7 && op != 8 && op != 9 && op != 10 && op != 11 && op != 12 &&
         (bytes > VIM_GL_CHUNK || bytes > s->vga.vram_size - offset - 24))) {
        return;
    }
    /* Native completion events are a separate queue, never reply bytes
     * injected into a live GL stream. Pop is device-global for the guest's
     * sole IRQ owner; subscription still requires an owned live stream. */
    if (op == 15 && !token && bytes == 24 && !ldl_le_p(p + 20)) {
        stl_le_p(p + 16, vmsvga_passthrough_opengl_notify_pop(s, p + 24));
        return;
    }
    if (op == 12) {
        uint32_t destination = ldl_le_p(p + 20);
        if (token == 1 && destination && destination <= 16384 && bytes == destination * 4096 &&
            (uint64_t)offset + 24 + (uint64_t)destination * 8 <= s->vga.vram_size) {
            if (vmsvga_passthrough_opengl_dirty_ram(s, p + 24, destination)) {
                stl_le_p(p + 16, bytes);
            }
            return;
        }
        if (!token && bytes && bytes <= VIM_GL_SCATTER_MAX_BYTES &&
            destination <= s->vga.vram_size && bytes <= s->vga.vram_size - destination &&
            ((uint64_t)destination >= (uint64_t)offset + 24 + VIM_GL_CHUNK ||
             (uint64_t)destination + bytes <= offset)) {
            memory_region_set_dirty(&s->vga.vram, destination, bytes);
            stl_le_p(p + 16, bytes);
        }
        return;
    }
    if (op == 9) {
        if (!token) {
            stl_le_p(p + 16, vmsvga_legacy_framebuffer_blit(s, offset, bytes, ldl_le_p(p + 20)));
        }
        return;
    }
    unsigned slot;
    for (slot = 0; slot < ARRAY_SIZE(s->passthrough_opengl_clients); slot++) {
        if (s->passthrough_opengl_clients[slot].token == token && token) {
            break;
        }
    }
    if (op == 1 && !token && !bytes) {
        for (slot = 0; slot < ARRAY_SIZE(s->passthrough_opengl_clients); slot++) {
            if (!s->passthrough_opengl_clients[slot].token && !s->passthrough_opengl_clients[slot].busy) {
                break;
            }
        }
        if (slot == ARRAY_SIZE(s->passthrough_opengl_clients) || s->passthrough_opengl_next == UINT32_MAX) {
            return;
        }
        struct VIMPassthroughOpenGLStream stream = vmsvga_gl_stream_open(s->passthrough_opengl_port);
        if (!vmsvga_gl_stream_valid(stream)) {
            return;
        }
        s->passthrough_opengl_clients[slot].stream = stream;
        s->passthrough_opengl_clients[slot].token = ++s->passthrough_opengl_next;
        stl_le_p(p + 8, s->passthrough_opengl_clients[slot].token);
        stl_le_p(p + 20, vmsvga_gl_stream_drained_close(stream) ?
                 VIM_GL_RESULT_CAP_DRAINED_CLOSE : 0);
        stl_le_p(p + 16, 0);
    } else if (slot < ARRAY_SIZE(s->passthrough_opengl_clients)) {
        struct VIMPassthroughOpenGLStream stream = s->passthrough_opengl_clients[slot].stream;
        if (s->passthrough_opengl_clients[slot].busy && (op != 4 || bytes)) {
            return;
        }
        if (op == 14 && bytes == 32 && !ldl_le_p(p + 20) && stream.memory) {
            if (!vmsvga_passthrough_opengl_notify_subscribe(s, p + 24)) {
                stl_le_p(p + 16, 0);
            }
        } else if (op == 4 && !bytes) {
            if (s->passthrough_opengl_clients[slot].busy) {
                vmsvga_gl_stream_shutdown(stream);
                /* Cancellation only. The unlocked owner still has to drain
                 * and retire the stream; never acknowledge GPU completion. */
            } else {
                vmsvga_gl_stream_close(stream);
                s->passthrough_opengl_clients[slot].stream = (struct VIMPassthroughOpenGLStream){ .fd = -1 };
                vmsvga_passthrough_opengl_free_exchange(s, slot);
                stl_le_p(p + 16, 0);
            }
            s->passthrough_opengl_clients[slot].token = 0;
        } else if (op == 13) {
            uint32_t receive = ldl_le_p(p + 20);
            if (!bytes || bytes > VIM_GL_LARGE_EXCHANGE_LIMIT - 16 ||
                !receive || receive > VIM_GL_EXCHANGE_LIMIT ||
                receive > s->vga.vram_size - offset - 24) {
                return;
            }
            if (s->passthrough_opengl_release_bql && bql_locked()) {
                vmsvga_passthrough_opengl_exchange_unlocked(s, slot, offset, bytes, receive);
                return;
            }
            /* One ordered exchange on the same token/stream. Small in-band
             * metadata only; no pixel buffer, physical address or new queue.
             * Consume partial socket I/O, but never retry a failed command. */
            uint32_t sent = 0, got = 0;
            while (sent < bytes) {
                ssize_t n = vmsvga_gl_stream_write(stream, p + 24 + sent, bytes - sent);
                if (n < 0 && errno == EINTR) {
                    continue;
                }
                if (n <= 0) {
                    break;
                }
                sent += n;
            }
            while (sent == bytes && got < receive) {
                ssize_t n = vmsvga_gl_stream_read(stream, p + 24 + got, receive - got);
                if (n < 0 && errno == EINTR) {
                    continue;
                }
                if (n <= 0) {
                    break;
                }
                got += n;
            }
            if (got == receive) {
                stl_le_p(p + 16, got);
            } else {
                vmsvga_gl_stream_close(stream);
                s->passthrough_opengl_clients[slot].stream = (struct VIMPassthroughOpenGLStream){ .fd = -1 };
                s->passthrough_opengl_clients[slot].token = 0;
                vmsvga_passthrough_opengl_free_exchange(s, slot);
            }
        } else if (op == 7 || op == 8 || op == 10 || op == 11) {
            bool current;
            int32_t result = vmsvga_passthrough_opengl_scatter(s, slot, offset, bytes,
                         ldl_le_p(p + 20), op != 7, op == 10, op == 11, &current);
            if (current) {
                stl_le_p(p + 16, result);
            }
        } else if ((op == 2 || op == 3 || op == 5 || op == 6) && bytes) {
            bool direct = op == 5 || op == 6;
            uint32_t destination = direct ? ldl_le_p(p + 20) : offset + 24;
            /* The guest driver verifies the caller's prepared task mapping.
             * Independently enforce the device boundary here: only VRAM,
             * never host/guest-RAM pointers, and never overwrite the command
             * or its reserved staging area while it is being completed. */
            if (direct &&
                (destination > s->vga.vram_size ||
                 bytes > s->vga.vram_size - destination ||
                 ((uint64_t)destination < (uint64_t)offset + 24 + VIM_GL_CHUNK &&
                  (uint64_t)destination + bytes > offset))) {
                return;
            }
            bool unlocked = s->passthrough_opengl_release_bql && bql_locked();
            struct vmsvga_gl_wait wait = {0};
            uint8_t *buffer = s->vga.vram_ptr + destination;
            if (unlocked) {
                wait = vmsvga_passthrough_opengl_wait_begin(s, slot);
            }
            ssize_t n;
            do {
                n = op == 2 || op == 6 ? vmsvga_gl_stream_write(stream, buffer, bytes) :
                    vmsvga_gl_stream_read_expected(stream, buffer, bytes);
            } while (n < 0 && errno == EINTR);
            bool current = !unlocked || vmsvga_passthrough_opengl_wait_end(s, slot, wait);
            if (op == 5 && n > 0) {
                memory_region_set_dirty(&s->vga.vram, destination, n);
            }
            if (current) {
                stl_le_p(p + 16, n > 0 ? n : (uint32_t)-1);
            }
        }
    }
}
