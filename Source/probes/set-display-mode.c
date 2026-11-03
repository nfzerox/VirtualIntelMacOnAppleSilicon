#include <ApplicationServices/ApplicationServices.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long parse_dimension(const char *value) {
    char *end = NULL;
    errno = 0;
    long result = strtol(value, &end, 10);
    if (errno || !end || *end || result <= 0) {
        fprintf(stderr, "invalid display dimension: %s\n", value);
        exit(64);
    }
    return result;
}

int main(int argc, char **argv) {
    const CGDirectDisplayID display = CGMainDisplayID();
    const void *keys[]={kCGDisplayShowDuplicateLowResolutionModes};
    const void *values[]={kCFBooleanTrue};
    CFDictionaryRef options=CFDictionaryCreate(NULL,keys,values,1,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    CFArrayRef modes = CGDisplayCopyAllDisplayModes(display, options);
    CFRelease(options);
    if (!modes) {
        fprintf(stderr, "could not enumerate display modes\n");
        return 1;
    }

    CGDisplayModeRef current = CGDisplayCopyDisplayMode(display);
    if(current)printf("current logical=%zux%zu pixels=%zux%zu\n",
        CGDisplayModeGetWidth(current),CGDisplayModeGetHeight(current),
        CGDisplayModeGetPixelWidth(current),CGDisplayModeGetPixelHeight(current));
    const int32_t current_id = current
        ? CGDisplayModeGetIODisplayModeID(current)
        : -1;
    const int hidpi = (argc == 2 || argc == 4) && !strcmp(argv[1], "--hidpi");
    const long requested_width = hidpi && argc == 4 ? parse_dimension(argv[2]) :
        (hidpi && current ? CGDisplayModeGetPixelWidth(current)/2 :
        (argc == 3 ? parse_dimension(argv[1]) : 0));
    const long requested_height = hidpi && argc == 4 ? parse_dimension(argv[3]) :
        (hidpi && current ? CGDisplayModeGetPixelHeight(current)/2 :
        (argc == 3 ? parse_dimension(argv[2]) : 0));
    CGDisplayModeRef selected = NULL;
    const CFIndex count = CFArrayGetCount(modes);
    for (CFIndex index = 0; index < count; ++index) {
        CGDisplayModeRef mode = (CGDisplayModeRef)CFArrayGetValueAtIndex(
            modes, index);
        const size_t width = CGDisplayModeGetWidth(mode);
        const size_t height = CGDisplayModeGetHeight(mode);
        const double refresh = CGDisplayModeGetRefreshRate(mode);
        const uint32_t flags = CGDisplayModeGetIOFlags(mode);
        // Synthesized 1x/2x modes share an IODisplayModeID on Mavericks.
        // Compare both geometries, not that ID alone, to mark the current one.
        int is_current=current && CGDisplayModeGetIODisplayModeID(mode)==current_id &&
            width==CGDisplayModeGetWidth(current) && height==CGDisplayModeGetHeight(current) &&
            CGDisplayModeGetPixelWidth(mode)==CGDisplayModeGetPixelWidth(current) &&
            CGDisplayModeGetPixelHeight(mode)==CGDisplayModeGetPixelHeight(current);
        printf("%c %4zu x %-4zu pixels=%zux%zu  %.3f Hz  flags=0x%08x\n",
               is_current ? '*' : ' ',
               width, height, CGDisplayModeGetPixelWidth(mode),CGDisplayModeGetPixelHeight(mode),refresh, flags);
        if (!selected && width == (size_t)requested_width &&
            height == (size_t)requested_height &&
            CGDisplayModeGetPixelWidth(mode)==(hidpi ? 2 : 1)*width &&
            CGDisplayModeGetPixelHeight(mode)==(hidpi ? 2 : 1)*height) {
            selected = mode;
        }
    }

    int result = 0;
    if (argc == 3 || hidpi) {
        if (!selected) {
            fprintf(stderr, "requested mode %ldx%ld is unavailable\n",
                    requested_width, requested_height);
            result = 2;
        } else {
            CGDisplayConfigRef config=NULL;
            CGError error= CGBeginDisplayConfiguration(&config);
            if(error==kCGErrorSuccess) {
                error=CGConfigureDisplayWithDisplayMode(config,display,selected,NULL);
                if(error==kCGErrorSuccess)
                    error=CGCompleteDisplayConfiguration(config,kCGConfigureForSession);
                else CGCancelDisplayConfiguration(config);
            }
            printf("set %ldx%ld: %d\n", requested_width, requested_height,
                   (int)error);
            result = error == kCGErrorSuccess ? 0 : 3;
            CGDisplayModeRef applied=CGDisplayCopyDisplayMode(display);
            if(applied) {
                size_t lw=CGDisplayModeGetWidth(applied),lh=CGDisplayModeGetHeight(applied);
                size_t pw=CGDisplayModeGetPixelWidth(applied),ph=CGDisplayModeGetPixelHeight(applied);
                printf("applied logical=%zux%zu pixels=%zux%zu\n",lw,lh,pw,ph);
                if(lw!=(size_t)requested_width || lh!=(size_t)requested_height ||
                    pw!=(hidpi ? 2 : 1)*lw || ph!=(hidpi ? 2 : 1)*lh) result=4;
                CGDisplayModeRelease(applied);
            } else result=4;
        }
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [width height | --hidpi [logical_width logical_height]]\n", argv[0]);
        result = 64;
    }

    if (current) {
        CGDisplayModeRelease(current);
    }
    CFRelease(modes);
    return result;
}
