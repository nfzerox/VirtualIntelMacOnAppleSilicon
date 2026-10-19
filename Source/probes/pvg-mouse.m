// Guest-side input only; verify results using host-side capture.
#import <ApplicationServices/ApplicationServices.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc != 3 && argc != 5) return 64;
    CGPoint start = CGPointMake(atof(argv[1]), atof(argv[2]));
    CGEventRef event = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, start, kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, event); CFRelease(event);
    event = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDown, start, kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, event); CFRelease(event);
    CGPoint end = start;
    if (argc == 5) {
        end = CGPointMake(atof(argv[3]), atof(argv[4]));
        for (int i=1; i<=30; i++) {
            CGPoint point = CGPointMake(start.x+(end.x-start.x)*i/30., start.y+(end.y-start.y)*i/30.);
            event = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseDragged, point, kCGMouseButtonLeft);
            CGEventPost(kCGHIDEventTap, event); CFRelease(event);
            usleep(16667);
        }
    } else usleep(50000);
    event = CGEventCreateMouseEvent(NULL, kCGEventLeftMouseUp, end, kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, event); CFRelease(event);
    return 0;
}
