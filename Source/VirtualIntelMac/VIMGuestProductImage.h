#pragma once
#import <AppKit/AppKit.h>

/// Product art, distinct from the installer roundel. Keep the supplied disc
/// sheet intact and display its exact disc slices for the first three guests.
NS_INLINE NSImage *VIMGuestProductImage(NSString *guest) {
    NSImage *product=[NSImage imageNamed:[guest stringByAppendingString:@"ProductIcon"]];
    if(product)return product;
    static NSDictionary<NSString *,NSValue *> *slices;
    static NSCache<NSString *,NSImage *> *cache;
    static dispatch_once_t once;
    dispatch_once(&once,^{
        slices=@{@"tiger":[NSValue valueWithRect:NSMakeRect(8,221,195,195)],
                 @"leopard":[NSValue valueWithRect:NSMakeRect(221,220,195,195)],
                 @"snowleopard":[NSValue valueWithRect:NSMakeRect(434,223,194,194)]};
        cache=[NSCache new];
    });
    NSValue *slice=slices[guest];
    if(!slice)return nil;
    NSImage *image=[cache objectForKey:guest];
    if(image)return image;
    NSImage *sheet=[NSImage imageNamed:@"ClassicDiscs"];
    if(!sheet)return nil;
    NSRect source=slice.rectValue;
    source.origin.y=sheet.size.height-NSMaxY(source);
    image=[NSImage imageWithSize:NSMakeSize(98,98) flipped:NO drawingHandler:^BOOL(NSRect destination) {
        [[NSBezierPath bezierPathWithOvalInRect:destination] addClip];
        [sheet drawInRect:destination fromRect:source operation:NSCompositingOperationSourceOver
            fraction:1 respectFlipped:YES hints:@{NSImageHintInterpolation:@(NSImageInterpolationHigh)}];
        return YES;
    }];
    [cache setObject:image forKey:guest];
    return image;
}
