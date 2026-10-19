/* usbredirparser.c usb redirection protocol parser

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

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "usbredirproto-compat.h"
#include "usbredirparser.h"
#include "usbredirfilter.h"

/* Put *some* upper limit on bulk transfer sizes */
#define MAX_BULK_TRANSFER_SIZE (128u * 1024u * 1024u)

/* Upper limit for accepted packet sizes including headers; makes the assumption
 * that no header is longer than 1kB
 */
#define MAX_PACKET_SIZE (1024u + MAX_BULK_TRANSFER_SIZE)

/* Locking convenience macros */
#define LOCK(parser) \
    do { \
        if ((parser)->lock) \
            (parser)->callb.lock_func((parser)->lock); \
    } while (0)

#define UNLOCK(parser) \
    do { \
        if ((parser)->lock) \
            (parser)->callb.unlock_func((parser)->lock); \
    } while (0)

struct usbredirparser_buf {
    uint8_t *buf;
    int pos;
    int len;

    struct usbredirparser_buf *next;
};

struct usbredirparser_priv {
    struct usbredirparser callb;
    int flags;

    int have_peer_caps;
    uint32_t our_caps[USB_REDIR_CAPS_SIZE];
    uint32_t peer_caps[USB_REDIR_CAPS_SIZE];

    void *lock;

    union {
        struct usb_redir_header header;
        struct usb_redir_header_32bit_id header_32bit_id;
    };
    uint8_t type_header[288];
    int header_read;
    int type_header_len;
    int type_header_read;
    uint8_t *data;
    int data_len;
    int data_read;
    int to_skip;
    int write_buf_count;
    struct usbredirparser_buf *write_buf;
    uint64_t write_buf_total_size;
};

static void
#if defined __MINGW_PRINTF_FORMAT
__attribute__((format(__MINGW_PRINTF_FORMAT, 3, 4)))
#elif defined __GNUC__
__attribute__((format(printf, 3, 4)))
#endif
va_log(struct usbredirparser_priv *parser, int verbose, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int n;

    n = sprintf(buf, "usbredirparser: ");
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);

    parser->callb.log_func(parser->callb.priv, verbose, buf);
}

#define ERROR(...)   va_log(parser, usbredirparser_error, __VA_ARGS__)
#define WARNING(...) va_log(parser, usbredirparser_warning, __VA_ARGS__)
#define INFO(...)    va_log(parser, usbredirparser_info, __VA_ARGS__)
#define DEBUG(...)    va_log(parser, usbredirparser_debug, __VA_ARGS__)

static inline void
usbredirparser_assert_invariants(const struct usbredirparser_priv *parser)
{
#ifdef ENABLE_EXTRA_CHECKS
    assert(parser != NULL);
    assert(parser->header_read >= 0);
    assert(parser->header_read <= sizeof(parser->header));
    assert(parser->type_header_read >= 0);
    assert(parser->type_header_len <= sizeof(parser->type_header));
    assert(parser->type_header_read <= parser->type_header_len);
    assert(parser->data_len >= 0);
    assert(parser->data_len <= MAX_PACKET_SIZE);
    assert(parser->data_read >= 0);
    assert(parser->data_read <= parser->data_len);
    assert((parser->data_len != 0) ^ (parser->data == NULL));

    int write_buf_count = 0;
    uint64_t total_size = 0;
    const struct usbredirparser_buf *write_buf = parser->write_buf;
    for (; write_buf != NULL ; write_buf = write_buf->next) {
        assert(write_buf->pos >= 0);
        assert(write_buf->len >= 0);
        assert(write_buf->pos <= write_buf->len);
        assert(write_buf->len == 0 || write_buf->buf != NULL);
        write_buf_count++;
        total_size += write_buf->len;
    }
    assert(parser->write_buf_count == write_buf_count);
    assert(parser->write_buf_total_size == total_size);
#endif
}

#if 0 /* Can be enabled and called from random place to test serialization */
static void serialize_test(struct usbredirparser *parser_pub)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;
    struct usbredirparser_buf *wbuf, *next_wbuf;
    uint8_t *data;
    int len;

    if (usbredirparser_serialize(parser_pub, &data, &len))
        return;

    wbuf = parser->write_buf;
    while (wbuf) {
        next_wbuf = wbuf->next;
        free(wbuf->buf);
        free(wbuf);
        wbuf = next_wbuf;
    }
    parser->write_buf = NULL;
    parser->write_buf_count = 0;

    free(parser->data);
    parser->data = NULL;

    parser->type_header_len = parser->data_len = parser->have_peer_caps = 0;

    usbredirparser_unserialize(parser_pub, data, len);
    free(data);
}
#endif

static void usbredirparser_queue(struct usbredirparser *parser, uint32_t type,
    uint64_t id, void *type_header_in, uint8_t *data_in, int data_len);
static int usbredirparser_caps_get_cap(struct usbredirparser_priv *parser,
    uint32_t *caps, int cap);

USBREDIR_VISIBLE
struct usbredirparser *usbredirparser_create(void)
{
    return calloc(1, sizeof(struct usbredirparser_priv));
}

static void usbredirparser_verify_caps(struct usbredirparser_priv *parser,
    uint32_t *caps, const char *desc)
{
    if (usbredirparser_caps_get_cap(parser, caps,
                                    usb_redir_cap_bulk_streams) &&
        !usbredirparser_caps_get_cap(parser, caps,
                                     usb_redir_cap_ep_info_max_packet_size)) {
        ERROR("error %s caps contains cap_bulk_streams without"
              "cap_ep_info_max_packet_size", desc);
        caps[0] &= ~(1 << usb_redir_cap_bulk_streams);
    }
}

USBREDIR_VISIBLE
void usbredirparser_init(struct usbredirparser *parser_pub,
    const char *version, uint32_t *caps, int caps_len, int flags)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;
    struct usb_redir_hello_header hello = { { 0 }, };

    parser->flags = (flags & ~usbredirparser_fl_no_hello);
    if (parser->callb.alloc_lock_func) {
        parser->lock = parser->callb.alloc_lock_func();
    }

    snprintf(hello.version, sizeof(hello.version), "%s", version);
    if (caps_len > USB_REDIR_CAPS_SIZE) {
        caps_len = USB_REDIR_CAPS_SIZE;
    }
    memcpy(parser->our_caps, caps, caps_len * sizeof(uint32_t));
    /* libusbredirparser handles sending the ack internally */
    if (!(flags & usbredirparser_fl_usb_host))
        usbredirparser_caps_set_cap(parser->our_caps,
                                    usb_redir_cap_device_disconnect_ack);
    usbredirparser_verify_caps(parser, parser->our_caps, "our");
    if (!(flags & usbredirparser_fl_no_hello))
        usbredirparser_queue(parser_pub, usb_redir_hello, 0, &hello,
                             (uint8_t *)parser->our_caps,
                             USB_REDIR_CAPS_SIZE * sizeof(uint32_t));
}

USBREDIR_VISIBLE
void usbredirparser_destroy(struct usbredirparser *parser_pub)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;
    struct usbredirparser_buf *wbuf, *next_wbuf;

    free(parser->data);
    parser->data = NULL;

    wbuf = parser->write_buf;
    while (wbuf) {
        next_wbuf = wbuf->next;
        free(wbuf->buf);
        free(wbuf);
        wbuf = next_wbuf;
    }

    if (parser->lock)
        parser->callb.free_lock_func(parser->lock);

    free(parser);
}

USBREDIR_VISIBLE
uint64_t usbredirparser_get_bufferered_output_size(struct usbredirparser *parser_pub)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;
    uint64_t size;

    LOCK(parser);
    size = parser->write_buf_total_size;
    UNLOCK(parser);
    return size;
}

static int usbredirparser_caps_get_cap(struct usbredirparser_priv *parser,
    uint32_t *caps, int cap)
{
    if (cap / 32 >= USB_REDIR_CAPS_SIZE) {
        ERROR("error request for out of bounds cap: %d", cap);
        return 0;
    }
    if (caps[cap / 32] & (1 << (cap % 32))) {
        return 1;
    } else {
        return 0;
    }
}

