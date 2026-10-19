#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/message.h>
int main(void) { @autoreleasepool {
    id d=MTLCopyAllDevices().firstObject;
    for (NSString *name in @[@"maxTextureWidth2D",@"maxTextureHeight2D",@"maxTextureDimension2D",@"maxBufferLength",@"recommendedMaxWorkingSetSize",@"supportsSharedStorageTextures"]) {
        SEL s=NSSelectorFromString(name);
        if ([d respondsToSelector:s]) fprintf(stderr,"%s=%llu\n",name.UTF8String,((unsigned long long(*)(id,SEL))objc_msgSend)(d,s));
    }
    for (NSNumber *n in @[@4096,@4097,@5000,@6016]) {
        MTLTextureDescriptor *p=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:n.unsignedIntegerValue height:n.unsignedIntegerValue mipmapped:NO];
        p.storageMode=MTLStorageModePrivate; p.usage=MTLTextureUsageShaderRead;
        id<MTLTexture> t=[d newTextureWithDescriptor:p];
        fprintf(stderr,"texture %lu=%p width=%lu\n",n.unsignedLongValue,t,(unsigned long)t.width);
        [t release];
    }
} }
