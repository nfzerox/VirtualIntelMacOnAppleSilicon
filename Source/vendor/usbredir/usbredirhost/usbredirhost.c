/* usbredirhost.c usb network redirection usb host code.

   Copyright 2010-2012 Red Hat, Inc.

   Red Hat Authors:
   Hans de Goede <hdegoede@redhat.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include "usbredirhost.h"

#define MAX_ENDPOINTS        32
#define MAX_INTERFACES       32 /* Max 32 endpoints and thus interfaces */
#define CTRL_TIMEOUT       5000 /* USB specifies a 5 second max timeout */
#define BULK_TIMEOUT          0 /* No timeout for bulk transfers */
#define ISO_TIMEOUT        1000
#define INTERRUPT_TIMEOUT     0 /* No timeout for interrupt transfers */

#define MAX_TRANSFER_COUNT        16
#define MAX_PACKETS_PER_TRANSFER  32
#define INTERRUPT_TRANSFER_COUNT   5
/* Special packet_idx value indicating a submitted transfer */
#define SUBMITTED_IDX             -1

/* quirk flags */
#define QUIRK_DO_NOT_RESET    0x01

/* Macros to go from an endpoint address to an index for our ep array */
#define EP2I(ep_address) (((ep_address & 0x80) >> 3) | (ep_address & 0x0f))
#define I2EP(i) (((i & 0x10) << 3) | (i & 0x0f))

/* Locking convenience macros */
#define LOCK(host) \
    do { \
        if ((host)->lock) \
            (host)->parser->lock_func((host)->lock); \
    } while (0)

#define UNLOCK(host) \
    do { \
        if ((host)->lock) \
            (host)->parser->unlock_func((host)->lock); \
    } while (0)

#define FLUSH(host) \
    do { \
        if ((host)->flush_writes_func) \
            (host)->flush_writes_func((host)->func_priv); \
    } while (0)

#define CLAMP(val, min, max) \
	((val) < (min) ? (min) : ((val) > (max) ? (max) : (val)))

struct usbredirtransfer {
    struct usbredirhost *host;        /* Back pointer to the the redirhost */
    struct libusb_transfer *transfer; /* Back pointer to the libusb transfer */
    uint64_t id;
    uint8_t cancelled;
    int packet_idx;
    union {
        struct usb_redir_control_packet_header control_packet;
        struct usb_redir_bulk_packet_header bulk_packet;
        struct usb_redir_iso_packet_header iso_packet;
        struct usb_redir_interrupt_packet_header interrupt_packet;
    };
    struct usbredirtransfer *next;
    struct usbredirtransfer *prev;
};

struct usbredirhost_ep {
    uint8_t type;
    uint8_t interval;
    uint8_t interface;
    uint8_t warn_on_drop;
    uint8_t stream_started;
    uint8_t pkts_per_transfer;
    uint8_t transfer_count;
    int out_idx;
    int drop_packets;
    int max_packetsize;
    unsigned int max_streams;
    struct usbredirtransfer *transfer[MAX_TRANSFER_COUNT];
};

struct usbredirhost {
    struct usbredirparser *parser;

    void *lock;
    void *disconnect_lock;

    usbredirparser_log log_func;
    usbredirparser_read read_func;
    usbredirparser_write write_func;
    usbredirhost_flush_writes flush_writes_func;
    usbredirhost_buffered_output_size buffered_output_size_func;
    void *func_priv;
    int verbose;
    int flags;
    unsigned int scsi_metadata_left;
    libusb_context *ctx;
    libusb_device *dev;
    libusb_device_handle *handle;
    struct libusb_device_descriptor desc;
    struct libusb_config_descriptor *config;
    int quirks;
    int restore_config;
    int claimed;
    int reset;
    int disconnected;
    int read_status;
    int cancels_pending;
    int wait_disconnect;
    int connect_pending;
    struct usbredirhost_ep endpoint[MAX_ENDPOINTS];
    uint8_t alt_setting[MAX_INTERFACES];
    struct usbredirtransfer transfers_head;
    struct usbredirfilter_rule *filter_rules;
    int filter_rules_count;
    struct {
        uint64_t higher;
        uint64_t lower;
        bool dropping;
    } iso_threshold;
};

struct usbredirhost_dev_ids {
    int vendor_id;
    int product_id;
};

static const struct usbredirhost_dev_ids usbredirhost_reset_blacklist[] = {
    { 0x1210, 0x001c },
    { 0x2798, 0x0001 },
    { -1, -1 } /* Terminating Entry */
};

static void
#if defined __MINGW_PRINTF_FORMAT
__attribute__((format(__MINGW_PRINTF_FORMAT, 3, 4)))
#elif defined __GNUC__
__attribute__((format(printf, 3, 4)))
#endif
va_log(struct usbredirhost *host, int level, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int n;

    if (level > host->verbose) {
        return;
    }

    n = sprintf(buf, "usbredirhost: ");
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);

    host->log_func(host->func_priv, level, buf);
}

#ifdef ERROR /* defined on WIN32 */
#undef ERROR
#endif
#define ERROR(...)   va_log(host, usbredirparser_error, __VA_ARGS__)
#define WARNING(...) va_log(host, usbredirparser_warning, __VA_ARGS__)
#define INFO(...)    va_log(host, usbredirparser_info, __VA_ARGS__)
#define DEBUG(...)   va_log(host, usbredirparser_debug, __VA_ARGS__)

static void usbredirhost_hello(void *priv, struct usb_redir_hello_header *h);
static void usbredirhost_reset(void *priv);
static void usbredirhost_set_configuration(void *priv, uint64_t id,
    struct usb_redir_set_configuration_header *set_configuration);
static void usbredirhost_get_configuration(void *priv, uint64_t id);
static void usbredirhost_set_alt_setting(void *priv, uint64_t id,
    struct usb_redir_set_alt_setting_header *set_alt_setting);
static void usbredirhost_get_alt_setting(void *priv, uint64_t id,
    struct usb_redir_get_alt_setting_header *get_alt_setting);
static void usbredirhost_start_iso_stream(void *priv, uint64_t id,
    struct usb_redir_start_iso_stream_header *start_iso_stream);
static void usbredirhost_stop_iso_stream(void *priv, uint64_t id,
    struct usb_redir_stop_iso_stream_header *stop_iso_stream);
static void usbredirhost_start_interrupt_receiving(void *priv, uint64_t id,
    struct usb_redir_start_interrupt_receiving_header *start_interrupt_receiving);
static void usbredirhost_stop_interrupt_receiving(void *priv, uint64_t id,
    struct usb_redir_stop_interrupt_receiving_header *stop_interrupt_receiving);
static void usbredirhost_alloc_bulk_streams(void *priv, uint64_t id,
    struct usb_redir_alloc_bulk_streams_header *alloc_bulk_streams);
static void usbredirhost_free_bulk_streams(void *priv, uint64_t id,
    struct usb_redir_free_bulk_streams_header *free_bulk_streams);
static void usbredirhost_cancel_data_packet(void *priv, uint64_t id);
static void usbredirhost_filter_reject(void *priv);
static void usbredirhost_filter_filter(void *priv,
    struct usbredirfilter_rule *rules, int rules_count);
static void usbredirhost_device_disconnect_ack(void *priv);
static void usbredirhost_start_bulk_receiving(void *priv, uint64_t id,
    struct usb_redir_start_bulk_receiving_header *start_bulk_receiving);
static void usbredirhost_stop_bulk_receiving(void *priv, uint64_t id,
    struct usb_redir_stop_bulk_receiving_header *stop_bulk_receiving);
static void usbredirhost_control_packet(void *priv, uint64_t id,
    struct usb_redir_control_packet_header *control_packet,
    uint8_t *data, int data_len);
static void usbredirhost_bulk_packet(void *priv, uint64_t id,
    struct usb_redir_bulk_packet_header *bulk_packet,
    uint8_t *data, int data_len);
static void usbredirhost_iso_packet(void *priv, uint64_t id,
    struct usb_redir_iso_packet_header *iso_packet,
    uint8_t *data, int data_len);
static void usbredirhost_interrupt_packet(void *priv, uint64_t id,
    struct usb_redir_interrupt_packet_header *interrupt_packet,
    uint8_t *data, int data_len);

static void LIBUSB_CALL usbredirhost_iso_packet_complete(
    struct libusb_transfer *libusb_transfer);
static void LIBUSB_CALL usbredirhost_buffered_packet_complete(
    struct libusb_transfer *libusb_transfer);
static int usbredirhost_cancel_pending_urbs(struct usbredirhost *host,
                                            int notify_guest);
static void usbredirhost_wait_for_cancel_completion(struct usbredirhost *host);
static void usbredirhost_clear_device(struct usbredirhost *host);

static void usbredirhost_log(void *priv, int level, const char *msg)
{
    struct usbredirhost *host = priv;

    host->log_func(host->func_priv, level, msg);
}

static int usbredirhost_read(void *priv, uint8_t *data, int count)
{
    struct usbredirhost *host = priv;

    if (host->read_status) {
        int ret = host->read_status;
        host->read_status = 0;
        return ret;
    }

    return host->read_func(host->func_priv, data, count);
}

static int usbredirhost_write(void *priv, uint8_t *data, int count)
{
    struct usbredirhost *host = priv;

    return host->write_func(host->func_priv, data, count);
}

/* Can be called both from parser read callbacks as well as from libusb
   packet completion callbacks */
static void usbredirhost_handle_disconnect(struct usbredirhost *host)
{
    /* Disconnect uses its own lock to avoid needing nesting capable locks */
    if (host->disconnect_lock) {
        host->parser->lock_func(host->disconnect_lock);
    }
    if (!host->disconnected) {
        INFO("device disconnected");
        usbredirparser_send_device_disconnect(host->parser);
        if (usbredirparser_peer_has_cap(host->parser,
                                        usb_redir_cap_device_disconnect_ack))
            host->wait_disconnect = 1;
        host->disconnected = 1;
    }
    if (host->disconnect_lock) {
        host->parser->unlock_func(host->disconnect_lock);
    }
}

/* One function to convert either a transfer status code, or a libusb error
   code to a usb_redir status. We handle both in one conversion function so
   that we can pass error codes as status codes to the completion handler
   in case of submission error (the codes don't overlap), using the completion
   handler to report back the status and cleanup as it would on completion of
   a successfully submitted transfer. */
static int libusb_status_or_error_to_redir_status(struct usbredirhost *host,
                                                  int status)
{
    switch (status) {
        case LIBUSB_TRANSFER_COMPLETED:
            return usb_redir_success;
        case LIBUSB_TRANSFER_ERROR:
            return usb_redir_ioerror;
        case LIBUSB_TRANSFER_TIMED_OUT:
            return usb_redir_timeout;
        case LIBUSB_TRANSFER_CANCELLED:
            return usb_redir_cancelled;
        case LIBUSB_TRANSFER_STALL:
            return usb_redir_stall;
        case LIBUSB_TRANSFER_NO_DEVICE:
            usbredirhost_handle_disconnect(host);
            return usb_redir_ioerror;
        case LIBUSB_TRANSFER_OVERFLOW:
            return usb_redir_babble;

        case LIBUSB_ERROR_INVALID_PARAM:
            return usb_redir_inval;
        case LIBUSB_ERROR_NO_DEVICE:
            usbredirhost_handle_disconnect(host);
            return usb_redir_ioerror;
        case LIBUSB_ERROR_TIMEOUT:
            return usb_redir_timeout;
        default:
            return usb_redir_ioerror;
    }
}

static void usbredirhost_set_max_packetsize(struct usbredirhost *host,
    uint8_t ep, uint16_t wMaxPacketSize)
{
    int maxp, mult = 1;

    maxp = wMaxPacketSize & 0x7ff;

    if (libusb_get_device_speed(host->dev) == LIBUSB_SPEED_HIGH &&
             host->endpoint[EP2I(ep)].type == usb_redir_type_iso) {
        switch ((wMaxPacketSize >> 11) & 3) {
        case 1:  mult = 2; break;
        case 2:  mult = 3; break;
        default: mult = 1; break;
        }
    }

    host->endpoint[EP2I(ep)].max_packetsize = maxp * mult;
}