USBREDIR_VISIBLE
void usbredirparser_caps_set_cap(uint32_t *caps, int cap)
{
    caps[cap / 32] |= 1 << (cap % 32);
}

USBREDIR_VISIBLE
int usbredirparser_have_peer_caps(struct usbredirparser *parser_pub)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;

    return parser->have_peer_caps;
}

USBREDIR_VISIBLE
int usbredirparser_peer_has_cap(struct usbredirparser *parser_pub, int cap)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;
    return usbredirparser_caps_get_cap(parser, parser->peer_caps, cap);
}

USBREDIR_VISIBLE
int usbredirparser_have_cap(struct usbredirparser *parser_pub, int cap)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;
    return usbredirparser_caps_get_cap(parser, parser->our_caps, cap);
}

static int usbredirparser_using_32bits_ids(struct usbredirparser *parser_pub)
{
    return !usbredirparser_have_cap(parser_pub, usb_redir_cap_64bits_ids) ||
           !usbredirparser_peer_has_cap(parser_pub, usb_redir_cap_64bits_ids);
}

static void usbredirparser_handle_hello(struct usbredirparser *parser_pub,
    struct usb_redir_hello_header *hello, uint8_t *data, int data_len)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;
    uint32_t *peer_caps = (uint32_t *)data;
    char buf[64];
    int i;

    if (parser->have_peer_caps) {
        ERROR("Received second hello message, ignoring");
        return;
    }

    /* In case hello->version is not 0 terminated (which would be a protocol
       violation)_ */
    strncpy(buf, hello->version, sizeof(buf));
    buf[sizeof(buf)-1] = '\0';

    memset(parser->peer_caps, 0, sizeof(parser->peer_caps));
    if (data_len > sizeof(parser->peer_caps)) {
        data_len = sizeof(parser->peer_caps);
    }
    for (i = 0; i < data_len / sizeof(uint32_t); i++) {
        parser->peer_caps[i] = peer_caps[i];
    }
    usbredirparser_verify_caps(parser, parser->peer_caps, "peer");
    parser->have_peer_caps = 1;

    INFO("Peer version: %s, using %d-bits ids", buf,
         usbredirparser_using_32bits_ids(parser_pub) ? 32 : 64);

    /* Added in 0.3.2, so no guarantee it is there */
    if (parser->callb.hello_func)
        parser->callb.hello_func(parser->callb.priv, hello);
}

static int usbredirparser_get_header_len(struct usbredirparser *parser_pub)
{
    if (usbredirparser_using_32bits_ids(parser_pub))
        return sizeof(struct usb_redir_header_32bit_id);
    else
        return sizeof(struct usb_redir_header);
}

static int usbredirparser_get_type_header_len(
    struct usbredirparser *parser_pub, int32_t type, int send)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;
    int command_for_host = 0;

    if (parser->flags & usbredirparser_fl_usb_host) {
        command_for_host = 1;
    }
    if (send) {
        command_for_host = !command_for_host;
    }

    switch (type) {
    case usb_redir_hello:
        return sizeof(struct usb_redir_hello_header);
    case usb_redir_device_connect:
        if (!command_for_host) {
            if (usbredirparser_have_cap(parser_pub,
                                    usb_redir_cap_connect_device_version) &&
                usbredirparser_peer_has_cap(parser_pub,
                                    usb_redir_cap_connect_device_version)) {
                return sizeof(struct usb_redir_device_connect_header);
            } else {
                return sizeof(struct usb_redir_device_connect_header_no_device_version);
            }
        } else {
            return -1;
        }
    case usb_redir_device_disconnect:
        if (!command_for_host) {
            return 0;
        } else {
            return -1;
        }
    case usb_redir_reset:
        if (command_for_host) {
            return 0; /* No packet type specific header */
        } else {
            return -1;
        }
    case usb_redir_interface_info:
        if (!command_for_host) {
            return sizeof(struct usb_redir_interface_info_header);
        } else {
            return -1;
        }
    case usb_redir_ep_info:
        if (!command_for_host) {
            if (usbredirparser_have_cap(parser_pub,
                                    usb_redir_cap_bulk_streams) &&
                usbredirparser_peer_has_cap(parser_pub,
                                    usb_redir_cap_bulk_streams)) {
                return sizeof(struct usb_redir_ep_info_header);
            } else if (usbredirparser_have_cap(parser_pub,
                                    usb_redir_cap_ep_info_max_packet_size) &&
                       usbredirparser_peer_has_cap(parser_pub,
                                    usb_redir_cap_ep_info_max_packet_size)) {
                return sizeof(struct usb_redir_ep_info_header_no_max_streams);
            } else {
                return sizeof(struct usb_redir_ep_info_header_no_max_pktsz);
            }
        } else {
            return -1;
        }
    case usb_redir_set_configuration:
        if (command_for_host) {
            return sizeof(struct usb_redir_set_configuration_header);
        } else {
            return -1; /* Should never be send to a guest */
        }
    case usb_redir_get_configuration:
        if (command_for_host) {
            return 0; /* No packet type specific header */
        } else {
            return -1;
        }
    case usb_redir_configuration_status:
        if (!command_for_host) {
            return sizeof(struct usb_redir_configuration_status_header);
        } else {
            return -1;
        }
    case usb_redir_set_alt_setting:
        if (command_for_host) {
            return sizeof(struct usb_redir_set_alt_setting_header);
        } else {
            return -1;
        }
    case usb_redir_get_alt_setting:
        if (command_for_host) {
            return sizeof(struct usb_redir_get_alt_setting_header);
        } else {
            return -1;
        }
    case usb_redir_alt_setting_status:
        if (!command_for_host) {
            return sizeof(struct usb_redir_alt_setting_status_header);
        } else {
            return -1;
        }
    case usb_redir_start_iso_stream:
        if (command_for_host) {
            return sizeof(struct usb_redir_start_iso_stream_header);
        } else {
            return -1;
        }
    case usb_redir_stop_iso_stream:
        if (command_for_host) {
            return sizeof(struct usb_redir_stop_iso_stream_header);
        } else {
            return -1;
        }
    case usb_redir_iso_stream_status:
        if (!command_for_host) {
            return sizeof(struct usb_redir_iso_stream_status_header);
        } else {
            return -1;
        }
    case usb_redir_start_interrupt_receiving:
        if (command_for_host) {
            return sizeof(struct usb_redir_start_interrupt_receiving_header);
        } else {
            return -1;
        }
    case usb_redir_stop_interrupt_receiving:
        if (command_for_host) {
            return sizeof(struct usb_redir_stop_interrupt_receiving_header);
        } else {
            return -1;
        }
    case usb_redir_interrupt_receiving_status:
        if (!command_for_host) {
            return sizeof(struct usb_redir_interrupt_receiving_status_header);
        } else {
            return -1;
        }
    case usb_redir_alloc_bulk_streams:
        if (command_for_host) {
            return sizeof(struct usb_redir_alloc_bulk_streams_header);
        } else {
            return -1;
        }
    case usb_redir_free_bulk_streams:
        if (command_for_host) {
            return sizeof(struct usb_redir_free_bulk_streams_header);
        } else {
            return -1;
        }
    case usb_redir_bulk_streams_status:
        if (!command_for_host) {
            return sizeof(struct usb_redir_bulk_streams_status_header);
        } else {
            return -1;
        }
    case usb_redir_cancel_data_packet:
        if (command_for_host) {
            return 0; /* No packet type specific header */
        } else {
            return -1;
        }
    case usb_redir_filter_reject:
        if (command_for_host) {
            return 0;
        } else {
            return -1;
        }
    case usb_redir_filter_filter:
        return 0;
    case usb_redir_device_disconnect_ack:
        if (command_for_host) {
            return 0;
        } else {
            return -1;
        }
    case usb_redir_start_bulk_receiving:
        if (command_for_host) {
            return sizeof(struct usb_redir_start_bulk_receiving_header);
        } else {
            return -1;
        }
    case usb_redir_stop_bulk_receiving:
        if (command_for_host) {
            return sizeof(struct usb_redir_stop_bulk_receiving_header);
        } else {
            return -1;
        }
    case usb_redir_bulk_receiving_status:
        if (!command_for_host) {
            return sizeof(struct usb_redir_bulk_receiving_status_header);
        } else {
            return -1;
        }
    case usb_redir_control_packet:
        return sizeof(struct usb_redir_control_packet_header);
    case usb_redir_bulk_packet:
        if (usbredirparser_have_cap(parser_pub,
                                usb_redir_cap_32bits_bulk_length) &&
            usbredirparser_peer_has_cap(parser_pub,
                                usb_redir_cap_32bits_bulk_length)) {
            return sizeof(struct usb_redir_bulk_packet_header);
        } else {
            return sizeof(struct usb_redir_bulk_packet_header_16bit_length);
        }
    case usb_redir_iso_packet:
        return sizeof(struct usb_redir_iso_packet_header);
    case usb_redir_interrupt_packet:
        return sizeof(struct usb_redir_interrupt_packet_header);
    case usb_redir_buffered_bulk_packet:
        if (!command_for_host) {
            return sizeof(struct usb_redir_buffered_bulk_packet_header);
        } else {
            return -1;
        }
    default:
        return -1;
    }
}

