#pragma once

// Native 11.x PVG leaves MTLIOAccelTexture's API usage metadata at zero.
// CIMetalCommandBufferCopyTexture checks ShaderWrite and silently skips ROI
// copies when this is zero. Preserve the caller's actual descriptor, as the
// older-guest adapter already does. This does not change storage or wire data.
// Resolve the ivar from runtime metadata, never an OS-build-specific offset.
static ptrdiff_t pvgBigSurUsageOffset=-1;
static IMP pvgBigSurDeviceInit,pvgBigSurBufferInit,pvgBigSurTiledInit,pvgBigSurSurfaceInit;
static void pvgBigSurRestoreUsage(id texture,MTLTextureDescriptor *descriptor) {
    if(texture&&descriptor&&pvgBigSurUsageOffset>=0)
        *(NSUInteger *)((uint8_t *)(__bridge void *)texture+pvgBigSurUsageOffset)=descriptor.usage;
}
static id pvgBigSurInitDevice(id self,SEL selector,id device,MTLTextureDescriptor *descriptor) {
    id texture=((id(*)(id,SEL,id,id))pvgBigSurDeviceInit)(self,selector,device,descriptor);
    pvgBigSurRestoreUsage(texture,descriptor);return texture;
}
static id pvgBigSurInitBuffer(id self,SEL selector,id buffer,MTLTextureDescriptor *descriptor,NSUInteger offset,NSUInteger row) {
    id texture=((id(*)(id,SEL,id,id,NSUInteger,NSUInteger))pvgBigSurBufferInit)
        (self,selector,buffer,descriptor,offset,row);
    pvgBigSurRestoreUsage(texture,descriptor);return texture;
}
static id pvgBigSurInitTiled(id self,SEL selector,id buffer,MTLTextureDescriptor *descriptor,NSUInteger offset,NSUInteger row) {
    id texture=((id(*)(id,SEL,id,id,NSUInteger,NSUInteger))pvgBigSurTiledInit)
        (self,selector,buffer,descriptor,offset,row);
    pvgBigSurRestoreUsage(texture,descriptor);return texture;
}
static id pvgBigSurInitSurface(id self,SEL selector,id device,MTLTextureDescriptor *descriptor,void *surface,NSUInteger plane,NSUInteger field) {
    id texture=((id(*)(id,SEL,id,id,void *,NSUInteger,NSUInteger))pvgBigSurSurfaceInit)
        (self,selector,device,descriptor,surface,plane,field);
    pvgBigSurRestoreUsage(texture,descriptor);return texture;
}
static BOOL pvgBigSurUsageMarker(id self,SEL selector){(void)self;(void)selector;return YES;}
static void pvgInstallBigSurTextureUsage(void) {
    if(NSProcessInfo.processInfo.operatingSystemVersion.majorVersion!=11||pvgBigSurDeviceInit)return;
    Class texture=objc_getClass("AppleParavirtTexture");
    Ivar usage=class_getInstanceVariable(objc_getClass("MTLIOAccelTexture"),"_usage");
    if(!texture||!usage||strcmp(ivar_getTypeEncoding(usage),@encode(NSUInteger)))return;
    ptrdiff_t offset=ivar_getOffset(usage);
    if(offset<0||(NSUInteger)offset+sizeof(NSUInteger)>class_getInstanceSize(texture))return;
    if(!class_addMethod(texture,sel_registerName("vim_preservesBigSurTextureUsage"),
                        (IMP)pvgBigSurUsageMarker,"c@:"))return;
    pvgBigSurUsageOffset=offset;
#define PVG_BIG_SUR_USAGE_INIT(name,original,replacement) do { \
    SEL selector=sel_registerName(name);Method method=class_getInstanceMethod(texture,selector); \
    if(method){original=method_getImplementation(method); \
        class_replaceMethod(texture,selector,(IMP)replacement,method_getTypeEncoding(method));} \
} while(0)
    PVG_BIG_SUR_USAGE_INIT("initWithDevice:descriptor:",pvgBigSurDeviceInit,pvgBigSurInitDevice);
    PVG_BIG_SUR_USAGE_INIT("initWithBuffer:descriptor:offset:bytesPerRow:",pvgBigSurBufferInit,pvgBigSurInitBuffer);
    PVG_BIG_SUR_USAGE_INIT("initTiledWithBuffer:descriptor:offset:bytesPerRow:",pvgBigSurTiledInit,pvgBigSurInitTiled);
    PVG_BIG_SUR_USAGE_INIT("initWithDevice:descriptor:iosurface:plane:field:",pvgBigSurSurfaceInit,pvgBigSurInitSurface);
#undef PVG_BIG_SUR_USAGE_INIT
}
