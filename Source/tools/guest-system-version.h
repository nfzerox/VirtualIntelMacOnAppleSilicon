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
    // Release major versions after Big Sur: 12-15, then the year-based 26.
    static const struct {const char *major;NSString *guest;} releases[]={
        {"11",@"bigsur"},{"12",@"monterey"},{"13",@"ventura"},{"14",@"sonoma"},{"15",@"sequoia"},{"26",@"tahoe"}};
    for(size_t i=0;i<sizeof(releases)/sizeof(*releases);i++){
        size_t n=strlen(releases[i].major);
        if(!strncmp(text,releases[i].major,n)&&(text[n]==0||text[n]=='.'))return releases[i].guest;
    }
    if(strncmp(text,"10.",3))return nil;
    char *end=NULL;long minor=strtol(text+3,&end,10);
    if(end==text+3||(*end&&*end!='.')||minor<4||minor>16)return nil;
    static NSString *const guests[]={@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",
        @"mavericks",@"yosemite",@"elcapitan",@"sierra",@"highsierra",@"mojave",@"catalina",@"bigsur"};
    return guests[minor-4];
}
// Mojave and later: Recovery/startosinstall installation, native account
// setup and Metal display, as opposed to the 10.4-10.13 OpenGL backport.
static inline NSArray *VIMModernGuests(void) {
    return [NSArray arrayWithObjects:@"mojave",@"catalina",@"bigsur",@"monterey",@"ventura",@"sonoma",@"sequoia",@"tahoe",nil];
}
// Release name used in Apple's installer app name and bundle identifier.
static inline NSString *VIMAssistantReleaseName(NSString *guest) {
    NSDictionary *names=[NSDictionary dictionaryWithObjectsAndKeys:@"Big Sur",@"bigsur",@"Monterey",@"monterey",
        @"Ventura",@"ventura",@"Sonoma",@"sonoma",@"Sequoia",@"sequoia",@"Tahoe",@"tahoe",nil];
    return [names objectForKey:guest?guest:@""];
}
// Big Sur and later ship as one signed InstallAssistant package.
static inline NSArray *VIMAssistantGuests(void) {
    return [NSArray arrayWithObjects:@"bigsur",@"monterey",@"ventura",@"sonoma",@"sequoia",@"tahoe",nil];
}
#endif
