# Index

[[_TOC_]]

# USB Network Redirection protocol description version 0.7 (19 May 2014)

## Revisions

### Version 0.1
- Initial version (released as initial RFC without a version number)

### Version 0.2
- Version demo-ed at FOSDEM 2011
- Remove `usb_redir_report_descriptor` packet, as it is not possible to get
 the cached descriptors from the OS on all platforms and we can do without
- Replace vm-host with usb-guest
- Replace the synchroneous / asynchroneous commands nomenclature with
 control / data packets
- Move the packet id to the main packet header shared by all packets
- Add note: "All integers in the protocol are send over the pipe in least
 significant byte first order."
- Add note: "All structs are packed"
- s/data_size/length/
- Add an `usb_redir_cancel_data_packet` packet
- Add `usb_redir_reset` and `usb_redir_reset_status` packets

### Version 0.3, released 14 July 2011
- First "stable" version, all later versions should be compatible with this
 version
- Add an `usb_redir_device_connect` packet
- Add an `usb_redir_device_disconnect` packet
- Add an `usb_redir_interface_info` packet
- Add an `usb_redir_ep_info` packet
- Add support for interrupt transfers, add the following packets:
 `usb_redir_start_interrupt_receiving`
 `usb_redir_stop_interrupt_receiving`
 `usb_redir_interrupt_receiving_status`
 `usb_redir_interrupt_packet`
- Add a list with the possible values for the status field
- Report `usb_redir_stall` as iso status error to indicate a stream stop
- Drop `usb_redir_disconnect` status, instead the usb-host should always
 send a `usb_redir_device_disconnect` packet on device disconnection. The
 reason behind this is that having to handle disconnection from data packet
 handlers make things unnecessarily hard for the usb-guest
- Drop `usb_redir_reset_status`, instead if reconnecting to the device fails
 after reset the usb-host will send a `usb_redir_device_disconnect` packet

### Version 0.3.1, released 18 August 2011
- No protocol changes

### Version 0.3.2, released  3 January 2012
- The `usb_redir_device_connect_header` has been extended with a
 `device_version_bcd` field. This is only send / received if both sides
 have the `usb_redir_cap_connect_device_version` capability

### Version 0.3.3, released 12 January 2012
- No protocol changes

### Version 0.4,   released 22 February 2012
- Add `usb_redir_filter_reject` and `usb_redir_filter_filter` packets and
  an `usb_redir_cap_filter` capability flag
- Add an `usb_redir_device_disconnect_ack` packet and
  an `usb_redir_cap_device_disconnect_ack` capability flag

### Version 0.4.1, released 25 February 2012
- No protocol changes

### Version 0.4.2, released  6 March 2012
- Add `usb_redir_babble` status code
- The `usb_redir_ep_info_header` has been extended with a `max_packet_size` field
  This is only send / received if both sides have the
  `usb_redir_cap_ep_info_max_packet_size` capability

### Version 0.5,   released  7 September 2012
- Add the posibility to use 64 bits packet ids

### Version 0.5.3, released  7 October 2012
- Extend the length field in bulk packets headers to 32 bits, the extra 16
  bits are only send / received if both sides have the
  `usb_redir_cap_32bits_bulk_length` capability

### Version 0.6,   released 13 December 2012
- Add support for buffered bulk input, new packets:
  `usb_redir_start_bulk_receiving,` `usb_redir_stop_bulk_receiving`,
  `usb_redir_bulk_receiving_status,` `usb_redir_buffered_bulk_packet`
  New capability: `usb_redir_cap_bulk_receiving`

### Version 0.7,   released 19 May 2014
- The `usb_redir_ep_info_header` has been extended with a `max_streams` field
  This is only send / received if both sides have the
  `usb_redir_cap_bulk_streams` capability.
- Change bulk_stream packet definitions to allow allocating / freeing
  streams on multiple endpoints in one go, technically this is a protocol
  change, but no-one has implemented `usb_redir_cap_bulk_streams` so far, so
  we can safely do this


# USB redirection protocol version 0.7

The protocol described in this document is meant for tunneling usb transfers
to a single usb device. Note: not an entire hub, only a single device.

The most significant use case for this is taking a usb device attached to
some machine "a" which acts as a client / viewer to a virtual machine "v"
hosted on another machine "b", and make the usb device show up inside the
virtual machine as if it were attached directly to the virtual machine "v".

The described protocol assumes a reliable ordered bidirectional transport is
available, for example a tcp socket. All integers in the protocol are send
over the pipe in least significant byte first order. All structs send over
the pipe are packed (no padding).

Definitions:

- _usb-device_: The usb-device whose usb transfers are being tunneled.
- _usb-guest_: The entity connecting to the usb-device and using it as if
    connected directly to it. For example a virtual machine running a guest
    os which accesses a usb-device over the network as if it is part of the
    virtual machine.
