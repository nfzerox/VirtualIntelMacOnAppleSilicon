#import <Foundation/Foundation.h>
#import "../VirtualIntelMac/VIMGuestFamilies.h"
#import "../VirtualIntelMac/VIMCameraCapture.h"

typedef struct VIMCameraAssembly {
    uint8_t frame[VIM_CAMERA_FRAME_BYTES];
    uint32_t filled;
    uint8_t fid;
    int fid_valid;
    int complete;
} VIMCameraAssembly;

static inline void vim_camera_assembly_init(VIMCameraAssembly *assembly) {
    memset(assembly, 0, sizeof *assembly);
}
static inline int vim_camera_assembly_packet(VIMCameraAssembly *assembly, const uint8_t *packet, int length) {
    unsigned payload;
    uint8_t fid;
    if (length < VIM_CAMERA_PAYLOAD_HEADER || !packet || packet[0] < VIM_CAMERA_PAYLOAD_HEADER ||
        packet[0] > length) return -1;
    fid = packet[1] & 1;
    payload = (unsigned)length - packet[0];
    if (assembly->fid_valid && fid != assembly->fid) {
        assembly->filled = 0;
        assembly->complete = 0;
    }
    assembly->fid = fid;
    assembly->fid_valid = 1;
    if (assembly->filled + payload > VIM_CAMERA_FRAME_BYTES) return -1;
    if (payload) memcpy(assembly->frame + assembly->filled, packet + packet[0], payload);
    assembly->filled += payload;
    if (packet[1] & 0x02) {
        assembly->complete = assembly->filled == VIM_CAMERA_FRAME_BYTES;
        return assembly->complete ? 1 : -1;
    }
    return 0;
}


static void Fail(const char *message) {
    fprintf(stderr, "camera routing: %s\n", message);
    exit(1);
}
static uint16_t LE16(const uint8_t *bytes) {
    return (uint16_t)(bytes[0] | (bytes[1] << 8));
}
static void Commit(VIMCameraStream *stream) {
    uint8_t probe[VIM_CAMERA_PROBE_BYTES];
    vim_camera_probe_bytes(probe);
    if (vim_camera_stream_control(stream, 0x2101, 0x0100, 1, probe, sizeof probe) != 0 ||
        vim_camera_stream_control(stream, 0x2101, 0x0200, 1, probe, sizeof probe) != 0)
        Fail("the device rejected its own probe");
    vim_camera_stream_set_interface(stream, 1, 1);
    if (!stream->streaming) Fail("commit plus alternate setting 1 did not start streaming");
}

static void CheckDescriptors(void) {
    const uint8_t guid[16] = {
        0x59, 0x55, 0x59, 0x32, 0x00, 0x00, 0x10, 0x00,
        0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
    };
    if (sizeof vim_camera_vc_header + sizeof vim_camera_camera_terminal + sizeof vim_camera_output_terminal != 40)
        Fail("video control descriptors do not match their header length");
    if (LE16(vim_camera_vc_header + 5) != 40 || vim_camera_vc_header[1] != 0x24)
        Fail("video control header is not USB Video Class 1.0");
    if (sizeof vim_camera_vs_header + sizeof vim_camera_format_uncompressed + sizeof vim_camera_frame_uncompressed +
        sizeof vim_camera_frame_tall != 101)
        Fail("video streaming descriptors do not match their header length");
    if (LE16(vim_camera_vs_header + 4) != 101 || vim_camera_vs_header[6] != 0x81)
        Fail("streaming header does not point at the isochronous endpoint");
    if (memcmp(vim_camera_format_uncompressed + 5, guid, 16) != 0)
        Fail("the only format is not YUY2");
    if (LE16(vim_camera_frame_uncompressed + 5) != VIM_CAMERA_WIDTH ||
        LE16(vim_camera_frame_uncompressed + 7) != VIM_CAMERA_HEIGHT)
        Fail("the first frame is not 640x240");
    if (vim_camera_format_uncompressed[4] != 2 || vim_camera_frame_tall[3] != VIM_CAMERA_FRAME_TALL ||
        LE16(vim_camera_frame_tall + 5) != 640 || LE16(vim_camera_frame_tall + 7) != 480)
        Fail("the second frame is not 640x480");
    if ((uint32_t)(vim_camera_frame_tall[17] | vim_camera_frame_tall[18] << 8 | vim_camera_frame_tall[19] << 16) !=
        2 * VIM_CAMERA_FRAME_BYTES)
        Fail("the 640x480 frame buffer size is wrong");
    {
        uint8_t desc[30];
        memcpy(desc, vim_camera_frame_tall, sizeof desc);
        vim_camera_frame_set_rate(desc, 10);
        if ((uint32_t)(desc[21] | desc[22] << 8 | desc[23] << 16 | (uint32_t)desc[24] << 24) != 1000000u ||
            (uint32_t)(desc[26] | desc[27] << 8 | desc[28] << 16 | (uint32_t)desc[29] << 24) != 1000000u ||
            (uint32_t)(desc[9] | desc[10] << 8 | desc[11] << 16 | (uint32_t)desc[12] << 24) != 640u * 480u * 16u * 10u)
            Fail("a frame descriptor does not carry its frame rate");
    }
}

