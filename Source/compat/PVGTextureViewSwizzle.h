#pragma once
#import <Foundation/Foundation.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <stdint.h>

// The Big Sur PVG texture initializer calls the older serializer selector,
// losing Catalina's nonidentity view channels. Forward those channels to the
// serializer's existing full selector, as the OpenGL adapter already does.
// Scoped to the donor texture/serializer classes; no host patch or pixel copy.
static IMP pvgOriginalSwizzledView, pvgOriginalSerializeView;
static _Thread_local __unsafe_unretained id pvgSwizzleParent;
static _Thread_local uint32_t pvgPendingSwizzle;

static const void *pvgSwizzleInstallationKey(void) {
    return (const void *)sel_registerName("vimTextureSwizzleSerializationInstalled");
}

static BOOL pvgTextureSwizzleTransportInstalled(void) {
    return [objc_getAssociatedObject(objc_getClass("AppleParavirtTexture"),
                                    pvgSwizzleInstallationKey()) boolValue];
}

static id pvgInitializeSwizzledView(id self, SEL selector, id parent,
                                    NSUInteger format, NSUInteger type,
                                    NSRange levels, NSRange slices, uint32_t swizzle) {
    id savedParent = pvgSwizzleParent;
    uint32_t saved = pvgPendingSwizzle;
    pvgSwizzleParent = parent;
    pvgPendingSwizzle = swizzle;
    @try {
        return ((id(*)(id,SEL,id,NSUInteger,NSUInteger,NSRange,NSRange,uint32_t))
                pvgOriginalSwizzledView)(self,selector,parent,format,type,levels,slices,swizzle);
    } @finally {
        pvgSwizzleParent = savedParent;
        pvgPendingSwizzle = saved;
    }
}

static uint32_t pvgSerializeSwizzledView(id self, SEL selector,
                                        NSUInteger format, NSUInteger type,
                                        NSRange levels, NSRange slices,
                                        id parent, id allocator) {
    if (parent == pvgSwizzleParent && pvgPendingSwizzle != UINT32_C(0x05040302)) {
        SEL full = sel_registerName("newTextureViewWithPixelFormat:textureType:levels:slices:swizzle:baseTexture:allocator:");
        return ((uint32_t(*)(id,SEL,NSUInteger,NSUInteger,NSRange,NSRange,uint32_t,id,id))
                objc_msgSend)(self,full,format,type,levels,slices,pvgPendingSwizzle,parent,allocator);
    }
    return ((uint32_t(*)(id,SEL,NSUInteger,NSUInteger,NSRange,NSRange,id,id))
            pvgOriginalSerializeView)(self,selector,format,type,levels,slices,parent,allocator);
}

static BOOL pvgInstallTextureSwizzleTransport(Class serializer) {
    Class texture = objc_getClass("AppleParavirtTexture");
    if (!texture || !serializer) return NO;
    @synchronized(texture) {
        // Metal and OpenGL can load in either order in the same process.
        // The class marker prevents a second adapter from wrapping it again.
        if (pvgTextureSwizzleTransportInstalled()) return YES;
        SEL old = sel_registerName("newTextureViewWithPixelFormat:textureType:levels:slices:baseTexture:allocator:");
        SEL full = sel_registerName("newTextureViewWithPixelFormat:textureType:levels:slices:swizzle:baseTexture:allocator:");
        SEL view = sel_registerName("initWithTexture:pixelFormat:textureType:levels:slices:swizzle:");
        Method sm = class_getInstanceMethod(serializer,old);
        Method vm = class_getInstanceMethod(texture,view);
        if (!sm || !vm || !class_getInstanceMethod(serializer,full)) return NO;
        pvgOriginalSerializeView = method_getImplementation(sm);
        pvgOriginalSwizzledView = method_getImplementation(vm);
        class_replaceMethod(serializer,old,(IMP)pvgSerializeSwizzledView,method_getTypeEncoding(sm));
        class_replaceMethod(texture,view,(IMP)pvgInitializeSwizzledView,method_getTypeEncoding(vm));
        objc_setAssociatedObject(texture,pvgSwizzleInstallationKey(),@YES,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return YES;
    }
}