static void usbredirhost_set_max_streams(struct usbredirhost *host,
    const struct libusb_endpoint_descriptor *endp)
{
#if LIBUSBX_API_VERSION >= 0x01000102
    struct libusb_ss_endpoint_companion_descriptor *endp_ss_comp;
    int max_streams, i = EP2I(endp->bEndpointAddress);

    host->endpoint[i].max_streams = 0;

    if (host->endpoint[i].type == usb_redir_type_bulk &&
            libusb_get_ss_endpoint_companion_descriptor(host->ctx, endp,
                &endp_ss_comp) == LIBUSB_SUCCESS) {
        max_streams = endp_ss_comp->bmAttributes & 0x1f;
        if (max_streams)
            host->endpoint[i].max_streams = 1 << max_streams;
        libusb_free_ss_endpoint_companion_descriptor(endp_ss_comp);
    }
#endif
}

/* Called from open/close and parser read callbacks */
static void usbredirhost_send_interface_n_ep_info(struct usbredirhost *host)
{
    int i;
    const struct libusb_interface_descriptor *intf_desc;
    struct usb_redir_ep_info_header ep_info;
    struct usb_redir_interface_info_header interface_info = { 0, };

    if (host->config)
        interface_info.interface_count = host->config->bNumInterfaces;

    for (i = 0; i < interface_info.interface_count; i++) {
        intf_desc =
            &host->config->interface[i].altsetting[host->alt_setting[i]];

        interface_info.interface[i] = intf_desc->bInterfaceNumber;
        interface_info.interface_class[i] = intf_desc->bInterfaceClass;
        interface_info.interface_subclass[i] = intf_desc->bInterfaceSubClass;
        interface_info.interface_protocol[i] = intf_desc->bInterfaceProtocol;
    }
    usbredirparser_send_interface_info(host->parser, &interface_info);

    for (i = 0; i < MAX_ENDPOINTS; i++) {
        ep_info.type[i] = host->endpoint[i].type;
        ep_info.interval[i] = host->endpoint[i].interval;
        ep_info.interface[i] = host->endpoint[i].interface;
        ep_info.max_packet_size[i] = host->endpoint[i].max_packetsize;
        ep_info.max_streams[i] = host->endpoint[i].max_streams;
    }
    usbredirparser_send_ep_info(host->parser, &ep_info);
}

/* Called from open/close and parser read callbacks */
static void usbredirhost_send_device_connect(struct usbredirhost *host)
{
    struct usb_redir_device_connect_header device_connect;
    enum libusb_speed speed;

    if (!host->disconnected) {
        ERROR("internal error sending device_connect but already connected");
        return;
    }

    if (!usbredirparser_have_peer_caps(host->parser) ||
            host->wait_disconnect) {
        host->connect_pending = 1;
        return;
    }

    speed = libusb_get_device_speed(host->dev);
    switch (speed) {
    case LIBUSB_SPEED_LOW:
        device_connect.speed = usb_redir_speed_low; break;
    case LIBUSB_SPEED_FULL:
        device_connect.speed = usb_redir_speed_full; break;
    case LIBUSB_SPEED_HIGH:
        device_connect.speed = usb_redir_speed_high; break;
    case LIBUSB_SPEED_SUPER:
        device_connect.speed = usb_redir_speed_super; break;
    default:
        device_connect.speed = usb_redir_speed_unknown;
    }
    device_connect.device_class = host->desc.bDeviceClass;
    device_connect.device_subclass = host->desc.bDeviceSubClass;
    device_connect.device_protocol = host->desc.bDeviceProtocol;
    device_connect.vendor_id = host->desc.idVendor;
    device_connect.product_id = host->desc.idProduct;
    device_connect.device_version_bcd = host->desc.bcdDevice;

    usbredirhost_send_interface_n_ep_info(host);
    usbredirparser_send_device_connect(host->parser, &device_connect);
    host->connect_pending = 0;
    host->disconnected = 0; /* The guest may now use the device */

    FLUSH(host);
}

/* Called from open/close and parser read callbacks */
static void usbredirhost_parse_interface(struct usbredirhost *host, int i)
{
    int j;
    const struct libusb_interface_descriptor *intf_desc;
    uint8_t ep_address;

    intf_desc =
        &host->config->interface[i].altsetting[host->alt_setting[i]];

    for (j = 0; j < intf_desc->bNumEndpoints; j++) {
        ep_address = intf_desc->endpoint[j].bEndpointAddress;
        host->endpoint[EP2I(ep_address)].type =
            intf_desc->endpoint[j].bmAttributes & LIBUSB_TRANSFER_TYPE_MASK;
        host->endpoint[EP2I(ep_address)].interval =
            intf_desc->endpoint[j].bInterval;
        host->endpoint[EP2I(ep_address)].interface =
            intf_desc->bInterfaceNumber;
        usbredirhost_set_max_packetsize(host, ep_address,
                                        intf_desc->endpoint[j].wMaxPacketSize);
        usbredirhost_set_max_streams(host, &intf_desc->endpoint[j]);
        host->endpoint[EP2I(ep_address)].warn_on_drop = 1;
    }
}

static void usbredirhost_parse_config(struct usbredirhost *host)
{
    int i;

    for (i = 0; i < MAX_ENDPOINTS; i++) {
        if ((i & 0x0f) == 0) {
            host->endpoint[i].type = usb_redir_type_control;
        } else {
            host->endpoint[i].type = usb_redir_type_invalid;
        }
        host->endpoint[i].interval = 0;
        host->endpoint[i].interface = 0;
        host->endpoint[i].max_packetsize = 0;
        host->endpoint[i].max_streams = 0;
    }

    for (i = 0; host->config && i < host->config->bNumInterfaces; i++) {
        usbredirhost_parse_interface(host, i);
    }
}

/* Called from open/close and parser read callbacks */
static int usbredirhost_claim(struct usbredirhost *host, int initial_claim)
{
    int i, n, r;

    if (host->config) {
        libusb_free_config_descriptor(host->config);
        host->config = NULL;
    }

    r = libusb_get_device_descriptor(host->dev, &host->desc);
    if (r < 0) {
        ERROR("could not get device descriptor: %s", libusb_error_name(r));
        return libusb_status_or_error_to_redir_status(host, r);
    }

    r = libusb_get_active_config_descriptor(host->dev, &host->config);
    if (r < 0 && r != LIBUSB_ERROR_NOT_FOUND) {
        ERROR("could not get descriptors for active configuration: %s",
              libusb_error_name(r));
        return libusb_status_or_error_to_redir_status(host, r);
    }
    if (host->config && host->config->bNumInterfaces > MAX_INTERFACES) {
        ERROR("usb decriptor has too much intefaces (%d > %d)",
              (int)host->config->bNumInterfaces, MAX_INTERFACES);
        return usb_redir_ioerror;
    }

    if (initial_claim) {
        if (host->config)
            host->restore_config = host->config->bConfigurationValue;
        else
            host->restore_config = -1; /* unconfigured */

        /* If the device is unconfigured and has only 1 config, we assume
           this is the result of the user doing "safely remove hardware",
           and we try to reset the device configuration to this config when
           we release the device, so that it becomes usable again. */
        if (host->restore_config == -1 && host->desc.bNumConfigurations == 1) {
            struct libusb_config_descriptor *config;

            r = libusb_get_config_descriptor(host->dev, 0, &config);
            if (r == 0) {
                host->restore_config = config->bConfigurationValue;
                libusb_free_config_descriptor(config);
            }
        }
    }

    /* All interfaces begin at alt setting 0 when (re)claimed */
    memset(host->alt_setting, 0, MAX_INTERFACES);

    host->claimed = 1;
#if LIBUSBX_API_VERSION >= 0x01000102
    libusb_set_auto_detach_kernel_driver(host->handle, 1);
#endif
    for (i = 0; host->config && i < host->config->bNumInterfaces; i++) {
        n = host->config->interface[i].altsetting[0].bInterfaceNumber;

#if LIBUSBX_API_VERSION < 0x01000102
        r = libusb_detach_kernel_driver(host->handle, n);
        if (r < 0 && r != LIBUSB_ERROR_NOT_FOUND
                  && r != LIBUSB_ERROR_NOT_SUPPORTED) {
            ERROR("could not detach driver from interface %d (configuration %d): %s",
                  n, host->config->bConfigurationValue, libusb_error_name(r));
            return libusb_status_or_error_to_redir_status(host, r);
        }
#endif

        r = libusb_claim_interface(host->handle, n);
        if (r < 0) {
            if (r == LIBUSB_ERROR_BUSY)
                ERROR("Device is in use by another application");
            else
                ERROR("could not claim interface %d (configuration %d): %s",
                      n, host->config->bConfigurationValue,
                      libusb_error_name(r));
            return libusb_status_or_error_to_redir_status(host, r);
        }
#ifdef __APPLE__
        /* Darwin capture detaches the driver without re-enumerating the
           device, and ResetDevice is not a configuration reset there. A host
           UAS/audio driver may have left a nonzero alternate setting active.
           Match the alt-0 state advertised below before using its endpoints;
           claiming an interface alone does not establish that state. */
        if (host->config->interface[i].num_altsetting > 1) {
            unsigned char alternate = 0;
            r = libusb_control_transfer(host->handle,
                LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_STANDARD |
                    LIBUSB_RECIPIENT_INTERFACE,
                LIBUSB_REQUEST_GET_INTERFACE, 0, n, &alternate, 1, 1000);
            if (r != 1) {
                ERROR("could not read initial alternate setting of interface %d: %s",
                      n, r < 0 ? libusb_error_name(r) : "short response");
                return usb_redir_ioerror;
            }
            if (alternate != 0) {
                r = libusb_set_interface_alt_setting(host->handle, n, 0);
                if (r != 0) {
                    ERROR("could not initialize alternate setting of interface %d: %s",
                          n, libusb_error_name(r));
                    return libusb_status_or_error_to_redir_status(host, r);
                }
                WARNING("initialized interface %d from host alternate setting %u to 0",
                        n, alternate);
            }
        }
#endif
    }

    usbredirhost_parse_config(host);
    return usb_redir_success;
}

/* Called from open/close and parser read callbacks */
static void usbredirhost_release(struct usbredirhost *host, int attach_drivers)
{
    int i, n, r, current_config = -1;

    if (!host->claimed)
        return;

#if LIBUSBX_API_VERSION >= 0x01000102
    /* We want to always do the attach ourselves because:
       1) For compound interfaces such as usb-audio we must first release all
          interfaces before we can attach the driver;
       2) When releasing interfaces before calling libusb_set_configuration,
          we don't want the kernel driver to get attached (our attach_drivers
          parameter is 0 in this case). */
    libusb_set_auto_detach_kernel_driver(host->handle, 0);
#endif

    for (i = 0; host->config && i < host->config->bNumInterfaces; i++) {
        n = host->config->interface[i].altsetting[0].bInterfaceNumber;

        r = libusb_release_interface(host->handle, n);
        if (r < 0 && r != LIBUSB_ERROR_NOT_FOUND
                  && r != LIBUSB_ERROR_NO_DEVICE) {
            ERROR("could not release interface %d (configuration %d): %s",
                  n, host->config->bConfigurationValue, libusb_error_name(r));
        }
    }

    if (!attach_drivers)
        return;

    host->claimed = 0;

    /* reset the device before re-binding the kernel drivers, so that the
       kernel drivers get the device in a clean state. */
    if (!(host->quirks & QUIRK_DO_NOT_RESET)) {
        r = libusb_reset_device(host->handle);
        if (r != 0) {
            /* if we're releasing the device because it was removed, resetting
             * will fail. Don't print a warning in this situation */
            if (r != LIBUSB_ERROR_NO_DEVICE) {
                ERROR("error resetting device: %s", libusb_error_name(r));
            }
            return;
        }
    }

    if (host->config)
        current_config = host->config->bConfigurationValue;

    if (current_config != host->restore_config) {
        r = libusb_set_configuration(host->handle, host->restore_config);
        if (r < 0)
            ERROR("could not restore configuration to %d: %s",
                  host->restore_config, libusb_error_name(r));
        return; /* set_config automatically binds drivers for the new config */
    }

    for (i = 0; host->config && i < host->config->bNumInterfaces; i++) {
        n = host->config->interface[i].altsetting[0].bInterfaceNumber;
        r = libusb_attach_kernel_driver(host->handle, n);
        if (r < 0 && r != LIBUSB_ERROR_NOT_FOUND /* No driver */
                  && r != LIBUSB_ERROR_NO_DEVICE /* Device unplugged */
                  && r != LIBUSB_ERROR_NOT_SUPPORTED /* Not supported */
                  && r != LIBUSB_ERROR_BUSY /* driver rebound already */) {
            ERROR("could not re-attach driver to interface %d (configuration %d): %s",
                  n, host->config->bConfigurationValue, libusb_error_name(r));
        }
    }
}