static void CheckHighBandwidth(void) {
    VIMCameraStream stream;
    uint8_t packet[VIM_CAMERA_MAX_PACKET], probe[VIM_CAMERA_PROBE_BYTES];
    static uint8_t frame[VIM_CAMERA_FRAME_BYTES];
    vim_camera_stream_init(&stream);
    stream.max_payload = VIM_CAMERA_HB_PACKET - VIM_CAMERA_PAYLOAD_HEADER;
    if (vim_camera_stream_control(&stream, 0xa181, 0x0100, 1, probe, sizeof probe) != VIM_CAMERA_PROBE_BYTES ||
        LE16(probe + 22) != VIM_CAMERA_HB_PACKET)
        Fail("the probe does not name the high-bandwidth packet size");
    Commit(&stream);
    vim_camera_stream_push_frame(&stream, frame, sizeof frame);
    if (vim_camera_stream_iso(&stream, packet, sizeof packet) != VIM_CAMERA_HB_PACKET)
        Fail("a high-bandwidth packet is not full");
    vim_camera_stream_fini(&stream);
}

static void CheckTallStream(void) {
    VIMCameraStream stream;
    uint8_t packet[1024], probe[VIM_CAMERA_PROBE_BYTES];
    static uint8_t frame[VIM_CAMERA_FRAME_BYTES], received[2 * VIM_CAMERA_FRAME_BYTES];
    uint32_t filled = 0, line = VIM_CAMERA_WIDTH * 2, y;
    int result;
    vim_camera_stream_init(&stream);
    vim_camera_probe_bytes(probe);
    probe[3] = VIM_CAMERA_FRAME_TALL;
    if (vim_camera_stream_control(&stream, 0x2101, 0x0100, 1, probe, sizeof probe) != 0 ||
        vim_camera_stream_control(&stream, 0x2101, 0x0200, 1, probe, sizeof probe) != 0)
        Fail("the device rejected 640x480");
    vim_camera_stream_set_interface(&stream, 1, 1);
    if (vim_camera_stream_control(&stream, 0xa181, 0x0200, 1, probe, sizeof probe) != VIM_CAMERA_PROBE_BYTES ||
        probe[3] != VIM_CAMERA_FRAME_TALL ||
        (uint32_t)(probe[18] | probe[19] << 8 | probe[20] << 16) != 2 * VIM_CAMERA_FRAME_BYTES)
        Fail("the 640x480 commit does not report its frame size");
    for (y = 0; y < VIM_CAMERA_HEIGHT; y++) memset(frame + y * line, (int)(y & 0xff), line);
    vim_camera_stream_push_frame(&stream, frame, sizeof frame);
    for (;;) {
        result = vim_camera_stream_iso(&stream, packet, sizeof packet);
        if (result < 12 || filled + (uint32_t)result - 12 > sizeof received) Fail("the 640x480 frame overran");
        memcpy(received + filled, packet + 12, (size_t)result - 12);
        filled += (uint32_t)result - 12;
        if (packet[1] & 0x02) break;
    }
    if (filled != sizeof received) Fail("the 640x480 frame has the wrong length");
    for (y = 0; y < 480; y++)
        if (received[y * line] != (uint8_t)(y / 2) || received[y * line + line - 1] != (uint8_t)(y / 2))
            Fail("640x480 lines are not the stored lines doubled");
    vim_camera_stream_fini(&stream);
}

