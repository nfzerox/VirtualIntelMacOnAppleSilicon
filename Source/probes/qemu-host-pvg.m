// Native host framework availability probe; no guest or command decoding.
// Memory callbacks deliberately refuse work: this tests creation, not execution.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <ParavirtualizedGraphics/ParavirtualizedGraphics.h>
#include <sys/mman.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>

// macOS 27 runtime metadata: addRange: takes {PGGuestPhysicalRange_s=QQ^v}.
// PGMemoryMap initWithDescriptor: confirms start, byte length, host pointer.
typedef struct { uint64_t address, length; void *host; } ProbeGuestRange;
@interface NSObject (PVGMemoryMapProbe)
- (void)addRange:(ProbeGuestRange)range;
- (void)setMemoryMapDescriptor:(id)descriptor;
@end

int main(int argc, char **argv) {
    @autoreleasepool {
        printf("Host process page size: %d\n", getpagesize());
        const char *path = getenv("PVG_PROBE_FRAMEWORK");
        if (!path) path = "/System/Library/Frameworks/ParavirtualizedGraphics.framework/ParavirtualizedGraphics";
        void *framework = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (!framework) { fprintf(stderr, "%s\n", dlerror()); return 7; }
        NSURL *(*copyROM)(void) = dlsym(framework, "PGCopyOptionROMURL");
        id<PGDevice> (*createDevice)(PGDeviceDescriptor *) = dlsym(framework, "PGCreateDeviceWithDescriptor");
        if (!copyROM || !createDevice) return 8;
        id<MTLDevice> metal = MTLCreateSystemDefaultDevice();
        if (!metal) { fprintf(stderr, "No default Metal device\n"); return 1; }
        printf("Metal device: %s\n", metal.name.UTF8String);
        NSURL *rom = copyROM();
        if (!rom || ![[NSFileManager defaultManager] isReadableFileAtPath:rom.path]) {
            fprintf(stderr, "PVG option ROM unavailable\n"); return 2;
        }
        printf("Option ROM: %s\n", rom.path.UTF8String);
        PGDeviceDescriptor *desc = [NSClassFromString(@"PGDeviceDescriptor") new];
        desc.device = metal;
        desc.createTask = ^PGTask_t *(uint64_t size, void **base) {
            *base = NULL; return NULL;
        };
        desc.destroyTask = ^(PGTask_t *task) {};
        desc.mapMemory = ^bool(PGTask_t *task, uint32_t count, uint64_t offset,
                              bool readOnly, PGPhysicalMemoryRange_t *ranges) {
            return false;
        };
        desc.unmapMemory = ^bool(PGTask_t *task, uint64_t offset, uint64_t size) {
            return false;
        };
        desc.readMemory = ^bool(uint64_t address, uint64_t size, void *dst) {
            return false;
        };
        desc.raiseInterrupt = ^(uint32_t vector) {};
        if (argc == 2 && !strcmp(argv[1], "--memory-map")) {
            Class cls = NSClassFromString(@"PGMemoryMapDescriptor");
            if (!cls || ![desc respondsToSelector:@selector(setMemoryMapDescriptor:)])
                return 5;
            size_t length = 16 * 1024 * 1024;
            void *ram = mmap(NULL, length, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANON, -1, 0);
            if (ram == MAP_FAILED) return 6;
            id map = [cls new];
            [map addRange:(ProbeGuestRange){0, length, ram}];
            [desc setMemoryMapDescriptor:map];
            // Keep RAM alive until process exit, including device teardown.
            printf("Using memory-map descriptor with 16 MiB dummy RAM\n");
        }
        id<PGDevice> device = createDevice(desc);
        if (!device) { fprintf(stderr, "PVG device creation failed\n"); return 3; }
        PGDisplayDescriptor *displayDesc = [NSClassFromString(@"PGDisplayDescriptor") new];
        displayDesc.name = @"PVG probe";
        displayDesc.queue = dispatch_queue_create("pvg.probe", DISPATCH_QUEUE_SERIAL);
        id<PGDisplay> display = [device newDisplayWithDescriptor:displayDesc
                                                          port:0 serialNum:1];
        if (!display) { fprintf(stderr, "PVG display creation failed\n"); return 4; }
        printf("PVG device/display created; MMIO length: %zu\n", desc.mmioLength);
        printf("Creation only; no guest GPU workload has been tested.\n");
    }
    return 0;
}