- _usb-host_: The entity making the usb-device available for use by a usb-guest.
    For example a daemon on a machine which "exports" the usb-device over the
    network which then "appears" inside a virtual machine on another machine.


## Basic packet structure / communication

Each packet exchanged between the usb-guest and the usb-host starts with a
`usb_redir_header`, followed by an optional packet type specific header
follow by optional additional data.

The `usb_redir_header` each packet starts with looks as follows:

```c
struct usb_redir_header {
    uint32_t type;
    uint32_t length;
    uint32_t id;
}
```

Or, if both sides have the `usb_redir_cap_64bits_ids` capability, it looks as
follows!  :

```c
struct usb_redir_header {
    uint32_t type;
    uint32_t length;
    uint64_t id;
}
```

- type:    This identifies the type of packet, from the type enum
- length:  Length of the optional type specific packet header + the optional
           additional data. Can be 0.
- id:      A unique id, generated by the usb-guest when sending a packet,
           the usb-host will use the same id in its response packet, allowing
           the usb-guest to match responses to its original requests.

There are 2 types of packets:

1) control packets
2) data packets

Control packets are handled synchroneously inside the usb-host, it will hand
the request over to the host os and then *wait* for a response. The usb-host
will thus stop processing further packets. Where as for data packets the
usb-host hands them over to the host os with the request to let the usb-host
process know when there is a response from the usb-device.

Note that control packets should only be send to the usb-host when no data
packets are pending on the device / interface / endpoint affected by the
control packet. Any pending data packets will get dropped, and any active
iso streams / allocated bulk streams will get stopped / free-ed.


### Packet type list

#### control packets
- `usb_redir_hello`
- `usb_redir_device_connect`
- `usb_redir_device_disconnect`
- `usb_redir_reset`
- `usb_redir_interface_info`
- `usb_redir_ep_info`
- `usb_redir_set_configuration`
- `usb_redir_get_configuration`
- `usb_redir_configuration_status`
- `usb_redir_set_alt_setting`
- `usb_redir_get_alt_setting`
- `usb_redir_alt_setting_status`
- `usb_redir_start_iso_stream`
- `usb_redir_stop_iso_stream`
- `usb_redir_iso_stream_status`
- `usb_redir_start_interrupt_receiving`
- `usb_redir_stop_interrupt_receiving`
- `usb_redir_interrupt_receiving_status`
- `usb_redir_alloc_bulk_streams`
- `usb_redir_free_bulk_streams`
- `usb_redir_bulk_streams_status`
- `usb_redir_cancel_data_packet`
- `usb_redir_filter_reject`
- `usb_redir_filter_filter`
- `usb_redir_device_disconnect_ack`
- `usb_redir_start_bulk_receiving`
- `usb_redir_stop_bulk_receiving`
- `usb_redir_bulk_receiving_status`

#### data packets
- `usb_redir_control_packet`
- `usb_redir_bulk_packet`
- `usb_redir_iso_packet`
- `usb_redir_interrupt_packet`
- `usb_redir_buffered_bulk_packet`

### Status code list

Many usb-host replies have a status field, this field can have the following
values:

```c
enum {
    usb_redir_success,
    usb_redir_cancelled,    /* The transfer was cancelled */
    usb_redir_inval,        /* Invalid packet type / length / ep, etc. */
    usb_redir_ioerror,      /* IO error */
    usb_redir_stall,        /* Stalled */
    usb_redir_timeout,      /* Request timed out */
    usb_redir_babble,       /* The device has "babbled" */
};
```

Note that in future versions there may be additional status codes to signal
new / other *error* conditions. So any unknown status value should be
interpreted as an error.


## usb_redir_hello

```
usb_redir_header.type:    usb_redir_hello
usb_redir_header.length:  <see description>
usb_redir_header.id:      0 (always as this is an unsolicited packet)
```

```c
struct usb_redir_hello_header {
    char     version[64];
    uint32_t capabilities[0];
}
```

No packet type specific additional data.

A packet of this type is send by both sides as soon as a connection is
establised. It is mandatory that this packet is the first packet send by
both sides! This packet contains:
- version:       A free form 0 terminated version string, useful for logging
                 should not be parsed! Suggested format: "qemu 0.13",
                 "usb-redir-daemon 0.1", etc.
- capabilities:  A variable length array for announcing capabilities.

Note that since the peer caps are not known until the `usb_redir_hello`
packet is received, the hello packet always has 32 bits id fields!

The value of the length field depends on the size of the capabilities array.
If we cross the 32 capabilities count, it will go from 1 `uint32_t` to 2,
etc. the value is `64 + capabilities-array-size * sizeof(uint32_t)`.

