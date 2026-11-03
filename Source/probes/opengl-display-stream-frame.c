/* Compare an actual live CGDisplayStream frame with a CGWindow image and
 * the host VM window. This diagnostic copies one frame, never in production. */
#include <ApplicationServices/ApplicationServices.h>
#include <IOSurface/IOSurface.h>
#include <ImageIO/ImageIO.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int SaveFrame(IOSurfaceRef surface, size_t width, size_t height, const char *path)
{
    if (IOSurfaceLock(surface, kIOSurfaceLockReadOnly, NULL)) return 1;
    const void *base = IOSurfaceGetBaseAddress(surface);
    size_t stride = IOSurfaceGetBytesPerRow(surface);
    CFDataRef data = base && stride >= width * 4 &&
        stride <= IOSurfaceGetAllocSize(surface) / height
        ? CFDataCreate(NULL, base, stride * height) : NULL;
    IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, NULL);
    if (!data) return 1;
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CGColorSpaceRef color = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef image = provider && color ? CGImageCreate(width, height, 8, 32, stride, color,
        kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst, provider, NULL,
        false, kCGRenderingIntentDefault) : NULL;
    CFMutableDataRef png = CFDataCreateMutable(NULL, 0);
    CGImageDestinationRef output = image && png
        ? CGImageDestinationCreateWithData(png, CFSTR("public.png"), 1, NULL) : NULL;
    int good = 0;
    if (output) {
        CGImageDestinationAddImage(output, image, NULL);
        if (CGImageDestinationFinalize(output)) {
            int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
            if (fd >= 0) {
                const UInt8 *p = CFDataGetBytePtr(png);
                size_t left = CFDataGetLength(png);
                while (left) {
                    ssize_t n = write(fd, p, left);
                    if (n < 0 && errno == EINTR) continue;
                    if (n <= 0) break;
                    p += n; left -= (size_t)n;
                }
                good = !close(fd) && !left;
                if (!good) unlink(path);
            }
        }
    }
    if (output) CFRelease(output);
    if (png) CFRelease(png);
    if (image) CGImageRelease(image);
    if (color) CGColorSpaceRelease(color);
    if (provider) CGDataProviderRelease(provider);
    CFRelease(data);
    return good ? 0 : 1;
}

int main(int argc, const char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s NEW.png\n", argv[0]);
        return 2;
    }
    CGDirectDisplayID display = CGMainDisplayID();
    size_t width = CGDisplayPixelsWide(display), height = CGDisplayPixelsHigh(display);
    if (!width || !height || width > 8192 || height > 8192) return 2;
    const char *path = argv[1];
    dispatch_queue_t queue = dispatch_queue_create("org.vim.display-stream-frame", NULL);
    const void *key = kCGDisplayStreamShowCursor, *value = kCFBooleanFalse;
    CFDictionaryRef options = CFDictionaryCreate(NULL, &key, &value, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CGDisplayStreamRef stream = CGDisplayStreamCreateWithDispatchQueue(display,
        width, height, 'BGRA', options, queue, ^(CGDisplayStreamFrameStatus status,
            uint64_t time, IOSurfaceRef surface, CGDisplayStreamUpdateRef update) {
        (void)update;
        if (status != kCGDisplayStreamFrameStatusFrameComplete || !surface) return;
        int result = SaveFrame(surface, width, height, path);
        fprintf(stderr, "display stream %zux%zu tick=%llu write=%s\n",
            width, height, (unsigned long long)time, result ? "failed" : "ok");
        exit(result);
    });
    if (options) CFRelease(options);
    if (!stream || CGDisplayStreamStart(stream)) return 1;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 12 * NSEC_PER_SEC),
        dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        fputs("No live display frame within diagnostic window\n", stderr);
        exit(1);
    });
    dispatch_main();
}