static void CheckPlainStream(void) {
    VIMCameraStream stream;
    VIMCameraAssembly assembly;
    uint8_t packet[1024], probe[VIM_CAMERA_PROBE_BYTES];
    static uint8_t frame[VIM_CAMERA_FRAME_BYTES];
    int result, packets = 0;
    vim_camera_stream_init(&stream);
    vim_camera_stream_set_interface(&stream, 1, 1);
    if (vim_camera_stream_iso(&stream, packet, sizeof packet) != -1)
        Fail("streaming started before the format was committed");
    Commit(&stream);
    if (vim_camera_stream_control(&stream, 0xa181, 0x0100, 1, probe, sizeof probe) != VIM_CAMERA_PROBE_BYTES ||
        LE16(probe + 22) != VIM_CAMERA_PAYLOAD_HEADER + VIM_CAMERA_PAYLOAD_BYTES)
        Fail("the probe does not name the packet size that arrives");
    result = vim_camera_stream_iso(&stream, packet, sizeof packet);
    if (result != VIM_CAMERA_PAYLOAD_HEADER + VIM_CAMERA_PAYLOAD_BYTES || packet[0] != 12 || !(packet[1] & 0x80))
        Fail("an idle camera did not send full UVC packets");
    vim_camera_stream_fini(&stream);

    vim_camera_stream_init(&stream);
    Commit(&stream);
    memset(frame, 0x5a, sizeof frame);
    frame[0] = 0x42;
    if (vim_camera_stream_push_frame(&stream, frame, sizeof frame) != 0)
        Fail("the camera rejected a full frame");
    vim_camera_assembly_init(&assembly);
    while (!assembly.complete) {
        result = vim_camera_stream_iso(&stream, packet, sizeof packet);
        if (result < 12 || ++packets > 2000) Fail("the frame did not finish on the isochronous endpoint");
        if (vim_camera_assembly_packet(&assembly, packet, result) < 0)
            Fail("an isochronous packet was not a valid payload");
    }
    if (packets != (int)(VIM_CAMERA_FRAME_BYTES / VIM_CAMERA_PAYLOAD_BYTES))
        Fail("a frame is not exactly 320 full packets");
    if (memcmp(assembly.frame, frame, sizeof frame) != 0)
        Fail("the isochronous frame did not match the picture that was sent");
    vim_camera_stream_fini(&stream);
}

static void CheckISightStream(void) {
    VIMCameraStream stream;
    uint8_t packet[1024];
    static uint8_t frame[VIM_CAMERA_FRAME_BYTES];
    int result;
    uint32_t received = 0;
    vim_camera_stream_init(&stream);
    stream.isight = 1;
    Commit(&stream);
    if (vim_camera_stream_iso(&stream, packet, sizeof packet) != 0)
        Fail("the iSight sent data before the first picture");
    for (uint32_t i = 0; i < sizeof frame; i += 2) {
        frame[i] = 0x10;
        frame[i + 1] = 0x80;
    }
    vim_camera_stream_push_frame(&stream, frame, sizeof frame);
    result = vim_camera_stream_iso(&stream, packet, sizeof packet);
    if (result != 14 || packet[0] != 14 || packet[2] != 0x11 || packet[13] != 0xce)
        Fail("the iSight picture did not start with its signature packet");
    while (received < 2 * VIM_CAMERA_FRAME_BYTES) {
        result = vim_camera_stream_iso(&stream, packet, sizeof packet);
        if (result != VIM_CAMERA_PAYLOAD_BYTES) Fail("an iSight sample packet was not 960 bytes");
        if (packet[0] != 0x80 || packet[1] != 0x10) Fail("iSight samples are not UYVY");
        received += (uint32_t)result;
    }
    vim_camera_stream_fini(&stream);
}