Currently the following capabilities are defined:

```c
enum {
    /* Supports USB 3 bulk streams */
    usb_redir_cap_bulk_streams,
    /* The device_connect packet has the device_version_bcd field */
    usb_redir_cap_connect_device_version,
    /* Supports usb_redir_filter_reject and usb_redir_filter_filter pkts */
    usb_redir_cap_filter,
    /* Supports the usb_redir_device_disconnect_ack packet */
    usb_redir_cap_device_disconnect_ack,
    /* The ep_info packet has the max_packet_size field */
    usb_redir_cap_ep_info_max_packet_size,
    /* Supports 64 bits ids in usb_redir_header */
    usb_redir_cap_64bits_ids,
    /* Supports 32 bits length in usb_redir_bulk_packet_header */
    usb_redir_cap_32bits_bulk_length,
    /* Supports bulk receiving / buffered bulk input */
    usb_redir_cap_bulk_receiving,
};
```

## usb_redir_device_connect

```
usb_redir_header.type:    usb_redir_device_connect
usb_redir_header.length:  sizeof(usb_redir_device_connect_header)
usb_redir_header.id:      0 (always as this is an unsolicited packet)
```

```c
enum {
    usb_redir_speed_low,
    usb_redir_speed_full,
    usb_redir_speed_high,
    usb_redir_speed_super,
    usb_redir_speed_unknown = 255
}

struct usb_redir_device_connect_header {
    uint8_t speed;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_version_bcd;
}
```

No packet type specific additional data.

This packet gets send by the usb-host when a device becomes available (it is
possible for the usb-host to wait for a device to get plugged in).

The `device_version_bcd` field should only be send (and expected on receive)
when both sides have the `usb_redir_cap_connect_device_version` capability.
If this is not the case the length of the packet will be 2 bytes less!

Note that a usb-host may re-use the existing connection for a new / re-plugged
device in this case this packet can be send after a `usb_redir_device_disconnect`
message to notify the usb-guest that a new device is available.

Note the usbredir-host *must* first send `usb_redir_ep_info` followed by
`usb_redir_interface_info` before sending the `usb_redir_device_connect_info`!

## usb_redir_device_disconnect

```
usb_redir_header.type:    usb_redir_device_disconnect
usb_redir_header.length:  0
usb_redir_header.id:      0 (always as this is an unsolicited packet)
```

No packet type specific header.

No packet type specific additional data.

This packet may be send by the usb-host to indicate that the device has been
disconnect (unplugged). Note on some platforms the usb-host may not become
aware of the disconnection until a usb packet is send to the device.

## usb_redir_reset

```
usb_redir_header.type:    usb_redir_reset
usb_redir_header.length:  0
```

No packet type specific header.

No packet type specific additional data.

This packet can be send by the usb-guest to cause a reset of the usb
device. Note that of things go wrong the usb-host may be unable to re-connect
to the device after the reset! If this happens a `usb_redir_device_disconnect`
packet will be send by the usb-host.

## usb_redir_interface_info

```
usb_redir_header.type:    usb_redir_interface_info
usb_redir_header.length:  sizeof(usb_redir_interface_info_header)
usb_redir_header.id:      0 (always as this is an unsolicited packet)
```

```c
struct usb_redir_interface_info_header {
    uint32_t interface_count;
    uint8_t interface[32];
    uint8_t interface_class[32];
    uint8_t interface_subclass[32];
    uint8_t interface_protocol[32];
}
```

No packet type specific additional data.

This packet gets send by the usb-host to inform the usb-guest about the
interfaces of the device. It contains the interface number, class and protocol
info for `interface_count` interfaces. This gets send after a (successful)
initial connection, `set_config` and `set_alt_setting`.

## usb_redir_ep_info

```
usb_redir_header.type:    usb_redir_ep_info
usb_redir_header.length:  sizeof(usb_redir_ep_info_header)
usb_redir_header.id:      0 (always as this is an unsolicited packet)
```

```c
enum {
    /* Note these 4 match the usb spec! */
    usb_redir_type_control,
    usb_redir_type_iso,
    usb_redir_type_bulk,
    usb_redir_type_interrupt,
    usb_redir_type_invalid = 255
}

struct usb_redir_ep_info_header {
    uint8_t type[32];
    uint8_t interval[32];
    uint8_t interface[32];
    uint16_t max_packet_size[32];
    uint32_t max_streams[32];
}
```

No packet type specific additional data.

This packet gets send by the usb-host to let the usb-guest know the endpoint
type, interval and interface it belongs to for all possible endpoints,
first 0-15 out, then 0-15 in. This gets send after a (successful) initial
connection, `set_config` and `set_alt_setting`.

