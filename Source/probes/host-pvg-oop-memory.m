// Local architecture probe: ARM parent, Rosetta Metal helper, shared RAM.
// Build this source separately for arm64 and x86_64. No PVG integration yet.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <ParavirtualizedGraphics/ParavirtualizedGraphics.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <spawn.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
extern char **environ;
#include "host-pvg-oop-ram.h"
#include "host-pvg-oop-wire.h"
enum { RAM_SIZE = 65536 };
int main(int argc, char **argv) {
    @autoreleasepool {
        BOOL helper = argc == 4 && !strcmp(argv[1], "--helper");
        int sockets[2] = {-1, -1};
        int fd;
        if (helper) {
            fd = atoi(argv[2]);
        } else {
            if (argc != 2) return 64;
            char path[] = "/tmp/pvg-oop-memory.XXXXXX";
            fd = mkstemp(path);
            if (fd < 0) return 1;
            unlink(path);
            if (ftruncate(fd, RAM_SIZE)) return 2;
            if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets)) return 10;
        }
        uint8_t *ram = mmap(NULL, RAM_SIZE, PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, 0);
        if (ram == MAP_FAILED) return 3;
        // Deliberately non-linear GPA map, like RAM above a PC's PCI hole.
        const OOPRAMRange ramRanges[] = {{0,32768,0},{0x100000000ULL,32768,32768}};
        const OOPRAM guestRAM = {ram, RAM_SIZE, ramRanges, 2};
        if (helper) {
            int channel = atoi(argv[3]);
            uint8_t request;
            if (read(channel, &request, 1) != 1 || request != 1) return 11;
            for (size_t i = 0; i < RAM_SIZE; i++) if (ram[i] != 0x19) return 4;
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            id<MTLBuffer> buffer = [device newBufferWithBytesNoCopy:ram
                length:RAM_SIZE options:MTLResourceStorageModeShared deallocator:nil];
            if (!buffer) return 5;
            id<MTLCommandQueue> queue = [device newCommandQueue];
            id<MTLCommandBuffer> command = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
            [blit fillBuffer:buffer range:NSMakeRange(16384, 16384) value:0x51];
            [blit endEncoding]; [command commit]; [command waitUntilCompleted];
            if (command.status != MTLCommandBufferStatusCompleted) return 6;
            const char *donor = getenv("PVG_OOP_DONOR");
            id<PGDevice> pg = nil;
            size_t mmioLength = 0;
            if (donor) {
                void *image = dlopen(donor, RTLD_NOW | RTLD_LOCAL);
                if (!image) { fprintf(stderr, "%s\n", dlerror()); return 18; }
                id<PGDevice> (*create)(PGDeviceDescriptor *) =
                    dlsym(image, "PGCreateDeviceWithDescriptor");
                if (!create) return 19;
                PGDeviceDescriptor *desc = [NSClassFromString(@"PGDeviceDescriptor") new];
                desc.device = device;
                desc.createTask = ^PGTask_t *(uint64_t size, void **base) {
                    return oop_task_create(size, base);
                };
                desc.destroyTask = ^(PGTask_t *task) { oop_task_destroy(task); };
                desc.mapMemory = ^bool(PGTask_t *task, uint32_t count,
                    uint64_t offset, bool readOnly, PGPhysicalMemoryRange_t *ranges) {
                    return oop_task_map(task, offset, ranges, count, readOnly, &guestRAM);
                };
                desc.unmapMemory = ^bool(PGTask_t *task, uint64_t offset, uint64_t size) {
                    return oop_task_unmap(task, offset, size);
                };
                desc.readMemory = ^bool(uint64_t address, uint64_t size, void *out) {
                    void *source = oop_ram_resolve(&guestRAM, address, size);
                    if (!source) return false;
                    memcpy(out, source, size); return true;
                };
                desc.raiseInterrupt = ^(uint32_t vector) {};
                void *taskBase = NULL;
                PGTask_t *task = desc.createTask(8192, &taskBase);
                PGPhysicalMemoryRange_t ranges[] = {{0x100000000ULL,4096},{0,4096}};
                if (oop_ram_resolve(&guestRAM, 32768, 4096) ||
                    oop_ram_resolve(&guestRAM, UINT64_MAX, 4096) ||
                    oop_ram_resolve(&guestRAM, 28672, 8192)) return 29;
                if (!task || !desc.mapMemory(task, 2, 0, false, ranges)) return 22;
                if (memcmp(taskBase, ram + 32768, 4096) ||
                    memcmp((uint8_t *)taskBase + 4096, ram, 4096)) return 23;
                ((uint8_t *)taskBase)[0] = 0x73;
                if (ram[32768] != 0x73) return 24;
                ram[32768] = 0x19;
                if (!desc.unmapMemory(task, 0, 8192) || ((uint8_t *)taskBase)[0]) return 25;
                desc.destroyTask(task);
                puts("helper: reordered 4K CPU aliases and unmap verified");
                pg = create(desc);
                mmioLength = desc.mmioLength;
                if (!pg) return 20;
                PGDisplayDescriptor *displayDesc = [NSClassFromString(@"PGDisplayDescriptor") new];
                displayDesc.name = @"OOP boundary probe";
                displayDesc.queue = dispatch_queue_create("pvg.oop", DISPATCH_QUEUE_SERIAL);
                id<PGDisplay> display = [pg newDisplayWithDescriptor:displayDesc port:0 serialNum:1];
                if (!display) return 21;
                printf("helper: Intel PVG device/display created; MMIO bytes=%zu\n", desc.mmioLength);
                // Retain these until helper exit: no teardown ordering is
                // being qualified by this short-lived boundary probe.
            }
            uint8_t reply = 2;
            if (write(channel, &reply, 1) != 1) return 12;
            // Remain alive until the parent checks visibility: process exit
            // must not be what makes the GPU result observable.
            uint64_t lastSequence = 0;
            for (;;) {
                OOPMessage message;
                if (!oop_message_io(channel, &message, false) ||
                    message.sequence <= lastSequence) return 13;
                lastSequence = message.sequence;
                message.status = 0;
                if (message.operation == OOP_STOP) break;
                if (!pg || (message.offset & 3) || message.offset >= mmioLength) {
                    message.status = EINVAL;
                } else if (message.operation == OOP_READ) {
                    message.value = [pg mmioReadAtOffset:message.offset];
                } else if (message.operation == OOP_WRITE) {
                    [pg mmioWriteAtOffset:message.offset value:(uint32_t)message.value];
                } else {
                    message.status = ENOTSUP;
                }
                if (!oop_message_io(channel, &message, true)) return 26;
            }
            close(channel);
            [queue release]; [buffer release]; [device release];
            puts("helper: shared RAM imported, GPU write completed");
        } else {
            memset(ram, 0x19, RAM_SIZE);
            char fdString[32]; snprintf(fdString, sizeof(fdString), "%d", fd);
            char channelString[32];
            snprintf(channelString, sizeof(channelString), "%d", sockets[1]);
            char *args[] = {argv[1], "--helper", fdString, channelString, NULL};
            pid_t child; int status;
            posix_spawnattr_t attr;
            posix_spawn_file_actions_t actions;
            if (posix_spawnattr_init(&attr) || posix_spawn_file_actions_init(&actions)) return 14;
            if (posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT) ||
                posix_spawn_file_actions_addinherit_np(&actions, fd) ||
                posix_spawn_file_actions_addinherit_np(&actions, sockets[1]) ||
                posix_spawn_file_actions_addinherit_np(&actions, STDOUT_FILENO) ||
                posix_spawn_file_actions_addinherit_np(&actions, STDERR_FILENO)) return 15;
            int error = posix_spawn(&child, argv[1], &actions, &attr, args, environ);
            posix_spawn_file_actions_destroy(&actions);
            posix_spawnattr_destroy(&attr);
            if (error) { fprintf(stderr, "spawn: %s\n", strerror(error)); return 7; }
            close(sockets[1]);
            uint8_t message = 1;
            if (write(sockets[0], &message, 1) != 1 ||
                read(sockets[0], &message, 1) != 1 || message != 2) return 16;
            for (size_t i = 0; i < RAM_SIZE; i++)
                if (ram[i] != (i >= 16384 && i < 32768 ? 0x51 : 0x19)) return 9;
            if (getenv("PVG_OOP_DONOR")) {
                OOPMessage mmio = {.version=OOP_VERSION, .operation=OOP_READ, .sequence=1};
                if (!oop_message_io(sockets[0], &mmio, true) ||
                    !oop_message_io(sockets[0], &mmio, false) ||
                    mmio.sequence != 1 || mmio.status) return 27;
                printf("parent: PVG MMIO[0]=0x%llx received across IPC\n", mmio.value);
                mmio = (OOPMessage){.version=OOP_VERSION, .operation=OOP_READ,
                    .sequence=2, .offset=UINT64_MAX};
                if (!oop_message_io(sockets[0], &mmio, true) ||
                    !oop_message_io(sockets[0], &mmio, false) ||
                    mmio.sequence != 2 || mmio.status != EINVAL) return 28;
            }
            OOPMessage stop = {.version=OOP_VERSION, .operation=OOP_STOP, .sequence=3};
            if (!oop_message_io(sockets[0], &stop, true)) return 17;
            close(sockets[0]);
            if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
                WEXITSTATUS(status)) { fprintf(stderr, "helper failed\n"); return 8; }
            puts("parent: GPU result and untouched surrounding RAM verified: PASS");
        }
        munmap(ram, RAM_SIZE); close(fd);
        return 0;
    }
}
