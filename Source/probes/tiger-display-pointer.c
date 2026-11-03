/* Read-only native Tiger display/input coordinate survey. No event posting. */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
int main(void)
{
    CGDirectDisplayID displays[16];CGDisplayCount count=0,index;
    CGDisplayErr error=CGGetActiveDisplayList(16,displays,&count);
    if(error)return 1;
    for(index=0;index<count;index++){
        CGRect bounds=CGDisplayBounds(displays[index]);
        printf("display=%u main=%d origin=%g,%g bounds=%g,%g pixels=%lu,%lu\n",
            (unsigned)displays[index],displays[index]==CGMainDisplayID(),
            (double)bounds.origin.x,(double)bounds.origin.y,(double)bounds.size.width,(double)bounds.size.height,
            (unsigned long)CGDisplayPixelsWide(displays[index]),(unsigned long)CGDisplayPixelsHigh(displays[index]));
    }
    CGEventRef event=CGEventCreate(NULL);if(!event)return 1;
    CGPoint cursor=CGEventGetLocation(event);CFRelease(event);
    printf("cursor=%g,%g\n",(double)cursor.x,(double)cursor.y);return 0;
}
