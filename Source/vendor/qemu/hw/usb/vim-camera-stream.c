#include "vim-camera-stream.h"

#include <stdlib.h>
#include <string.h>

const uint8_t vim_camera_vc_header[13] = {
    0x0d, 0x24, 0x01, 0x00, 0x01, 0x28, 0x00, 0xc0, 0xe1, 0xe4, 0x00, 0x01, 0x01
};
const uint8_t vim_camera_camera_terminal[18] = {
    0x12, 0x24, 0x02, 0x01, 0x01, 0x02, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00
};
const uint8_t vim_camera_output_terminal[9] = {
    0x09, 0x24, 0x03, 0x02, 0x01, 0x01, 0x00, 0x01, 0x00
};
const uint8_t vim_camera_vs_header[14] = {
    0x0e, 0x24, 0x01, 0x01, 0x65, 0x00, 0x81, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00
};
const uint8_t vim_camera_format_uncompressed[27] = {
    0x1b, 0x24, 0x04, 0x01, 0x02,
    0x59, 0x55, 0x59, 0x32, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71,
    0x10, 0x01, 0x00, 0x00, 0x00, 0x00
};
const uint8_t vim_camera_frame_uncompressed[30] = {
    0x1e, 0x24, 0x05, 0x01, 0x00, 0x80, 0x02, 0xf0, 0x00,
    0x00, 0x80, 0x32, 0x02, 0x00, 0x80, 0x32, 0x02,
    0x00, 0xb0, 0x04, 0x00, 0x2a, 0x2c, 0x0a, 0x00,
    0x01, 0x2a, 0x2c, 0x0a, 0x00
};
const uint8_t vim_camera_frame_tall[30] = {
    0x1e, 0x24, 0x05, 0x02, 0x00, 0x80, 0x02, 0xe0, 0x01,
    0x00, 0x00, 0x65, 0x04, 0x00, 0x00, 0x65, 0x04,
    0x00, 0x60, 0x09, 0x00, 0x2a, 0x2c, 0x0a, 0x00,
    0x01, 0x2a, 0x2c, 0x0a, 0x00
};

void vim_camera_function_headers(unsigned index, uint8_t vc[13], uint8_t vs[14]) {
    memcpy(vc, vim_camera_vc_header, 13);
    memcpy(vs, vim_camera_vs_header, 14);
    vc[12] = (uint8_t)(2 * index + 1);
    vs[6] = (uint8_t)(0x81 + index);
}

