/* Private/local experiment, not an upstream contribution. This non-display
 * PCI endpoint feeds the SAME native GL interpreter as SVGA. PVG retains all
 * display ownership. Prepared RAM ranges reuse the SVGA interpreter protocol,
 * without its framebuffer/surface loans or any guest virtual pointers. */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/pci/pci_device.h"
#include "migration/vmstate.h"
#include "qom/object.h"
#include "system/address-spaces.h"
#include "hw/display/vim-passthrough-opengl-inprocess.h"
/* The private app fork is built in this repository with its guest sources. */
#include "../../../../vendor/source/VMsvga2/vminclude/vim_gl_headless.h"
#include <dlfcn.h>

#define TYPE_VIM_GL_HEADLESS "vim-gl-headless"
OBJECT_DECLARE_SIMPLE_TYPE(VIMGLHeadless, VIM_GL_HEADLESS)
struct VIMGLHeadless {
    PCIDevice parent_obj;
    MemoryRegion registers, mailbox;
    const struct VIMGLInprocessAPI *api;
    void *library;
    uint64_t epoch;
    uint32_t next_token;
    struct {
        void *stream;
        uint32_t token;
        uint32_t capabilities;
        bool busy;
        uint8_t *scratch;
    } clients[VIM_GL_HEADLESS_SLOTS];
};

typedef struct VIMGLHeadlessRange {
    uint8_t *pointer;
    uint32_t bytes, written;
} VIMGLHeadlessRange;

static uint32_t vim_gl_headless_ram_capabilities(VIMGLHeadless *s)
{
    return s->api->readv && s->api->writev ? VIM_GL_HEADLESS_RAM_CAPABILITIES : 0;
}

static void *vim_gl_headless_map_ram(VIMGLHeadless *s, uint64_t address,
                                    uint32_t bytes, bool write)
{
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
        section.mr != &s->mailbox &&
        memory_access_is_direct(section.mr, write, MEMTXATTRS_UNSPECIFIED)) {
        hwaddr length = bytes, offset = 0;
        pointer = address_space_map(&address_space_memory, address, &length,
                                    write, MEMTXATTRS_UNSPECIFIED);
        if (pointer && (length != bytes ||
            memory_region_from_host(pointer, &offset) != section.mr ||
            offset != section.offset_within_region)) {
            address_space_unmap(&address_space_memory, pointer, length, false, 0);
            pointer = NULL;
        }
    }
    memory_region_unref(section.mr);
    return pointer;
}

static void vim_gl_headless_unmap(VIMGLHeadlessRange *ranges, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (ranges[i].pointer) {
            /* Includes the actually written prefix on EOF/reset. The DMA API
             * dirties RAM and invalidates translated code, not just pixels. */
            address_space_unmap(&address_space_memory, ranges[i].pointer,
                                ranges[i].bytes, ranges[i].written != 0,
                                ranges[i].written);
        }
    }
    g_free(ranges);
}

static int32_t vim_gl_headless_scatter(VIMGLHeadless *s, void *stream,
                                      VIMGLHeadlessRange *ranges, uint32_t count,
                                      bool upload)
{
    uint32_t first = 0, within = 0, done = 0;
    while (first < count) {
        struct iovec vectors[64];
        unsigned nvec = MIN(count - first, ARRAY_SIZE(vectors));
        size_t offered = 0;
        for (unsigned i = 0; i < nvec; i++) {
            uint32_t skip = i ? 0 : within;
            vectors[i].iov_base = ranges[first + i].pointer + skip;
            vectors[i].iov_len = ranges[first + i].bytes - skip;
            offered += vectors[i].iov_len;
        }
        ssize_t n;
        do {
            n = upload ? s->api->writev(stream, vectors, nvec) :
                         s->api->readv(stream, vectors, nvec);
        } while (n < 0 && errno == EINTR);
        if (n <= 0 || n > offered) {
            break;
        }
        done += n;
        size_t left = n;
        while (left) {
            uint32_t amount = MIN(left, ranges[first].bytes - within);
            if (!upload) {
                ranges[first].written += amount;
            }
            left -= amount;
            within += amount;
            if (within == ranges[first].bytes) {
                first++;
                within = 0;
            }
        }
    }
    return done ? done : -1;
}