USBREDIR_VISIBLE
struct usbredirhost *usbredirhost_open(
    libusb_context *usb_ctx,
    libusb_device_handle *usb_dev_handle,
    usbredirparser_log log_func,
    usbredirparser_read  read_guest_data_func,
    usbredirparser_write write_guest_data_func,
    void *func_priv, const char *version, int verbose, int flags)
{
    return usbredirhost_open_full(usb_ctx, usb_dev_handle, log_func,
                                  read_guest_data_func, write_guest_data_func,
                                  NULL, NULL, NULL, NULL, NULL,
                                  func_priv, version, verbose, flags);
}

USBREDIR_VISIBLE
struct usbredirhost *usbredirhost_open_full(
    libusb_context *usb_ctx,
    libusb_device_handle *usb_dev_handle,
    usbredirparser_log log_func,
    usbredirparser_read  read_guest_data_func,
    usbredirparser_write write_guest_data_func,
    usbredirhost_flush_writes flush_writes_func,
    usbredirparser_alloc_lock alloc_lock_func,
    usbredirparser_lock lock_func,
    usbredirparser_unlock unlock_func,
    usbredirparser_free_lock free_lock_func,
    void *func_priv, const char *version, int verbose, int flags)
{
    struct usbredirhost *host;
    int parser_flags = usbredirparser_fl_usb_host;
    uint32_t caps[USB_REDIR_CAPS_SIZE] = { 0, };

    host = calloc(1, sizeof(*host));
    if (!host) {
        log_func(func_priv, usbredirparser_error,
            "usbredirhost error: Out of memory allocating usbredirhost");
        libusb_close(usb_dev_handle);
        return NULL;
    }

    host->ctx = usb_ctx;
    host->log_func = log_func;
    host->read_func = read_guest_data_func;
    host->write_func = write_guest_data_func;
    host->flush_writes_func = flush_writes_func;
    host->func_priv = func_priv;
    host->verbose = verbose;
    host->disconnected = 1; /* No device is connected initially */
    host->flags = flags;
    host->scsi_metadata_left = (flags & usbredirhost_fl_scsi_metadata) ? 256 : 0;
    host->parser = usbredirparser_create();
    if (!host->parser) {
        log_func(func_priv, usbredirparser_error,
            "usbredirhost error: Out of memory allocating usbredirparser");
        libusb_close(usb_dev_handle);
        usbredirhost_close(host);
        return NULL;
    }
    host->parser->priv = host;
    host->parser->log_func = usbredirhost_log;
    host->parser->read_func = usbredirhost_read;
    host->parser->write_func = usbredirhost_write;
    host->parser->hello_func = usbredirhost_hello;
    host->parser->reset_func = usbredirhost_reset;
    host->parser->set_configuration_func = usbredirhost_set_configuration;
    host->parser->get_configuration_func = usbredirhost_get_configuration;
    host->parser->set_alt_setting_func = usbredirhost_set_alt_setting;
    host->parser->get_alt_setting_func = usbredirhost_get_alt_setting;
    host->parser->start_iso_stream_func = usbredirhost_start_iso_stream;
    host->parser->stop_iso_stream_func = usbredirhost_stop_iso_stream;
    host->parser->start_interrupt_receiving_func =
        usbredirhost_start_interrupt_receiving;
    host->parser->stop_interrupt_receiving_func =
        usbredirhost_stop_interrupt_receiving;
    host->parser->alloc_bulk_streams_func = usbredirhost_alloc_bulk_streams;
    host->parser->free_bulk_streams_func = usbredirhost_free_bulk_streams;
    host->parser->cancel_data_packet_func = usbredirhost_cancel_data_packet;
    host->parser->filter_reject_func = usbredirhost_filter_reject;
    host->parser->filter_filter_func = usbredirhost_filter_filter;
    host->parser->device_disconnect_ack_func =
        usbredirhost_device_disconnect_ack;
    host->parser->start_bulk_receiving_func =
        usbredirhost_start_bulk_receiving;
    host->parser->stop_bulk_receiving_func =
        usbredirhost_stop_bulk_receiving;
    host->parser->control_packet_func = usbredirhost_control_packet;
    host->parser->bulk_packet_func = usbredirhost_bulk_packet;
    host->parser->iso_packet_func = usbredirhost_iso_packet;
    host->parser->interrupt_packet_func = usbredirhost_interrupt_packet;
    host->parser->alloc_lock_func = alloc_lock_func;
    host->parser->lock_func = lock_func;
    host->parser->unlock_func = unlock_func;
    host->parser->free_lock_func = free_lock_func;

    if (host->parser->alloc_lock_func) {
        host->lock = host->parser->alloc_lock_func();
        host->disconnect_lock = host->parser->alloc_lock_func();
    }

    if (flags & usbredirhost_fl_write_cb_owns_buffer) {
        parser_flags |= usbredirparser_fl_write_cb_owns_buffer;
    }

    usbredirparser_caps_set_cap(caps, usb_redir_cap_connect_device_version);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_filter);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_device_disconnect_ack);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_ep_info_max_packet_size);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_64bits_ids);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_32bits_bulk_length);
    usbredirparser_caps_set_cap(caps, usb_redir_cap_bulk_receiving);
#if LIBUSBX_API_VERSION >= 0x01000103
    usbredirparser_caps_set_cap(caps, usb_redir_cap_bulk_streams);
#endif

    usbredirparser_init(host->parser, version, caps, USB_REDIR_CAPS_SIZE,
                        parser_flags);

#if LIBUSB_API_VERSION >= 0x01000106
    int ret = libusb_set_option(host->ctx, LIBUSB_OPTION_LOG_LEVEL,
            CLAMP(host->verbose, LIBUSB_LOG_LEVEL_NONE, LIBUSB_LOG_LEVEL_DEBUG));
    if (ret != LIBUSB_SUCCESS) {
        ERROR("error setting libusb log level: %s", libusb_error_name(ret));
        usbredirhost_close(host);
        return NULL;
    }
#else
    libusb_set_debug(host->ctx, host->verbose);
#endif

    if (usbredirhost_set_device(host, usb_dev_handle) != usb_redir_success) {
        usbredirhost_close(host);
        return NULL;
    }

    FLUSH(host);

    return host;
}

USBREDIR_VISIBLE
void usbredirhost_close(struct usbredirhost *host)
{
    usbredirhost_clear_device(host);

    if (host->lock) {
        host->parser->free_lock_func(host->lock);
    }
    if (host->disconnect_lock) {
        host->parser->free_lock_func(host->disconnect_lock);
    }
    if (host->parser) {
        usbredirparser_destroy(host->parser);
    }
    free(host->filter_rules);
    free(host);
}

static int usbredirhost_reset_device(struct usbredirhost *host)
{
    int r;

    if (host->quirks & QUIRK_DO_NOT_RESET) {
        return 0;
    }

    r = libusb_reset_device(host->handle);
    if (r != 0) {
        ERROR("error resetting device: %s", libusb_error_name(r));
        usbredirhost_clear_device(host);
        return r;
    }

    host->reset = 1;
    return 0;
}

USBREDIR_VISIBLE
int usbredirhost_set_device(struct usbredirhost *host,
                             libusb_device_handle *usb_dev_handle)
{
    int i, r, status;

    usbredirhost_clear_device(host);

    if (!usb_dev_handle)
        return usb_redir_success;

    host->dev = libusb_get_device(usb_dev_handle);
    host->handle = usb_dev_handle;

    status = usbredirhost_claim(host, 1);
    if (status != usb_redir_success) {
        usbredirhost_clear_device(host);
        return status;
    }

    for (i = 0; usbredirhost_reset_blacklist[i].vendor_id != -1; i++) {
        if (host->desc.idVendor == usbredirhost_reset_blacklist[i].vendor_id &&
            host->desc.idProduct ==
                                usbredirhost_reset_blacklist[i].product_id) {
            host->quirks |= QUIRK_DO_NOT_RESET;
            break;
        }
    }

    /* The first thing almost any usb-guest does is a (slow) device-reset
       so lets do that before hand */
    r = usbredirhost_reset_device(host);
    if (r != 0) {
        return libusb_status_or_error_to_redir_status(host, r);
    }

    usbredirhost_send_device_connect(host);

    return usb_redir_success;
}

static void usbredirhost_clear_device(struct usbredirhost *host)
{
    if (!host->dev)
        return;

    if (usbredirhost_cancel_pending_urbs(host, 0))
        usbredirhost_wait_for_cancel_completion(host);

    usbredirhost_release(host, 1);

    if (host->config) {
        libusb_free_config_descriptor(host->config);
        host->config = NULL;
    }
    if (host->handle) {
        libusb_close(host->handle);
        host->handle = NULL;
    }

    host->connect_pending = 0;
    host->quirks = 0;
    host->dev = NULL;

    usbredirhost_handle_disconnect(host);
    FLUSH(host);
}

USBREDIR_VISIBLE
int usbredirhost_read_guest_data(struct usbredirhost *host)
{
    return usbredirparser_do_read(host->parser);
}

USBREDIR_VISIBLE
int usbredirhost_has_data_to_write(struct usbredirhost *host)
{
    return usbredirparser_has_data_to_write(host->parser);
}

USBREDIR_VISIBLE
int usbredirhost_write_guest_data(struct usbredirhost *host)
{
    return usbredirparser_do_write(host->parser);
}

USBREDIR_VISIBLE
void usbredirhost_free_write_buffer(struct usbredirhost *host, uint8_t *data)
{
    usbredirparser_free_write_buffer(host->parser, data);
}

/**************************************************************************/

static struct usbredirtransfer *usbredirhost_alloc_transfer(
    struct usbredirhost *host, int iso_packets)
{
    struct usbredirtransfer *redir_transfer;
    struct libusb_transfer *libusb_transfer;

    redir_transfer  = calloc(1, sizeof(*redir_transfer));
    libusb_transfer = libusb_alloc_transfer(iso_packets);
    if (!redir_transfer || !libusb_transfer) {
        ERROR("out of memory allocating usb transfer, dropping packet");
        free(redir_transfer);
        libusb_free_transfer(libusb_transfer);
        return NULL;
    }
    redir_transfer->host       = host;
    redir_transfer->transfer   = libusb_transfer;
    libusb_transfer->user_data = redir_transfer;

    return redir_transfer;
}

static void usbredirhost_free_transfer(struct usbredirtransfer *transfer)
{
    if (!transfer)
        return;

    /* In certain cases this should really be a usbredirparser_free_packet_data
       but since we use the same malloc impl. as usbredirparser this is ok. */
    free(transfer->transfer->buffer);
    libusb_free_transfer(transfer->transfer);
    free(transfer);
}

static void usbredirhost_add_transfer(struct usbredirhost *host,
    struct usbredirtransfer *new_transfer)
{
    struct usbredirtransfer *transfer = &host->transfers_head;

    LOCK(host);
    while (transfer->next) {
        transfer = transfer->next;
    }

    new_transfer->prev = transfer;
    transfer->next = new_transfer;
    UNLOCK(host);
}

/* Note caller must hold the host lock */
static void usbredirhost_remove_and_free_transfer(
    struct usbredirtransfer *transfer)
{
    if (transfer->next)
        transfer->next->prev = transfer->prev;
    if (transfer->prev)
        transfer->prev->next = transfer->next;
    usbredirhost_free_transfer(transfer);
}

/**************************************************************************/

/* Called from both parser read and packet complete callbacks */
static void usbredirhost_cancel_stream_unlocked(struct usbredirhost *host,
    uint8_t ep)
{
    int i;
    struct usbredirtransfer *transfer;

    for (i = 0; i < host->endpoint[EP2I(ep)].transfer_count; i++) {
        transfer = host->endpoint[EP2I(ep)].transfer[i];
        if (transfer->packet_idx == SUBMITTED_IDX) {
            libusb_cancel_transfer(transfer->transfer);
            transfer->cancelled = 1;
            host->cancels_pending++;
        } else {
            usbredirhost_free_transfer(transfer);
        }
        host->endpoint[EP2I(ep)].transfer[i] = NULL;
    }
    host->endpoint[EP2I(ep)].out_idx = 0;
    host->endpoint[EP2I(ep)].stream_started = 0;
    host->endpoint[EP2I(ep)].drop_packets = 0;
    host->endpoint[EP2I(ep)].pkts_per_transfer = 0;
    host->endpoint[EP2I(ep)].transfer_count = 0;
}

