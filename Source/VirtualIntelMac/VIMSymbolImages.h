#import <Cocoa/Cocoa.h>

@interface NSImage (VIMPrivateSystemSymbols)
+ (NSImage *)_imageWithSystemSymbolName:(NSString *)name;
+ (NSImage *)_imageWithSystemSymbolName:(NSString *)name variableValue:(double)value
    accessibilityDescription:(NSString *)description allowPrivateLookup:(BOOL)allowPrivate;
@end

static inline NSImage *VIMAppsSymbolImage(void) {
    NSImage *image=nil;
    if([NSImage respondsToSelector:@selector(_imageWithSystemSymbolName:variableValue:accessibilityDescription:allowPrivateLookup:)])
        image=[NSImage _imageWithSystemSymbolName:@"appstore" variableValue:0
            accessibilityDescription:@"Download Apps" allowPrivateLookup:YES];
    else if([NSImage respondsToSelector:@selector(_imageWithSystemSymbolName:)])
        image=[NSImage _imageWithSystemSymbolName:@"appstore"];
    return image?:[NSImage imageWithSystemSymbolName:@"bag" accessibilityDescription:@"Download Apps"];
}
