#import <Foundation/Foundation.h>
#import "../VirtualIntelMac/VIMOSNaming.h"
#import "../tools/guest-system-version.h"
int main(void){@autoreleasepool{
    for(NSString *guest in @[@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",@"mavericks",@"yosemite",@"elcapitan",@"sierra",@"highsierra"])
        if(!VIMGuestUsesOpenGLDesktop(guest)){fprintf(stderr,"FAIL: %s desktop family\n",guest.UTF8String);return 1;}
    for(NSString *guest in @[@"mojave",@"catalina",@"bigsur",@"unsupported",@""])
        if(VIMGuestUsesOpenGLDesktop(guest)){fprintf(stderr,"FAIL: %s must not inherit the OpenGL desktop path\n",guest.UTF8String);return 1;}
    if(VIMGuestUsesOpenGLDesktop(nil))return 1;
    NSArray *families=@[@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",@"mavericks",@"yosemite",@"elcapitan",@"sierra",@"highsierra",@"mojave",@"catalina"];
    for(NSUInteger minor=4;minor<=15;minor++)
        if(![VIMGuestForSystemVersion([NSString stringWithFormat:@"10.%lu.99",(unsigned long)minor]) isEqual:families[minor-4]])return 1;
    if(![VIMGuestForSystemVersion(@"11.7.10") isEqual:@"bigsur"])return 1;
    if(![VIMGuestForSystemVersion(@"10.16") isEqual:@"bigsur"]||![VIMGuestForSystemVersion(@"10.16.99") isEqual:@"bigsur"])return 1;
    for(NSString *invalid in @[@"12.0",@"27.0",@"11.foo",@"11.",@"10.14..6",@"10.15x",@""])
        if(VIMGuestForSystemVersion(invalid))return 1;
    puts("PASS: desktop graphics selection is independent of automatic account setup; all thirteen guest families and unknown values checked.");
}return 0;}