static void usbredirhost_cancel_stream(struct usbredirhost *host,
    uint8_t ep)
{
    LOCK(host);
    usbredirhost_cancel_stream_unlocked(host, ep);
    UNLOCK(host);
}

static void usbredirhost_send_stream_status(struct usbredirhost *host,
    uint64_t id, uint8_t ep, uint8_t status)
{
    switch (host->endpoint[EP2I(ep)].type) {
    case usb_redir_type_iso: {
        struct usb_redir_iso_stream_status_header iso_status = {
            .endpoint = ep,
            .status   = status,
        };
        usbredirparser_send_iso_stream_status(host->parser, id, &iso_status);
        break;
    }
    case usb_redir_type_bulk: {
        struct usb_redir_bulk_receiving_status_header bulk_status = {
            .endpoint = ep,
            .status   = status,
        };
        usbredirparser_send_bulk_receiving_status(host->parser, id,
                                                  &bulk_status);
        break;
    }
    case usb_redir_type_interrupt: {
        struct usb_redir_interrupt_receiving_status_header interrupt_status = {
            .endpoint = ep,
            .status   = status,
        };
        usbredirparser_send_interrupt_receiving_status(host->parser, id,
                                                       &interrupt_status);
        break;
    }
    }
}

static int usbredirhost_can_write_iso_package(struct usbredirhost *host)
{
    uint64_t size;

    if (host->flags & usbredirhost_fl_write_cb_owns_buffer) {
        if (!host->buffered_output_size_func) {
            /* Application is not dropping isoc packages */
            return true;
        }
        size = host->buffered_output_size_func(host->func_priv);
    } else {
        /* queue is on usbredirparser */
        size = usbredirparser_get_bufferered_output_size(host->parser);
    }

    if (size >= host->iso_threshold.higher) {
        if (!host->iso_threshold.dropping)
            DEBUG("START dropping isoc packets %" PRIu64 " buffer > %" PRIu64 " hi threshold",
                  size, host->iso_threshold.higher);
        host->iso_threshold.dropping = true;
    } else if (size < host->iso_threshold.lower) {
        if (host->iso_threshold.dropping)
            DEBUG("STOP dropping isoc packets %" PRIu64 " buffer < %" PRIu64 " low threshold",
                  size, host->iso_threshold.lower);

        host->iso_threshold.dropping = false;
    }

    return !host->iso_threshold.dropping;
}

static void usbredirhost_send_stream_data(struct usbredirhost *host,
    uint64_t id, uint8_t ep, uint8_t status, uint8_t *data, int len)
{
    /* USB-2 is max 8000 packets / sec, if we've queued up more then 0.1 sec,
       assume our connection is not keeping up and start dropping packets. */
    if (usbredirparser_has_data_to_write(host->parser) > 800) {
        if (host->endpoint[EP2I(ep)].warn_on_drop) {
            WARNING("buffered stream on endpoint %02X, connection too slow, "
                    "dropping packets", ep);
            host->endpoint[EP2I(ep)].warn_on_drop = 0;
        }
        DEBUG("buffered complete ep %02X dropping packet status %d len %d",
              ep, status, len);
        return;
    }

    DEBUG("buffered complete ep %02X status %d len %d", ep, status, len);

    switch (host->endpoint[EP2I(ep)].type) {
    case usb_redir_type_iso: {
        struct usb_redir_iso_packet_header iso_packet = {
            .endpoint = ep,
            .status   = status,
            .length   = len,
        };

        if (usbredirhost_can_write_iso_package(host))
            usbredirparser_send_iso_packet(host->parser, id, &iso_packet,
                                           data, len);
        break;
    }
    case usb_redir_type_bulk: {
        struct usb_redir_buffered_bulk_packet_header bulk_packet = {
            .endpoint = ep,
            .status   = status,
            .length   = len,
        };
        usbredirparser_send_buffered_bulk_packet(host->parser, id,
                                                 &bulk_packet, data, len);
        break;
    }
    case usb_redir_type_interrupt: {
        struct usb_redir_interrupt_packet_header interrupt_packet = {
            .endpoint = ep,
            .status   = status,
            .length   = len,
        };
        usbredirparser_send_interrupt_packet(host->parser, id,
                                             &interrupt_packet, data, len);
        break;
    }
    }
}

/* Called from both parser read and packet complete callbacks */
static int usbredirhost_submit_stream_transfer_unlocked(
    struct usbredirhost *host, struct usbredirtransfer *transfer)
{
    int r;

    host->reset = 0;

    r = libusb_submit_transfer(transfer->transfer);
    if (r < 0) {
        uint8_t ep = transfer->transfer->endpoint;
        if (r == LIBUSB_ERROR_NO_DEVICE) {
            usbredirhost_handle_disconnect(host);
        } else {
            ERROR("error submitting transfer on ep %02X: %s, stopping stream",
                  ep, libusb_error_name(r));
            usbredirhost_cancel_stream_unlocked(host, ep);
            usbredirhost_send_stream_status(host, transfer->id, ep,
                                            usb_redir_stall);
        }
        return usb_redir_stall;
    }

    transfer->packet_idx = SUBMITTED_IDX;
    return usb_redir_success;
}

/* Called from both parser read and packet complete callbacks */
static int usbredirhost_start_stream_unlocked(struct usbredirhost *host,
    uint8_t ep)
{
    unsigned int i, count = host->endpoint[EP2I(ep)].transfer_count;
    int status;

    /* For out endpoints 1/2 the transfers are a buffer for usb-guest data */
    if (!(ep & LIBUSB_ENDPOINT_IN)) {
        count /= 2;
    }
    for (i = 0; i < count; i++) {
        if (ep & LIBUSB_ENDPOINT_IN) {
            host->endpoint[EP2I(ep)].transfer[i]->id =
                i * host->endpoint[EP2I(ep)].pkts_per_transfer;
        }
        status = usbredirhost_submit_stream_transfer_unlocked(host,
                               host->endpoint[EP2I(ep)].transfer[i]);
        if (status != usb_redir_success) {
            return status;
        }
    }
    host->endpoint[EP2I(ep)].stream_started = 1;
    return usb_redir_success;
}

static void usbredirhost_stop_stream(struct usbredirhost *host,
    uint64_t id, uint8_t ep)
{
    if (host->disconnected) {
        return;
    }

    usbredirhost_cancel_stream(host, ep);
    usbredirhost_send_stream_status(host, id, ep, usb_redir_success);
    FLUSH(host);
}

static void usbredirhost_set_iso_threshold(struct usbredirhost *host,
    uint8_t pkts_per_transfer, uint8_t transfer_count, uint16_t max_packetsize)
{
    uint64_t reference = pkts_per_transfer * transfer_count * max_packetsize;
    host->iso_threshold.lower = reference / 2;
    host->iso_threshold.higher = reference * 3;
    DEBUG("higher threshold is %" PRIu64 " bytes | lower threshold is %" PRIu64 " bytes",
           host->iso_threshold.higher, host->iso_threshold.lower);
}

/* Called from both parser read and packet complete callbacks */
static void usbredirhost_alloc_stream_unlocked(struct usbredirhost *host,
    uint64_t id, uint8_t ep, uint8_t type, uint8_t pkts_per_transfer,
    int pkt_size, uint8_t transfer_count, int send_success)
{
    int i, buf_size, status = usb_redir_success;
    unsigned char *buffer;

    if (host->disconnected) {
        goto error;
    }

    if (host->endpoint[EP2I(ep)].type != type) {
        ERROR("error start stream type %d on type %d endpoint",
              type, host->endpoint[EP2I(ep)].type);
        goto error;
    }

    if (   pkts_per_transfer < 1 ||
           pkts_per_transfer > MAX_PACKETS_PER_TRANSFER ||
           transfer_count < 1 ||
           transfer_count > MAX_TRANSFER_COUNT ||
           host->endpoint[EP2I(ep)].max_packetsize == 0 ||
           (pkt_size % host->endpoint[EP2I(ep)].max_packetsize) != 0) {
        ERROR("error start stream type %d invalid parameters", type);
        goto error;
    }

    if (host->endpoint[EP2I(ep)].transfer_count) {
        ERROR("error received start type %d for already started stream", type);
        usbredirhost_send_stream_status(host, id, ep, usb_redir_inval);
        return;
    }

    DEBUG("allocating stream ep %02X type %d packet-size %d pkts %d urbs %d",
          ep, type, pkt_size, pkts_per_transfer, transfer_count);
    for (i = 0; i < transfer_count; i++) {
        host->endpoint[EP2I(ep)].transfer[i] =
            usbredirhost_alloc_transfer(host, (type == usb_redir_type_iso) ?
                                              pkts_per_transfer : 0);
        if (!host->endpoint[EP2I(ep)].transfer[i]) {
            goto alloc_error;
        }

        buf_size = pkt_size * pkts_per_transfer;
        buffer = malloc(buf_size);
        if (!buffer) {
            goto alloc_error;
        }
        switch (type) {
        case usb_redir_type_iso:
            libusb_fill_iso_transfer(
                host->endpoint[EP2I(ep)].transfer[i]->transfer, host->handle,
                ep, buffer, buf_size, pkts_per_transfer,
                usbredirhost_iso_packet_complete,
                host->endpoint[EP2I(ep)].transfer[i], ISO_TIMEOUT);
            libusb_set_iso_packet_lengths(
                host->endpoint[EP2I(ep)].transfer[i]->transfer, pkt_size);

            usbredirhost_set_iso_threshold(
                host, pkts_per_transfer,  transfer_count,
                host->endpoint[EP2I(ep)].max_packetsize);
            break;
        case usb_redir_type_bulk:
            libusb_fill_bulk_transfer(
                host->endpoint[EP2I(ep)].transfer[i]->transfer, host->handle,
                ep, buffer, buf_size, usbredirhost_buffered_packet_complete,
                host->endpoint[EP2I(ep)].transfer[i], BULK_TIMEOUT);
            break;
        case usb_redir_type_interrupt:
            libusb_fill_interrupt_transfer(
                host->endpoint[EP2I(ep)].transfer[i]->transfer, host->handle,
                ep, buffer, buf_size, usbredirhost_buffered_packet_complete,
                host->endpoint[EP2I(ep)].transfer[i], INTERRUPT_TIMEOUT);
            break;
        }
    }
    host->endpoint[EP2I(ep)].out_idx = 0;
    host->endpoint[EP2I(ep)].drop_packets = 0;
    host->endpoint[EP2I(ep)].pkts_per_transfer = pkts_per_transfer;
    host->endpoint[EP2I(ep)].transfer_count = transfer_count;

    /* For input endpoints submit the transfers now */
    if (ep & LIBUSB_ENDPOINT_IN) {
        status = usbredirhost_start_stream_unlocked(host, ep);
    }

    if (send_success && status == usb_redir_success) {
        usbredirhost_send_stream_status(host, id, ep, status);
    }
    return;

alloc_error:
    ERROR("out of memory allocating type %d stream buffers", type);
    do {
        usbredirhost_free_transfer(host->endpoint[EP2I(ep)].transfer[i]);
        host->endpoint[EP2I(ep)].transfer[i] = NULL;
        i--;
    } while (i >= 0);
error:
    usbredirhost_send_stream_status(host, id, ep, usb_redir_stall);
}

static void usbredirhost_alloc_stream(struct usbredirhost *host,
    uint64_t id, uint8_t ep, uint8_t type, uint8_t pkts_per_transfer,
    int pkt_size, uint8_t transfer_count, int send_success)
{
    LOCK(host);
    usbredirhost_alloc_stream_unlocked(host, id, ep, type, pkts_per_transfer,
                                       pkt_size, transfer_count, send_success);
    UNLOCK(host);
}

static void usbredirhost_clear_stream_stall_unlocked(
    struct usbredirhost *host, uint64_t id, uint8_t ep)
{
    int r;
    uint8_t pkts_per_transfer = host->endpoint[EP2I(ep)].pkts_per_transfer;
    uint8_t transfer_count    = host->endpoint[EP2I(ep)].transfer_count;
    int pkt_size = host->endpoint[EP2I(ep)].transfer[0]->transfer->length /
                   pkts_per_transfer;

    WARNING("buffered stream on endpoint %02X stalled, clearing stall", ep);

