// Bounded, read-only GUI-session display notification probe. No capture APIs.
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <IOKit/IOKitLib.h>
static void Interest(void *context,io_service_t service,natural_t type,void *argument) {
    (void)context;(void)argument;
    CFTypeRef power=IORegistryEntryCreateCFProperty(service,CFSTR("IOPowerManagement"),NULL,0);
    printf("interest=0x%x %s\n",type,[[(__bridge id)power description] UTF8String]);fflush(stdout);
    if(power)CFRelease(power);
}
static void Emit(NSString *event) {
    printf("%s asleep=%d\n",event.UTF8String,CGDisplayIsAsleep(CGMainDisplayID()));fflush(stdout);
}
int main(void) {@autoreleasepool {
    Emit(@"initial");
    IONotificationPortRef port=IONotificationPortCreate(MACH_PORT_NULL);
    io_service_t service=IOServiceGetMatchingService(MACH_PORT_NULL,IOServiceMatching("IODisplayWrangler"));
    io_object_t notifier=0;
    if(port&&service){printf("interest-status=%x\n",IOServiceAddInterestNotification(port,service,kIOGeneralInterest,Interest,NULL,&notifier));
        CFRunLoopAddSource(CFRunLoopGetCurrent(),IONotificationPortGetRunLoopSource(port),kCFRunLoopCommonModes);}
    NSNotificationCenter *center=NSWorkspace.sharedWorkspace.notificationCenter;
    for(NSString *name in @[NSWorkspaceScreensDidSleepNotification,NSWorkspaceScreensDidWakeNotification])
        [center addObserverForName:name object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note){Emit(note.name);}];
    [NSRunLoop.mainRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:25]];
    if(notifier)IOObjectRelease(notifier);if(service)IOObjectRelease(service);if(port)IONotificationPortDestroy(port);
    return 0;
}}