static void vim_gl_headless_submit(VIMGLHeadless *s, uint32_t index)
{
    if (index >= VIM_GL_HEADLESS_SLOTS || s->clients[index].busy) {
        return;
    }
    uint8_t *mailbox = memory_region_get_ram_ptr(&s->mailbox);
    uint8_t *slot = mailbox + (size_t)index * VIM_GL_HEADLESS_SLOT_BYTES;
    struct VIMGLDevicePacket packet;
    VIMGLHeadlessRange *ranges = NULL;
    uint32_t range_count = 0;
    memcpy(&packet, slot, sizeof(packet));
    packet.result = -1;
    bool scatter = packet.action == 8 || packet.action == 10;
    if (packet.magic != VIM_GL_MAGIC ||
        packet.bytes > (scatter ? VIM_GL_SCATTER_MAX_BYTES : VIM_GL_CHUNK)) {
        goto reply;
    }
    bool opening = packet.action == 1;
    bool closing = packet.action == VIM_GL_HEADLESS_CLOSE;
    bool exchange = packet.action == 13;
    if ((opening && (packet.token || packet.bytes ||
                     (packet.reserved & ~VIM_GL_HEADLESS_RAM_CAPABILITIES) ||
                     s->clients[index].stream || !s->next_token)) ||
        (!opening && (!s->clients[index].stream ||
                      packet.token != s->clients[index].token)) ||
        (closing && (packet.bytes || packet.reserved)) ||
        (exchange && (!packet.bytes || packet.bytes > VIM_GL_EXCHANGE_LIMIT ||
                      !packet.reserved || packet.reserved > VIM_GL_EXCHANGE_LIMIT)) ||
        (scatter && (!packet.bytes || !packet.reserved ||
                     packet.reserved > VIM_GL_RAM_SCATTER_MAX_RANGES ||
                     !(s->clients[index].capabilities & VIM_GL_RESULT_CAP_RAM_SCATTER) ||
                     (packet.action == 10 &&
                      !(s->clients[index].capabilities & VIM_GL_RESULT_CAP_ROW_UPLOAD)))) ||
        ((!opening && !closing && !exchange && !scatter) &&
         ((packet.action != 2 && packet.action != 3) ||
          !packet.bytes || packet.reserved))) {
        goto reply;
    }
    uint8_t *scratch = s->clients[index].scratch;
    if (packet.action == 2 || exchange) {
        memcpy(scratch, slot + sizeof(packet), packet.bytes);
    }
    if (scatter) {
        /* Snapshot every descriptor before checking it or releasing BQL.
         * Only prepared ordinary guest RAM is accepted, never MMIO, ROM,
         * mailbox aliases, task pointers or address_space_map bounce copies. */
        memcpy(scratch, slot + sizeof(packet), packet.reserved *
               sizeof(struct VIMGLScatterMemorySegment));
        range_count = packet.reserved;
        ranges = g_new0(VIMGLHeadlessRange, range_count);
        uint64_t total = 0;
        for (uint32_t i = 0; i < range_count; i++) {
            uint8_t *entry = scratch + i * sizeof(struct VIMGLScatterMemorySegment);
            uint64_t address = ldq_le_p(entry);
            uint32_t bytes = ldl_le_p(entry + 8), kind = ldl_le_p(entry + 12);
            if (kind != 1 || !bytes || total + bytes > packet.bytes) {
                goto reply;
            }
            ranges[i].bytes = bytes;
            ranges[i].pointer = vim_gl_headless_map_ram(s, address, bytes, packet.action == 8);
            if (!ranges[i].pointer) {
                goto reply;
            }
            total += bytes;
        }
        if (total != packet.bytes) {
            goto reply;
        }
    }
    void *stream = s->clients[index].stream;
    uint64_t epoch = s->epoch;
    DeviceState *device = DEVICE(s);
    bool guarded = device->mem_reentrancy_guard.engaged_in_io;
    s->clients[index].busy = true;
    if (closing) {
        /* close may free the stream while another vCPU requests reset. */
        s->clients[index].stream = NULL;
    }
    /* Native GL and a bounded ring can wait. Do not block other vCPUs, PVG,
     * guest input or reset behind the BQL. Reset shuts down busy streams and
     * their submitting caller retires them after it regains the BQL. */
    /* A different vCPU's endpoint request during the native wait is not
     * recursive MMIO. Match the SVGA transport's scoped guard handling;
     * retaining this flag across BQL release silently rejects its doorbell. */
    device->mem_reentrancy_guard.engaged_in_io = false;
    bql_unlock();
    if (opening) {
        stream = s->api->open();
        packet.result = stream ? 0 : -1;
    } else if (closing) {
        packet.result = s->api->close(stream);
        stream = NULL;
    } else if (scatter) {
        packet.result = vim_gl_headless_scatter(s, stream, ranges, range_count,
                                               packet.action == 10);
    } else if (packet.action == 2 || exchange) {
        size_t done = 0;
        while (done < packet.bytes) {
            ssize_t n = s->api->write(stream, scratch + done, packet.bytes - done);
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n <= 0) {
                break;
            }
            done += n;
        }
        if (done == packet.bytes) {
            packet.result = exchange ? VIMGLInprocessReadExpected(s->api, stream,
                scratch, packet.reserved) : done;
        }
    } else {
        packet.result = s->api->read(stream, scratch, packet.bytes);
    }
    bql_lock();
    g_assert(!device->mem_reentrancy_guard.engaged_in_io);
    if (epoch != s->epoch) {
        /* Reset invalidated this submission. Never publish stale bytes into
         * the new generation or leave the old interpreter alive. */
        if (stream) {
            s->api->shutdown(stream);
            bql_unlock();
            s->api->close(stream);
            bql_lock();
        }
        device->mem_reentrancy_guard.engaged_in_io = guarded;
        s->clients[index].stream = NULL;
        s->clients[index].token = 0;
        s->clients[index].busy = false;
        vim_gl_headless_unmap(ranges, range_count);
        return;
    }
    device->mem_reentrancy_guard.engaged_in_io = guarded;
    s->clients[index].busy = false;
    s->clients[index].stream = stream;
    if (opening && stream) {
        packet.token = s->clients[index].token = s->next_token++;
        s->clients[index].capabilities = packet.reserved & vim_gl_headless_ram_capabilities(s);
        packet.reserved = s->clients[index].capabilities |
            VIM_GL_RESULT_CAP_EXCHANGE | VIM_GL_RESULT_CAP_DRAINED_CLOSE;
    } else if (closing) {
        s->clients[index].token = 0;
        s->clients[index].capabilities = 0;
    } else if ((packet.action == 3 || exchange) && packet.result > 0) {
        memcpy(slot + sizeof(packet), scratch, packet.result);
    }
