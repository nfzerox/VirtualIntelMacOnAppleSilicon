
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"
#include "qemu/module.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/usb/usb.h"
#include "migration/vmstate.h"
#include "desc.h"
#include "chardev/char-fe.h"
#include "chardev/char.h"
#include "qom/object.h"
#include "vim-camera-stream.h"

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT,
    STR_SERIAL,
    STR_NAME0,
};

struct USBCameraState {
    USBDevice dev;
    CharFrontend chr;
    char *names_path;
    unsigned nfunctions;
    char *names[VIM_CAMERA_MAX_FUNCTIONS];
    VIMCameraStream stream[VIM_CAMERA_MAX_FUNCTIONS];
    VIMCameraFeed feed;
    VIMCameraIngress ingress;
    VIMCameraDirectory directory;
    uint8_t vc_header[VIM_CAMERA_MAX_FUNCTIONS][13];
    uint8_t vs_header[VIM_CAMERA_MAX_FUNCTIONS][14];
    uint8_t isight_format[27];
    uint8_t isight_frame[30];
    uint8_t frame_desc[30];
    uint8_t frame_tall_desc[30];
    uint32_t fps;
    USBDescEndpoint eps_high[VIM_CAMERA_MAX_FUNCTIONS];
    USBDescEndpoint eps_full[VIM_CAMERA_MAX_FUNCTIONS];
    USBDescEndpoint status_high;
    USBDescEndpoint status_full;
    USBDescOther control_descs[VIM_CAMERA_MAX_FUNCTIONS][4];
    USBDescOther stream_descs[VIM_CAMERA_MAX_FUNCTIONS][5];
    USBDescIface ifaces_high[VIM_CAMERA_MAX_FUNCTIONS * 3];
    USBDescIface ifaces_full[VIM_CAMERA_MAX_FUNCTIONS * 3];
    USBDescIfaceAssoc groups_high[VIM_CAMERA_MAX_FUNCTIONS];
    USBDescIfaceAssoc groups_full[VIM_CAMERA_MAX_FUNCTIONS];
    USBDescConfig config_high;
    USBDescConfig config_full;
    USBDescDevice device_high;
    USBDescDevice device_full;
    USBDesc desc;
    const char *strings[256];
    bool isight;
    bool driver;
    QEMUTimer *promote;
    QEMUTimer *reattach;
};

#define TYPE_USB_CAMERA "usb-camera"
OBJECT_DECLARE_SIMPLE_TYPE(USBCameraState, USB_CAMERA)

static const uint8_t isight_processing[] = {
    0x0b, 0x24, 0x05, 0x02, 0x01, 0x00, 0x00, 0x02, 0xc2, 0x01, 0x00
};
static const uint8_t isight_output[] = {
    0x09, 0x24, 0x03, 0x03, 0x01, 0x01, 0x00, 0x02, 0x00
};

static void usb_camera_send_status(USBCameraState *s, unsigned function)
{
    uint8_t bytes[2];
    int offset = 0;

    if (function >= s->nfunctions) {
        return;
    }
    bytes[0] = (uint8_t)function;
    bytes[1] = s->stream[function].streaming ? 1 : 0;
    while (offset < 2) {
        int wrote = qemu_chr_fe_write(&s->chr, bytes + offset, 2 - offset);
        if (wrote <= 0) {
            break;
        }
        offset += wrote;
    }
}

static void usb_camera_send_driver(USBCameraState *s)
{
    uint8_t bytes[2] = { VIM_CAMERA_STATUS_DRIVER, s->driver ? 1 : 0 };
    int offset = 0;

    while (offset < 2) {
        int wrote = qemu_chr_fe_write(&s->chr, bytes + offset, 2 - offset);
        if (wrote <= 0) {
            break;
        }
        offset += wrote;
    }
}

static void usb_camera_set_driver(USBCameraState *s, bool driver)
{
    if (s->driver != driver) {
        s->driver = driver;
        usb_camera_send_driver(s);
    }
}

static void usb_camera_send_all_status(USBCameraState *s)
{
    unsigned i;

    for (i = 0; i < s->nfunctions; i++) {
        usb_camera_send_status(s, i);
    }
    usb_camera_send_driver(s);
}