/* Note this function only checks if extra data is allowed for the
   packet type being read at all, a check if it is actually allowed
   given the direction of the packet + ep is done in _verify_type_header */
static int usbredirparser_expect_extra_data(struct usbredirparser_priv *parser)
{
    switch (parser->header.type) {
    case usb_redir_hello: /* For the variable length capabilities array */
    case usb_redir_filter_filter:
    case usb_redir_control_packet:
    case usb_redir_bulk_packet:
    case usb_redir_iso_packet:
    case usb_redir_interrupt_packet:
    case usb_redir_buffered_bulk_packet:
        return 1;
    default:
        return 0;
    }
}

static int usbredirparser_verify_bulk_recv_cap(
    struct usbredirparser *parser_pub, int send)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;

    if ((send && !usbredirparser_peer_has_cap(parser_pub,
                                              usb_redir_cap_bulk_receiving)) ||
        (!send && !usbredirparser_have_cap(parser_pub,
                                           usb_redir_cap_bulk_receiving))) {
        ERROR("error bulk_receiving without cap_bulk_receiving");
        return 0;
    }
    return 1; /* Verify ok */
}

static int usbredirparser_verify_type_header(
    struct usbredirparser *parser_pub,
    int32_t type, void *header, uint8_t *data, int data_len, int send)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;
    int command_for_host = 0, expect_extra_data = 0;
    uint32_t length = 0;
    int ep = -1;

    if (parser->flags & usbredirparser_fl_usb_host) {
        command_for_host = 1;
    }
    if (send) {
        command_for_host = !command_for_host;
    }

    switch (type) {
    case usb_redir_interface_info: {
        struct usb_redir_interface_info_header *intf_info = header;

        if (intf_info->interface_count > 32) {
            ERROR("error interface_count > 32");
            return 0;
        }
        break;
    }
    case usb_redir_start_interrupt_receiving: {
        struct usb_redir_start_interrupt_receiving_header *start_int = header;

        if (!(start_int->endpoint & 0x80)) {
            ERROR("start int receiving on non input ep %02x",
                  start_int->endpoint);
            return 0;
        }
        break;
    }
    case usb_redir_stop_interrupt_receiving: {
        struct usb_redir_stop_interrupt_receiving_header *stop_int = header;

        if (!(stop_int->endpoint & 0x80)) {
            ERROR("stop int receiving on non input ep %02x",
                  stop_int->endpoint);
            return 0;
        }
        break;
    }
    case usb_redir_interrupt_receiving_status: {
        struct usb_redir_interrupt_receiving_status_header *int_status = header;

        if (!(int_status->endpoint & 0x80)) {
            ERROR("int receiving status for non input ep %02x",
                  int_status->endpoint);
            return 0;
        }
        break;
    }
    case usb_redir_filter_reject:
        if ((send && !usbredirparser_peer_has_cap(parser_pub,
                                             usb_redir_cap_filter)) ||
            (!send && !usbredirparser_have_cap(parser_pub,
                                             usb_redir_cap_filter))) {
            ERROR("error filter_reject without cap_filter");
            return 0;
        }
        break;
    case usb_redir_filter_filter:
        if ((send && !usbredirparser_peer_has_cap(parser_pub,
                                             usb_redir_cap_filter)) ||
            (!send && !usbredirparser_have_cap(parser_pub,
                                             usb_redir_cap_filter))) {
            ERROR("error filter_filter without cap_filter");
            return 0;
        }
        if (data_len < 1) {
            ERROR("error filter_filter without data");
            return 0;
        }
        if (data[data_len - 1] != 0) {
            ERROR("error non 0 terminated filter_filter data");
            return 0;
        }
        break;
    case usb_redir_device_disconnect_ack:
        if ((send && !usbredirparser_peer_has_cap(parser_pub,
                                     usb_redir_cap_device_disconnect_ack)) ||
            (!send && !usbredirparser_have_cap(parser_pub,
                                     usb_redir_cap_device_disconnect_ack))) {
            ERROR("error device_disconnect_ack without cap_device_disconnect_ack");
            return 0;
        }
        break;
    case usb_redir_start_bulk_receiving: {
        struct usb_redir_start_bulk_receiving_header *start_bulk = header;

        if (!usbredirparser_verify_bulk_recv_cap(parser_pub, send)) {
            return 0;
        }
        if (start_bulk->bytes_per_transfer > MAX_BULK_TRANSFER_SIZE) {
            ERROR("start bulk receiving length exceeds limits %u > %u",
                  start_bulk->bytes_per_transfer, MAX_BULK_TRANSFER_SIZE);
            return 0;
        }
        if (!(start_bulk->endpoint & 0x80)) {
            ERROR("start bulk receiving on non input ep %02x",
                  start_bulk->endpoint);
            return 0;
        }
        break;
    }
    case usb_redir_stop_bulk_receiving: {
        struct usb_redir_stop_bulk_receiving_header *stop_bulk = header;

        if (!usbredirparser_verify_bulk_recv_cap(parser_pub, send)) {
            return 0;
        }
        if (!(stop_bulk->endpoint & 0x80)) {
            ERROR("stop bulk receiving on non input ep %02x",
                  stop_bulk->endpoint);
            return 0;
        }
        break;
    }
    case usb_redir_bulk_receiving_status: {
        struct usb_redir_bulk_receiving_status_header *bulk_status = header;

        if (!usbredirparser_verify_bulk_recv_cap(parser_pub, send)) {
            return 0;
        }
        if (!(bulk_status->endpoint & 0x80)) {
            ERROR("bulk receiving status for non input ep %02x",
                  bulk_status->endpoint);
            return 0;
        }
        break;
    }
    case usb_redir_control_packet:
        length = ((struct usb_redir_control_packet_header *)header)->length;
        ep = ((struct usb_redir_control_packet_header *)header)->endpoint;
        break;
    case usb_redir_bulk_packet: {
        struct usb_redir_bulk_packet_header *bulk_packet = header;
        if (usbredirparser_have_cap(parser_pub,
                                usb_redir_cap_32bits_bulk_length) &&
            usbredirparser_peer_has_cap(parser_pub,
                                usb_redir_cap_32bits_bulk_length)) {
            length = (((uint32_t)bulk_packet->length_high) << 16) | bulk_packet->length;
        } else {
            length = bulk_packet->length;
            if (!send)
                bulk_packet->length_high = 0;
        }
        if (length > MAX_BULK_TRANSFER_SIZE) {
            ERROR("bulk transfer length exceeds limits %u > %u",
                  (uint32_t)length, MAX_BULK_TRANSFER_SIZE);
            return 0;
        }
        ep = bulk_packet->endpoint;
        break;
    }
    case usb_redir_iso_packet:
        length = ((struct usb_redir_iso_packet_header *)header)->length;
        ep = ((struct usb_redir_iso_packet_header *)header)->endpoint;
        break;
    case usb_redir_interrupt_packet:
        length = ((struct usb_redir_interrupt_packet_header *)header)->length;
        ep = ((struct usb_redir_interrupt_packet_header *)header)->endpoint;
        break;
    case usb_redir_buffered_bulk_packet: {
        struct usb_redir_buffered_bulk_packet_header *buf_bulk_pkt = header;
        length = buf_bulk_pkt->length;
        if (!usbredirparser_verify_bulk_recv_cap(parser_pub, send)) {
            return 0;
        }
        if ((uint32_t)length > MAX_BULK_TRANSFER_SIZE) {
            ERROR("buffered bulk transfer length exceeds limits %u > %u",
                  (uint32_t)length, MAX_BULK_TRANSFER_SIZE);
            return 0;
        }
        ep = buf_bulk_pkt->endpoint;
        break;
    }
    }

    if (ep != -1) {
        if (((ep & 0x80) && !command_for_host) ||
            (!(ep & 0x80) && command_for_host)) {
            expect_extra_data = 1;
        }
        if (expect_extra_data) {
            if (data_len != length) {
                ERROR("error data len %d != header len %d ep %02X",
                      data_len, length, ep);
                return 0;
            }
        } else {
            if (data || data_len) {
                ERROR("error unexpected extra data ep %02X", ep);
                return 0;
            }
            switch (type) {
            case usb_redir_iso_packet:
                ERROR("error iso packet send in wrong direction");
                return 0;
            case usb_redir_interrupt_packet:
                if (command_for_host) {
                    ERROR("error interrupt packet send in wrong direction");
                    return 0;
                }
                break;
            case usb_redir_buffered_bulk_packet:
                ERROR("error buffered bulk packet send in wrong direction");
                return 0;
            }
        }
    }

    return 1; /* Verify ok */
}