The `max_packet_size` field should only be send (and expected on receive)
when both sides have the `usb_redir_cap_ep_info_max_packet_size` capability.
If this is not the case the length of the packet will be 64 bytes less!

The `max_streams` field should only be send (and expected on receive)
when both sides have the `usb_redir_cap_bulk_streams` capability. If this is
not the case the length of the packet will be 128 bytes less!

Note implementations with the `usb_redir_cap_bulk_streams` capability must
always also have the `usb_redir_cap_ep_info_max_packet_size` capability.
Advertising `usb_redir_cap_bulk_streams` without
`usb_redir_cap_ep_info_max_packet_size` is not allowed!


## usb_redir_set_configuration

```
usb_redir_header.type:    usb_redir_set_configuration
usb_redir_header.length:  sizeof(usb_redir_set_configuration_header)
```

```c
struct usb_redir_set_configuration_header {
    uint8_t configuration;
}
```

No packet type specific additional data.

This packet can be send by the usb-guest to set (change) the active
configuration of the usb-device.

## usb_redir_get_configuration

```
usb_redir_header.type:    usb_redir_get_configuration
usb_redir_header.length:  0
```

No packet type specific header.

No packet type specific additional data.

This packet can be send by the usb-guest to get (query) the active
configuration of the usb-device.

## usb_redir_configuration_status

```
usb_redir_header.type:    usb_redir_configuration_status
usb_redir_header.length:  sizeof(usb_redir_configuration_status_header)
```

```c
struct usb_redir_configuration_status_header {
    uint8_t status;
    uint8_t configuration;
}
```

No packet type specific additional data.

This is send by the usb-host in response to a `usb_redir_set_configuration` /
`usb_redir_get_configuration` packet. It reports a status code and on success
the resulting / active configuration.

Note that after a successful `usb_redir_set_configuration` command the
usbredir-host *must* first send `usb_redir_ep_info` followed by
`usb_redir_interface_info` before sending the `usb_redir_configuration_status`,
to ensure the usb-guest has the new info when it starts using the new
configuration.


## usb_redir_set_alt_setting

```
usb_redir_header.type:    usb_redir_set_alt_setting
usb_redir_header.length:  sizeof(usb_redir_set_alt_setting_header)
```

```c
struct usb_redir_set_alt_setting_header {
    uint8_t interface;
    uint8_t alt;
}
```

No packet type specific additional data.

This packet can be send by the usb-guest to set (change) the `alt_setting` of
interface `<interface>` to `<alt>`.

## usb_redir_get_alt_setting

```
usb_redir_header.type:    usb_redir_get_alt_setting
usb_redir_header.length:  sizeof(usb_redir_get_alt_setting_header)
```

```c
struct usb_redir_get_alt_setting_header {
    uint8_t interface;
}
```

No packet type specific additional data.

This packet can be send by the usb-guest to get (query) the active
`alt_setting` of an interface of the usb-device.

## usb_redir_alt_setting_status

```
usb_redir_header.type:    usb_redir_alt_setting_status
usb_redir_header.length:  sizeof(usb_redir_alt_setting_status_header)
```

```c
struct usb_redir_alt_setting_status_header {
    uint8_t status;
    uint8_t interface;
    uint8_t alt;
}
```

No packet type specific additional data.

This is send by the usb-host in response to a `usb_redir_set_alt_setting` /
`usb_redir_get_alt_setting` packet. It reports a status code, the affected
interface and on success the resulting / active `alt_setting` for that interface.

Note that after a successful `usb_redir_set_alt_setting` command the
usbredir-host *must* first send `usb_redir_ep_info` followed by
`usb_redir_interface_info` before sending the `usb_redir_alt_setting_status`,
to ensure the usb-guest has the new info when it starts using the new
alt setting.


## usb_redir_start_iso_stream

```
usb_redir_header.type:    usb_redir_start_iso_stream
usb_redir_header.length:  sizeof(usb_redir_start_iso_stream_header)
```

```c
struct usb_redir_start_iso_stream_header {
    uint8_t endpoint;
    uint8_t pkts_per_urb;
    uint8_t no_urbs;
}
```

No packet type specific additional data.

This packet can be send by the usb-guest to start a iso stream on the
designated endpoint of the usb-device.

This function allocates `no_urbs` urbs with `pkts_per_urb` iso packets/frames
per urb. For iso input endpoints these urbs will get submitted to the
device *immediately*, for iso output endpoints the usb-host will wait till
it has received `(pkts_per_urb * no_urbs / 2)` packets to fill its buffers,
before submitting the first urb.

## usb_redir_stop_iso_stream

```
usb_redir_header.type:    usb_redir_stop_iso_stream
usb_redir_header.length:  sizeof(struct usb_redir_start_iso_stream_header)
```

