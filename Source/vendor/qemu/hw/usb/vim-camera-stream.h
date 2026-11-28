#ifndef VIM_CAMERA_STREAM_H
#define VIM_CAMERA_STREAM_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error Virtual Intel Mac camera frames are little-endian
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define VIM_CAMERA_VENDOR_ID 0x46f4
#define VIM_CAMERA_PRODUCT_ID 0x0003
#define VIM_CAMERA_BCD_DEVICE 0x0100
#define VIM_CAMERA_WIDTH 640
#define VIM_CAMERA_HEIGHT 240
#define VIM_CAMERA_FPS 15
#define VIM_CAMERA_FRAME_INTERVAL 666666u
#define VIM_CAMERA_FRAME_BYTES ((uint32_t)(VIM_CAMERA_WIDTH * VIM_CAMERA_HEIGHT * 2))
#define VIM_CAMERA_FRAME_MAGIC 0x31434d56u
#define VIM_CAMERA_DIRECTORY_MAGIC 0x32434d56u
#define VIM_CAMERA_SLOT_KEY_BYTES 256
#define VIM_CAMERA_FRAME_HEADER_BYTES 9
#define VIM_CAMERA_PROBE_BYTES 26
#define VIM_CAMERA_FRAME_TALL 2
#define VIM_CAMERA_PAYLOAD_HEADER 12
#define VIM_CAMERA_PAYLOAD_BYTES 960
#define VIM_CAMERA_MAX_PACKET 3072
#define VIM_CAMERA_HB_PACKET 3072
#define VIM_CAMERA_HB_ENDPOINT_SIZE 0x1400
#define VIM_CAMERA_MAX_FUNCTIONS 8
#define VIM_CAMERA_NAME_BYTES 128

#define VIM_CAMERA_CLASS_VIDEO 0x0e
#define VIM_CAMERA_SUBCLASS_CONTROL 0x01
#define VIM_CAMERA_SUBCLASS_STREAMING 0x02
#define VIM_CAMERA_DEVICE_CLASS 0xef
#define VIM_CAMERA_DEVICE_SUBCLASS 0x02
#define VIM_CAMERA_DEVICE_PROTOCOL 0x01
#define VIM_CAMERA_COLLECTION_SUBCLASS 0x03

#define VIM_CAMERA_ISIGHT_PACKET 962


extern const uint8_t vim_camera_vc_header[13];
extern const uint8_t vim_camera_camera_terminal[18];
extern const uint8_t vim_camera_output_terminal[9];
extern const uint8_t vim_camera_vs_header[14];
extern const uint8_t vim_camera_format_uncompressed[27];
extern const uint8_t vim_camera_frame_uncompressed[30];
extern const uint8_t vim_camera_frame_tall[30];

typedef struct VIMCameraStream {
    uint8_t committed;
    uint8_t streaming;
    uint8_t alternate;
    uint8_t control_interface;
    uint8_t stream_interface;
    uint8_t isight;
    uint8_t fid;
    uint32_t pts;
    uint32_t scr;
    uint8_t isight_header;
    uint8_t frame_index;
    uint32_t interval;
    uint16_t max_payload;
    uint8_t have_negotiated;
    uint8_t negotiated[VIM_CAMERA_PROBE_BYTES];
    int active;
    int active_valid;
    int pending_valid;
    uint8_t have_frame;
    uint32_t sequence;
    uint32_t offset;
    uint8_t *frame[2];
} VIMCameraStream;

typedef struct VIMCameraFeed {
    uint8_t header[VIM_CAMERA_FRAME_HEADER_BYTES];
    unsigned header_got;
    uint8_t function;
    int drop;
    uint8_t *payload;
    uint32_t payload_got;
    int in_payload;
} VIMCameraFeed;

typedef struct VIMCameraDirectory {
    unsigned count;
    uint8_t mode;
    uint8_t preferred;
    uint8_t present[VIM_CAMERA_MAX_FUNCTIONS];
    char name[VIM_CAMERA_MAX_FUNCTIONS][VIM_CAMERA_NAME_BYTES];
} VIMCameraDirectory;

typedef struct VIMCameraSlots {
    char key[VIM_CAMERA_MAX_FUNCTIONS][VIM_CAMERA_SLOT_KEY_BYTES];
    char name[VIM_CAMERA_MAX_FUNCTIONS][VIM_CAMERA_NAME_BYTES];
    uint8_t present[VIM_CAMERA_MAX_FUNCTIONS];
    unsigned count;
} VIMCameraSlots;