static void usbredirparser_call_type_func(struct usbredirparser *parser_pub,
    bool *data_ownership_transferred)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;
    uint64_t id;

    if (usbredirparser_using_32bits_ids(parser_pub))
        id = parser->header_32bit_id.id;
    else
        id = parser->header.id;

    switch (parser->header.type) {
    case usb_redir_hello:
        usbredirparser_handle_hello(parser_pub,
            (struct usb_redir_hello_header *)parser->type_header,
            parser->data, parser->data_len);
        break;
    case usb_redir_device_connect:
        parser->callb.device_connect_func(parser->callb.priv,
            (struct usb_redir_device_connect_header *)parser->type_header);
        break;
    case usb_redir_device_disconnect:
        parser->callb.device_disconnect_func(parser->callb.priv);
        if (usbredirparser_peer_has_cap(parser_pub,
                                        usb_redir_cap_device_disconnect_ack))
            usbredirparser_queue(parser_pub, usb_redir_device_disconnect_ack,
                                 0, NULL, NULL, 0);
        break;
    case usb_redir_reset:
        parser->callb.reset_func(parser->callb.priv);
        break;
    case usb_redir_interface_info:
        parser->callb.interface_info_func(parser->callb.priv,
            (struct usb_redir_interface_info_header *)parser->type_header);
        break;
    case usb_redir_ep_info:
        parser->callb.ep_info_func(parser->callb.priv,
            (struct usb_redir_ep_info_header *)parser->type_header);
        break;
    case usb_redir_set_configuration:
        parser->callb.set_configuration_func(parser->callb.priv, id,
            (struct usb_redir_set_configuration_header *)parser->type_header);
        break;
    case usb_redir_get_configuration:
        parser->callb.get_configuration_func(parser->callb.priv, id);
        break;
    case usb_redir_configuration_status:
        parser->callb.configuration_status_func(parser->callb.priv, id,
          (struct usb_redir_configuration_status_header *)parser->type_header);
        break;
    case usb_redir_set_alt_setting:
        parser->callb.set_alt_setting_func(parser->callb.priv, id,
            (struct usb_redir_set_alt_setting_header *)parser->type_header);
        break;
    case usb_redir_get_alt_setting:
        parser->callb.get_alt_setting_func(parser->callb.priv, id,
            (struct usb_redir_get_alt_setting_header *)parser->type_header);
        break;
    case usb_redir_alt_setting_status:
        parser->callb.alt_setting_status_func(parser->callb.priv, id,
            (struct usb_redir_alt_setting_status_header *)parser->type_header);
        break;
    case usb_redir_start_iso_stream:
        parser->callb.start_iso_stream_func(parser->callb.priv, id,
            (struct usb_redir_start_iso_stream_header *)parser->type_header);
        break;
    case usb_redir_stop_iso_stream:
        parser->callb.stop_iso_stream_func(parser->callb.priv, id,
            (struct usb_redir_stop_iso_stream_header *)parser->type_header);
        break;
    case usb_redir_iso_stream_status:
        parser->callb.iso_stream_status_func(parser->callb.priv, id,
            (struct usb_redir_iso_stream_status_header *)parser->type_header);
        break;
    case usb_redir_start_interrupt_receiving:
        parser->callb.start_interrupt_receiving_func(parser->callb.priv, id,
            (struct usb_redir_start_interrupt_receiving_header *)
            parser->type_header);
        break;
    case usb_redir_stop_interrupt_receiving:
        parser->callb.stop_interrupt_receiving_func(parser->callb.priv, id,
            (struct usb_redir_stop_interrupt_receiving_header *)
            parser->type_header);
        break;
    case usb_redir_interrupt_receiving_status:
        parser->callb.interrupt_receiving_status_func(parser->callb.priv, id,
            (struct usb_redir_interrupt_receiving_status_header *)
            parser->type_header);
        break;
    case usb_redir_alloc_bulk_streams:
        parser->callb.alloc_bulk_streams_func(parser->callb.priv, id,
            (struct usb_redir_alloc_bulk_streams_header *)parser->type_header);
        break;
    case usb_redir_free_bulk_streams:
        parser->callb.free_bulk_streams_func(parser->callb.priv, id,
            (struct usb_redir_free_bulk_streams_header *)parser->type_header);
        break;
    case usb_redir_bulk_streams_status:
        parser->callb.bulk_streams_status_func(parser->callb.priv, id,
          (struct usb_redir_bulk_streams_status_header *)parser->type_header);
        break;
    case usb_redir_cancel_data_packet:
        parser->callb.cancel_data_packet_func(parser->callb.priv, id);
        break;
    case usb_redir_filter_reject:
        parser->callb.filter_reject_func(parser->callb.priv);
        break;
    case usb_redir_filter_filter: {
        struct usbredirfilter_rule *rules;
        int r, count;

        r = usbredirfilter_string_to_rules((char *)parser->data, ",", "|",
                                           &rules, &count);
        if (r) {
            ERROR("error parsing filter (%d), ignoring filter message", r);
            break;
        }
        parser->callb.filter_filter_func(parser->callb.priv, rules, count);
        break;
    }
    case usb_redir_device_disconnect_ack:
        parser->callb.device_disconnect_ack_func(parser->callb.priv);
        break;
    case usb_redir_start_bulk_receiving:
        parser->callb.start_bulk_receiving_func(parser->callb.priv, id,
            (struct usb_redir_start_bulk_receiving_header *)
            parser->type_header);
        break;
    case usb_redir_stop_bulk_receiving:
        parser->callb.stop_bulk_receiving_func(parser->callb.priv, id,
            (struct usb_redir_stop_bulk_receiving_header *)
            parser->type_header);
        break;
    case usb_redir_bulk_receiving_status:
        parser->callb.bulk_receiving_status_func(parser->callb.priv, id,
            (struct usb_redir_bulk_receiving_status_header *)
            parser->type_header);
        break;
    case usb_redir_control_packet:
        *data_ownership_transferred = true;
        parser->callb.control_packet_func(parser->callb.priv, id,
            (struct usb_redir_control_packet_header *)parser->type_header,
            parser->data, parser->data_len);
        break;
    case usb_redir_bulk_packet:
        *data_ownership_transferred = true;
        parser->callb.bulk_packet_func(parser->callb.priv, id,
            (struct usb_redir_bulk_packet_header *)parser->type_header,
            parser->data, parser->data_len);
        break;
    case usb_redir_iso_packet:
        *data_ownership_transferred = true;
        parser->callb.iso_packet_func(parser->callb.priv, id,
            (struct usb_redir_iso_packet_header *)parser->type_header,
            parser->data, parser->data_len);
        break;
    case usb_redir_interrupt_packet:
        *data_ownership_transferred = true;
        parser->callb.interrupt_packet_func(parser->callb.priv, id,
            (struct usb_redir_interrupt_packet_header *)parser->type_header,
            parser->data, parser->data_len);
        break;
    case usb_redir_buffered_bulk_packet:
        *data_ownership_transferred = true;
        parser->callb.buffered_bulk_packet_func(parser->callb.priv, id,
          (struct usb_redir_buffered_bulk_packet_header *)parser->type_header,
          parser->data, parser->data_len);
        break;
    }
}

