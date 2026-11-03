/* Finite read-only public IOFramebuffer panel-clock comparison. These are
 * the target SDK's StdFBShmem fields, not a guessed WindowServer pointer chain.
 * A live clock is not proof of delivered/presented animation frames. */
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <IOKit/graphics/IOFramebufferShared.h>
#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#if defined(__LP64__) && MAC_OS_X_VERSION_MIN_REQUIRED < 1060
#define IOConnectMapMemory64 IOConnectMapMemory
#define IOConnectUnmapMemory64 IOConnectUnmapMemory
#endif
#if defined(__i386__)
/* Genuine Tiger exposes the native-width mapping API, not MapMemory64. */
#define IOConnectMapMemory64 IOConnectMapMemory
#define IOConnectUnmapMemory64 IOConnectUnmapMemory
typedef vm_address_t VIMClockAddress;
typedef vm_size_t VIMClockSize;
#else
typedef mach_vm_address_t VIMClockAddress;
typedef mach_vm_size_t VIMClockSize;
#endif
static uint64_t read_clock_word(const volatile void *source)
{
    /* Native i386 publishes a split word. Avoid aliasing AbsoluteTime's
     * period struct or assuming this diagnostic read is an atomic snapshot. */
    uint64_t word;unsigned char *out=(unsigned char *)&word;
    const volatile unsigned char *in=(const volatile unsigned char *)source;
    for(unsigned i=0;i<sizeof(word);++i)out[i]=in[i];
    return word;
}
int main(void)
{
    io_service_t service=CGDisplayIOServicePort(CGMainDisplayID());
    io_connect_t connection=0;VIMClockAddress address=0;VIMClockSize size=0;
    IOReturn status=service?IOServiceOpen(service,mach_task_self(),kIOFBSharedConnectType,&connection):kIOReturnNotFound;
    if(!status)status=IOConnectMapMemory64(connection,kIOFBCursorMemory,mach_task_self(),&address,&size,kIOMapAnywhere|kIOMapReadOnly);
    if(status||size<sizeof(StdFBShmem_t)){
        fprintf(stderr,"public shared clock unavailable status=%x bytes=%llu\n",status,(unsigned long long)size);
        if(address)IOConnectUnmapMemory64(connection,kIOFBCursorMemory,mach_task_self(),address);
        if(connection)IOServiceClose(connection);return 1;
    }
    const volatile StdFBShmem_t *clock=(const volatile StdFBShmem_t *)(uintptr_t)address;
    mach_timebase_info_data_t timebase;mach_timebase_info(&timebase);
    CGRect display=CGDisplayBounds(CGMainDisplayID());
    printf("display=%.0f,%.0f %.0fx%.0f version=%x timebase=%u/%u\n",
        display.origin.x,display.origin.y,display.size.width,display.size.height,
        clock->version,timebase.numer,timebase.denom);
    for(unsigned i=0;i<11;++i){
        uint64_t now=mach_absolute_time(),time=read_clock_word(&clock->vblTime);
        uint64_t delta=read_clock_word(&clock->vblDelta),count=read_clock_word(&clock->vblCount);
        int stable=time==read_clock_word(&clock->vblTime);
        printf("now=%llu vbl=%llu delta=%llu count=%llu stable_time=%d\n",
            (unsigned long long)now,(unsigned long long)time,(unsigned long long)delta,(unsigned long long)count,stable);
        if(i<10)usleep(100000);
    }
    IOConnectUnmapMemory64(connection,kIOFBCursorMemory,mach_task_self(),address);
    IOServiceClose(connection);return 0;
}