static void CheckFeedAndDirectory(void) {
    VIMCameraSlots slots;
    VIMCameraDirectory directory;
    VIMCameraIngress ingress;
    VIMCameraFeed feed;
    VIMCameraStream streams[2];
    static uint8_t frame[VIM_CAMERA_FRAME_BYTES];
    uint8_t message[2048], header[VIM_CAMERA_FRAME_HEADER_BYTES];
    const char *keys[] = {"builtin", "iphone"};
    const char *names[] = {"MacBook Pro Camera", "iPhone de Th\xc3\xa9o Camera"};
    const char *only[] = {"builtin"};
    int wrote, updated = 0, index;
    vim_camera_slots_init(&slots);
    if (vim_camera_slots_update(&slots, keys, names, 2) != 2 || strcmp(slots.key[1], "iphone") != 0)
        Fail("two host cameras did not take the first two slots");
    wrote = vim_camera_directory_write(message, sizeof message, &slots, 1, 1);
    if (wrote < 7 || vim_camera_directory_parse(&directory, message + 6, (unsigned)wrote - 6) < 0 ||
        directory.count != 2 || directory.mode != 1 || directory.preferred != 1 ||
        strcmp(directory.name[1], names[1]) != 0)
        Fail("the camera directory did not round-trip");
    if (vim_camera_slots_update(&slots, only, only, 1) != 0 || slots.present[1] != 0 ||
        strcmp(slots.key[1], "iphone") != 0)
        Fail("disconnecting the iPhone moved a slot");
    wrote = vim_camera_directory_write(message, sizeof message, &slots, 0, 0);
    if (vim_camera_directory_parse(&directory, message + 6, (unsigned)wrote - 6) < 0 ||
        directory.count != 2 || directory.present[1] != 0)
        Fail("a disconnected camera was published");
    slots.present[1] = 1;
    wrote = vim_camera_directory_write(message, sizeof message, &slots, 1, 1);
    memset(frame, 0x11, sizeof frame);
    vim_camera_frame_header(header, 1);
    vim_camera_ingress_init(&ingress);
    vim_camera_feed_init(&feed);
    vim_camera_stream_init(&streams[0]);
    vim_camera_stream_init(&streams[1]);
    for (index = 0; index < wrote; index++)
        vim_camera_ingress(&ingress, &feed, streams, 2, &directory, &updated, message + index, 1);
    vim_camera_ingress(&ingress, &feed, streams, 2, &directory, &updated, header, sizeof header);
    vim_camera_ingress(&ingress, &feed, streams, 2, &directory, &updated, frame, sizeof frame);
    if (!updated || directory.count != 2 || !streams[1].have_frame || streams[0].have_frame ||
        memcmp(streams[1].frame[streams[1].active], frame, sizeof frame) != 0)
        Fail("a picture after the directory did not reach its own camera");
    {
        static uint8_t joined[2 * (VIM_CAMERA_FRAME_HEADER_BYTES + VIM_CAMERA_FRAME_BYTES)];
        size_t at = 0, step = 3, sizes[] = {3, 4093, 7, 65536, 1};
        unsigned k = 0;
        vim_camera_frame_header(joined, 0);
        memset(joined + VIM_CAMERA_FRAME_HEADER_BYTES, 0x22, VIM_CAMERA_FRAME_BYTES);
        vim_camera_frame_header(joined + VIM_CAMERA_FRAME_HEADER_BYTES + VIM_CAMERA_FRAME_BYTES, 1);
        memset(joined + 2 * VIM_CAMERA_FRAME_HEADER_BYTES + VIM_CAMERA_FRAME_BYTES, 0x33, VIM_CAMERA_FRAME_BYTES);
        while (at < sizeof joined) {
            step = sizes[k++ % 5];
            if (step > sizeof joined - at) step = sizeof joined - at;
            vim_camera_ingress(&ingress, &feed, streams, 2, &directory, &updated, joined + at, step);
            at += step;
        }
        if (!streams[0].have_frame || streams[0].frame[streams[0].active][0] != 0x22 ||
            streams[0].frame[streams[0].active][VIM_CAMERA_FRAME_BYTES - 1] != 0x22 ||
            !streams[1].pending_valid ||
            streams[1].frame[streams[1].active ^ 1][0] != 0x33 ||
            streams[1].frame[streams[1].active ^ 1][VIM_CAMERA_FRAME_BYTES - 1] != 0x33)
            Fail("pictures split across reads were misrouted or misaligned");
    }
    vim_camera_stream_fini(&streams[0]);
    vim_camera_stream_fini(&streams[1]);
    vim_camera_feed_fini(&feed);
}