reply:
    vim_gl_headless_unmap(ranges, range_count);
    memcpy(slot, &packet, sizeof(packet));
    memory_region_set_dirty(&s->mailbox,
        (size_t)index * VIM_GL_HEADLESS_SLOT_BYTES, sizeof(packet) +
        ((packet.action == 3 || packet.action == 13) && packet.result > 0 ? packet.result : 0));
}

static uint64_t vim_gl_headless_read(void *opaque, hwaddr address, unsigned size)
{
    VIMGLHeadless *s = opaque;
    switch (address) {
    case VIM_GL_HEADLESS_MAGIC_REGISTER: return VIM_GL_MAGIC;
    case VIM_GL_HEADLESS_VERSION_REGISTER: return VIM_GL_HEADLESS_VERSION;
    case VIM_GL_HEADLESS_CAPABILITIES_REGISTER: return vim_gl_headless_ram_capabilities(s);
    default: return 0;
    }
}
static void vim_gl_headless_write(void *opaque, hwaddr address, uint64_t value, unsigned size)
{
    if (address == VIM_GL_HEADLESS_DOORBELL) {
        vim_gl_headless_submit(opaque, value);
    }
}
static const MemoryRegionOps vim_gl_headless_ops = {
    .read = vim_gl_headless_read,
    .write = vim_gl_headless_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl = { .min_access_size = 4, .max_access_size = 4 },
};
static void vim_gl_headless_reset(DeviceState *device)
{
    VIMGLHeadless *s = VIM_GL_HEADLESS(device);
    ++s->epoch;
    for (unsigned i = 0; i < VIM_GL_HEADLESS_SLOTS; ++i) {
        if (s->clients[i].stream) {
            s->api->shutdown(s->clients[i].stream);
            if (!s->clients[i].busy) {
                s->api->close(s->clients[i].stream);
                s->clients[i].stream = NULL;
                s->clients[i].token = 0;
            }
        }
    }
}
static void vim_gl_headless_realize(PCIDevice *device, Error **errp)
{
    VIMGLHeadless *s = VIM_GL_HEADLESS(device);
    const char *path = getenv("VIM_PASSTHROUGH_OPENGL_INPROCESS_LIBRARY");
    if (path && g_path_is_absolute(path)) {
        s->library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    }
    VIMGLGetInprocessAPI get = s->library ?
        (VIMGLGetInprocessAPI)dlsym(s->library, "VIMPassthroughOpenGLInprocessGetAPI") : NULL;
    s->api = get ? get(VIM_GL_INPROCESS_ABI, sizeof(*s->api)) : NULL;
    if (!s->api || s->api->version != VIM_GL_INPROCESS_ABI ||
        s->api->bytes != sizeof(*s->api) || !s->api->open || !s->api->close ||
        !s->api->shutdown || !s->api->read || !s->api->write) {
        error_setg(errp, "Headless GL requires the ABI-2 native executor; no network fallback");
        return;
    }
    s->next_token = 1;
    memory_region_init_io(&s->registers, OBJECT(s), &vim_gl_headless_ops, s,
                          "vim-gl-headless-registers", 4096);
    memory_region_init_ram(&s->mailbox, OBJECT(s), "vim-gl-headless-mailbox",
                          VIM_GL_HEADLESS_MAILBOX_BYTES, &error_abort);
    pci_register_bar(device, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->registers);
    pci_register_bar(device, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mailbox);
    for (unsigned i = 0; i < VIM_GL_HEADLESS_SLOTS; ++i) {
        s->clients[i].scratch = g_malloc(VIM_GL_CHUNK);
    }
}
static const VMStateDescription vim_gl_headless_vmstate = {
    .name = TYPE_VIM_GL_HEADLESS,
    .unmigratable = 1,
};
static void vim_gl_headless_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);
    pc->realize = vim_gl_headless_realize;
    pc->vendor_id = VIM_GL_HEADLESS_VENDOR;
    pc->device_id = VIM_GL_HEADLESS_DEVICE;
    pc->revision = VIM_GL_HEADLESS_VERSION;
    pc->class_id = PCI_CLASS_OTHERS;
    dc->desc = "Headless PassthroughOpenGL command transport (no display)";
    dc->hotpluggable = false;
    dc->vmsd = &vim_gl_headless_vmstate;
    device_class_set_legacy_reset(dc, vim_gl_headless_reset);
}
static const TypeInfo vim_gl_headless_type = {
    .name = TYPE_VIM_GL_HEADLESS,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(VIMGLHeadless),
    .class_init = vim_gl_headless_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { }
    },
};
static void vim_gl_headless_register(void) { type_register_static(&vim_gl_headless_type); }
type_init(vim_gl_headless_register)
