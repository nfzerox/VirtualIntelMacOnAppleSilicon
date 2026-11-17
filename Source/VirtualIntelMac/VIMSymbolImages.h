#import "VIMLocalization.h"
#import <Cocoa/Cocoa.h>

@interface NSImage (VIMPrivateSystemSymbols)
+ (NSImage *)_imageWithSystemSymbolName:(NSString *)name;
+ (NSImage *)_imageWithSystemSymbolName:(NSString *)name variableValue:(double)value
    accessibilityDescription:(NSString *)description allowPrivateLookup:(BOOL)allowPrivate;
@end

static inline NSImage *VIMSystemSymbolImage(NSString *name,NSString *description) {
    NSImage *image=[NSImage imageWithSystemSymbolName:name accessibilityDescription:description];
    if(image)return image;
    NSString *fallback=@{@"cable.connector":@"externaldrive",@"power.circle":@"power"}[name];
    return fallback?[NSImage imageWithSystemSymbolName:fallback accessibilityDescription:description]:nil;
}

static inline NSImage *VIMAppsSymbolImage(void) {
    NSImage *image=nil;
    if([NSImage respondsToSelector:@selector(_imageWithSystemSymbolName:variableValue:accessibilityDescription:allowPrivateLookup:)])
        image=[NSImage _imageWithSystemSymbolName:@"appstore" variableValue:0
            accessibilityDescription:VIMLocalized(@"ui.download-apps", @"Download Apps") allowPrivateLookup:YES];
    else if([NSImage respondsToSelector:@selector(_imageWithSystemSymbolName:)])
        image=[NSImage _imageWithSystemSymbolName:@"appstore"];
    return image?:[NSImage imageWithSystemSymbolName:@"bag" accessibilityDescription:VIMLocalized(@"ui.download-apps", @"Download Apps")];
}