static int utf8_next(const uint8_t **cursor, uint32_t *code) {
    const uint8_t *p = *cursor;
    uint32_t cp;
    if (*p < 0x80) {
        *code = *p;
        *cursor = p + 1;
        return 1;
    }
    if ((*p & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) {
        cp = ((uint32_t)(*p & 0x1f) << 6) | (p[1] & 0x3f);
        if (cp < 0x80) return 0;
        *code = cp;
        *cursor = p + 2;
        return 1;
    }
    if ((*p & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
        cp = ((uint32_t)(*p & 0x0f) << 12) | ((uint32_t)(p[1] & 0x3f) << 6) | (p[2] & 0x3f);
        if (cp < 0x800 || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
        *code = cp;
        *cursor = p + 3;
        return 1;
    }
    if ((*p & 0xf8) == 0xf0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80) {
        cp = ((uint32_t)(*p & 0x07) << 18) | ((uint32_t)(p[1] & 0x3f) << 12) |
            ((uint32_t)(p[2] & 0x3f) << 6) | (p[3] & 0x3f);
        if (cp < 0x10000 || cp > 0x10ffff) return 0;
        *code = cp;
        *cursor = p + 4;
        return 1;
    }
    return 0;
}

int vim_camera_usb_string(const char *utf8, uint8_t *dest, size_t len) {
    uint16_t units[126];
    unsigned count = 0;
    const uint8_t *cursor = (const uint8_t *)(utf8 ? utf8 : "");
    if (!dest || len < 2) return -1;
    while (*cursor && count < 126) {
        uint32_t code;
        if (!utf8_next(&cursor, &code)) return -1;
        if (code > 0xffff) {
            if (count + 2 > 126) break;
            code -= 0x10000;
            units[count++] = (uint16_t)(0xd800 + (code >> 10));
            units[count++] = (uint16_t)(0xdc00 + (code & 0x3ff));
        } else units[count++] = (uint16_t)code;
    }
    unsigned full = 2u + count * 2u, copy = full, i;
    if (copy > len) copy = (unsigned)len;
    if (copy > 2) copy = 2u + ((copy - 2u) / 2u) * 2u;
    dest[0] = (uint8_t)full;
    dest[1] = 0x03;
    for (i = 0; i < count && 2u + (i + 1u) * 2u <= copy; i++) {
        dest[2 + i * 2] = (uint8_t)units[i];
        dest[3 + i * 2] = (uint8_t)(units[i] >> 8);
    }
    return (int)copy;
}

void vim_camera_ingress_init(VIMCameraIngress *ingress) {
    memset(ingress, 0, sizeof *ingress);
}

static void ingress_feed(VIMCameraFeed *feed, VIMCameraStream *streams, unsigned count,
    const uint8_t *bytes, size_t nbytes) {
    if (feed) vim_camera_feed(feed, streams, count, bytes, nbytes);
}

void vim_camera_ingress(VIMCameraIngress *ingress, VIMCameraFeed *feed, VIMCameraStream *streams,
    unsigned stream_count, VIMCameraDirectory *directory, int *updated, const uint8_t *bytes, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        uint8_t byte = bytes[i];
        uint32_t magic;
        if (ingress->phase == 1) {
            size_t run = count - i;
            if (run > ingress->frame_left) run = ingress->frame_left;
            ingress_feed(feed, streams, stream_count, bytes + i, run);
            i += run - 1;
            ingress->frame_left -= (uint32_t)run;
            if (ingress->frame_left == 0) {
                ingress->phase = 0;
                ingress->magic_got = 0;
            }
            continue;
        }
        if (ingress->phase == 2) {
            if (!ingress->body_got) {
                ingress->length_low = byte;
                ingress->body_got = 1;
                continue;
            }
            ingress->body_left = (uint16_t)(ingress->length_low | (byte << 8));
            ingress->body_got = 0;
            ingress->phase = ingress->body_left && ingress->body_left <= sizeof ingress->body ? 3 : 0;
            if (!ingress->phase) ingress->magic_got = 0;
            continue;
        }
        if (ingress->phase == 3) {
            ingress->body[ingress->body_got++] = byte;
            if (ingress->body_got < ingress->body_left) continue;
            if (directory && vim_camera_directory_parse(directory, ingress->body, ingress->body_got) >= 0 && updated)
                *updated = 1;
            ingress->phase = 0;
            ingress->magic_got = 0;
            continue;
        }
        ingress->magic[ingress->magic_got++] = byte;
        if (ingress->magic_got < 4) continue;
        memcpy(&magic, ingress->magic, 4);
        if (magic == VIM_CAMERA_DIRECTORY_MAGIC) {
            ingress->phase = 2;
            ingress->body_got = 0;
        } else if (magic == VIM_CAMERA_FRAME_MAGIC) {
            ingress->phase = 1;
            ingress->frame_left = 5u + VIM_CAMERA_FRAME_BYTES;
            ingress_feed(feed, streams, stream_count, ingress->magic, 4);
        } else {
            memmove(ingress->magic, ingress->magic + 1, 3);
            ingress->magic_got = 3;
        }
    }
}

void vim_camera_stream_init(VIMCameraStream *stream) {
    memset(stream, 0, sizeof *stream);
    stream->control_interface = 0;
    stream->stream_interface = 1;
    stream->frame[0] = calloc(1, VIM_CAMERA_FRAME_BYTES);
    stream->frame[1] = calloc(1, VIM_CAMERA_FRAME_BYTES);
}

void vim_camera_stream_fini(VIMCameraStream *stream) {
    free(stream->frame[0]);
    free(stream->frame[1]);
    memset(stream, 0, sizeof *stream);
}

static int probe_acceptable(const uint8_t *data, unsigned length) {
    if (length < VIM_CAMERA_PROBE_BYTES) return 0;
    return data[2] <= 1 && data[3] <= VIM_CAMERA_FRAME_TALL;
}

static void put32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)value; out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16); out[3] = (uint8_t)(value >> 24);
}

