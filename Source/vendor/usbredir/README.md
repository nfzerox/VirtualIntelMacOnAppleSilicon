# usbredir

usbredir is a protocol for redirection USB traffic from a single USB device,
to a different (virtual) machine then the one to which the USB device is
attached. See [usb-redirection-protocol.md](docs/usb-redirection-protocol.md)
for the description / definition of this protocol.

This package contains a number of libraries to help implementing support
for usbredir and a few simple usbredir applications:

## usbredirparser

A library containing the parser for the usbredir protocol

## usbredirhost

A library implementing the usb-host side of a usbredir connection, which is the
side to which the actual USB device is attached.

All that an application wishing to implement an usb-host needs to do is:
- Provide a libusb device handle for the device
- Provide write and read callbacks for the actual transport of usbredir data
- Monitor for usbredir and libusb read/write events and call their handlers

## usbredirect

The usbredirect binary is an usbredir client for exporting an USB device either
as TCP client or server, for use from another (virtual) machine through the
usbredir protocol.

## usbredirtestclient

A small testclient for the usbredir protocol over tcp, using usbredirparser

The upstream git repository can be found at
http://gitlab.freedesktop.org/spice/usbredir

Bug reports can be filed against the Spice/usbredir component:
https://gitlab.freedesktop.org/spice/usbredir/issues

You can also send patches to the spice-devel mailing list:
http://lists.freedesktop.org/mailman/listinfo/spice-devel