    usbredirhost_cancel_stream_unlocked(host, ep);
    r = libusb_clear_halt(host->handle, ep);
    if (r < 0) {
        usbredirhost_send_stream_status(host, id, ep, usb_redir_stall);
        return;
    }
    usbredirhost_alloc_stream_unlocked(host, id, ep,
                                       host->endpoint[EP2I(ep)].type,
                                       pkts_per_transfer, pkt_size,
                                       transfer_count, 0);
}

/**************************************************************************/

/* Called from close and parser read callbacks */
static int usbredirhost_cancel_pending_urbs(struct usbredirhost *host,
                                            int notify_guest)
{
    struct usbredirtransfer *t;
    int i, wait;

    LOCK(host);
    for (i = 0; i < MAX_ENDPOINTS; i++) {
        if (notify_guest && host->endpoint[i].transfer_count)
            usbredirhost_send_stream_status(host, 0, I2EP(i), usb_redir_stall);
        usbredirhost_cancel_stream_unlocked(host, I2EP(i));
    }

    wait = host->cancels_pending;
    for (t = host->transfers_head.next; t; t = t->next) {
        libusb_cancel_transfer(t->transfer);
        wait = 1;
    }
    UNLOCK(host);

    if (notify_guest)
        FLUSH(host);

    return wait;
}

/* Called from close and parser read callbacks */
void usbredirhost_wait_for_cancel_completion(struct usbredirhost *host)
{
    int wait;
    struct timeval tv;

    do {
        memset(&tv, 0, sizeof(tv));
        tv.tv_usec = 2500;
        libusb_handle_events_timeout(host->ctx, &tv);
        LOCK(host);
        wait = host->cancels_pending || host->transfers_head.next;
        UNLOCK(host);
    } while (wait);
}

/* Only called from read callbacks */
static void usbredirhost_cancel_pending_urbs_on_interface(
    struct usbredirhost *host, int i)
{
    struct usbredirtransfer *t;
    const struct libusb_interface_descriptor *intf_desc;

    LOCK(host);

    intf_desc = &host->config->interface[i].altsetting[host->alt_setting[i]];
    for (i = 0; i < intf_desc->bNumEndpoints; i++) {
        uint8_t ep = intf_desc->endpoint[i].bEndpointAddress;

        usbredirhost_cancel_stream_unlocked(host, ep);

        for (t = host->transfers_head.next; t; t = t->next) {
            if (t->transfer->endpoint == ep)
                libusb_cancel_transfer(t->transfer);
        }
    }

    UNLOCK(host);
}

/* Only called from read callbacks */
static int usbredirhost_bInterfaceNumber_to_index(
    struct usbredirhost *host, uint8_t bInterfaceNumber)
{
    int i, n;

    for (i = 0; host->config && i < host->config->bNumInterfaces; i++) {
        n = host->config->interface[i].altsetting[0].bInterfaceNumber;
        if (n == bInterfaceNumber) {
            return i;
        }
    }

    ERROR("invalid bNumInterface: %d\n", (int)bInterfaceNumber);
    return -1;
}

static void usbredirhost_log_data(struct usbredirhost *host, const char *desc,
    const uint8_t *data, int len)
{
    if (usbredirparser_debug_data <= host->verbose) {
        int i, j, n;

        for (i = 0; i < len; i += j) {
            char buf[128];

            n = sprintf(buf, "%s", desc);
            for (j = 0; j < 8 && i + j < len; j++){
                 n += sprintf(buf + n, " %02X", data[i + j]);
            }
            va_log(host, usbredirparser_debug_data, "%s", buf);
        }
    }
}

/**************************************************************************/

USBREDIR_VISIBLE
void usbredirhost_set_buffered_output_size_cb(struct usbredirhost *host,
    usbredirhost_buffered_output_size buffered_output_size_func)
{
    if (!host) {
        fprintf(stderr, "%s: invalid usbredirhost", __func__);
        return;
    }

    if (!(host->flags & usbredirhost_fl_write_cb_owns_buffer)) {
        host->log_func(host->func_priv, usbredirparser_warning,
                       "can't set callback as usbredirhost owns the output "
                       "buffer (flag: usbredirhost_fl_write_cb_owns_buffer)");
        return;
    }

    host->buffered_output_size_func = buffered_output_size_func;
}

/* Return value:
    0 All ok
    1 Packet borked, continue with next packet / urb
    2 Stream borked, full stop, no resubmit, etc.
   Note in the case of a return value of 2 this function takes care of
   sending an iso status message to the usb-guest. */
static int usbredirhost_handle_iso_status(struct usbredirhost *host,
    uint64_t id, uint8_t ep, int r)
{
    switch (r) {
    case LIBUSB_TRANSFER_COMPLETED:
    case -EXDEV: /* FIXlibusb: Passing regular error codes, bad libusb, bad! */
        return 0;
    case LIBUSB_TRANSFER_CANCELLED:
        /* Stream was intentionally stopped */
        return 2;
    case LIBUSB_TRANSFER_STALL:
        usbredirhost_clear_stream_stall_unlocked(host, id, ep);
        return 2;
    case LIBUSB_TRANSFER_NO_DEVICE:
        usbredirhost_handle_disconnect(host);
        return 2;
    case LIBUSB_TRANSFER_OVERFLOW:
    case LIBUSB_TRANSFER_ERROR:
    case LIBUSB_TRANSFER_TIMED_OUT:
    default:
        ERROR("iso stream error on endpoint %02X: %d", ep, r);
        return 1;
    }
}

static void LIBUSB_CALL usbredirhost_iso_packet_complete(
    struct libusb_transfer *libusb_transfer)
{
    struct usbredirtransfer *transfer = libusb_transfer->user_data;
    uint8_t ep = libusb_transfer->endpoint;
    struct usbredirhost *host = transfer->host;
    int i, r, len, status;

    LOCK(host);
    if (transfer->cancelled) {
        host->cancels_pending--;
        usbredirhost_free_transfer(transfer);
        goto unlock;
    }

    /* Mark transfer completed (iow not submitted) */
    transfer->packet_idx = 0;

    /* Check overal transfer status */
    r = libusb_transfer->status;
    switch (usbredirhost_handle_iso_status(host, transfer->id, ep, r)) {
    case 0:
        break;
    case 1:
        status = libusb_status_or_error_to_redir_status(host, r);
        if (ep & LIBUSB_ENDPOINT_IN) {
            struct usb_redir_iso_packet_header iso_packet = {
                .endpoint = ep,
                .status   = status,
                .length   = 0
            };
            usbredirparser_send_iso_packet(host->parser, transfer->id,
                           &iso_packet, NULL, 0);
            transfer->id += libusb_transfer->num_iso_packets;
            goto resubmit;
        } else {
            usbredirhost_send_stream_status(host, transfer->id, ep, status);
            goto unlock;
        }
        break;
    case 2:
        goto unlock;
    }

    /* Check per packet status and send ok input packets to usb-guest */
    for (i = 0; i < libusb_transfer->num_iso_packets; i++) {
        r   = libusb_transfer->iso_packet_desc[i].status;
        len = libusb_transfer->iso_packet_desc[i].actual_length;
        status = libusb_status_or_error_to_redir_status(host, r);
        switch (usbredirhost_handle_iso_status(host, transfer->id, ep, r)) {
        case 0:
            break;
        case 1:
            if (ep & LIBUSB_ENDPOINT_IN) {
                len = 0;
            } else {
                usbredirhost_send_stream_status(host, transfer->id, ep,
                                                status);
                goto unlock; /* We send max one iso status message per urb */
            }
            break;
        case 2:
            goto unlock;
        }
        if (ep & LIBUSB_ENDPOINT_IN) {
            usbredirhost_send_stream_data(host, transfer->id, ep, status,
                   libusb_get_iso_packet_buffer(libusb_transfer, i), len);
            transfer->id++;
        } else {
            DEBUG("iso-in complete ep %02X pkt %d len %d id %"PRIu64,
                  ep, i, len, transfer->id);
        }
    }

    /* And for input transfers resubmit the transfer (output transfers
       get resubmitted when they have all their packets filled with data) */
    if (ep & LIBUSB_ENDPOINT_IN) {
resubmit:
        transfer->id += (host->endpoint[EP2I(ep)].transfer_count - 1) *
                        libusb_transfer->num_iso_packets;
        usbredirhost_submit_stream_transfer_unlocked(host, transfer);
    } else {
        for (i = 0; i < host->endpoint[EP2I(ep)].transfer_count; i++) {
            transfer = host->endpoint[EP2I(ep)].transfer[i];
            if (transfer->packet_idx == SUBMITTED_IDX)
                break;
        }
        if (i == host->endpoint[EP2I(ep)].transfer_count) {
            DEBUG("underflow of iso out queue on ep: %02X", ep);
            /* Re-fill buffers before submitting urbs again */
            for (i = 0; i < host->endpoint[EP2I(ep)].transfer_count; i++)
                host->endpoint[EP2I(ep)].transfer[i]->packet_idx = 0;
            host->endpoint[EP2I(ep)].out_idx = 0;
            host->endpoint[EP2I(ep)].stream_started = 0;
            host->endpoint[EP2I(ep)].drop_packets = 0;
        }
    }
unlock:
    UNLOCK(host);
    FLUSH(host);
}

/**************************************************************************/

static void LIBUSB_CALL usbredirhost_buffered_packet_complete(
    struct libusb_transfer *libusb_transfer)
{
    struct usbredirtransfer *transfer = libusb_transfer->user_data;
    uint8_t ep = libusb_transfer->endpoint;
    struct usbredirhost *host = transfer->host;
    int r, len = libusb_transfer->actual_length;

    LOCK(host);

    if (transfer->cancelled) {
        host->cancels_pending--;
        usbredirhost_free_transfer(transfer);
        goto unlock;
    }

    /* Mark transfer completed (iow not submitted) */
    transfer->packet_idx = 0;

    r = libusb_transfer->status;
    switch (r) {
    case LIBUSB_TRANSFER_COMPLETED:
        break;
    case LIBUSB_TRANSFER_STALL:
        usbredirhost_clear_stream_stall_unlocked(host, transfer->id, ep);
        goto unlock;
    case LIBUSB_TRANSFER_NO_DEVICE:
        usbredirhost_handle_disconnect(host);
        goto unlock;
    default:
        ERROR("buffered in error on endpoint %02X: %d", ep, r);
        len = 0;
    }

    usbredirhost_send_stream_data(host, transfer->id, ep,
                           libusb_status_or_error_to_redir_status(host, r),
                           transfer->transfer->buffer, len);
    usbredirhost_log_data(host, "buffered data in:",
                          transfer->transfer->buffer, len);

    transfer->id += host->endpoint[EP2I(ep)].transfer_count;
    usbredirhost_submit_stream_transfer_unlocked(host, transfer);
unlock:
    UNLOCK(host);
    FLUSH(host);
}

/**************************************************************************/

static void usbredirhost_hello(void *priv, struct usb_redir_hello_header *h)
{
    struct usbredirhost *host = priv;

    if (host->connect_pending)
        usbredirhost_send_device_connect(host);
}

static void usbredirhost_reset(void *priv)
{
    struct usbredirhost *host = priv;
    int r;

    if (host->disconnected || host->reset) {
        return;
    }

    /*
     * The guest should have cancelled any pending urbs already, but the
     * cancellations may be awaiting completion, and if we then do a reset
     * they will complete with an error code of LIBUSB_TRANSFER_NO_DEVICE.
     *
     * And we also need to cleanly shutdown any streams (and let the guest
     * know they should be restarted after the reset).
     */
    if (usbredirhost_cancel_pending_urbs(host, 1))
        usbredirhost_wait_for_cancel_completion(host);

    r = usbredirhost_reset_device(host);
    if (r != 0) {
        host->read_status = usbredirhost_read_device_lost;
    }
}

static void usbredirhost_set_configuration(void *priv, uint64_t id,
    struct usb_redir_set_configuration_header *set_config)
{
    struct usbredirhost *host = priv;
    int r, claim_status;
    struct usb_redir_configuration_status_header status = {
        .status = usb_redir_success,
    };

    if (host->disconnected) {
        status.status = usb_redir_ioerror;
        goto exit;
    }

    if (host->config &&
            host->config->bConfigurationValue == set_config->configuration) {
        goto exit;
    }

    host->reset = 0;

    usbredirhost_cancel_pending_urbs(host, 0);
    usbredirhost_release(host, 0);