static void usb_camera_note(USBCameraState *s, unsigned function, uint8_t before)
{
    if (function < s->nfunctions && s->stream[function].streaming != before) {
        usb_camera_send_status(s, function);
    }
}

static int usb_camera_function_for_interface(USBCameraState *s, unsigned interface)
{
    unsigned i;

    for (i = 0; i < s->nfunctions; i++) {
        if (interface == s->stream[i].control_interface ||
            interface == s->stream[i].stream_interface) {
            return (int)i;
        }
    }
    return -1;
}

static void usb_camera_fill_device(USBCameraState *s, int high)
{
    USBDescConfig *config = high ? &s->config_high : &s->config_full;
    USBDescDevice *device = high ? &s->device_high : &s->device_full;

    memset(config, 0, sizeof *config);
    config->bConfigurationValue = 1;
    config->bmAttributes = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER;
    config->bMaxPower = 50;
    memset(device, 0, sizeof *device);
    device->bcdUSB = 0x0200;
    device->bMaxPacketSize0 = 64;
    device->bNumConfigurations = 1;
    device->confs = config;
}

static void usb_camera_fill_video(USBCameraState *s, int high)
{
    USBDescIface *ifaces = high ? s->ifaces_high : s->ifaces_full;
    USBDescEndpoint *eps = high ? s->eps_high : s->eps_full;
    USBDescIfaceAssoc *groups = high ? s->groups_high : s->groups_full;
    USBDescConfig *config = high ? &s->config_high : &s->config_full;
    USBDescDevice *device = high ? &s->device_high : &s->device_full;
    uint16_t packet = VIM_CAMERA_PAYLOAD_HEADER + VIM_CAMERA_PAYLOAD_BYTES;
    unsigned i;

    usb_camera_fill_device(s, high);
    memset(ifaces, 0, sizeof(s->ifaces_high));
    memset(eps, 0, sizeof(s->eps_high));
    memset(groups, 0, sizeof(s->groups_high));
    for (i = 0; i < s->nfunctions; i++) {
        USBDescIface *control = &ifaces[i * 3];
        USBDescIface *off = &ifaces[i * 3 + 1];
        USBDescIface *on = &ifaces[i * 3 + 2];
        uint8_t name = (uint8_t)(STR_NAME0 + i);

        s->control_descs[i][0].data = s->vc_header[i];
        s->control_descs[i][1].data = vim_camera_camera_terminal;
        s->control_descs[i][2].data = vim_camera_output_terminal;
        s->stream_descs[i][0].data = s->vs_header[i];
        s->stream_descs[i][1].data = vim_camera_format_uncompressed;
        s->stream_descs[i][2].data = s->frame_desc;
        s->stream_descs[i][3].data = s->frame_tall_desc;
        eps[i].bEndpointAddress = USB_DIR_IN | (uint8_t)(i + 1);
        eps[i].bmAttributes = 0x05;
        eps[i].wMaxPacketSize = high ? VIM_CAMERA_HB_ENDPOINT_SIZE : packet;
        eps[i].bInterval = 1;

        control->bInterfaceNumber = (uint8_t)(2 * i);
        control->bInterfaceClass = VIM_CAMERA_CLASS_VIDEO;
        control->bInterfaceSubClass = VIM_CAMERA_SUBCLASS_CONTROL;
        control->iInterface = name;
        control->ndesc = 3;
        control->descs = s->control_descs[i];

        off->bInterfaceNumber = (uint8_t)(2 * i + 1);
        off->bInterfaceClass = VIM_CAMERA_CLASS_VIDEO;
        off->bInterfaceSubClass = VIM_CAMERA_SUBCLASS_STREAMING;
        off->iInterface = name;
        off->ndesc = 4;
        off->descs = s->stream_descs[i];

        on->bInterfaceNumber = (uint8_t)(2 * i + 1);
        on->bAlternateSetting = 1;
        on->bNumEndpoints = 1;
        on->bInterfaceClass = VIM_CAMERA_CLASS_VIDEO;
        on->bInterfaceSubClass = VIM_CAMERA_SUBCLASS_STREAMING;
        on->iInterface = name;
        on->eps = &eps[i];

        groups[i].bFirstInterface = (uint8_t)(2 * i);
        groups[i].bInterfaceCount = 2;
        groups[i].bFunctionClass = VIM_CAMERA_CLASS_VIDEO;
        groups[i].bFunctionSubClass = VIM_CAMERA_COLLECTION_SUBCLASS;
        groups[i].iFunction = name;
        groups[i].nif = 3;
        groups[i].ifs = control;
    }
    config->bNumInterfaces = (uint8_t)(s->nfunctions * 2);
    config->nif_groups = (uint8_t)s->nfunctions;
    config->if_groups = groups;
    device->bDeviceClass = VIM_CAMERA_DEVICE_CLASS;
    device->bDeviceSubClass = VIM_CAMERA_DEVICE_SUBCLASS;
    device->bDeviceProtocol = VIM_CAMERA_DEVICE_PROTOCOL;
}

