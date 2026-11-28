/* Local PVG protocol compatibility, not an upstream contribution.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>

typedef struct {uint32_t offset,width,height,strideAndElement;} VIMPackedBackingPlane;
typedef struct {uint64_t length;uint32_t page,pixelFormat,planeCount;} VIMPackedBackingDescriptor;

// Big Sur's v216 backing sends pixel width but an eight-byte/two-pixel
// element. Intel PVG validates width as element count. Correct only this
// rejected packed layout; accepted descriptors and all other formats pass
// through unchanged. Original range/stride validation must still run.
static inline int VIMNormalizePacked422Plane(uint32_t format,VIMPackedBackingPlane *plane) {
    uint32_t row=plane->strideAndElement&0xffffffu,element=plane->strideAndElement>>24;
    uint32_t pair=format==0x76323136u?8:format==0x32767579u||format==0x79757673u?4:0;
    if(!pair||element!=pair||!plane->width||
       (uint64_t)plane->width*element<=row)return 0;
    uint32_t elements=plane->width/2+plane->width%2;
    if((uint64_t)elements*element>row)return 0;
    plane->width=elements;return 1;
}

#ifdef __OBJC__
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
static IMP VIMPacked422OriginalValidation;
static inline BOOL VIMPacked422Validate(id self,SEL selector,const VIMPackedBackingDescriptor *descriptor,
    const VIMPackedBackingPlane *source,VIMPackedBackingPlane *destination,uint32_t count,uint64_t length) {
    VIMPackedBackingPlane plane;
    if(count==1&&descriptor&&source) {
        plane=*source;
        if(VIMNormalizePacked422Plane(descriptor->pixelFormat,&plane))source=&plane;
    }
    return ((BOOL(*)(id,SEL,const void *,const void *,void *,uint32_t,uint64_t))VIMPacked422OriginalValidation)
        (self,selector,descriptor,source,destination,count,length);
}

// Call after the selected framework is loaded, before creating its device.
// This is a protocol adapter, not an OS/build/address-specific binary patch.
static inline void VIMInstallPacked422Backing(void) {
    static dispatch_once_t once;
    dispatch_once(&once,^{
        Class cls=object_getClass(NSClassFromString(@"PGBackingResource"));
        SEL selector=sel_registerName("validateBackingDescriptor:srcPlanes:destPlanes:planeCount:backingAllocationLength:");
        Method method=class_getInstanceMethod(cls,selector);
        if(method&&method_getNumberOfArguments(method)==7) {
            VIMPacked422OriginalValidation=method_getImplementation(method);
            class_replaceMethod(cls,selector,(IMP)VIMPacked422Validate,method_getTypeEncoding(method));
        }
    });
}
#endif
