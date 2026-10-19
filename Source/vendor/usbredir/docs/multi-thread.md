# Multithreading

libusbredirparser and libusbredirhost are *not* 100% thread-safe. They allow
usage from multiple threads, but with limitations.

The intended usage of the multi-threading support for libusbredirparser is to
have one reader thread and allow writes / packet sends from multiple threads
(including the reader thread). It is up to the app to deal with flushing
writes by calling `do_write` itself. `do_write` may be called from multiple
threads, libusbredirparser will serialize any calls to the write callback.

The intended usage of the multi-threading support for libusbredirhost is to
have one reader thread, one thread calling libusb's `handle_events` function
and optionally also a separate writer thread.

libusbredirhost offers setting a write flush callback, which it will call
(if set) everytime it has queued some data to write. This can be used to
wakeup a writer thread or it can call `usbredirhost_write_guest_data`, to
directly write out the queued data from the context of its caller. Note that
the flush callback may be called from both `usbredirparser_do_read` as well
as from `libusb_handle_events`, so if those are done in separate threads,
it may get called from multiple threads!!


The above translates to some functions only allowing one caller at a time,
while others allow multiple callers, see below for a detailed overview.

In order to enable the multi-thread support in libusbredir[^1] the app
must provide a number of locking callback functions, for libusbredirparser
this is done by filling in the `usbredirparser_*_lock` funcs in the
usbredirparser struct before calling `usbredirparser_init().` For
libusbredirhost the locking functions (and a write-flush callback) can
be specified by using `usbredirhost_open_full()` instead of
`usbredirhost_open()`.

[^1]: Note that the `alloc_lock_func` may not fail! If it returns NULL no locking
will be done and usage from multiple threads will be unsafe.


## Overview of per function multi-thread safeness

### usbredirparser

#### Only one caller allowed at a time:
- `usbredirparser_create`
- `usbredirparser_init`
- `usbredirparser_destroy`
- `usbredirparser_do_read`

#### Multiple callers allowed:
- `usbredirparser_get_peer_caps`[^2]
- `usbredirparser_peer_has_cap`[^2]
- `usbredirparser_has_data_to_write`
- `usbredirparser_do_write`
- `usbredirparser_free_write_buffer`
- `usbredirparser_free_packet_data`
- `usbredirparser_send_*`

### usbredirhost

#### Only one caller allowed at a time:
- `usbredirhost_open`
- `usbredirhost_open_full`
- `usbredirhost_close`
- `usbredirhost_read_guest_data`
- `usbredirhost_set_device`

#### Multiple callers allowed:
- `usbredirhost_has_data_to_write`
- `usbredirhost_write_guest_data`
- `usbredirhost_free_write_buffer`
- `libusb_handle_events`[^3]

# Footnotes

[^2]: These only return the actual peer caps after the initial hello message
    has been read, as indicated by the hello_func callback.

[^3]: libusb is thread safe itself, thus allowing multiple callers.