    r = libusb_set_configuration(host->handle, set_config->configuration);
    if (r < 0) {
        ERROR("could not set active configuration to %d: %s",
              (int)set_config->configuration, libusb_error_name(r));
        status.status = usb_redir_ioerror;
    }

    claim_status = usbredirhost_claim(host, 0);
    if (claim_status != usb_redir_success) {
        usbredirhost_clear_device(host);
        host->read_status = usbredirhost_read_device_lost;
        status.status = usb_redir_ioerror;
        goto exit;
    }

    usbredirhost_send_interface_n_ep_info(host);

exit:
    status.configuration = host->config ? host->config->bConfigurationValue:0;
    usbredirparser_send_configuration_status(host->parser, id, &status);
    FLUSH(host);
}

static void usbredirhost_get_configuration(void *priv, uint64_t id)
{
    struct usbredirhost *host = priv;
    struct usb_redir_configuration_status_header status;

    if (host->disconnected)
        status.status = usb_redir_ioerror;
    else
        status.status = usb_redir_success;
    status.configuration = host->config ? host->config->bConfigurationValue:0;
    usbredirparser_send_configuration_status(host->parser, id, &status);
    FLUSH(host);
}

static void usbredirhost_set_alt_setting(void *priv, uint64_t id,
    struct usb_redir_set_alt_setting_header *set_alt_setting)
{
    struct usbredirhost *host = priv;
    int i, j, r;
    struct usb_redir_alt_setting_status_header status = {
        .status = usb_redir_success,
    };

    if (host->disconnected) {
        status.status = usb_redir_ioerror;
        status.alt = -1;
        goto exit_unknown_interface;
    }

    i = usbredirhost_bInterfaceNumber_to_index(host,
                                               set_alt_setting->interface);
    if (i == -1) {
        status.status = usb_redir_inval;
        status.alt = -1;
        goto exit_unknown_interface;
    }

    host->reset = 0;

    usbredirhost_cancel_pending_urbs_on_interface(host, i);

    r = libusb_set_interface_alt_setting(host->handle,
                                         set_alt_setting->interface,
                                         set_alt_setting->alt);
    if (r < 0) {
        ERROR("could not set alt setting for interface %d to %d: %s",
              set_alt_setting->interface, set_alt_setting->alt,
              libusb_error_name(r));
        status.status = libusb_status_or_error_to_redir_status(host, r);
        goto exit;
    }

    /* The new alt setting may have lost endpoints compared to the old! ->
       Clear settings for all endpoints which used to be part of the intf. */
    for (j = 0; j < MAX_ENDPOINTS; j++) {
        if (host->endpoint[j].interface != set_alt_setting->interface)
            continue;

        if ((j & 0x0f) == 0) {
            host->endpoint[j].type = usb_redir_type_control;
        } else {
            host->endpoint[j].type = usb_redir_type_invalid;
        }
        host->endpoint[j].interval = 0;
        host->endpoint[j].interface = 0;
        host->endpoint[j].max_packetsize = 0;
    }

    host->alt_setting[i] = set_alt_setting->alt;
    usbredirhost_parse_interface(host, i);
    usbredirhost_send_interface_n_ep_info(host);

exit:
    status.alt = host->alt_setting[i];
exit_unknown_interface:
    status.interface = set_alt_setting->interface;
    usbredirparser_send_alt_setting_status(host->parser, id, &status);
    FLUSH(host);
}

static void usbredirhost_get_alt_setting(void *priv, uint64_t id,
    struct usb_redir_get_alt_setting_header *get_alt_setting)
{
    struct usbredirhost *host = priv;
    struct usb_redir_alt_setting_status_header status;
    int i;

    if (host->disconnected) {
        status.status = usb_redir_ioerror;
        status.alt = -1;
        goto exit;
    }

    i = usbredirhost_bInterfaceNumber_to_index(host,
                                               get_alt_setting->interface);
    if (i >= 0) {
        status.status = usb_redir_success;
        status.alt = host->alt_setting[i];
    } else {
        status.status = usb_redir_inval;
        status.alt = -1;
    }

exit:
    status.interface = get_alt_setting->interface;
    usbredirparser_send_alt_setting_status(host->parser, id, &status);
    FLUSH(host);
}

static void usbredirhost_start_iso_stream(void *priv, uint64_t id,
    struct usb_redir_start_iso_stream_header *start_iso_stream)
{
    struct usbredirhost *host = priv;
    uint8_t ep = start_iso_stream->endpoint;

    usbredirhost_alloc_stream(host, id, ep, usb_redir_type_iso,
                              start_iso_stream->pkts_per_urb,
                              host->endpoint[EP2I(ep)].max_packetsize,
                              start_iso_stream->no_urbs, 1);
    FLUSH(host);
}

static void usbredirhost_stop_iso_stream(void *priv, uint64_t id,
    struct usb_redir_stop_iso_stream_header *stop_iso_stream)
{
    usbredirhost_stop_stream(priv, id, stop_iso_stream->endpoint);
}

static void usbredirhost_start_interrupt_receiving(void *priv, uint64_t id,
    struct usb_redir_start_interrupt_receiving_header *start_interrupt_receiving)
{
    struct usbredirhost *host = priv;
    uint8_t ep = start_interrupt_receiving->endpoint;

    usbredirhost_alloc_stream(host, id, ep, usb_redir_type_interrupt, 1,
                              host->endpoint[EP2I(ep)].max_packetsize,
                              INTERRUPT_TRANSFER_COUNT, 1);
    FLUSH(host);
}

static void usbredirhost_stop_interrupt_receiving(void *priv, uint64_t id,
    struct usb_redir_stop_interrupt_receiving_header *stop_interrupt_receiving)
{
    usbredirhost_stop_stream(priv, id, stop_interrupt_receiving->endpoint);
}

#if LIBUSBX_API_VERSION >= 0x01000103
static int usbredirhost_ep_mask_to_eps(uint32_t ep_mask, unsigned char *eps)
{
    int i, j;

    for (i = 0, j = 0; i < MAX_ENDPOINTS; i++) {
        if (ep_mask & (1 << i))
            eps[j++] = I2EP(i);
    }

    return j;
}
#endif

static void usbredirhost_alloc_bulk_streams(void *priv, uint64_t id,
    struct usb_redir_alloc_bulk_streams_header *alloc_bulk_streams)
{
#if LIBUSBX_API_VERSION >= 0x01000103
    struct usbredirhost *host = priv;
    unsigned char eps[MAX_ENDPOINTS];
    int r, no_eps;
    struct usb_redir_bulk_streams_status_header streams_status = {
        .endpoints = alloc_bulk_streams->endpoints,
        .no_streams = alloc_bulk_streams->no_streams,
        .status = usb_redir_success,
    };

    no_eps = usbredirhost_ep_mask_to_eps(alloc_bulk_streams->endpoints, eps);
    r = libusb_alloc_streams(host->handle, alloc_bulk_streams->no_streams,
                             eps, no_eps);
    if (r < 0) {
        ERROR("could not alloc bulk streams: %s", libusb_error_name(r));
        streams_status.status =
            libusb_status_or_error_to_redir_status(host, r);
    } else if (r < alloc_bulk_streams->no_streams) {
        ERROR("tried to alloc %u bulk streams but got only %d",
              alloc_bulk_streams->no_streams, r);
        streams_status.status = usb_redir_ioerror;
    }

    usbredirparser_send_bulk_streams_status(host->parser, id, &streams_status);
    FLUSH(host);
#endif
}

static void usbredirhost_free_bulk_streams(void *priv, uint64_t id,
    struct usb_redir_free_bulk_streams_header *free_bulk_streams)
{
#if LIBUSBX_API_VERSION >= 0x01000103
    struct usbredirhost *host = priv;
    unsigned char eps[MAX_ENDPOINTS];
    int r, no_eps;
    struct usb_redir_bulk_streams_status_header streams_status = {
        .endpoints = free_bulk_streams->endpoints,
        .no_streams = 0,
        .status = usb_redir_success,
    };

    no_eps = usbredirhost_ep_mask_to_eps(free_bulk_streams->endpoints, eps);
    r = libusb_free_streams(host->handle, eps, no_eps);
    if (r < 0) {
        ERROR("could not free bulk streams: %s", libusb_error_name(r));
        streams_status.status =
            libusb_status_or_error_to_redir_status(host, r);
    }

    usbredirparser_send_bulk_streams_status(host->parser, id, &streams_status);
    FLUSH(host);
#endif
}

static void usbredirhost_filter_reject(void *priv)
{
    struct usbredirhost *host = priv;

    if (host->disconnected)
        return;

    INFO("device rejected");
    host->read_status = usbredirhost_read_device_rejected;
}

static void usbredirhost_filter_filter(void *priv,
    struct usbredirfilter_rule *rules, int rules_count)
{
    struct usbredirhost *host = priv;

    free(host->filter_rules);
    host->filter_rules = rules;
    host->filter_rules_count = rules_count;
}

static void usbredirhost_device_disconnect_ack(void *priv)
{
    struct usbredirhost *host = priv;

    if (!host->wait_disconnect) {
        ERROR("error received disconnect ack without sending a disconnect");
        return;
    }

    host->wait_disconnect = 0;

    if (host->connect_pending)
        usbredirhost_send_device_connect(host);
}

static void usbredirhost_start_bulk_receiving(void *priv, uint64_t id,
    struct usb_redir_start_bulk_receiving_header *start_bulk_receiving)
{
    struct usbredirhost *host = priv;
    uint8_t ep = start_bulk_receiving->endpoint;

    usbredirhost_alloc_stream(host, id, ep, usb_redir_type_bulk, 1,
                              start_bulk_receiving->bytes_per_transfer,
                              start_bulk_receiving->no_transfers, 1);
    FLUSH(host);
}

static void usbredirhost_stop_bulk_receiving(void *priv, uint64_t id,
    struct usb_redir_stop_bulk_receiving_header *stop_bulk_receiving)
{
    usbredirhost_stop_stream(priv, id, stop_bulk_receiving->endpoint);
}

/**************************************************************************/

static void usbredirhost_cancel_data_packet(void *priv, uint64_t id)
{
    struct usbredirhost *host = priv;
    struct usbredirtransfer *t;
    struct usb_redir_control_packet_header   control_packet;
    struct usb_redir_bulk_packet_header      bulk_packet;
    struct usb_redir_interrupt_packet_header interrupt_packet;

    /*
     * This is a bit tricky, we are run from a parser read callback, while
     * at the same time the packet completion callback may run from another
     * thread.
     *
     * Since the completion handler will remove the transfer from our list,
     * send it back to the usb-guest (which we don't want to do twice),
     * and *free* the transfer, we must do the libusb_cancel_transfer()
     * with the lock held to ensure that it is not freed while we try to
     * cancel it.
     *
     * Doing this means libusb taking the transfer lock, while
     * we are holding our own lock, this is ok, since libusb releases the
     * transfer lock before calling the packet completion callback, so there
     * is no deadlock here.
     */

    LOCK(host);
    for (t = host->transfers_head.next; t; t = t->next) {
        /* After cancellation the guest may re-use the id, so skip already
           cancelled packets */
        if (!t->cancelled && t->id == id) {
            break;
        }
    }

    /*
     * Note not finding the transfer is not an error, the transfer may have
     * completed by the time we receive the cancel.
     */
    if (t) {
        t->cancelled = 1;
        libusb_cancel_transfer(t->transfer);
        switch(t->transfer->type) {
        case LIBUSB_TRANSFER_TYPE_CONTROL:
            control_packet = t->control_packet;
            control_packet.status = usb_redir_cancelled;
            control_packet.length = 0;
            usbredirparser_send_control_packet(host->parser, t->id,
                                               &control_packet, NULL, 0);
            DEBUG("cancelled control packet ep %02x id %"PRIu64,
                  control_packet.endpoint, id);
            break;
        case LIBUSB_TRANSFER_TYPE_BULK:
#if LIBUSBX_API_VERSION >= 0x01000103
        case LIBUSB_TRANSFER_TYPE_BULK_STREAM:
#endif
            bulk_packet = t->bulk_packet;
            bulk_packet.status = usb_redir_cancelled;
            bulk_packet.length = 0;
            bulk_packet.length_high = 0;
            usbredirparser_send_bulk_packet(host->parser, t->id,
                                               &bulk_packet, NULL, 0);
            DEBUG("cancelled bulk packet ep %02x id %"PRIu64,
                  bulk_packet.endpoint, id);
            break;
        case LIBUSB_TRANSFER_TYPE_INTERRUPT:
            interrupt_packet = t->interrupt_packet;
            interrupt_packet.status = usb_redir_cancelled;
            interrupt_packet.length = 0;
            usbredirparser_send_interrupt_packet(host->parser, t->id,
                                                 &interrupt_packet, NULL, 0);
            DEBUG("cancelled interrupt packet ep %02x id %"PRIu64,
                  interrupt_packet.endpoint, id);
            break;
        }
    } else
        DEBUG("cancel packet id %"PRIu64" not found", id);
    UNLOCK(host);
    FLUSH(host);
}