```c
struct usb_redir_stop_iso_stream_header {
    uint8_t endpoint;
}
```

No packet type specific additional data.

This packet can be send by the usb-guest to stop an iso stream on the
designated endpoint. This will cancel all pending urbs, flush the usb-host's
buffers and free all relevant resources. Note that the usb-guest can still
receive isoc data packets from an isoc in endpoint after sending this, as
some data packets may already be inside the transport pipe.

## usb_redir_iso_stream_status

```
usb_redir_header.type:    usb_redir_iso_stream_status
usb_redir_header.length:  sizeof(usb_redir_iso_stream_status_header)
```

```c
struct usb_redir_iso_stream_status_header {
    uint8_t status;
    uint8_t endpoint;
}
```

No packet type specific additional data.

This packet is send by the usb-host in response to a
`usb_redir_start_iso_stream` or `usb_redir_stop_iso_stream` packet. Note that
for the starting of output iso streams a success status only indicates that
all the buffers were successfully allocated, the actual stream is not
started until enough packets are buffered.

Note that this can also be send unsolicited by a usb-host in case of an
error with an iso output stream, see `usb_redir_iso_packet`.

To allow the usb-guest to detect if the stream was adversely stopped, the
usb-host will always report `usb_redir_stall` as status if the stream was
stopped for any reason other then an `usb_redir_stop_iso_stream`.


## usb_redir_start_interrupt_receiving

```
usb_redir_header.type:    usb_redir_start_interrupt_receiving
usb_redir_header.length:  sizeof(usb_redir_start_interrupt_receiving_header)
```

```c
struct usb_redir_start_interrupt_receiving_header {
    uint8_t endpoint;
}
```

No packet type specific additional data.

This packet can be send by the usb-guest to start receiving interrupts
from the designated endpoint of the usb-device.

This function is for *input* interrupt endpoints only. Input interrupt
endpoints need to be polled timely otherwise data may get lost. So for
input interrupt endpoints the usb-host takes care of the submitting and
re-submitting of urbs.

On receiving this packet the usb-host will start an interrupt transfer
to the endpoint using the interval and `maxPacketSize` from the descriptors.
When this transfer completes, the usb-host will send an
`usb_redir_interrupt_packet` to the usb-guest, and will re-submit the urb.

## usb_redir_stop_interrupt_receiving

```
usb_redir_header.type:    usb_redir_stop_interrupt_receiving
usb_redir_header.length:  sizeof(struct usb_redir_start_interrupt_receiving_header)
```

```c
struct usb_redir_stop_interrupt_receiving_header {
    uint8_t endpoint;
}
```

No packet type specific additional data.

This packet can be send by the usb-guest to stop interrupt receiving on the
designated endpoint. This will cancel the pending urb. Note that the usb-guest
can still receive `usb_redir_interrupt_packet-s` after sending this, as
some data packets may already be inside the transport pipe.

## usb_redir_interrupt_receiving_status

```
usb_redir_header.type:    usb_redir_interrupt_receiving_status
usb_redir_header.length:  sizeof(usb_redir_interrupt_receiving_status_header)
```

```c
struct usb_redir_interrupt_receiving_status_header {
    uint8_t status;
    uint8_t endpoint;
}
```

No packet type specific additional data.

This packet is send by the usb-host in response to a
`usb_redir_start_interrupt_receiving` or `usb_redir_stop_interrupt_receiving`
packet.

Note that this can also be send unsolicited by a usb-host in case of an
error re-submitting the interrupt urb.

To allow the usb-guest to detect if the stream was adversely stopped, the
usb-host will always report `usb_redir_stall` as status if the stream was
stopped for any reason other then an `usb_redir_stop_interrupt_receiving`.


## usb_redir_alloc_bulk_streams

```
usb_redir_header.type:    usb_redir_alloc_bulk_streams
usb_redir_header.length:  sizeof(usb_redir_alloc_bulk_streams_header)
```

```c
struct usb_redir_alloc_bulk_streams_header {
    uint32_t endpoints; /* bitmask indicating on which eps to alloc streams */
    uint32_t no_streams;
}
```

No packet type specific additional data.

This packet can be send by the usb-guest to the usb-host to request
that the usb-host allocates IDs so the usb-guest can use up to `no_streams`
stream IDs on the endpoints indicated by the `endpoints` bitmask. Endpoints in
the bitmask are indicated by bit number (0-31) using the same numbering as
in `usb_redir_ep_info_header`.

## usb_redir_free_bulk_streams

```
usb_redir_header.type:    usb_redir_free_bulk_streams
usb_redir_header.length:  sizeof(usb_redir_free_bulk_streams_header)
```

```c
struct usb_redir_free_bulk_streams_header {
    uint32_t endpoints; /* bitmask indicating on which eps to free streams */
}
```