static void usb_camera_fill_isight(USBCameraState *s)
{
    int speed;

    s->desc.id.idVendor = 0x05ac;
    s->desc.id.idProduct = 0x8300;
    s->desc.id.bcdDevice = 0x0001;
    s->strings[STR_MANUFACTURER] = "Micron";
    s->strings[STR_PRODUCT] = "Built-in iSight";
    s->vs_header[0][6] = 0x82;
    memcpy(s->isight_format, vim_camera_format_uncompressed, sizeof s->isight_format);
    memcpy(s->isight_format + 5, "UYVY", 4);
    s->isight_format[4] = 1;
    s->vs_header[0][4] = 0x47;
    s->stream_descs[0][1].data = s->isight_format;
    memcpy(s->isight_frame, vim_camera_frame_tall, sizeof s->isight_frame);
    s->isight_frame[3] = 1;
    vim_camera_frame_set_rate(s->isight_frame, s->fps < 10 ? s->fps : 10);
    s->stream_descs[0][2].data = s->isight_frame;
    s->vc_header[0][7] = 0x40;
    s->vc_header[0][8] = 0x4b;
    s->vc_header[0][9] = 0xce;
    s->vc_header[0][10] = 0x00;
    s->vc_header[0][5] = 0x33;
    s->vs_header[0][8] = 0x03;
    s->control_descs[0][2].data = isight_processing;
    s->control_descs[0][3].data = isight_output;
    for (speed = 0; speed < 2; speed++) {
        USBDescIface *ifaces = speed ? s->ifaces_high : s->ifaces_full;
        USBDescEndpoint *isoc = speed ? &s->eps_high[0] : &s->eps_full[0];
        USBDescEndpoint *status = speed ? &s->status_high : &s->status_full;

        ifaces[0].ndesc = 4;
        ifaces[1].ndesc = 3;
        status->bEndpointAddress = USB_DIR_IN | 1;
        status->bmAttributes = 0x03;
        status->wMaxPacketSize = 8;
        status->bInterval = 10;
        ifaces[0].bNumEndpoints = 1;
        ifaces[0].eps = status;
        isoc->bEndpointAddress = USB_DIR_IN | 2;
        isoc->wMaxPacketSize = speed ? VIM_CAMERA_ISIGHT_PACKET : 1023;
        isoc->bInterval = 1;
        ifaces[2].eps = isoc;
    }
}

static int usb_camera_load_names(USBCameraState *s, Error **errp)
{
    gchar *contents = NULL;
    gchar **lines;
    gsize length = 0;
    unsigned i;

    s->nfunctions = 0;
    if (s->names_path && s->names_path[0]) {
        if (!g_file_get_contents(s->names_path, &contents, &length, NULL)) {
            error_setg(errp, "usb-camera could not read camera names");
            return -1;
        }
        lines = g_strsplit(contents, "\n", 0);
        g_free(contents);
        for (i = 0; lines && lines[i] && s->nfunctions < VIM_CAMERA_MAX_FUNCTIONS; i++) {
            g_strstrip(lines[i]);
            if (!lines[i][0]) {
                continue;
            }
            s->names[s->nfunctions++] = g_strdup(lines[i]);
        }
        g_strfreev(lines);
    }
    if (!s->nfunctions) {
        s->names[0] = g_strdup("Pentamirror Driver");
        s->nfunctions = 1;
    }
    if (s->isight) {
        s->nfunctions = 1;
    }
    s->directory.count = s->nfunctions;
    s->directory.mode = 0;
    s->directory.preferred = 0;
    for (i = 0; i < s->nfunctions; i++) {
        s->directory.present[i] = 1;
        g_strlcpy(s->directory.name[i], s->names[i], sizeof s->directory.name[i]);
    }
    return 0;
}