USBREDIR_VISIBLE
int usbredirparser_do_read(struct usbredirparser *parser_pub)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;
    int r, header_len, type_header_len, data_len;
    bool data_ownership_transferred;
    uint8_t *dest;

    header_len = usbredirparser_get_header_len(parser_pub);

    usbredirparser_assert_invariants(parser);
    /* Skip forward to next packet (only used in error conditions) */
    while (parser->to_skip > 0) {
        uint8_t buf[65536];
        r = (parser->to_skip > sizeof(buf)) ? sizeof(buf) : parser->to_skip;
        r = parser->callb.read_func(parser->callb.priv, buf, r);
        if (r <= 0) {
            usbredirparser_assert_invariants(parser);
            return r;
        }
        parser->to_skip -= r;
    }

    /* Consume data until read would block or returns an error */
    while (1) {
        if (parser->header_read < header_len) {
            r = header_len - parser->header_read;
            dest = (uint8_t *)&parser->header + parser->header_read;
        } else if (parser->type_header_read < parser->type_header_len) {
            r = parser->type_header_len - parser->type_header_read;
            dest = parser->type_header + parser->type_header_read;
        } else {
            r = parser->data_len - parser->data_read;
            dest = parser->data + parser->data_read;
        }

        if (r > 0) {
            r = parser->callb.read_func(parser->callb.priv, dest, r);
            if (r <= 0) {
                usbredirparser_assert_invariants(parser);
                return r;
            }
        }

        if (parser->header_read < header_len) {
            parser->header_read += r;
            if (parser->header_read == header_len) {
                type_header_len =
                    usbredirparser_get_type_header_len(parser_pub,
                                                       parser->header.type, 0);
                if (type_header_len < 0) {
                    ERROR("error invalid usb-redir packet type: %u",
                          parser->header.type);
                    parser->to_skip = parser->header.length;
                    parser->header_read = 0;
                    usbredirparser_assert_invariants(parser);
                    return usbredirparser_read_parse_error;
                }
                /* This should never happen */
                if (type_header_len > sizeof(parser->type_header)) {
                    ERROR("error type specific header buffer too small, please report!!");
                    parser->to_skip = parser->header.length;
                    parser->header_read = 0;
                    usbredirparser_assert_invariants(parser);
                    return usbredirparser_read_parse_error;
                }
                if (parser->header.length > MAX_PACKET_SIZE) {
                    ERROR("packet length of %d larger than permitted %d bytes",
                          parser->header.length, MAX_PACKET_SIZE);
                    parser->to_skip = parser->header.length;
                    parser->header_read = 0;
                    usbredirparser_assert_invariants(parser);
                    return usbredirparser_read_parse_error;
                }
                if ((int)parser->header.length < type_header_len ||
                    ((int)parser->header.length > type_header_len &&
                     !usbredirparser_expect_extra_data(parser))) {
                    ERROR("error invalid packet type %u length: %u",
                          parser->header.type, parser->header.length);
                    parser->to_skip = parser->header.length;
                    parser->header_read = 0;
                    usbredirparser_assert_invariants(parser);
                    return usbredirparser_read_parse_error;
                }
                data_len = parser->header.length - type_header_len;
                if (data_len) {
                    parser->data = malloc(data_len);
                    if (!parser->data) {
                        ERROR("Out of memory allocating data buffer");
                        parser->to_skip = parser->header.length;
                        parser->header_read = 0;
                        usbredirparser_assert_invariants(parser);
                        return usbredirparser_read_parse_error;
                    }
                }
                parser->type_header_len = type_header_len;
                parser->data_len = data_len;
            }
        } else if (parser->type_header_read < parser->type_header_len) {
            parser->type_header_read += r;
        } else {
            parser->data_read += r;
            if (parser->data_read == parser->data_len) {
                r = usbredirparser_verify_type_header(parser_pub,
                         parser->header.type, parser->type_header,
                         parser->data, parser->data_len, 0);
                data_ownership_transferred = false;
                if (r) {
                    usbredirparser_call_type_func(parser_pub,
                                                  &data_ownership_transferred);
                }
                if (!data_ownership_transferred) {
                    free(parser->data);
                }
                parser->header_read = 0;
                parser->type_header_len  = 0;
                parser->type_header_read = 0;
                parser->data_len  = 0;
                parser->data_read = 0;
                parser->data = NULL;
                if (!r) {
                    usbredirparser_assert_invariants(parser);
                    return usbredirparser_read_parse_error;
                }
                /* header len may change if this was an hello packet */
                header_len = usbredirparser_get_header_len(parser_pub);
            }
        }
    }
}

USBREDIR_VISIBLE
int usbredirparser_has_data_to_write(struct usbredirparser *parser_pub)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;
    return parser->write_buf_count;
}

USBREDIR_VISIBLE
int usbredirparser_do_write(struct usbredirparser *parser_pub)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;
    struct usbredirparser_buf* wbuf;
    int w, ret = 0;

    LOCK(parser);
    assert((parser->write_buf_count != 0) ^ (parser->write_buf == NULL));

    for (;;) {
        wbuf = parser->write_buf;
        if (!wbuf)
            break;

        w = wbuf->len - wbuf->pos;
        w = parser->callb.write_func(parser->callb.priv,
                                     wbuf->buf + wbuf->pos, w);
        if (w <= 0) {
            ret = w;
            break;
        }

        /* See usbredirparser_write documentation */
        if ((parser->flags & usbredirparser_fl_write_cb_owns_buffer) &&
                w != wbuf->len)
            abort();

        wbuf->pos += w;
        if (wbuf->pos == wbuf->len) {
            parser->write_buf = wbuf->next;
            if (!(parser->flags & usbredirparser_fl_write_cb_owns_buffer))
                free(wbuf->buf);

            parser->write_buf_total_size -= wbuf->len;
            parser->write_buf_count--;
            free(wbuf);
        }
    }
    UNLOCK(parser);
    return ret;
}

USBREDIR_VISIBLE
void usbredirparser_free_write_buffer(struct usbredirparser *parser,
    uint8_t *data)
{
    free(data);
}

USBREDIR_VISIBLE
void usbredirparser_free_packet_data(struct usbredirparser *parser,
    uint8_t *data)
{
    free(data);
}