No packet type specific additional data.

This packet can be send by the usb-guest to the usb-host to free any
bulk streams previously allocated on the endpoints indicated by the
endpoints bitmask.

## usb_redir_bulk_streams_status

```
usb_redir_header.type:    usb_redir_bulk_streams_status
usb_redir_header.length:  sizeof(usb_redir_bulk_streams_status_header)
```

```c
struct usb_redir_bulk_streams_status_header {
    uint32_t endpoints; /* bitmask indicating eps this status message is for */
    uint32_t no_streams;
    uint8_t status;
}
```

No packet type specific additional data.

This packet is send by the usb-host in response to a
`usb_redir_alloc_bulk_streams` or `usb_redir_free_bulk_streams` packet.

For `usb_redir_alloc_bulk_streams` responses `no_streams` will be the `no_streams`
passed to the `usb_redir_alloc_bulk_streams` packet. usb-hosts are not allowed
to return less streams then requested! For `usb_redir_free_bulk_streams`
responses `no_streams` will be 0.

On a success status in response to a `usb_redir_alloc_bulk_streams`
the usb-guest may use stream ids 1 through `no_streams`.


## usb_redir_start_bulk_receiving

```
usb_redir_header.type:    usb_redir_start_bulk_receiving
usb_redir_header.length:  sizeof(usb_redir_start_bulk_receiving_header)
```

```c
struct usb_redir_start_bulk_receiving_header {
    uint32_t stream_id;
    uint32_t bytes_per_transfer;
    uint8_t endpoint;
    uint8_t no_transfers;
}
```

No packet type specific additional data.

This packet can be send by the usb-guest to start buffered reading from a
bulk endpoint.

Upon receiving this packet the usb-host will submit `no_transfers` bulk in
transfer of `bytes_per_transfer` each to the designated endpoint of the
usb-device. Upon completion of a transfer the usb-host will send an
`usb_redir_buffered_bulk_packet` with the received data to the usb-guest,
and immediately re-submit the completed transfer.

Note `bytes_per_transfer` must be a multiple of the endpoints `max_packet_size`.

Note this packet should only be send to usb-hosts with the
`usb_redir_cap_bulk_receiving` capability.

## usb_redir_stop_bulk_receiving

```
usb_redir_header.type:    usb_redir_stop_bulk_receiving
usb_redir_header.length:  sizeof(usb_redir_stop_bulk_receiving_header)
```

```c
struct usb_redir_stop_bulk_receiving_header {
    uint32_t stream_id;
    uint8_t endpoint;
}
```

No packet type specific additional data.

This packet can be send by the usb-guest to stop bulk receiving on the
designated endpoint. This will cancel all pending transfers. Note that the
usb-guest can still receive `usb_redir_bulk_packet-s` after sending this, as
some data packets may already be inside the transport pipe.

Note this packet should only be send to usb-hosts with the
`usb_redir_cap_bulk_receiving` capability.

## usb_redir_bulk_receiving_status

```
usb_redir_header.type:    usb_redir_bulk_receiving_status
usb_redir_header.length:  sizeof(usb_redir_bulk_receiving_status_header)
```

```c
struct usb_redir_bulk_receiving_status_header {
    uint32_t stream_id;
    uint8_t endpoint;
    uint8_t status;
}
```

No packet type specific additional data.

This packet is send by the usb-host in response to a
`usb_redir_start_bulk_receiving` or `usb_redir_stop_bulk_receiving` packet.

Note that this can also be send unsolicited by an usb-host in case of an
error re-submitting the bulk transfer.

To allow the usb-guest to detect if the stream was adversely stopped, the
usb-host will always report `usb_redir_stall` as status if the stream was
stopped for any reason other then an `usb_redir_stop_interrupt_receiving`.

Note this packet should only be send to usb-guests with the
`usb_redir_cap_bulk_receiving` capability.


## usb_redir_cancel_data_packet

```
usb_redir_header.type:    usb_redir_cancel_data_packet
usb_redir_header.id       <id of packet to cancel>
usb_redir_header.length:  0
```

No packet type specific header.

No packet type specific additional data.

This packet can be send by the usb-guest to cancel an earlier send data
packet, the id should be set to the id used when sending the packet the
guest now wishes to cancel.

Note that the usb-guest will always receive back a data packet of the same type
and with the same id, the usb-guest can check if the packet completed
normally (before the cancel packet was processed by the usb-host), or was
cancelled by looking at the return data packet's status field.

## usb_redir_filter_reject

```
usb_redir_header.type:    usb_redir_filter_reject
usb_redir_header.length:  0
usb_redir_header.id:      0 (always as this is an unsolicited packet)
```

No packet type specific header.

No packet type specific additional data.