static void usb_camera_remember_directory(USBCameraState *s, const VIMCameraDirectory *directory)
{
    unsigned i;

    s->directory = *directory;
    for (i = 0; i < directory->count && i < VIM_CAMERA_MAX_FUNCTIONS; i++) {
        g_free(s->names[i]);
        s->names[i] = g_strdup(directory->name[i]);
        s->strings[STR_NAME0 + i] = s->names[i];
    }
}

static void usb_camera_build(USBCameraState *s)
{
    unsigned i;

    memset(s->strings, 0, sizeof s->strings);
    s->strings[STR_MANUFACTURER] = "Virtual Intel Mac";
    s->strings[STR_PRODUCT] = "Pentamirror Driver";
    s->strings[STR_SERIAL] = "1";
    if (s->fps < 1) s->fps = 1;
    if (s->fps > 30) s->fps = 30;
    memcpy(s->frame_desc, vim_camera_frame_uncompressed, sizeof s->frame_desc);
    memcpy(s->frame_tall_desc, vim_camera_frame_tall, sizeof s->frame_tall_desc);
    vim_camera_frame_set_rate(s->frame_desc, s->fps);
    vim_camera_frame_set_rate(s->frame_tall_desc, s->fps);
    for (i = 0; i < s->nfunctions; i++) {
        vim_camera_function_headers(i, s->vc_header[i], s->vs_header[i]);
        s->strings[STR_NAME0 + i] = s->names[i];
        vim_camera_stream_init(&s->stream[i]);
        s->stream[i].control_interface = (uint8_t)(2 * i);
        s->stream[i].stream_interface = (uint8_t)(2 * i + 1);
        s->stream[i].isight = s->isight ? 1 : 0;
        if (!s->isight) {
            s->stream[i].max_payload = VIM_CAMERA_HB_PACKET - VIM_CAMERA_PAYLOAD_HEADER;
        }
        s->stream[i].interval = 10000000u / (s->isight && s->fps > 10 ? 10 : s->fps);
    }
    memset(&s->desc, 0, sizeof s->desc);
    s->desc.id.idVendor = VIM_CAMERA_VENDOR_ID;
    s->desc.id.idProduct = VIM_CAMERA_PRODUCT_ID;
    s->desc.id.bcdDevice = VIM_CAMERA_BCD_DEVICE;
    usb_camera_fill_video(s, 1);
    usb_camera_fill_video(s, 0);
    if (s->isight) {
        usb_camera_fill_isight(s);
    }
    s->desc.id.iManufacturer = STR_MANUFACTURER;
    s->desc.id.iProduct = STR_PRODUCT;
    s->desc.id.iSerialNumber = STR_SERIAL;
    s->desc.full = &s->device_full;
    s->desc.high = &s->device_high;
    s->desc.str = (const char *const *)s->strings;
}

static int usb_camera_can_read(void *opaque)
{
    USBCameraState *s = opaque;
    if (!s->dev.attached) {
        return 0;
    }
    return 4096;
}

static void usb_camera_read(void *opaque, const uint8_t *buf, int size)
{
    USBCameraState *s = opaque;
    int updated = 0;
    if (size > 0) {
        vim_camera_ingress(&s->ingress, &s->feed, s->stream, s->nfunctions, &s->directory,
            &updated, buf, (size_t)size);
        if (updated) {
            usb_camera_remember_directory(s, &s->directory);
        }
    }
}

static void usb_camera_event(void *opaque, QEMUChrEvent event)
{
    USBCameraState *s = opaque;
    if (event == CHR_EVENT_OPENED) {
        usb_camera_send_all_status(s);
    }
}

