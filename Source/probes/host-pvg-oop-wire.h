// Local experimental control ABI; both peers are little-endian, 64-bit hosts.
// No pointers, Objective-C objects, or process-local descriptors cross this ABI.
#include <stdint.h>
#include <errno.h>
enum { OOP_VERSION = 1, OOP_READ = 1, OOP_WRITE = 2, OOP_STOP = 3 };
typedef struct {
    uint32_t version, operation;
    uint64_t sequence, offset, value;
    int32_t status;
    uint32_t reserved;
} OOPMessage;
_Static_assert(sizeof(OOPMessage) == 40, "wire layout");
static bool oop_message_io(int fd, OOPMessage *message, bool sending) {
    uint8_t *p = (uint8_t *)message;
    size_t remaining = sizeof(*message);
    while (remaining) {
        ssize_t n = sending ? write(fd, p, remaining) : read(fd, p, remaining);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n; remaining -= n;
    }
    return message->version == OOP_VERSION && !message->reserved;
}