void vim_camera_frame_set_rate(uint8_t desc[30], unsigned fps) {
    uint32_t width = (uint32_t)(desc[5] | desc[6] << 8), height = (uint32_t)(desc[7] | desc[8] << 8);
    uint32_t bits = width * height * 16u * fps, interval = 10000000u / fps;
    put32(desc + 9, bits);
    put32(desc + 13, bits);
    put32(desc + 21, interval);
    put32(desc + 26, interval);
}

uint32_t vim_camera_stream_frame_bytes(const VIMCameraStream *stream) {
    return stream->isight || stream->frame_index == VIM_CAMERA_FRAME_TALL ?
        2u * VIM_CAMERA_FRAME_BYTES : VIM_CAMERA_FRAME_BYTES;
}

static void refresh_streaming(VIMCameraStream *stream) {
    stream->streaming = stream->alternate == 1 && stream->committed;
    if (!stream->streaming) stream->offset = 0;
}

static uint32_t packet_bytes(const VIMCameraStream *stream) {
    return stream->isight ? VIM_CAMERA_ISIGHT_PACKET : VIM_CAMERA_PAYLOAD_HEADER +
        (stream->max_payload ? stream->max_payload : VIM_CAMERA_PAYLOAD_BYTES);
}

static void remember_commit(VIMCameraStream *stream, const uint8_t *data) {
    uint32_t frame, packet = packet_bytes(stream);
    if (!stream->isight) {
        stream->frame_index = data[3] == VIM_CAMERA_FRAME_TALL ? VIM_CAMERA_FRAME_TALL : 1;
        stream->offset = 0;
    }
    frame = vim_camera_stream_frame_bytes(stream);
    memcpy(stream->negotiated, data, VIM_CAMERA_PROBE_BYTES);
    if (!stream->isight) stream->negotiated[3] = stream->frame_index;
    memcpy(stream->negotiated + 18, &frame, 4);
    memcpy(stream->negotiated + 22, &packet, 4);
    stream->have_negotiated = 1;
}

int vim_camera_stream_control(VIMCameraStream *stream, uint16_t request,
    uint16_t value, uint16_t index, uint8_t *data, unsigned length) {
    unsigned selector = value >> 8;
    unsigned interface = index & 0xff;
    unsigned operation = request & 0xff;
    int inbound = (request & 0x80) != 0;
    uint8_t probe[VIM_CAMERA_PROBE_BYTES];

    if (interface == stream->control_interface) {
        if (stream->isight) {
            if (inbound && operation == 0x86 && length >= 1) {
                data[0] = 0x01;
                return 1;
            }
            if (inbound && length >= 1 &&
                (operation == 0x81 || operation == 0x82 || operation == 0x83 ||
                 operation == 0x84 || operation == 0x87)) {
                unsigned copy = length < 4 ? length : 4;
                unsigned entity = index >> 8;
                memset(data, 0, copy);
                if (entity == 1 && selector == 0x02) data[0] = 0x02;
                else if (operation == 0x83) data[0] = 0xff;
                else if (operation == 0x84) data[0] = 0x01;
                else data[0] = 0x80;
                return (int)copy;
            }
            if (!inbound && operation == 0x01) return 0;
            return -1;
        }
        if (inbound && selector == 0x02 && operation == 0x81 && length >= 1) {
            data[0] = 0;
            return 1;
        }
        return -1;
    }
    if (interface != stream->stream_interface) return -1;
    if (selector != 0x01 && selector != 0x02) return -1;
    vim_camera_probe_bytes(probe);
    if (inbound && operation == 0x85 && length >= 2) {
        data[0] = VIM_CAMERA_PROBE_BYTES;
        data[1] = 0;
        return 2;
    }
    if (inbound && operation == 0x86 && length >= 1) {
        data[0] = 0x03;
        return 1;
    }
    if (inbound && (operation == 0x81 || operation == 0x82 || operation == 0x83 || operation == 0x87)) {
        unsigned copy = length < VIM_CAMERA_PROBE_BYTES ? length : VIM_CAMERA_PROBE_BYTES;
        if (stream->have_negotiated) {
            memcpy(data, stream->negotiated, copy);
        } else {
            if (stream->interval) put32(probe + 4, stream->interval);
            if (!stream->isight) {
                uint32_t packet = packet_bytes(stream);
                memcpy(probe + 22, &packet, 4);
            } else {
                uint32_t frame = vim_camera_stream_frame_bytes(stream);
                memcpy(probe + 18, &frame, 4);
            }
            memcpy(data, probe, copy);
        }
        return (int)copy;
    }
    if (!inbound && operation == 0x01) {
        if (stream->isight && length >= VIM_CAMERA_PROBE_BYTES) {
            remember_commit(stream, data);
        } else if (!probe_acceptable(data, length)) {
            return -1;
        } else {
            remember_commit(stream, data);
        }
        if (selector == 0x02) {
            stream->committed = 1;
            refresh_streaming(stream);
        }
        return 0;
    }
    return -1;
}