typedef struct VIMCameraIngress {
    uint8_t magic[4];
    unsigned magic_got;
    int phase;
    unsigned frame_left;
    uint8_t length_low;
    uint16_t body_left;
    uint8_t body[2048];
    unsigned body_got;
} VIMCameraIngress;

#define VIM_CAMERA_STATUS_DRIVER 0xfe

typedef struct VIMCameraStatus {
    uint8_t streaming[VIM_CAMERA_MAX_FUNCTIONS];
    uint8_t driver;
    unsigned count;
    uint8_t index;
    int have_index;
} VIMCameraStatus;

static inline void vim_camera_probe_bytes(uint8_t out[VIM_CAMERA_PROBE_BYTES]) {
    memset(out, 0, VIM_CAMERA_PROBE_BYTES);
    out[2] = 1;
    out[3] = 1;
    out[4] = (uint8_t)VIM_CAMERA_FRAME_INTERVAL;
    out[5] = (uint8_t)(VIM_CAMERA_FRAME_INTERVAL >> 8);
    out[6] = (uint8_t)(VIM_CAMERA_FRAME_INTERVAL >> 16);
    out[7] = (uint8_t)(VIM_CAMERA_FRAME_INTERVAL >> 24);
    out[16] = 1;
    out[18] = (uint8_t)VIM_CAMERA_FRAME_BYTES;
    out[19] = (uint8_t)(VIM_CAMERA_FRAME_BYTES >> 8);
    out[20] = (uint8_t)(VIM_CAMERA_FRAME_BYTES >> 16);
    out[21] = (uint8_t)(VIM_CAMERA_FRAME_BYTES >> 24);
    out[22] = (uint8_t)2048;
    out[23] = (uint8_t)(2048 >> 8);
}

static inline void vim_camera_frame_header(uint8_t out[VIM_CAMERA_FRAME_HEADER_BYTES], uint8_t function) {
    uint32_t magic = VIM_CAMERA_FRAME_MAGIC, length = VIM_CAMERA_FRAME_BYTES;
    memcpy(out, &magic, 4);
    memcpy(out + 4, &length, 4);
    out[8] = function;
}

static inline int vim_camera_function_should_run(const char *mode, int preferred, int streaming) {
    if (mode && strcmp(mode, "off") == 0) return 0;
    if (mode && strcmp(mode, "autosense") == 0) return streaming ? 1 : 0;
    if (preferred) return 1;
    return streaming ? 1 : 0;
}

static inline int vim_camera_function_for_slot(unsigned slot, unsigned preferred, unsigned functions) {
    if (!functions) return -1;
    if (slot == preferred) return slot < functions ? (int)slot : 0;
    if (slot == 0 && preferred >= functions) return -1;
    return slot < functions ? (int)slot : -1;
}

