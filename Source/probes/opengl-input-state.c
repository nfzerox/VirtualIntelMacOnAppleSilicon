/* Read-only HID/session delivery diagnostic; no event injection or polling. */
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <IOKit/graphics/IOFramebufferShared.h>
#include <stdio.h>
int main(void) {
    CGEventRef event=CGEventCreate(NULL);
    CGPoint point=event?CGEventGetLocation(event):CGPointZero;
    if(event)CFRelease(event);
    printf("cursor=%.0f,%.0f idle=%.3f\n",point.x,point.y,
        CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateHIDSystemState,kCGAnyInputEventType));
    const CGEventType types[]={kCGEventMouseMoved,kCGEventLeftMouseDown,kCGEventLeftMouseUp,
        kCGEventKeyDown,kCGEventKeyUp,kCGEventFlagsChanged};
    const char *names[]={"move","down","up","keyDown","keyUp","flags"};
    for(unsigned i=0;i<sizeof(types)/sizeof(*types);++i)
        printf("%s hid=%u session=%u\n",names[i],
            CGEventSourceCounterForEventType(kCGEventSourceStateHIDSystemState,types[i]),
            CGEventSourceCounterForEventType(kCGEventSourceStateCombinedSessionState,types[i]));
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2"));
    io_connect_t connection=0;
    IOReturn status=service?IOServiceOpen(service,mach_task_self(),kIOFBSharedConnectType,&connection):kIOReturnNotFound;
    printf("cursor shared open=%x connection=%x\n",status,connection);
    mach_vm_address_t address=0;mach_vm_size_t size=0;
    if(!status)status=IOConnectMapMemory64(connection,kIOFBCursorMemory,mach_task_self(),&address,&size,kIOMapAnywhere|kIOMapReadOnly);
    printf("cursor shared map=%x bytes=%llu\n",status,(unsigned long long)size);
    if(!status && size>=sizeof(StdFBShmem_t)) {
        const volatile StdFBShmem_t *cursor=(const volatile StdFBShmem_t *)(uintptr_t)address;
        printf("cursor shared version=%x frame=%d show=%d capable=%u active=%u location=%d,%d size=%d,%d\n",
            cursor->version,cursor->frame,cursor->cursorShow,cursor->hardwareCursorCapable,
            cursor->hardwareCursorActive,cursor->cursorLoc.x,cursor->cursorLoc.y,
            cursor->cursorSize[0].width,cursor->cursorSize[0].height);
    }
    if(address)IOConnectUnmapMemory64(connection,kIOFBCursorMemory,mach_task_self(),address);
    if(connection)IOServiceClose(connection);
    if(service)IOObjectRelease(service);
    return status?1:0;
}