void vim_camera_stream_set_interface(VIMCameraStream *stream, int interface, int alt) {
    if (interface != stream->stream_interface) return;
    stream->alternate = (uint8_t)alt;
    refresh_streaming(stream);
}

int vim_camera_stream_push_frame(VIMCameraStream *stream, const uint8_t *yuy2, uint32_t length) {
    int slot;
    if (!yuy2 || length != VIM_CAMERA_FRAME_BYTES || !stream->frame[0] || !stream->frame[1]) return -1;
    if (!stream->active_valid && !stream->pending_valid) {
        slot = stream->active;
        stream->offset = 0;
        stream->isight_header = 0;
        stream->active_valid = 1;
    } else {
        slot = stream->active ^ 1;
        stream->pending_valid = 1;
    }
    memcpy(stream->frame[slot], yuy2, VIM_CAMERA_FRAME_BYTES);
    stream->have_frame = 1;
    stream->sequence++;
    return 0;
}

static uint32_t vim_bus_frindex;
static uint32_t vim_bus_frame_raw;
static uint32_t vim_bus_frames;

void vim_camera_note_bus_frame(uint32_t frindex) {
    uint32_t frame = frindex >> 3;
    if (frame != vim_bus_frame_raw) {
        uint32_t delta = (frame - vim_bus_frame_raw) & 0x7ff;
        if (!vim_bus_frames || !delta || delta > 32) delta = 1;
        vim_bus_frames += delta;
        vim_bus_frame_raw = frame;
    }
    vim_bus_frindex = frindex;
}