static void usbredirparser_queue(struct usbredirparser *parser_pub,
    uint32_t type, uint64_t id, void *type_header_in,
    uint8_t *data_in, int data_len)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;
    uint8_t *buf, *type_header_out, *data_out;
    struct usb_redir_header *header;
    struct usbredirparser_buf *wbuf, *new_wbuf;
    int header_len, type_header_len, total_size;

    header_len = usbredirparser_get_header_len(parser_pub);
    type_header_len = usbredirparser_get_type_header_len(parser_pub, type, 1);
    if (type_header_len < 0) { /* This should never happen */
        ERROR("error packet type unknown with internal call, please report!!");
        return;
    }

    if (!usbredirparser_verify_type_header(parser_pub, type, type_header_in,
                                           data_in, data_len, 1)) {
        ERROR("error usbredirparser_send_* call invalid params, please report!!");
        return;
    }

    total_size = header_len + type_header_len + data_len;
    new_wbuf = calloc(1, sizeof(*new_wbuf));
    buf = malloc(total_size);
    if (!new_wbuf || !buf) {
        ERROR("Out of memory allocating buffer to send packet, dropping!");
        free(new_wbuf); free(buf);
        return;
    }

    new_wbuf->buf = buf;
    new_wbuf->len = total_size;

    header = (struct usb_redir_header *)buf;
    type_header_out = buf + header_len;
    data_out = type_header_out + type_header_len;

    header->type   = type;
    header->length = type_header_len + data_len;
    if (usbredirparser_using_32bits_ids(parser_pub))
        ((struct usb_redir_header_32bit_id *)header)->id = id;
    else
        header->id = id;
    memcpy(type_header_out, type_header_in, type_header_len);
    memcpy(data_out, data_in, data_len);

    LOCK(parser);
    if (!parser->write_buf) {
        parser->write_buf = new_wbuf;
    } else {
        /* limiting the write_buf's stack depth is our users responsibility */
        wbuf = parser->write_buf;
        while (wbuf->next)
            wbuf = wbuf->next;

        wbuf->next = new_wbuf;
    }
    parser->write_buf_total_size += total_size;
    parser->write_buf_count++;
    UNLOCK(parser);
}