static void LIBUSB_CALL usbredirhost_control_packet_complete(
    struct libusb_transfer *libusb_transfer)
{
    struct usb_redir_control_packet_header control_packet;
    struct usbredirtransfer *transfer = libusb_transfer->user_data;
    struct usbredirhost *host = transfer->host;

    LOCK(host);

    control_packet = transfer->control_packet;
    control_packet.status = libusb_status_or_error_to_redir_status(host,
                                                  libusb_transfer->status);
    control_packet.length = libusb_transfer->actual_length;

    DEBUG("control complete ep %02X status %d len %d id %"PRIu64,
          control_packet.endpoint, control_packet.status,
          control_packet.length, transfer->id);

    if (!transfer->cancelled) {
        if (control_packet.endpoint & LIBUSB_ENDPOINT_IN) {
            usbredirhost_log_data(host, "ctrl data in:",
                         libusb_transfer->buffer + LIBUSB_CONTROL_SETUP_SIZE,
                         libusb_transfer->actual_length);
            usbredirparser_send_control_packet(host->parser, transfer->id,
                                               &control_packet,
                                               libusb_transfer->buffer +
                                                   LIBUSB_CONTROL_SETUP_SIZE,
                                               libusb_transfer->actual_length);
        } else {
            usbredirparser_send_control_packet(host->parser, transfer->id,
                                               &control_packet, NULL, 0);
        }
    }

    usbredirhost_remove_and_free_transfer(transfer);
    UNLOCK(host);
    FLUSH(host);
}

static void usbredirhost_send_control_status(struct usbredirhost *host,
    uint64_t id, struct usb_redir_control_packet_header *control_packet,
    uint8_t status)
{
    control_packet->status = status;
    control_packet->length = 0;
    usbredirparser_send_control_packet(host->parser, id, control_packet,
                                       NULL, 0);
}

static void usbredirhost_control_packet(void *priv, uint64_t id,
    struct usb_redir_control_packet_header *control_packet,
    uint8_t *data, int data_len)
{
    struct usbredirhost *host = priv;
    uint8_t ep = control_packet->endpoint;
    struct usbredirtransfer *transfer;
    unsigned char *buffer;
    int r;

    DEBUG("control submit ep %02X len %d id %"PRIu64, ep,
          control_packet->length, id);

    if (host->disconnected) {
        usbredirhost_send_control_status(host, id, control_packet,
                                         usb_redir_ioerror);
        usbredirparser_free_packet_data(host->parser, data);
        FLUSH(host);
        return;
    }

    /* Verify endpoint type */
    if (host->endpoint[EP2I(ep)].type != usb_redir_type_control) {
        ERROR("error control packet on non control ep %02X", ep);
        usbredirhost_send_control_status(host, id, control_packet,
                                         usb_redir_inval);
        usbredirparser_free_packet_data(host->parser, data);
        FLUSH(host);
        return;
    }

    host->reset = 0;

    /* If it is a clear stall, we need to do an actual clear stall, rather then
       just forward the control packet, so that the usbhost usbstack knows
       the stall is cleared */
    if (control_packet->requesttype == LIBUSB_RECIPIENT_ENDPOINT &&
            control_packet->request == LIBUSB_REQUEST_CLEAR_FEATURE &&
            control_packet->value == 0x00 && data_len == 0) {
        r = libusb_clear_halt(host->handle, control_packet->index);
        r = libusb_status_or_error_to_redir_status(host, r);
        DEBUG("clear halt ep %02X status %d", control_packet->index, r);
        usbredirhost_send_control_status(host, id, control_packet, r);
        FLUSH(host);
        return;
    }

    buffer = malloc(LIBUSB_CONTROL_SETUP_SIZE + control_packet->length);
    if (!buffer) {
        ERROR("out of memory allocating transfer buffer, dropping packet");
        usbredirparser_free_packet_data(host->parser, data);
        return;
    }

    transfer = usbredirhost_alloc_transfer(host, 0);
    if (!transfer) {
        free(buffer);
        usbredirparser_free_packet_data(host->parser, data);
        return;
    }

    libusb_fill_control_setup(buffer,
                              control_packet->requesttype,
                              control_packet->request,
                              control_packet->value,
                              control_packet->index,
                              control_packet->length);

    if (!(ep & LIBUSB_ENDPOINT_IN)) {
        usbredirhost_log_data(host, "ctrl data out:", data, data_len);
        memcpy(buffer + LIBUSB_CONTROL_SETUP_SIZE, data, data_len);
        usbredirparser_free_packet_data(host->parser, data);
    }

    libusb_fill_control_transfer(transfer->transfer, host->handle, buffer,
                                 usbredirhost_control_packet_complete,
                                 transfer, CTRL_TIMEOUT);
    transfer->id = id;
    transfer->control_packet = *control_packet;

    usbredirhost_add_transfer(host, transfer);

    r = libusb_submit_transfer(transfer->transfer);
    if (r < 0) {
        ERROR("error submitting control transfer on ep %02X: %s",
              ep, libusb_error_name(r));
        transfer->transfer->actual_length = 0;
        transfer->transfer->status = r;
        usbredirhost_control_packet_complete(transfer->transfer);
    }
}

/* Opt-in metadata only. UAS pipe-usage descriptors, not hard-coded endpoint
   numbers, distinguish command/status IUs from file data. No CDB address,
   inquiry strings, blocks or payload dumps are recorded. */
static void usbredirhost_trace_uas(struct usbredirhost *host, uint64_t id,
    const struct usb_redir_bulk_packet_header *packet,
    const uint8_t *data, int length, bool completed)
{
    int i, j, k, pipe = 0;
    const struct libusb_interface_descriptor *intf;
    if (!host->scsi_metadata_left || !host->config)
        return;
    i = usbredirhost_bInterfaceNumber_to_index(host,
        host->endpoint[EP2I(packet->endpoint)].interface);
    if (i < 0 || host->alt_setting[i] >= host->config->interface[i].num_altsetting)
        return;
    intf = &host->config->interface[i].altsetting[host->alt_setting[i]];
    if (intf->bInterfaceClass == 8 && intf->bInterfaceProtocol == 0x50) {
        /* Command/status envelopes only. Never log file data, LBA, inquiry
         * strings or arbitrary bulk bytes, even when tracing is enabled. */
        if (!completed && !(packet->endpoint & 0x80) && data && length == 31 &&
            memcmp(data, "USBC", 4) == 0) {
            host->scsi_metadata_left--;
            INFO("BOT command wire=%"PRIu64" op=%02x direction=%s",
                 id, data[15], (data[12] & 0x80) ? "in" : "out");
        } else if (completed && (packet->endpoint & 0x80) && data &&
                   length == 13 && memcmp(data, "USBS", 4) == 0) {
            host->scsi_metadata_left--;
            INFO("BOT status wire=%"PRIu64" scsi=%u usb=%u",
                 id, data[12], packet->status);
        } else if (completed && packet->status != usb_redir_success) {
            host->scsi_metadata_left--;
            INFO("BOT error wire=%"PRIu64" ep=%02x length=%d usb=%u",
                 id, packet->endpoint, length, packet->status);
        }
        return;
    }
    if (intf->bInterfaceClass != 8 || intf->bInterfaceSubClass != 6 ||
        intf->bInterfaceProtocol != 0x62)
        return;
    for (j = 0; j < intf->bNumEndpoints; j++) {
        const struct libusb_endpoint_descriptor *ep = &intf->endpoint[j];
        if (ep->bEndpointAddress != packet->endpoint)
            continue;
        for (k = 0; k + 2 <= ep->extra_length;) {
            int size = ep->extra[k];
            if (size < 2 || k + size > ep->extra_length)
                break;
            if (size >= 3 && ep->extra[k + 1] == 0x24) {
                pipe = ep->extra[k + 2];
                break;
            }
            k += size;
        }
    }
    if (!pipe)
        return;
    host->scsi_metadata_left--;
    if (!completed && pipe == 1 && data && length >= 32 && data[0] == 1) {
        int tag = (data[2] << 8) | data[3];
        int page = (data[16] == 0x12 || data[16] == 0x1a || data[16] == 0x5a) ? data[18] : -1;
        INFO("UAS command wire=%"PRIu64" tag=%d op=%02x flags=%02x page=%d",
             id, tag, data[16], data[17], page);
    } else if (completed && pipe == 2 && packet->status == usb_redir_success &&
               data && length >= 16 && data[0] == 3) {
        int key = -1, asc = -1, ascq = -1;
        if (length >= 30 && ((data[16] & 0x7f) == 0x70 || (data[16] & 0x7f) == 0x71)) {
            key = data[18] & 15; asc = data[28]; ascq = data[29];
        } else if (length >= 20 && ((data[16] & 0x7f) == 0x72 || (data[16] & 0x7f) == 0x73)) {
            key = data[17] & 15; asc = data[18]; ascq = data[19];
        }
        INFO("UAS sense wire=%"PRIu64" stream=%u tag=%d scsi=%u key=%d asc=%d ascq=%d",
             id, packet->stream_id, (data[2] << 8) | data[3], data[6], key, asc, ascq);
    } else {
        INFO("UAS %s wire=%"PRIu64" pipe=%d ep=%02x stream=%u length=%d usb=%u",
             completed ? "done" : "submit", id, pipe, packet->endpoint,
             packet->stream_id, length, completed ? packet->status : 0);
    }
}

static void LIBUSB_CALL usbredirhost_bulk_packet_complete(
    struct libusb_transfer *libusb_transfer)
{
    struct usb_redir_bulk_packet_header bulk_packet;
    struct usbredirtransfer *transfer = libusb_transfer->user_data;
    struct usbredirhost *host = transfer->host;

    LOCK(host);

    bulk_packet = transfer->bulk_packet;
    bulk_packet.status = libusb_status_or_error_to_redir_status(host,
                                                  libusb_transfer->status);
    bulk_packet.length = libusb_transfer->actual_length;
    bulk_packet.length_high = libusb_transfer->actual_length >> 16;
    usbredirhost_trace_uas(host, transfer->id, &bulk_packet,
                          libusb_transfer->buffer, libusb_transfer->actual_length, true);

    DEBUG("bulk complete ep %02X status %d len %d id %"PRIu64,
          bulk_packet.endpoint, bulk_packet.status,
          libusb_transfer->actual_length, transfer->id);

    if (!transfer->cancelled) {
        if (bulk_packet.endpoint & LIBUSB_ENDPOINT_IN) {
            usbredirhost_log_data(host, "bulk data in:",
                                  libusb_transfer->buffer,
                                  libusb_transfer->actual_length);
            usbredirparser_send_bulk_packet(host->parser, transfer->id,
                                            &bulk_packet,
                                            libusb_transfer->buffer,
                                            libusb_transfer->actual_length);
        } else {
            usbredirparser_send_bulk_packet(host->parser, transfer->id,
                                            &bulk_packet, NULL, 0);
        }
    }

    usbredirhost_remove_and_free_transfer(transfer);
    UNLOCK(host);
    FLUSH(host);
}

static void usbredirhost_send_bulk_status(struct usbredirhost *host,
    uint64_t id, struct usb_redir_bulk_packet_header *bulk_packet,
    uint8_t status)
{
    bulk_packet->status = status;
    bulk_packet->length = 0;
    bulk_packet->length_high = 0;
    usbredirparser_send_bulk_packet(host->parser, id, bulk_packet, NULL, 0);
}

