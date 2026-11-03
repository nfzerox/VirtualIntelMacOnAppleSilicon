#include <sys/types.h>
#include <sys/tty.h>
#include <sys/termios.h>
#include <kern/thread_call.h>
#include "IOSerialBSDClient.h"

// Only the two emulated PC UARTs use this subclass. The native serial kext
// remains the owner of registration and all I/O after startup.
class VIMSerialStartup : public IOSerialBSDClient {
    OSDeclareDefaultStructors(VIMSerialStartup)
public:
    virtual bool start(IOService *provider);
};

OSDefineMetaClassAndStructors(VIMSerialStartup, IOSerialBSDClient)

class SerialStartLock {
public:
    IOLock *lock;
    SerialStartLock() : lock(IOLockAlloc()) {}
    ~SerialStartLock() { if (lock) IOLockFree(lock); }
};
static SerialStartLock allocation;

bool VIMSerialStartup::start(IOService *provider)
{
    if (!allocation.lock) return false;
    IOLockLock(allocation.lock);
    bool started = IOSerialBSDClient::start(provider);
    IOLockUnlock(allocation.lock);
    return started;
}