int vim_camera_stream_iso(VIMCameraStream *stream, uint8_t *packet, int capacity) {
    unsigned header = stream->isight ? 0 : VIM_CAMERA_PAYLOAD_HEADER;
    uint32_t chunk, total;
    int finishing;

    if (!stream->streaming || capacity < (int)header) return -1;
    {
        int most = (int)header + (stream->max_payload ? stream->max_payload : VIM_CAMERA_PAYLOAD_BYTES);
        if (capacity > most) capacity = most;
    }
    if (!stream->active_valid && stream->pending_valid) {
        stream->active ^= 1;
        stream->pending_valid = 0;
        stream->active_valid = 1;
        stream->offset = 0;
        stream->isight_header = 0;
    }
    if (!stream->active_valid) {
        if (stream->isight) return 0;
        if (!stream->have_frame) {
            uint32_t i;
            uint8_t *dest = stream->frame[stream->active];
            for (i = 0; i + 3 < VIM_CAMERA_FRAME_BYTES; i += 4) {
                dest[i] = 0;
                dest[i + 1] = 128;
                dest[i + 2] = 0;
                dest[i + 3] = 128;
            }
            stream->have_frame = 1;
        }
        stream->active_valid = 1;
        stream->offset = 0;
        stream->isight_header = 0;
    }
    if (stream->isight && !stream->isight_header) {
        static const uint8_t signature[12] = {
            0x11, 0x22, 0x33, 0x44, 0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0xfa, 0xce
        };
        if (capacity < 14) return -1;
        packet[0] = 14;
        packet[1] = (uint8_t)((stream->fid & 1) | 0x80);
        memcpy(packet + 2, signature, 12);
        stream->isight_header = 1;
        return 14;
    }
    if (header) {
        uint32_t scr = vim_bus_frames * 15000u + (vim_bus_frindex & 7u) * 1875u;
        uint16_t sof = (uint16_t)((vim_bus_frindex >> 3) & 0x7ff);
        if (stream->offset == 0) stream->pts = scr;
        stream->scr = scr;
        packet[0] = (uint8_t)header;
        packet[1] = (uint8_t)((stream->fid & 1) | 0x80 | 0x04 | 0x08);
        memcpy(packet + 2, &stream->pts, 4);
        memcpy(packet + 6, &scr, 4);
        memcpy(packet + 10, &sof, 2);
    }
    total = vim_camera_stream_frame_bytes(stream);
    chunk = total - stream->offset;
    if (chunk > (uint32_t)(capacity - (int)header))
        chunk = (uint32_t)(capacity - (int)header);
    finishing = stream->offset + chunk >= total;
    if (total == VIM_CAMERA_FRAME_BYTES) {
        memcpy(packet + header, stream->frame[stream->active] + stream->offset, chunk);
    } else {
        const uint32_t line = VIM_CAMERA_WIDTH * 2;
        uint32_t done = 0;
        while (done < chunk) {
            uint32_t at = stream->offset + done, column = at % line;
            uint32_t piece = line - column;
            if (piece > chunk - done) piece = chunk - done;
            memcpy(packet + header + done, stream->frame[stream->active] + (at / line / 2) * line + column, piece);
            done += piece;
        }
    }
    if (stream->isight) {
        uint32_t pair;
        for (pair = 0; pair + 1 < chunk; pair += 2) {
            uint8_t first = packet[pair];
            packet[pair] = packet[pair + 1];
            packet[pair + 1] = first;
        }
    }
    stream->offset += chunk;
    if (finishing) {
        if (header) packet[1] |= 0x02;
        stream->offset = 0;
        stream->isight_header = 0;
        stream->active_valid = 0;
        stream->fid ^= 1;
    }
    return (int)(header + chunk);
}

void vim_camera_feed_init(VIMCameraFeed *feed) {
    memset(feed, 0, sizeof *feed);
    feed->payload = calloc(1, VIM_CAMERA_FRAME_BYTES);
}

void vim_camera_feed_fini(VIMCameraFeed *feed) {
    free(feed->payload);
    memset(feed, 0, sizeof *feed);
}

static void vim_camera_feed_header(VIMCameraFeed *feed, unsigned count, int streams, uint8_t byte) {
    uint32_t magic, length;
    feed->header[feed->header_got++] = byte;
    if (feed->header_got < VIM_CAMERA_FRAME_HEADER_BYTES) return;
    memcpy(&magic, feed->header, 4);
    memcpy(&length, feed->header + 4, 4);
    if (magic == VIM_CAMERA_FRAME_MAGIC && length == VIM_CAMERA_FRAME_BYTES) {
        feed->function = feed->header[8];
        feed->drop = !streams || feed->function >= count;
        feed->in_payload = 1;
        feed->payload_got = 0;
        return;
    }
    memmove(feed->header, feed->header + 1, VIM_CAMERA_FRAME_HEADER_BYTES - 1);
    feed->header_got = VIM_CAMERA_FRAME_HEADER_BYTES - 1;
}

void vim_camera_feed(VIMCameraFeed *feed, VIMCameraStream *streams, unsigned count,
    const uint8_t *bytes, size_t nbytes) {
    size_t i = 0;
    if (!feed->payload) return;
    while (i < nbytes) {
        if (feed->in_payload) {
            size_t run = VIM_CAMERA_FRAME_BYTES - feed->payload_got;
            if (run > nbytes - i) run = nbytes - i;
            memcpy(feed->payload + feed->payload_got, bytes + i, run);
            feed->payload_got += (uint32_t)run;
            i += run;
            if (feed->payload_got < VIM_CAMERA_FRAME_BYTES) continue;
            if (!feed->drop)
                vim_camera_stream_push_frame(&streams[feed->function], feed->payload, VIM_CAMERA_FRAME_BYTES);
            feed->in_payload = 0;
            feed->header_got = 0;
            continue;
        }
        vim_camera_feed_header(feed, count, streams != NULL, bytes[i++]);
    }
}