This packet is send by the usb-guest after receiving a `usb_redir_device_connect`
or `usb_redir_interface_info` packet which was rejected by an usb-guest side
device filter. This packet should only be send to usb-hosts with the
`usb_redir_cap_filter` capability.

## usb_redir_filter_filter

```
usb_redir_header.type:    usb_redir_filter_filter
usb_redir_header.length:  string-length + 1 (for 0 termination)
usb_redir_header.id:      0 (always as this is an unsolicited packet)
```

No packet type specific header.

The additional data contains a 0 terminated usredirfilter string.

This packet can be send directly after the hello packet to inform the other
side that a filter is in place and some devices may be rejected.

An usredirfilter consists of one or more rules, where in string form each rule
has the following format:
   `<class>,<vendor>,<product>,<version>,<allow>`

Values can be either in decimal format, or in hexadecimal format pre-fixed
with 0x, a value of -1 can be used to allow any value.

All rules of a filter are concatenated, separated by the '|' character
to form a single usredirfilter string:
   `<rule1>|<rule2>|<rule3>`

If a device matches none of the rules the result of the filter is deny and
the device will be rejected.

For more info on filtering see usbredirfilter.h

This packet should only be send to peers with the `usb_redir_cap_filter`
capability.

## usb_redir_device_disconnect_ack

```
usb_redir_header.type:    usb_redir_device_disconnect_ack
usb_redir_header.length:  0
usb_redir_header.id:      0 (as the id of the device_disconnect is always 0)
```

No packet type specific header.

No packet type specific additional data.

This packet is send by the usb-guest after having processed a
`usb_redir_device_disconnect` packet send by the usb-host. This allows an
usb-host which wants to re-use an existing connection to know that the
usb-guest has seen the disconnect and will not send any more packets intended
for the disconnected device. Without this there is a race where the usb-host
may have a new device available, but it is still receiving packets intended for
the old device as the usb-guest has not yet seen the disconnect.

Note this packet is only send if both sides have the
`usb_redir_cap_device_disconnect_ack` capability.


## usb_redir_control_packet

```
usb_redir_header.type:    usb_redir_control_packet
usb_redir_header.length:  sizeof(usb_redir_control_packet_header) [+ length]
```

```c
struct usb_redir_control_packet_header {
    uint8_t endpoint;
    uint8_t request;
    uint8_t requesttype;
    uint8_t status;
    uint16_t value;
    uint16_t index;
    uint16_t length;
}
```

The additional data contains the control msg data to be send / received.

Packets of this type can be send by the usb-guest to the usb-host to
initiate a control transfer on the usb-device. endpoint, request, requesttype,
value and index have their standard meaning for usb control messages.
The status field is only used in the usb-host's response.

length is the amount of data the usb-guest is sending / expects to read
(in the USB_DIR_IN case). Note that the length should only be added
to `usb_redir_header.length` in one direction (and the actual packet
length should match).

When the control msg has been processed by the usb-device the usb-host sends
a `usb_redir_control_packet` back to the usb-guest, with all fields unchanged
except for the status field and length which get updated to match the
actual results.


## usb_redir_bulk_packet

```
usb_redir_header.type:    usb_redir_bulk_packet
usb_redir_header.length:  sizeof(usb_redir_bulk_packet_header) [+ length]
```

```c
struct usb_redir_bulk_packet_header {
    uint8_t endpoint;
    uint8_t status;
    uint16_t length;
    uint32_t stream_id;
    uint16_t length_high; /* High 16 bits of the packet length */
}
```

The additional data contains the bulk msg data to be send / received.

Packets of this type can be send by the usb-guest to the usb-host to
initiate a bulk transfer on the usb-device. `endpoint` and `stream_id` have
their standard meaning for usb bulk messages. The `status` field is only used
in the usb-host's response. `length` is the amount of data the usb-guest is
sending / expects to read (depending on the direction of the endpoint).

`length_high` contains the 16 high bits of length to allow packets larger
then 65535 bytes, it is only send/received if both sides have the
`usb_redir_cap_32bits_bulk_length` capability.

When the bulk msg has been processed by the usb-device the usb-host sends
a `usb_redir_bulk_packet` back to the usb-guest, with the `status` field and
length updated to match the actual results.

Note just as `usb_redir_control_packet` this packet only has additional data
in one direction depending on the direction of the endpoint.

Note see `usb_redir_buffered_bulk_packet` for an alternative for receiving data
from bulk endpoints.


## usb_redir_iso_packet

```
usb_redir_header.type:    usb_redir_iso_packet
usb_redir_header.length:  sizeof(usb_redir_iso_packet_header) + length
```

```c
struct usb_redir_iso_packet_header {
    uint8_t endpoint;
    uint8_t status;
    uint16_t length;
}
```

