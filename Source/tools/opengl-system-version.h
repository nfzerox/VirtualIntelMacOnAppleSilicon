#ifndef VIM_OPENGL_SYSTEM_VERSION_H
#define VIM_OPENGL_SYSTEM_VERSION_H
#import <Foundation/Foundation.h>
#include <stdlib.h>
#include <string.h>
/* One period-family mapping for setup, desktop evidence and guest services.
 * Do not gate security updates on an exact build or dotted point release. */
static NSString *VIMOpenGLGuestForSystemVersion(NSString *version)
{
    const char *text=[version UTF8String];if(!text||strncmp(text,"10.",3))return nil;
    char *end=NULL;long minor=strtol(text+3,&end,10);
    if(end==text+3||(*end&&*end!='.')||minor<4||minor>13)return nil;
    static NSString *const guests[]={@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",
        @"mavericks",@"yosemite",@"elcapitan",@"sierra",@"highsierra"};
    return guests[minor-4];
}
#endif