USBREDIR_VISIBLE
void usbredirparser_send_device_connect(struct usbredirparser *parser,
    struct usb_redir_device_connect_header *device_connect)
{
    usbredirparser_queue(parser, usb_redir_device_connect, 0, device_connect,
                         NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_device_disconnect(struct usbredirparser *parser)
{
    usbredirparser_queue(parser, usb_redir_device_disconnect, 0, NULL,
                         NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_reset(struct usbredirparser *parser)
{
    usbredirparser_queue(parser, usb_redir_reset, 0, NULL, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_interface_info(struct usbredirparser *parser,
    struct usb_redir_interface_info_header *interface_info)
{
    usbredirparser_queue(parser, usb_redir_interface_info, 0, interface_info,
                         NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_ep_info(struct usbredirparser *parser,
    struct usb_redir_ep_info_header *ep_info)
{
    usbredirparser_queue(parser, usb_redir_ep_info, 0, ep_info, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_set_configuration(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_set_configuration_header *set_configuration)
{
    usbredirparser_queue(parser, usb_redir_set_configuration, id,
                         set_configuration, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_get_configuration(struct usbredirparser *parser,
    uint64_t id)
{
    usbredirparser_queue(parser, usb_redir_get_configuration, id,
                         NULL, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_configuration_status(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_configuration_status_header *configuration_status)
{
    usbredirparser_queue(parser, usb_redir_configuration_status, id,
                         configuration_status, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_set_alt_setting(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_set_alt_setting_header *set_alt_setting)
{
    usbredirparser_queue(parser, usb_redir_set_alt_setting, id,
                         set_alt_setting, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_get_alt_setting(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_get_alt_setting_header *get_alt_setting)
{
    usbredirparser_queue(parser, usb_redir_get_alt_setting, id,
                         get_alt_setting, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_alt_setting_status(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_alt_setting_status_header *alt_setting_status)
{
    usbredirparser_queue(parser, usb_redir_alt_setting_status, id,
                         alt_setting_status, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_start_iso_stream(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_start_iso_stream_header *start_iso_stream)
{
    usbredirparser_queue(parser, usb_redir_start_iso_stream, id,
                         start_iso_stream, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_stop_iso_stream(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_stop_iso_stream_header *stop_iso_stream)
{
    usbredirparser_queue(parser, usb_redir_stop_iso_stream, id,
                         stop_iso_stream, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_iso_stream_status(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_iso_stream_status_header *iso_stream_status)
{
    usbredirparser_queue(parser, usb_redir_iso_stream_status, id,
                         iso_stream_status, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_start_interrupt_receiving(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_start_interrupt_receiving_header *start_interrupt_receiving)
{
    usbredirparser_queue(parser, usb_redir_start_interrupt_receiving, id,
                         start_interrupt_receiving, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_stop_interrupt_receiving(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_stop_interrupt_receiving_header *stop_interrupt_receiving)
{
    usbredirparser_queue(parser, usb_redir_stop_interrupt_receiving, id,
                         stop_interrupt_receiving, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_interrupt_receiving_status(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_interrupt_receiving_status_header *interrupt_receiving_status)
{
    usbredirparser_queue(parser, usb_redir_interrupt_receiving_status, id,
                         interrupt_receiving_status, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_alloc_bulk_streams(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_alloc_bulk_streams_header *alloc_bulk_streams)
{
    usbredirparser_queue(parser, usb_redir_alloc_bulk_streams, id,
                         alloc_bulk_streams, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_free_bulk_streams(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_free_bulk_streams_header *free_bulk_streams)
{
    usbredirparser_queue(parser, usb_redir_free_bulk_streams, id,
                         free_bulk_streams, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_bulk_streams_status(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_bulk_streams_status_header *bulk_streams_status)
{
    usbredirparser_queue(parser, usb_redir_bulk_streams_status, id,
                         bulk_streams_status, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_cancel_data_packet(struct usbredirparser *parser,
    uint64_t id)
{
    usbredirparser_queue(parser, usb_redir_cancel_data_packet, id,
                         NULL, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_filter_reject(struct usbredirparser *parser)
{
    if (!usbredirparser_peer_has_cap(parser, usb_redir_cap_filter))
        return;

    usbredirparser_queue(parser, usb_redir_filter_reject, 0, NULL, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_filter_filter(struct usbredirparser *parser_pub,
    const struct usbredirfilter_rule *rules, int rules_count)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;
    char *str;

    if (!usbredirparser_peer_has_cap(parser_pub, usb_redir_cap_filter))
        return;

    str = usbredirfilter_rules_to_string(rules, rules_count, ",", "|");
    if (!str) {
        ERROR("error creating filter string, not sending filter");
        return;
    }
    usbredirparser_queue(parser_pub, usb_redir_filter_filter, 0, NULL,
                         (uint8_t *)str, strlen(str) + 1);
    free(str);
}

USBREDIR_VISIBLE
void usbredirparser_send_start_bulk_receiving(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_start_bulk_receiving_header *start_bulk_receiving)
{
    usbredirparser_queue(parser, usb_redir_start_bulk_receiving, id,
                         start_bulk_receiving, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_stop_bulk_receiving(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_stop_bulk_receiving_header *stop_bulk_receiving)
{
    usbredirparser_queue(parser, usb_redir_stop_bulk_receiving, id,
                         stop_bulk_receiving, NULL, 0);
}

USBREDIR_VISIBLE
void usbredirparser_send_bulk_receiving_status(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_bulk_receiving_status_header *bulk_receiving_status)
{
    usbredirparser_queue(parser, usb_redir_bulk_receiving_status, id,
                         bulk_receiving_status, NULL, 0);
}

/* Data packets: */
USBREDIR_VISIBLE
void usbredirparser_send_control_packet(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_control_packet_header *control_header,
    uint8_t *data, int data_len)
{
    usbredirparser_queue(parser, usb_redir_control_packet, id, control_header,
                         data, data_len);
}

USBREDIR_VISIBLE
void usbredirparser_send_bulk_packet(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_bulk_packet_header *bulk_header,
    uint8_t *data, int data_len)
{
    usbredirparser_queue(parser, usb_redir_bulk_packet, id, bulk_header,
                         data, data_len);
}

USBREDIR_VISIBLE
void usbredirparser_send_iso_packet(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_iso_packet_header *iso_header,
    uint8_t *data, int data_len)
{
    usbredirparser_queue(parser, usb_redir_iso_packet, id, iso_header,
                         data, data_len);
}

USBREDIR_VISIBLE
void usbredirparser_send_interrupt_packet(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_interrupt_packet_header *interrupt_header,
    uint8_t *data, int data_len)
{
    usbredirparser_queue(parser, usb_redir_interrupt_packet, id,
                         interrupt_header, data, data_len);
}

USBREDIR_VISIBLE
void usbredirparser_send_buffered_bulk_packet(struct usbredirparser *parser,
    uint64_t id,
    struct usb_redir_buffered_bulk_packet_header *buffered_bulk_header,
    uint8_t *data, int data_len)
{
    usbredirparser_queue(parser, usb_redir_buffered_bulk_packet, id,
                         buffered_bulk_header, data, data_len);
}

/****** Serialization support ******/

#define USBREDIRPARSER_SERIALIZE_BUF_SIZE     65536

/* Serialization format, send and receiving endian are expected to be the same!
    uint32 MAGIC: 0x55525031 ascii: URP1 (UsbRedirParser version 1)
    uint32 len: length of the entire serialized state, including MAGIC
    uint32 our_caps_len
    uint32 our_caps[our_caps_len]
    uint32 peer_caps_len
    uint32 peer_caps[peer_caps_len]
    uint32 to_skip
    uint32 header_read
    uint8  header[header_read]
    uint32 type_header_read
    uint8  type_header[type_header_read]
    uint32 data_read
    uint8  data[data_read]
    uint32 write_buf_count: followed by write_buf_count times:
        uint32 write_buf_len
        uint8  write_buf_data[write_buf_len]
*/

static int serialize_alloc(struct usbredirparser_priv *parser,
                           uint8_t **state, uint8_t **pos,
                           uint32_t *remain, uint32_t needed)
{
    uint8_t *old_state = *state;
    uint32_t used, size;

    if (*remain >= needed)
        return 0;

    used = *pos - *state;
    size = (used + needed + USBREDIRPARSER_SERIALIZE_BUF_SIZE - 1) &
           ~(USBREDIRPARSER_SERIALIZE_BUF_SIZE - 1);

    *state = realloc(*state, size);
    if (!*state) {
        free(old_state);
        ERROR("Out of memory allocating serialization buffer");
        return -1;
    }

    *pos = *state + used;
    *remain = size - used;

    return 0;
}

static int serialize_int(struct usbredirparser_priv *parser,
                         uint8_t **state, uint8_t **pos, uint32_t *remain,
                         uint32_t val, const char *desc)
{
    DEBUG("serializing int %08x : %s", val, desc);

    if (serialize_alloc(parser, state, pos, remain, sizeof(uint32_t)))
        return -1;

    memcpy(*pos, &val, sizeof(uint32_t));
    *pos += sizeof(uint32_t);
    *remain -= sizeof(uint32_t);

    return 0;
}

static int unserialize_int(struct usbredirparser_priv *parser,
                           uint8_t **pos, uint32_t *remain, uint32_t *val,
                           const char *desc)
{
    if (*remain < sizeof(uint32_t)) {
        ERROR("error buffer underrun while unserializing state");
        return -1;
    }
    memcpy(val, *pos, sizeof(uint32_t));
    *pos += sizeof(uint32_t);
    *remain -= sizeof(uint32_t);

    DEBUG("unserialized int %08x : %s", *val, desc);

    return 0;
}

static int serialize_data(struct usbredirparser_priv *parser,
                          uint8_t **state, uint8_t **pos, uint32_t *remain,
                          uint8_t *data, uint32_t len, const char *desc)
{
    DEBUG("serializing %d bytes of %s data", len, desc);
    if (len >= 8)
        DEBUG("First 8 bytes of %s: %02x %02x %02x %02x %02x %02x %02x %02x",
              desc, data[0], data[1], data[2], data[3],
                    data[4], data[5], data[6], data[7]);

    if (serialize_alloc(parser, state, pos, remain, sizeof(uint32_t) + len))
        return -1;

    memcpy(*pos, &len, sizeof(uint32_t));
    *pos += sizeof(uint32_t);
    *remain -= sizeof(uint32_t);

    memcpy(*pos, data, len);
    *pos += len;
    *remain -= len;

    return 0;
}

/* If *data == NULL, allocs buffer dynamically, else len_in_out must contain
   the length of the passed in buffer. */
static int unserialize_data(struct usbredirparser_priv *parser,
                            uint8_t **pos, uint32_t *remain,
                            uint8_t **data, uint32_t *len_in_out,
                            const char *desc)
{
    uint32_t len;

    if (*remain < sizeof(uint32_t)) {
        ERROR("error buffer underrun while unserializing state");
        return -1;
    }
    memcpy(&len, *pos, sizeof(uint32_t));
    *pos += sizeof(uint32_t);
    *remain -= sizeof(uint32_t);

    if (*remain < len) {
        ERROR("error buffer underrun while unserializing state");
        return -1;
    }
    if (*data == NULL && len > 0) {
        *data = malloc(len);
        if (!*data) {
            ERROR("Out of memory allocating unserialize buffer");
            return -1;
        }
    } else {
        if (*len_in_out < len) {
            ERROR("error buffer overrun while unserializing state");
            return -1;
        }
    }

    memcpy(*data, *pos, len);
    *pos += len;
    *remain -= len;
    *len_in_out = len;

    DEBUG("unserialized %d bytes of %s data", len, desc);
    if (len >= 8)
        DEBUG("First 8 bytes of %s: %02x %02x %02x %02x %02x %02x %02x %02x",
              desc, (*data)[0], (*data)[1], (*data)[2], (*data)[3],
              (*data)[4], (*data)[5], (*data)[6], (*data)[7]);

    return 0;
}

USBREDIR_VISIBLE
int usbredirparser_serialize(struct usbredirparser *parser_pub,
                             uint8_t **state_dest, int *state_len)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;
    struct usbredirparser_buf *wbuf;
    uint8_t *state = NULL, *pos = NULL;
    uint32_t write_buf_count = 0, len, remain = 0;
    ptrdiff_t write_buf_count_pos;

    *state_dest = NULL;
    *state_len = 0;

    if (serialize_int(parser, &state, &pos, &remain,
                                   USBREDIRPARSER_SERIALIZE_MAGIC, "magic"))
        return -1;

    /* To be replaced with length later */
    if (serialize_int(parser, &state, &pos, &remain, 0, "length"))
        return -1;

    if (serialize_data(parser, &state, &pos, &remain,
                       (uint8_t *)parser->our_caps,
                       USB_REDIR_CAPS_SIZE * sizeof(int32_t), "our_caps"))
        return -1;

    if (parser->have_peer_caps) {
        if (serialize_data(parser, &state, &pos, &remain,
                           (uint8_t *)parser->peer_caps,
                           USB_REDIR_CAPS_SIZE * sizeof(int32_t), "peer_caps"))
            return -1;
    } else {
        if (serialize_int(parser, &state, &pos, &remain, 0, "peer_caps_len"))
            return -1;
    }

    if (serialize_int(parser, &state, &pos, &remain, parser->to_skip, "skip"))
        return -1;

    if (serialize_data(parser, &state, &pos, &remain,
                       (uint8_t *)&parser->header, parser->header_read,
                       "header"))
        return -1;

    if (serialize_data(parser, &state, &pos, &remain,
                       parser->type_header, parser->type_header_read,
                       "type_header"))
        return -1;

    if (serialize_data(parser, &state, &pos, &remain,
                       parser->data, parser->data_read, "packet-data"))
        return -1;

    write_buf_count_pos = pos - state;
    /* To be replaced with write_buf_count later */
    if (serialize_int(parser, &state, &pos, &remain, 0, "write_buf_count"))
        return -1;

    wbuf = parser->write_buf;
    while (wbuf) {
        if (serialize_data(parser, &state, &pos, &remain,
                           wbuf->buf + wbuf->pos, wbuf->len - wbuf->pos,
                           "write-buf"))
            return -1;
        write_buf_count++;
        wbuf = wbuf->next;
    }
    /* Patch in write_buf_count */
    memcpy(state + write_buf_count_pos, &write_buf_count, sizeof(int32_t));

    /* Patch in length */
    len = pos - state;
    memcpy(state + sizeof(int32_t), &len, sizeof(int32_t));

    *state_dest = state;
    *state_len = len;

    return 0;
}

USBREDIR_VISIBLE
int usbredirparser_unserialize(struct usbredirparser *parser_pub,
                               uint8_t *state, int len)
{
    struct usbredirparser_priv *parser =
        (struct usbredirparser_priv *)parser_pub;
    struct usbredirparser_buf *wbuf, **next;
    uint32_t orig_caps[USB_REDIR_CAPS_SIZE];
    uint8_t *data;
    uint32_t i, l, header_len, remain = len;

    usbredirparser_assert_invariants(parser);
    if (unserialize_int(parser, &state, &remain, &i, "magic")) {
        usbredirparser_assert_invariants(parser);
        return -1;
    }
    if (i != USBREDIRPARSER_SERIALIZE_MAGIC) {
        ERROR("error unserialize magic mismatch");
        usbredirparser_assert_invariants(parser);
        return -1;
    }

    if (!(parser->data == NULL && parser->header_read == 0 &&
          parser->type_header_read == 0 && parser->data_read == 0)) {
        ERROR("unserialization must use a pristine parser");
        usbredirparser_assert_invariants(parser);
        return -1;
    }

    {
        /* We need to reset parser's state to receive unserialized
         * data. */
        struct usbredirparser_buf *wbuf = parser->write_buf;
        while (wbuf) {
            struct usbredirparser_buf *next_wbuf = wbuf->next;
            free(wbuf->buf);
            free(wbuf);
            wbuf = next_wbuf;
        }
        parser->write_buf = NULL;
        parser->write_buf_count = 0;
        parser->write_buf_total_size = 0;
    }

    if (unserialize_int(parser, &state, &remain, &i, "length")) {
        usbredirparser_assert_invariants(parser);
        return -1;
    }
    if (i != len) {
        ERROR("error unserialize length mismatch");
        usbredirparser_assert_invariants(parser);
        return -1;
    }

    data = (uint8_t *)parser->our_caps;
    i = USB_REDIR_CAPS_SIZE * sizeof(int32_t);
    memcpy(orig_caps, parser->our_caps, i);
    if (unserialize_data(parser, &state, &remain, &data, &i, "our_caps")) {
        usbredirparser_assert_invariants(parser);
        return -1;
    }
    for (i =0; i < USB_REDIR_CAPS_SIZE; i++) {
        if (parser->our_caps[i] != orig_caps[i]) {
            /* orig_caps is our original settings
             * parser->our_caps is off the wire.
             * We want to allow reception from an older
             * usbredir that doesn't have all our features.
             */
            if (parser->our_caps[i] & ~orig_caps[i]) {
                /* Source has a cap we don't */
                ERROR("error unserialize caps mismatch ours: %x recv: %x",
                      orig_caps[i], parser->our_caps[i]);
                usbredirparser_assert_invariants(parser);
                return -1;
            } else {
                /* We've got a cap the source doesn't - that's OK */
                WARNING("unserialize missing some caps; ours: %x recv: %x",
                      orig_caps[i], parser->our_caps[i]);
            }
        }
    }

    data = (uint8_t *)parser->peer_caps;
    i = USB_REDIR_CAPS_SIZE * sizeof(int32_t);
    if (unserialize_data(parser, &state, &remain, &data, &i, "peer_caps")) {
        usbredirparser_assert_invariants(parser);
        return -1;
    }
    if (i)
        parser->have_peer_caps = 1;

    if (unserialize_int(parser, &state, &remain, &i, "skip")) {
        usbredirparser_assert_invariants(parser);
        return -1;
    }
    parser->to_skip = i;

    header_len = usbredirparser_get_header_len(parser_pub);
    data = (uint8_t *)&parser->header;
    i = header_len;
    memset(&parser->header, 0, sizeof(parser->header));
    if (unserialize_data(parser, &state, &remain, &data, &i, "header")) {
        usbredirparser_assert_invariants(parser);
        return -1;
    }
    if (parser->header.length > MAX_PACKET_SIZE) {
        ERROR("packet length of %d larger than permitted %d bytes",
              parser->header.length, MAX_PACKET_SIZE);
        usbredirparser_assert_invariants(parser);
        return -1;
    }
    parser->header_read = i;
    parser->type_header_len = 0;

    /* Set various length field from the header (if any) */
    if (parser->header_read == header_len) {
        int type_header_len =
            usbredirparser_get_type_header_len(parser_pub,
                                               parser->header.type, 0);
        if (type_header_len < 0 ||
            type_header_len > sizeof(parser->type_header) ||
            parser->header.length < type_header_len ||
            (parser->header.length > type_header_len &&
             !usbredirparser_expect_extra_data(parser))) {
            ERROR("error unserialize packet header invalid");
            usbredirparser_assert_invariants(parser);
            return -1;
        }
        parser->type_header_len = type_header_len;
    }

    data = parser->type_header;
    i = parser->type_header_len;
    if (unserialize_data(parser, &state, &remain, &data, &i, "type_header")) {
        usbredirparser_assert_invariants(parser);
        return -1;
    }
    if (parser->header_read == header_len) {
        parser->type_header_read = i;
    }

    if (parser->type_header_read == parser->type_header_len) {
        parser->data_len = parser->header.length - parser->type_header_len;
        if (parser->data_len) {
            parser->data = malloc(parser->data_len);
            if (!parser->data) {
                ERROR("Out of memory allocating unserialize buffer");
                usbredirparser_assert_invariants(parser);
                return -1;
            }
        }
    }
    i = parser->data_len;
    if (unserialize_data(parser, &state, &remain, &parser->data, &i, "data")) {
        free(parser->data);
        parser->data = NULL;
        parser->data_len = 0;
        usbredirparser_assert_invariants(parser);
        return -1;
    }
    if (parser->header_read == header_len &&
        parser->type_header_read == parser->type_header_len &&
        parser->data_len > 0) {
        parser->data_read = i;
    } else if (parser->data != NULL) {
        free(parser->data);
        parser->data = NULL;
        parser->data_len = 0;
    }

    /* Get the write buffer count and the write buffers */
    if (unserialize_int(parser, &state, &remain, &i, "write_buf_count")) {
        usbredirparser_assert_invariants(parser);
        return -1;
    }
    next = &parser->write_buf;
    usbredirparser_assert_invariants(parser);
    while (i) {
        uint8_t *buf = NULL;

        l = 0;
        if (unserialize_data(parser, &state, &remain, &buf, &l, "wbuf")) {
            usbredirparser_assert_invariants(parser);
            return -1;
        }

        if (l == 0) {
            free(buf);
            ERROR("write buffer %d is empty", i);
            usbredirparser_assert_invariants(parser);
            return -1;
        }

        wbuf = calloc(1, sizeof(*wbuf));
        if (!wbuf) {
            free(buf);
            ERROR("Out of memory allocating unserialize buffer");
            usbredirparser_assert_invariants(parser);
            return -1;
        }
        wbuf->buf = buf;
        wbuf->len = l;
        *next = wbuf;
        next = &wbuf->next;
        parser->write_buf_total_size += wbuf->len;
        parser->write_buf_count++;
        i--;
    }

    if (remain) {
        ERROR("error unserialize %d bytes of extraneous state data", remain);
        usbredirparser_assert_invariants(parser);
        return -1;
    }

    usbredirparser_assert_invariants(parser);
    return 0;
}