static void usbredirhost_bulk_packet(void *priv, uint64_t id,
    struct usb_redir_bulk_packet_header *bulk_packet,
    uint8_t *data, int data_len)
{
    struct usbredirhost *host = priv;
    uint8_t ep = bulk_packet->endpoint;
    int len = (bulk_packet->length_high << 16) | bulk_packet->length;
    struct usbredirtransfer *transfer;
    int r;

    DEBUG("bulk submit ep %02X len %d id %"PRIu64, ep, len, id);

    if (host->disconnected) {
        usbredirhost_send_bulk_status(host, id, bulk_packet,
                                      usb_redir_ioerror);
        usbredirparser_free_packet_data(host->parser, data);
        FLUSH(host);
        return;
    }

    if (host->endpoint[EP2I(ep)].type != usb_redir_type_bulk) {
        ERROR("error bulk packet on non bulk ep %02X", ep);
        usbredirhost_send_bulk_status(host, id, bulk_packet, usb_redir_inval);
        usbredirparser_free_packet_data(host->parser, data);
        FLUSH(host);
        return;
    }

    usbredirhost_trace_uas(host, id, bulk_packet, data,
                          (ep & LIBUSB_ENDPOINT_IN) ? len : data_len, false);

    if (ep & LIBUSB_ENDPOINT_IN) {
        data = malloc(len);
        if (!data) {
            ERROR("out of memory allocating bulk buffer, dropping packet");
            return;
        }
    } else {
        usbredirhost_log_data(host, "bulk data out:", data, data_len);
        /* Note no memcpy, we can re-use the data buffer the parser
           malloc-ed for us and expects us to free */
    }

    transfer = usbredirhost_alloc_transfer(host, 0);
    if (!transfer) {
        free(data);
        return;
    }

    host->reset = 0;

    if (bulk_packet->stream_id) {
#if LIBUSBX_API_VERSION >= 0x01000103
        libusb_fill_bulk_stream_transfer(transfer->transfer, host->handle, ep,
                                         bulk_packet->stream_id, data, len,
                                         usbredirhost_bulk_packet_complete,
                                         transfer, BULK_TIMEOUT);
#else
        r = LIBUSB_ERROR_INVALID_PARAM;
        free(data);
        goto error;
#endif
    } else {
        libusb_fill_bulk_transfer(transfer->transfer, host->handle, ep,
                                  data, len, usbredirhost_bulk_packet_complete,
                                  transfer, BULK_TIMEOUT);
    }
    transfer->id = id;
    transfer->bulk_packet = *bulk_packet;

    usbredirhost_add_transfer(host, transfer);

    r = libusb_submit_transfer(transfer->transfer);
    if (r < 0) {
#if LIBUSBX_API_VERSION < 0x01000103
error:
#endif
        ERROR("error submitting bulk transfer on ep %02X: %s",
              ep, libusb_error_name(r));
        transfer->transfer->actual_length = 0;
        transfer->transfer->status = r;
        usbredirhost_bulk_packet_complete(transfer->transfer);
    }
}

static void usbredirhost_iso_packet(void *priv, uint64_t id,
    struct usb_redir_iso_packet_header *iso_packet,
    uint8_t *data, int data_len)
{
    struct usbredirhost *host = priv;
    uint8_t ep = iso_packet->endpoint;
    struct usbredirtransfer *transfer;
    int i, j, status = usb_redir_success;

    LOCK(host);

    if (host->disconnected) {
        status = usb_redir_ioerror;
        goto leave;
    }

    if (host->endpoint[EP2I(ep)].type != usb_redir_type_iso) {
        ERROR("error received iso packet for non iso ep %02X", ep);
        status = usb_redir_inval;
        goto leave;
    }

    if (host->endpoint[EP2I(ep)].transfer_count == 0) {
        ERROR("error received iso out packet for non started iso stream");
        status = usb_redir_inval;
        goto leave;
    }

    if (data_len > host->endpoint[EP2I(ep)].max_packetsize) {
        ERROR("error received iso out packet is larger than wMaxPacketSize");
        status = usb_redir_inval;
        goto leave;
    }

    if (host->endpoint[EP2I(ep)].drop_packets) {
        host->endpoint[EP2I(ep)].drop_packets--;
        goto leave;
    }

    i = host->endpoint[EP2I(ep)].out_idx;
    transfer = host->endpoint[EP2I(ep)].transfer[i];
    j = transfer->packet_idx;
    if (j == SUBMITTED_IDX) {
        DEBUG("overflow of iso out queue on ep: %02X, dropping packet", ep);
        /* Since we're interupting the stream anyways, drop enough packets to
           get back to our target buffer size */
        host->endpoint[EP2I(ep)].drop_packets =
                     (host->endpoint[EP2I(ep)].pkts_per_transfer *
                      host->endpoint[EP2I(ep)].transfer_count) / 2;
        goto leave;
    }

    /* Store the id of the first packet in the urb */
    if (j == 0) {
        transfer->id = id;
    }
    memcpy(libusb_get_iso_packet_buffer(transfer->transfer, j),
           data, data_len);
    transfer->transfer->iso_packet_desc[j].length = data_len;
    DEBUG("iso-in queue ep %02X urb %d pkt %d len %d id %"PRIu64,
           ep, i, j, data_len, transfer->id);

    j++;
    transfer->packet_idx = j;
    if (j == host->endpoint[EP2I(ep)].pkts_per_transfer) {
        i = (i + 1) % host->endpoint[EP2I(ep)].transfer_count;
        host->endpoint[EP2I(ep)].out_idx = i;
        j = 0;
    }

    if (host->endpoint[EP2I(ep)].stream_started) {
        if (transfer->packet_idx ==
                host->endpoint[EP2I(ep)].pkts_per_transfer) {
            usbredirhost_submit_stream_transfer_unlocked(host, transfer);
        }
    } else {
        /* We've not started the stream (submitted some transfers) yet,
           do so once we have half our buffers filled */
        int available = i * host->endpoint[EP2I(ep)].pkts_per_transfer + j;
        int needed = (host->endpoint[EP2I(ep)].pkts_per_transfer *
                      host->endpoint[EP2I(ep)].transfer_count) / 2;
        if (available == needed) {
            DEBUG("iso-in starting stream on ep %02X", ep);
            usbredirhost_start_stream_unlocked(host, ep);
        }
    }

leave:
    UNLOCK(host);
    usbredirparser_free_packet_data(host->parser, data);
    if (status != usb_redir_success) {
        usbredirhost_send_stream_status(host, id, ep, status);
    }
    FLUSH(host);
}

static void LIBUSB_CALL usbredirhost_interrupt_out_packet_complete(
    struct libusb_transfer *libusb_transfer)
{
    struct usbredirtransfer *transfer = libusb_transfer->user_data;
    struct usb_redir_interrupt_packet_header interrupt_packet;
    struct usbredirhost *host = transfer->host;

    LOCK(host);

    interrupt_packet = transfer->interrupt_packet;
    interrupt_packet.status = libusb_status_or_error_to_redir_status(host,
                                                    libusb_transfer->status);
    interrupt_packet.length = libusb_transfer->actual_length;

    DEBUG("interrupt out complete ep %02X status %d len %d id %"PRIu64,
          interrupt_packet.endpoint, interrupt_packet.status,
          interrupt_packet.length, transfer->id);

    if (!transfer->cancelled) {
        usbredirparser_send_interrupt_packet(host->parser, transfer->id,
                                             &interrupt_packet, NULL, 0);
    }
    usbredirhost_remove_and_free_transfer(transfer);
    UNLOCK(host);
    FLUSH(host);
}

static void usbredirhost_send_interrupt_status(struct usbredirhost *host,
    uint64_t id, struct usb_redir_interrupt_packet_header *interrupt_packet,
    uint8_t status)
{
    interrupt_packet->status = status;
    interrupt_packet->length = 0;
    usbredirparser_send_interrupt_packet(host->parser, id, interrupt_packet,
                                         NULL, 0);
}

static void usbredirhost_interrupt_packet(void *priv, uint64_t id,
    struct usb_redir_interrupt_packet_header *interrupt_packet,
    uint8_t *data, int data_len)
{
    struct usbredirhost *host = priv;
    uint8_t ep = interrupt_packet->endpoint;
    struct usbredirtransfer *transfer;
    int r;

    DEBUG("interrupt submit ep %02X len %d id %"PRIu64, ep,
          interrupt_packet->length, id);

    if (host->disconnected) {
        usbredirhost_send_interrupt_status(host, id, interrupt_packet,
                                           usb_redir_ioerror);
        usbredirparser_free_packet_data(host->parser, data);
        FLUSH(host);
        return;
    }

    if (host->endpoint[EP2I(ep)].type != usb_redir_type_interrupt) {
        ERROR("error received interrupt packet for non interrupt ep %02X", ep);
        usbredirhost_send_interrupt_status(host, id, interrupt_packet,
                                           usb_redir_inval);
        usbredirparser_free_packet_data(host->parser, data);
        FLUSH(host);
        return;
    }

    if (data_len > host->endpoint[EP2I(ep)].max_packetsize) {
        ERROR("error received interrupt out packet is larger than wMaxPacketSize");
        usbredirhost_send_interrupt_status(host, id, interrupt_packet,
                                           usb_redir_inval);
        usbredirparser_free_packet_data(host->parser, data);
        FLUSH(host);
        return;
    }

    usbredirhost_log_data(host, "interrupt data out:", data, data_len);

    /* Note no memcpy, we can re-use the data buffer the parser
       malloc-ed for us and expects us to free */

    transfer = usbredirhost_alloc_transfer(host, 0);
    if (!transfer) {
        usbredirparser_free_packet_data(host->parser, data);
        return;
    }

    host->reset = 0;

    libusb_fill_interrupt_transfer(transfer->transfer, host->handle, ep,
        data, data_len, usbredirhost_interrupt_out_packet_complete,
        transfer, INTERRUPT_TIMEOUT);
    transfer->id = id;
    transfer->interrupt_packet = *interrupt_packet;

    usbredirhost_add_transfer(host, transfer);

    r = libusb_submit_transfer(transfer->transfer);
    if (r < 0) {
        ERROR("error submitting interrupt transfer on ep %02X: %s",
              ep, libusb_error_name(r));
        transfer->transfer->actual_length = 0;
        transfer->transfer->status = r;
        usbredirhost_interrupt_out_packet_complete(transfer->transfer);
    }
}

/**************************************************************************/

USBREDIR_VISIBLE
void usbredirhost_get_guest_filter(struct usbredirhost *host,
    const struct usbredirfilter_rule **rules_ret, int *rules_count_ret)
{
    *rules_ret = host->filter_rules;
    *rules_count_ret = host->filter_rules_count;
}

USBREDIR_VISIBLE
int usbredirhost_check_device_filter(const struct usbredirfilter_rule *rules,
    int rules_count, libusb_device *dev, int flags)
{
    int i, r, num_interfaces;
    struct libusb_device_descriptor dev_desc;
    struct libusb_config_descriptor *config = NULL;
    uint8_t interface_class[MAX_INTERFACES];
    uint8_t interface_subclass[MAX_INTERFACES];
    uint8_t interface_protocol[MAX_INTERFACES];

    r = libusb_get_device_descriptor(dev, &dev_desc);
    if (r < 0) {
        if (r == LIBUSB_ERROR_NO_MEM)
            return -ENOMEM;
        return -EIO;
    }

    r = libusb_get_active_config_descriptor(dev, &config);
    if (r < 0 && r != LIBUSB_ERROR_NOT_FOUND) {
        if (r == LIBUSB_ERROR_NO_MEM)
            return -ENOMEM;
        return -EIO;
    }
    if (config == NULL) {
        return usbredirfilter_check(rules, rules_count, dev_desc.bDeviceClass,
                    dev_desc.bDeviceSubClass, dev_desc.bDeviceProtocol,
                    NULL, NULL, NULL, 0,
                    dev_desc.idVendor, dev_desc.idProduct,
                    dev_desc.bcdDevice, flags);
    }

    num_interfaces = config->bNumInterfaces;
    for (i = 0; i < num_interfaces; i++) {
        const struct libusb_interface_descriptor *intf_desc =
            config->interface[i].altsetting;
        interface_class[i] = intf_desc->bInterfaceClass;
        interface_subclass[i] = intf_desc->bInterfaceSubClass;
        interface_protocol[i] = intf_desc->bInterfaceProtocol;
    }
    libusb_free_config_descriptor(config);

    return usbredirfilter_check(rules, rules_count, dev_desc.bDeviceClass,
                dev_desc.bDeviceSubClass, dev_desc.bDeviceProtocol,
                interface_class, interface_subclass, interface_protocol,
                num_interfaces, dev_desc.idVendor, dev_desc.idProduct,
                dev_desc.bcdDevice, flags);
}
