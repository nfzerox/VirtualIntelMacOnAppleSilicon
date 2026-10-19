/*
 * Copyright 2022 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/
#include "config.h"

#define G_LOG_DOMAIN "serializer"
#define G_LOG_USE_STRUCTURED

#include "usbredirparser.h"

#include <errno.h>
#include <locale.h>
#include <glib.h>
#include <stdlib.h>


static void
log_cb(void *priv, int level, const char *msg)
{
    GLogLevelFlags glog_level;

    switch(level) {
    case usbredirparser_error:
        glog_level = G_LOG_LEVEL_ERROR;
        break;
    case usbredirparser_warning:
        glog_level = G_LOG_LEVEL_WARNING;
        break;
    case usbredirparser_info:
        glog_level = G_LOG_LEVEL_INFO;
        break;
    case usbredirparser_debug:
    case usbredirparser_debug_data:
        glog_level = G_LOG_LEVEL_DEBUG;
        break;
    default:
        g_warn_if_reached();
        return;
    }
    g_log_structured(G_LOG_DOMAIN, glog_level, "MESSAGE", msg);
}

static struct usbredirparser *
get_usbredirparser(void)
{
    struct usbredirparser *parser = usbredirparser_create();
    g_assert_nonnull(parser);

    uint32_t caps[USB_REDIR_CAPS_SIZE] = { 0, };
    /* Typical caps set by usbredirhost */
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
    int parser_flags = usbredirparser_fl_usb_host;

    parser->log_func = log_cb;
    usbredirparser_init(parser,
                        PACKAGE_STRING,
                        caps,
                        USB_REDIR_CAPS_SIZE,
                        parser_flags);
    return parser;
}

static void
simple (gconstpointer user_data)
{
    uint8_t *state = NULL;
    int ret, len = -1;

    struct usbredirparser *source = get_usbredirparser();
    ret = usbredirparser_serialize(source, &state, &len);
    g_assert_cmpint(ret, ==, 0);

    struct usbredirparser *target = get_usbredirparser();
    ret = usbredirparser_unserialize(target, state, len);
    g_assert_cmpint(ret, ==, 0);

    g_clear_pointer(&state, free);
    usbredirparser_destroy(source);
    usbredirparser_destroy(target);
}

int
main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    g_test_init(&argc, &argv, NULL);

    g_test_add_data_func("/serializer/serialize-and-unserialize", NULL, simple);

    return g_test_run();
}