The additional data contains the iso msg data to be send / received.

Packets of this type should be send continuesly (at the endpoint interval
speed) as soon as an iso stream is started using `usb_redir_start_iso_stream`
the direction in which they gets send depends on the endpoints direction.

The status field only has meaning for packets send from the usb-host to
the usb-guest (for iso input endpoints). Due to buffering it is not possibly
to timely notify the usb-guest of transfer errors for iso output packets. The
usb-host will try to clear any error conditions itself. If it fails to do
so it will send a `usb_redir_iso_stream_status` to the usb-guest indicating
there is a problem with the iso stream.

Since `usb_redir_iso_packet`s are send continuously by the usb-host once
a stream is started on an iso input endpoint, the usb-host cannot set the
`usb_redir_header.id` to the id of the corresponding received packet. So for
`usb_redir_iso_packet's` the usb-host simply starts with an id of 0 and
increments this every packet. Note that when the usb-host has recovered from
a stall the id will restart at 0!


## usb_redir_interrupt_packet

```
usb_redir_header.type:    usb_redir_interrupt_packet
usb_redir_header.length:  sizeof(usb_redir_interrupt_packet_header) [+ length]
```

```c
struct usb_redir_interrupt_packet_header {
    uint8_t endpoint;
    uint8_t status;
    uint16_t length;
}
```

The additional data contains the interrupt msg data to be send / received.

The handling of interrupt endpoints differs significantly depending on wether
the endpoint is an input or output endpoint.

# Input endpoints

Input interrupt endpoints need to
be polled timely otherwise data may get lost. So for input interrupt endpoints
the usb-host takes care of the submitting and re-submitting of urbs, the
usb-guest can start / stop the receiving of interrupt packets using the
`usb_redir_start_interrupt_receiving` / `usb_redir_stop_interrupt_receiving`
packets. Note that for an input interrupt endpoint `usb_redir_interrupt_packet-s`
are only send in one direction, from the usb-host to the usb-guest!

Since `usb_redir_interrupt_packet`s are send unsolicited by the usb-host once
interrupt receiving has started, the usb-host cannot set the
`usb_redir_header.id` to the id of the corresponding received packet. So for
`usb_redir_interrupt_packet`s the usb-host simply starts with an id of 0 and
increments this every packet. Note that when the usb-host has recovered from
a stall the id will restart at 0!

# Output endpoints

For interrupt output endpoints the normal asynchroneous mechanism also used
for control and bulk transfers is used:

The usb-guest sends a `usb_redir_interrupt_packet` to the usb-host. When the
interrupt msg has been processed by the usb-device the usb-host sends
a `usb_redir_interrupt_packet` back to the usb-guest, with the status field and
length updated to match the actual results. This packet only has additional
data (the data to output) when send from usb-guest to usb-host.

Note that since unlike with iso data there is usually no notion of a stream
with interrupt data, buffering makes no sense for output interrupt packets,
instead they are delivered asap. Despite this asap delivery it is likely
that the timing constraints which apply to interrupt output transfers will
not be met. The consequences of this will vary from device to device.


## usb_redir_buffered_bulk_packet

```
usb_redir_header.type:    usb_redir_bulk_packet
usb_redir_header.length:  sizeof(usb_redir_bulk_packet_header) + length
usb_redir_header.id:      starts at 0, incremented by 1 per send packet
```

```c
struct usb_redir_buffered_bulk_packet_header {
    uint32_t stream_id;
    uint32_t length;
    uint8_t endpoint;
    uint8_t status;
}
```

The additional data contains the bulk msg data received.

Buffered bulk mode is intended for bulk *input* endpoints, where the data is
of a streaming nature (not part of a command-response protocol). These
endpoints' input buffer may overflow if data is not read quickly enough.
So in buffered bulk mode the usb-host takes care of the submitting and
re-submitting of bulk transfers. The usb-guest can start / stop the receiving
of buffered bulk data using the `usb_redir_start_bulk_receiving` /
`usb_redir_stop_bulk_receiving` packets.

Note that `usb_redir_buffered_bulk_packet-s` are only send in one direction,
from the usb-host to the usb-guest!

Since `usb_redir_buffered_bulk_packet-s` are send unsolicited by the usb-host
once bulk receiving has started, the usb-host cannot set the
`usb_redir_header.id` to the id of the corresponding received packet. So for
`usb_redir_buffered_bulk_packet-s` the usb-host simply starts with an id of 0 and
increments this every packet. Note that when the usb-host has recovered from
a stall the id will restart at 0!

A typical example where buffered bulk mode should be used is with the bulk in
endpoints of usb to serial convertors.

Note buffered bulk mode can only be used when both sides have the
`usb_redir_cap_bulk_receiving` capability.
