#ifndef VIM_GUEST_SYSTEM_VERSION_H
#define VIM_GUEST_SYSTEM_VERSION_H
#import <Foundation/Foundation.h>
#include <stdlib.h>
#include <string.h>

// Shared by provisioning tools, not a graphics capability test. Point releases
// remain in their OS family; an unknown newer system never inherits a policy.
static inline NSString *VIMGuestForSystemVersion(NSString *version) {
    if(![version isKindOfClass:[NSString class]])return nil;
    const char *text=[version UTF8String];if(!text||!text[0])return nil;
    BOOL dot=YES;
    for(const char *p=text;*p;p++){
        if(*p=='.'){if(dot)return nil;dot=YES;}
        else {if(*p<'0'||*p>'9')return nil;dot=NO;}
    }
    if(dot)return nil;
    if(!strcmp(text,"11")||!strncmp(text,"11.",3))return @"bigsur";
    if(strncmp(text,"10.",3))return nil;
    char *end=NULL;long minor=strtol(text+3,&end,10);
    if(end==text+3||(*end&&*end!='.')||minor<4||minor>16)return nil;
    static NSString *const guests[]={@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",
        @"mavericks",@"yosemite",@"elcapitan",@"sierra",@"highsierra",@"mojave",@"catalina",@"bigsur"};
    return guests[minor-4];
}
#endif