static void CheckRouting(void) {
    VIMCameraStatus status;
    uint8_t on[] = {0, 1}, off[] = {0, 0};
    NSDictionary *expected = @{@"tiger": @",isight=on", @"leopard": @",isight=on",
        @"snowleopard": @",isight=on", @"lion": @"", @"mountainlion": @"",
        @"mavericks": @"", @"yosemite": @"", @"elcapitan": @"", @"sierra": @"", @"highsierra": @"",
        @"mojave": @"", @"catalina": @"", @"bigsur": @""};
    for (NSString *guest in expected) {
        if (![VIMCameraDeviceSuffix(guest) isEqual:expected[guest]])
            Fail("a guest family has the wrong camera personality");
        if (VIMCameraFrameRate(guest) != ([expected[guest] containsString:@"isight"] ? 10u : 30u))
            Fail("a guest family has the wrong camera frame rate");
    }
    if (vim_camera_function_for_slot(0, 0, 1) != 0 || vim_camera_function_for_slot(2, 2, 1) != 0 ||
        vim_camera_function_for_slot(2, 0, 1) != -1 || vim_camera_function_for_slot(1, 0, 3) != 1 ||
        vim_camera_function_for_slot(0, 0, 0) != -1 || vim_camera_function_for_slot(0, 1, 1) != -1 ||
        vim_camera_function_for_slot(1, 1, 1) != 0)
        Fail("host slots do not map onto the device's functions");
    vim_camera_status_init(&status, 2);
    if (vim_camera_function_should_run("autosense", 1, status.streaming[0]))
        Fail("Autosense started before the guest opened the stream");
    vim_camera_status_bytes(&status, on, sizeof on);
    if (!vim_camera_function_should_run("autosense", 1, status.streaming[0]))
        Fail("Autosense did not follow the opened camera");
    vim_camera_status_bytes(&status, off, sizeof off);
    if (vim_camera_function_should_run("autosense", 1, status.streaming[0]))
        Fail("Autosense stayed on after the guest closed the stream");
    {
        const uint8_t loaded[] = {VIM_CAMERA_STATUS_DRIVER, 1}, released[] = {VIM_CAMERA_STATUS_DRIVER, 0};
        uint8_t streamingBefore = status.streaming[0];
        if (status.driver) Fail("the driver light started on");
        vim_camera_status_bytes(&status, loaded, 1);
        vim_camera_status_bytes(&status, loaded + 1, 1);
        if (!status.driver || status.streaming[0] != streamingBefore)
            Fail("a split driver status was misread");
        vim_camera_status_bytes(&status, released, sizeof released);
        if (status.driver) Fail("the driver light stayed on after release");
    }
    if (!vim_camera_function_should_run("constant", 1, 0) || vim_camera_function_should_run("constant", 0, 0))
        Fail("Constant Output opened a camera the guest did not select");
    if (!vim_camera_function_should_run("testpattern", 1, 0) || vim_camera_function_should_run("off", 1, 1))
        Fail("Test Pattern or Off misbehaved");
    if (VIMCameraDeviceAdvertised(@{@"CameraDevice": @"disabled"}) || !VIMCameraDeviceAdvertised(@{@"CameraDevice": @"default"}))
        Fail("Disconnected does not decide whether the camera is attached");
    if (VIMCameraDeviceAdvertised(@{}))
        Fail("a new virtual Mac was not Disconnected by default");
    if (VIMCameraRoutingEnabled(@{@"CameraDevice": @"disabled"}) ||
        VIMCameraRoutingEnabled(@{@"CameraMode": @"testpattern"}) ||
        VIMCameraFunctions(@{@"CameraMode": @"testpattern"}).count)
        Fail("a disconnected camera was routed");
    if (VIMCameraModeUsesHostCamera(@"testpattern") || VIMCameraModeUsesHostCamera(@"off") ||
        !VIMCameraModeUsesHostCamera(@"constant") || !VIMCameraModeUsesHostCamera(@"autosense"))
        Fail("Test Pattern or Off would ask for camera access");
    if (!VIMCameraRoutingEnabled(@{@"CameraDevice": @"default", @"CameraMode": @"testpattern"}))
        Fail("Test Pattern waited for camera access");
    if (!VIMCameraChimesEnabled(@{}) || VIMCameraChimesEnabled(@{@"CameraChimesEnabled": @NO}))
        Fail("Autosense chimes default or setting misread");
}

static void CheckNoGuestPayloads(void) {
    NSFileManager *files = NSFileManager.defaultManager;
    for (NSString *guest in [files contentsOfDirectoryAtPath:@"prebuilt/PassthroughOpenGL" error:NULL]) {
        NSString *root = [NSString stringWithFormat:@"prebuilt/PassthroughOpenGL/%@/GuestTools", guest];
        if ([files fileExistsAtPath:[root stringByAppendingPathComponent:@"Camera"]] ||
            [files fileExistsAtPath:[root stringByAppendingPathComponent:@"opengl-tiger-camera"]])
            Fail("a guest ships a prebuilt camera payload");
    }
}

int main(void) {
    @autoreleasepool {
        static const uint8_t red[4] = {0, 0, 255, 255};
        NSData *picture;
        const uint8_t *pixels;
        CheckDescriptors();
        CheckPlainStream();
        CheckTallStream();
        CheckHighBandwidth();
        CheckISightStream();
        CheckFeedAndDirectory();
        CheckRouting();
        CheckNoGuestPayloads();
        picture = VIMCameraYUY2FromBGRA(red, 1, 1, 4);
        pixels = picture.bytes;
        if (picture.length != VIM_CAMERA_FRAME_BYTES || pixels[0] != 82 || pixels[1] != 91 ||
            pixels[2] != 82 || pixels[3] != 240)
            Fail("a camera buffer was not scaled to 640x240 YUY2");
        for (NSDictionary *device in VIMHostCameraDevices()) {
            if (![device[@"Name"] isKindOfClass:NSString.class] || ![device[@"UID"] length])
                Fail("camera enumeration did not keep a name and unique id");
        }
        printf("camera routing: all checks passed\n");
    }
    return 0;
}
