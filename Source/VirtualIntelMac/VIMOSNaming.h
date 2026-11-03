#import <Foundation/Foundation.h>
/// Desktop graphics architecture, deliberately independent of whether the
/// account/installer automation backend supports this guest version.
NS_INLINE BOOL VIMGuestUsesOpenGLDesktop(NSString *guest) {
    return [@[@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",@"mavericks",@"yosemite",@"elcapitan",@"sierra",@"highsierra"] containsObject:guest?:@""];
}
NS_INLINE NSString *VIMOperatingSystemName(NSString *guest) {
    if([@[@"tiger",@"leopard",@"snowleopard",@"lion"] containsObject:guest?:@""])return @"Mac OS X";
    if([@[@"mountainlion",@"mavericks",@"yosemite",@"elcapitan"] containsObject:guest?:@""])return @"OS X";
    return @"macOS";
}