static void usb_camera_handle_reset(USBDevice *dev)
{
    USBCameraState *s = USB_CAMERA(dev);
    unsigned i;

    usb_camera_set_driver(s, false);
    for (i = 0; i < s->nfunctions; i++) {
        uint8_t before = s->stream[i].streaming;
        vim_camera_stream_set_interface(&s->stream[i], s->stream[i].stream_interface, 0);
        s->stream[i].committed = 0;
        s->stream[i].streaming = 0;
        usb_camera_note(s, i, before);
    }
}

static void usb_camera_reattach(void *opaque)
{
    USBCameraState *s = opaque;
    Error *err = NULL;

    if (s->dev.attached) {
        return;
    }
    usb_device_attach(&s->dev, &err);
    if (err) {
        error_report_err(err);
    }
}

static void usb_camera_promote(void *opaque)
{
    USBCameraState *s = opaque;

    if (!s->isight || s->desc.id.idProduct != 0x8300) {
        return;
    }
    s->desc.id.idProduct = 0x8501;
    s->desc.id.bcdDevice = 386;
    usb_desc_init(&s->dev);
    if (s->dev.attached) {
        usb_device_detach(&s->dev);
    }
    timer_mod(s->reattach, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 300);
}

static bool usb_camera_isight_control(USBCameraState *s, USBPacket *p,
    int request, int value, int length, uint8_t *data)
{
    const int loader = ((USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE) << 8) | 0xa0;

    if (!s->isight) {
        return false;
    }
    if (request == loader) {
        if (s->desc.id.idProduct == 0x8501) {
            p->status = USB_RET_STALL;
            return true;
        }
        if (value == 0xe600 && length >= 1 && data && data[0] == 0) {
            timer_mod(s->promote, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
        }
        p->actual_length = length < 0 ? 0 : length;
        return true;
    }
    if (s->desc.id.idProduct == 0x8501 && request == 0x40f0) {
        p->actual_length = 0;
        return true;
    }
    return false;
}

static void usb_camera_handle_control(USBDevice *dev, USBPacket *p,
    int request, int value, int index, int length, uint8_t *data)
{
    USBCameraState *s = USB_CAMERA(dev);
    int function, ret;
    uint8_t before;

    if (((request >> 8) & USB_TYPE_MASK) != USB_TYPE_STANDARD) {
        usb_camera_set_driver(s, true);
    }
    if (usb_camera_isight_control(s, p, request, value, length, data)) {
        return;
    }
    if (request == (DeviceRequest | USB_REQ_GET_DESCRIPTOR) &&
        (value >> 8) == USB_DT_STRING) {
        unsigned name = (unsigned)(value & 0xff);
        if (name >= STR_NAME0 && name < STR_NAME0 + s->nfunctions) {
            ret = vim_camera_usb_string(s->names[name - STR_NAME0], data,
                length < 0 ? 0 : (size_t)length);
            if (ret < 0) {
                p->status = USB_RET_STALL;
                return;
            }
            p->actual_length = ret;
            return;
        }
    }
    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        if (request == (DeviceOutRequest | USB_REQ_SET_CONFIGURATION) &&
            !(s->isight && s->desc.id.idProduct == 0x8300)) {
            usb_camera_set_driver(s, value != 0);
        }
        return;
    }
    function = usb_camera_function_for_interface(s, (unsigned)(index & 0xff));
    if (function < 0) {
        p->status = USB_RET_STALL;
        return;
    }
    before = s->stream[function].streaming;
    ret = vim_camera_stream_control(&s->stream[function], (uint16_t)request,
        (uint16_t)value, (uint16_t)index, data, length < 0 ? 0 : (unsigned)length);
    usb_camera_note(s, (unsigned)function, before);
    if (ret < 0) {
        p->status = USB_RET_STALL;
        return;
    }
    p->actual_length = ret;
}

static void usb_camera_handle_data(USBDevice *dev, USBPacket *p)
{
    USBCameraState *s = USB_CAMERA(dev);
    uint8_t packet[VIM_CAMERA_MAX_PACKET];
    int got;
    unsigned function;

    if (p->pid != USB_TOKEN_IN || !p->ep || p->ep->nr < 1) {
        p->status = USB_RET_STALL;
        return;
    }
    if (s->isight) {
        if (p->ep->nr == 1) {
            p->status = USB_RET_NAK;
            return;
        }
        if (p->ep->nr != 2) {
            p->status = USB_RET_STALL;
            return;
        }
        function = 0;
    } else if ((unsigned)p->ep->nr > s->nfunctions) {
        p->status = USB_RET_STALL;
        return;
    } else {
        function = (unsigned)p->ep->nr - 1;
    }
    got = vim_camera_stream_iso(&s->stream[function], packet, (int)p->iov.size);
    if (got < 0) {
        p->status = USB_RET_STALL;
        return;
    }
    usb_packet_copy(p, packet, (size_t)got);
}