void vim_camera_function_headers(unsigned index, uint8_t vc[13], uint8_t vs[14]);
int vim_camera_usb_string(const char *utf8, uint8_t *dest, size_t len);
static inline void vim_camera_copy_text(char *dest, size_t cap, const char *text) {
    size_t i;
    if (!dest || !cap) return;
    if (!text) text = "";
    for (i = 0; i + 1 < cap && text[i]; i++) dest[i] = text[i];
    dest[i] = 0;
}
static inline void vim_camera_slots_init(VIMCameraSlots *slots) {
    memset(slots, 0, sizeof *slots);
}
static inline unsigned vim_camera_slots_update(VIMCameraSlots *slots, const char *const *keys,
    const char *const *names, unsigned count) {
    unsigned i, slot, added = 0;
    for (i = 0; i < slots->count; i++) slots->present[i] = 0;
    for (i = 0; i < count; i++) {
        const char *key = keys && keys[i] ? keys[i] : "";
        for (slot = 0; slot < slots->count; slot++)
            if (strcmp(slots->key[slot], key) == 0) break;
        if (slot == slots->count) {
            if (slots->count >= VIM_CAMERA_MAX_FUNCTIONS) continue;
            vim_camera_copy_text(slots->key[slot], sizeof slots->key[slot], key);
            slots->count++;
            added++;
        }
        vim_camera_copy_text(slots->name[slot], sizeof slots->name[slot], names && names[i] ? names[i] : key);
        slots->present[slot] = 1;
    }
    return added;
}
static inline int vim_camera_directory_write(uint8_t *dest, size_t cap, const VIMCameraSlots *slots,
    int autosense, unsigned preferred) {
    uint8_t body[2048];
    unsigned count = slots ? slots->count : 0, offset = 3, i, nlen, total;
    uint32_t magic = VIM_CAMERA_DIRECTORY_MAGIC;
    if (!dest || count > VIM_CAMERA_MAX_FUNCTIONS) return -1;
    if (preferred >= count) preferred = 0;
    body[0] = (uint8_t)count;
    body[1] = autosense ? 1 : 0;
    body[2] = (uint8_t)preferred;
    for (i = 0; i < count; i++) {
        nlen = (unsigned)strlen(slots->name[i]);
        if (nlen > 127) nlen = 127;
        if (offset + 2 + nlen > sizeof body) return -1;
        body[offset++] = slots->present[i] ? 1 : 0;
        body[offset++] = (uint8_t)nlen;
        memcpy(body + offset, slots->name[i], nlen);
        offset += nlen;
    }
    total = 6 + offset;
    if (total > cap) return -1;
    memcpy(dest, &magic, 4);
    dest[4] = (uint8_t)offset;
    dest[5] = (uint8_t)(offset >> 8);
    memcpy(dest + 6, body, offset);
    return (int)total;
}
static inline int vim_camera_directory_parse(VIMCameraDirectory *directory, const uint8_t *payload,
    unsigned length) {
    unsigned count, offset, i, nlen;
    if (!directory || !payload || length < 3) return -1;
    count = payload[0];
    if (count > VIM_CAMERA_MAX_FUNCTIONS) return -1;
    memset(directory, 0, sizeof *directory);
    directory->count = count;
    directory->mode = payload[1] ? 1 : 0;
    directory->preferred = payload[2] < count ? payload[2] : 0;
    offset = 3;
    for (i = 0; i < count; i++) {
        if (offset + 2 > length) return -1;
        directory->present[i] = payload[offset++] ? 1 : 0;
        nlen = payload[offset++];
        if (offset + nlen > length || nlen >= VIM_CAMERA_NAME_BYTES) return -1;
        memcpy(directory->name[i], payload + offset, nlen);
        directory->name[i][nlen] = 0;
        offset += nlen;
    }
    return (int)offset;
}
void vim_camera_ingress_init(VIMCameraIngress *ingress);
void vim_camera_ingress(VIMCameraIngress *ingress, VIMCameraFeed *feed, VIMCameraStream *streams,
    unsigned stream_count, VIMCameraDirectory *directory, int *updated, const uint8_t *bytes, size_t count);

void vim_camera_note_bus_frame(uint32_t frindex);
void vim_camera_stream_init(VIMCameraStream *stream);
void vim_camera_stream_fini(VIMCameraStream *stream);
int vim_camera_stream_control(VIMCameraStream *stream, uint16_t request,
    uint16_t value, uint16_t index, uint8_t *data, unsigned length);
void vim_camera_stream_set_interface(VIMCameraStream *stream, int interface, int alt);
int vim_camera_stream_push_frame(VIMCameraStream *stream, const uint8_t *yuy2, uint32_t length);
int vim_camera_stream_iso(VIMCameraStream *stream, uint8_t *packet, int capacity);
uint32_t vim_camera_stream_frame_bytes(const VIMCameraStream *stream);
void vim_camera_frame_set_rate(uint8_t desc[30], unsigned fps);
void vim_camera_feed_init(VIMCameraFeed *feed);
void vim_camera_feed_fini(VIMCameraFeed *feed);
void vim_camera_feed(VIMCameraFeed *feed, VIMCameraStream *streams, unsigned count,
    const uint8_t *bytes, size_t nbytes);

static inline void vim_camera_status_init(VIMCameraStatus *status, unsigned count) {
    memset(status, 0, sizeof *status);
    status->count = count > VIM_CAMERA_MAX_FUNCTIONS ? VIM_CAMERA_MAX_FUNCTIONS : count;
}

static inline void vim_camera_status_bytes(VIMCameraStatus *status, const uint8_t *bytes, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (!status->have_index) {
            status->index = bytes[i];
            status->have_index = 1;
            continue;
        }
        if (status->index == VIM_CAMERA_STATUS_DRIVER) status->driver = bytes[i] ? 1 : 0;
        else if (status->index < status->count) status->streaming[status->index] = bytes[i] ? 1 : 0;
        status->have_index = 0;
    }
}

#ifdef __cplusplus
}
#endif

#endif
