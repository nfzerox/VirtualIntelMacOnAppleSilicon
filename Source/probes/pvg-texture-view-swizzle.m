// Native, GPU-independent regression for the shared donor ABI adapter.
#import <Foundation/Foundation.h>
#include <assert.h>
#include "../compat/PVGTextureViewSwizzle.h"

@interface MTLSerializer : NSObject
- (uint32_t)newTextureViewWithPixelFormat:(NSUInteger)format textureType:(NSUInteger)type
    levels:(NSRange)levels slices:(NSRange)slices baseTexture:(id)parent allocator:(id)allocator;
- (uint32_t)newTextureViewWithPixelFormat:(NSUInteger)format textureType:(NSUInteger)type
    levels:(NSRange)levels slices:(NSRange)slices swizzle:(uint32_t)swizzle
    baseTexture:(id)parent allocator:(id)allocator;
@end
@implementation MTLSerializer
- (uint32_t)newTextureViewWithPixelFormat:(NSUInteger)format textureType:(NSUInteger)type
    levels:(NSRange)levels slices:(NSRange)slices baseTexture:(id)parent allocator:(id)allocator {
    (void)format;(void)type;(void)levels;(void)slices;(void)parent;(void)allocator;
    return 0x05040302;
}
- (uint32_t)newTextureViewWithPixelFormat:(NSUInteger)format textureType:(NSUInteger)type
    levels:(NSRange)levels slices:(NSRange)slices swizzle:(uint32_t)swizzle
    baseTexture:(id)parent allocator:(id)allocator {
    (void)format;(void)type;(void)levels;(void)slices;(void)parent;(void)allocator;
    return swizzle;
}
@end

@interface AppleParavirtTexture : NSObject
@property uint32_t serialized;
- (id)initWithTexture:(id)parent pixelFormat:(NSUInteger)format textureType:(NSUInteger)type
    levels:(NSRange)levels slices:(NSRange)slices swizzle:(uint32_t)swizzle;
@end
@implementation AppleParavirtTexture
- (id)initWithTexture:(id)parent pixelFormat:(NSUInteger)format textureType:(NSUInteger)type
    levels:(NSRange)levels slices:(NSRange)slices swizzle:(uint32_t)swizzle {
    (void)swizzle;
    if (!(self=[super init])) return nil;
    if (format==99) @throw [NSException exceptionWithName:@"Fixture" reason:nil userInfo:nil];
    if (format==7) {
        AppleParavirtTexture *nested=[[AppleParavirtTexture alloc] initWithTexture:[NSObject new]
            pixelFormat:8 textureType:type levels:levels slices:slices swizzle:0x01000005];
        assert(nested.serialized==0x01000005);
    }
    // The donor drops its swizzle parameter and calls this older selector.
    self.serialized=[[MTLSerializer new] newTextureViewWithPixelFormat:format textureType:type
        levels:levels slices:slices baseTexture:parent allocator:nil];
    return self;
}
@end

int main(void) {
    @autoreleasepool {
        assert(pvgInstallTextureSwizzleTransport(MTLSerializer.class));
        SEL view=@selector(initWithTexture:pixelFormat:textureType:levels:slices:swizzle:);
        IMP installed=class_getMethodImplementation(AppleParavirtTexture.class,view);
        assert(pvgInstallTextureSwizzleTransport(MTLSerializer.class));
        assert(class_getMethodImplementation(AppleParavirtTexture.class,view)==installed);
        NSObject *parent=[NSObject new];
        for (unsigned format=6;format<=7;format++) {
            for (unsigned key=0;key<3;key++) {
                uint32_t swizzle=key==0?0x05040302:key==1?0x02000000:0x01000005;
                AppleParavirtTexture *texture=[[AppleParavirtTexture alloc] initWithTexture:parent
                    pixelFormat:format textureType:2 levels:NSMakeRange(0,1) slices:NSMakeRange(0,1) swizzle:swizzle];
                assert(texture.serialized==swizzle);
            }
        }
        @try {
            (void)[[AppleParavirtTexture alloc] initWithTexture:parent pixelFormat:99 textureType:2
                levels:NSMakeRange(0,1) slices:NSMakeRange(0,1) swizzle:0x02000000];
            assert(0);
        } @catch (NSException *exception) { assert([exception.name isEqual:@"Fixture"]); }
        assert([[MTLSerializer new] newTextureViewWithPixelFormat:8 textureType:2
            levels:NSMakeRange(0,1) slices:NSMakeRange(0,1) baseTexture:parent allocator:nil]==0x05040302);
        puts("PASS: identity, nonidentity, nested views, exception cleanup and duplicate installation");
    }
    return 0;
}