static void usb_camera_set_interface(USBDevice *dev, int iface, int old, int value)
{
    USBCameraState *s = USB_CAMERA(dev);
    int function;
    uint8_t before;

    (void)old;
    function = usb_camera_function_for_interface(s, (unsigned)iface);
    if (function < 0) {
        return;
    }
    before = s->stream[function].streaming;
    vim_camera_stream_set_interface(&s->stream[function], iface, value);
    usb_camera_note(s, (unsigned)function, before);
}

static void usb_camera_realize(USBDevice *dev, Error **errp)
{
    USBCameraState *s = USB_CAMERA(dev);

    if (!qemu_chr_fe_backend_connected(&s->chr)) {
        error_setg(errp, "usb-camera requires a chardev");
        return;
    }
    if (usb_camera_load_names(s, errp) < 0) {
        return;
    }
    usb_camera_build(s);
    if (s->isight) {
        s->promote = timer_new_ms(QEMU_CLOCK_VIRTUAL, usb_camera_promote, s);
        s->reattach = timer_new_ms(QEMU_CLOCK_VIRTUAL, usb_camera_reattach, s);
    }
    usb_camera_remember_directory(s, &s->directory);
    vim_camera_ingress_init(&s->ingress);
    dev->usb_desc = &s->desc;
    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    vim_camera_feed_init(&s->feed);
    qemu_chr_fe_set_handlers(&s->chr, usb_camera_can_read, usb_camera_read,
        usb_camera_event, NULL, s, NULL, true);
}

static void usb_camera_unrealize(USBDevice *dev)
{
    USBCameraState *s = USB_CAMERA(dev);
    unsigned i;

    qemu_chr_fe_set_handlers(&s->chr, NULL, NULL, NULL, NULL, NULL, NULL, true);
    if (s->promote) {
        timer_free(s->promote);
        s->promote = NULL;
    }
    if (s->reattach) {
        timer_free(s->reattach);
        s->reattach = NULL;
    }
    vim_camera_feed_fini(&s->feed);
    for (i = 0; i < s->nfunctions; i++) {
        vim_camera_stream_fini(&s->stream[i]);
    }
    for (i = 0; i < VIM_CAMERA_MAX_FUNCTIONS; i++) {
        g_free(s->names[i]);
        s->names[i] = NULL;
    }
    s->nfunctions = 0;
}

static const VMStateDescription vmstate_usb_camera = {
    .name = TYPE_USB_CAMERA,
    .unmigratable = 1,
};

static const Property usb_camera_properties[] = {
    DEFINE_PROP_CHR("chardev", USBCameraState, chr),
    DEFINE_PROP_BOOL("isight", USBCameraState, isight, false),
    DEFINE_PROP_STRING("names", USBCameraState, names_path),
    DEFINE_PROP_UINT32("fps", USBCameraState, fps, VIM_CAMERA_FPS),
};

static void usb_camera_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *k = USB_DEVICE_CLASS(klass);
    (void)data;
    dc->vmsd = &vmstate_usb_camera;
    device_class_set_props(dc, usb_camera_properties);
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    k->product_desc = "Pentamirror Driver";
    k->realize = usb_camera_realize;
    k->unrealize = usb_camera_unrealize;
    k->handle_attach = usb_desc_attach;
    k->handle_reset = usb_camera_handle_reset;
    k->handle_control = usb_camera_handle_control;
    k->handle_data = usb_camera_handle_data;
    k->set_interface = usb_camera_set_interface;
}

static const TypeInfo usb_camera_info = {
    .name = TYPE_USB_CAMERA,
    .parent = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBCameraState),
    .class_init = usb_camera_class_init,
};

static void usb_camera_register_types(void)
{
    type_register_static(&usb_camera_info);
}

type_init(usb_camera_register_types)
