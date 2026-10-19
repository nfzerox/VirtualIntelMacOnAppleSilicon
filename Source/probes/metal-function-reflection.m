#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/message.h>
#include <dlfcn.h>
#include <unistd.h>
static id inputBuffer,inputTexture;
static unsigned inputReads,inputWrites;
static BOOL (*addReference)(id,SEL,id,BOOL);
static BOOL tracedReference(id self,SEL sel,id resource,BOOL write) {
    if(resource==inputBuffer || resource==inputTexture) {
        if(write)inputWrites++;else inputReads++;
    }
    return addReference(self,sel,resource,write);
}
int main(void) {
    alarm(30); setvbuf(stdout,NULL,_IONBF,0);
    @autoreleasepool {
        id<MTLDevice> d=MTLCopyAllDevices().firstObject;
        NSError *err=nil;
        NSString *src=@"#include <metal_stdlib>\nusing namespace metal; kernel void test(const device float *in [[buffer(0)]], device float *out [[buffer(3)]], texture2d<float,access::read> a [[texture(1)]], texture2d<float,access::write> b [[texture(4)]], uint2 p [[thread_position_in_grid]]) {float4 v=a.read(p)+in[p.x]; out[p.x]=v.x; b.write(v,p);}";
        id<MTLLibrary> l=[d newLibraryWithSource:src options:nil error:&err];
        id f=[l newFunctionWithName:@"test"];
        if(!f){fprintf(stderr,"library error=%s\n",err.description.UTF8String);return 1;}
        printf("function=%s reflection start\n",object_getClassName(f));
        id r=((id(*)(id,SEL,NSUInteger))objc_msgSend)(f,sel_registerName("reflectionWithOptions:"),0);
        printf("reflection=%s\n",object_getClassName(r));
        NSArray *args=((id(*)(id,SEL))objc_msgSend)(r,sel_registerName("arguments"));
        for(MTLArgument *a in args)printf("type=%lu index=%lu access=%lu name=%s\n",a.type,a.index,a.access,a.name.UTF8String);
        if(args.count!=4)return 2;
        void (*masks)(id,uint32_t *,unsigned,uint32_t *,unsigned)=dlsym(RTLD_DEFAULT,"PVGSetWritability");
        if(masks) {
            uint32_t b=0,t=0;masks(f,&b,8,&t,8);
            printf("masks buffer=%x texture=%x\n",b,t);
            if(!getenv("PVG_CONSERVATIVE_WRITABILITY") && (b!=8 || t!=16))return 3;
            if(!getenv("PVG_CONSERVATIVE_WRITABILITY")) {
                b=0;t=0;masks(f,&b,3,&t,4);if(b || t)return 9;
                NSObject *unknown=[NSObject new];masks(unknown,&b,8,&t,8);[unknown release];
                if(b!=UINT32_MAX || t!=UINT32_MAX)return 10;
            }
        }
        id<MTLComputePipelineState> pipeline=[d newComputePipelineStateWithFunction:f error:&err];
        if(!pipeline)return 4;
        id<MTLBuffer> in=[d newBufferWithLength:64 options:MTLResourceStorageModeShared];
        id<MTLBuffer> out=[d newBufferWithLength:64 options:MTLResourceStorageModeShared];
        for(int i=0;i<16;i++)((float *)in.contents)[i]=2;
        MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:16 height:1 mipmapped:NO];
        td.storageMode=MTLStorageModeShared;td.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 101500
        // Native Mojave Metal rejects shared textures; use a private texture
        // and upload through a shared buffer. Catalina's existing test stays
        // unchanged. This is probe setup, not a production rendering change.
        td.storageMode=MTLStorageModePrivate;
#endif
        id<MTLTexture> a=[d newTextureWithDescriptor:td],b=[d newTextureWithDescriptor:td];
        if(!a || !b)return 11;
        inputBuffer=in;inputTexture=a;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 101500
        inputTexture=nil; // exclude the one-time upload from the access census
#endif
        Method method=class_getInstanceMethod(objc_getClass("AppleParavirtCommandBuffer"),sel_registerName("addResourceReference:isWrite:"));
        if(method){addReference=(void *)method_getImplementation(method);method_setImplementation(method,(IMP)tracedReference);}
        float pixels[64];for(int i=0;i<64;i++)pixels[i]=.25f;
        id<MTLCommandQueue> q=[d newCommandQueue];
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 101500
        id<MTLBuffer> upload=[d newBufferWithBytes:pixels length:256 options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> setup=[q commandBuffer];
        id<MTLBlitCommandEncoder> uploadEncoder=[setup blitCommandEncoder];
        [uploadEncoder copyFromBuffer:upload sourceOffset:0 sourceBytesPerRow:256 sourceBytesPerImage:256 sourceSize:MTLSizeMake(16,1,1) toTexture:a destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
        [uploadEncoder endEncoding];[setup commit];[setup waitUntilCompleted];
        if(setup.status!=MTLCommandBufferStatusCompleted)return 12;
        inputTexture=a;
#else
        [a replaceRegion:MTLRegionMake2D(0,0,16,1) mipmapLevel:0 withBytes:pixels bytesPerRow:256];
#endif
        id<MTLBuffer> textureReadback=[d newBufferWithLength:256 options:MTLResourceStorageModeShared];
        for(int i=0;i<20;i++) {
            id<MTLCommandBuffer> cb=[q commandBuffer];id<MTLComputeCommandEncoder> e=[cb computeCommandEncoder];
            [e setComputePipelineState:pipeline];[e setBuffer:in offset:0 atIndex:0];[e setBuffer:out offset:0 atIndex:3];
            [e setTexture:a atIndex:1];[e setTexture:b atIndex:4];
            [e dispatchThreadgroups:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(16,1,1)];
            [e endEncoding];
            id<MTLBlitCommandEncoder> blit=[cb blitCommandEncoder];
            [blit copyFromTexture:b sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(16,1,1) toBuffer:textureReadback destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:256];
            [blit endEncoding];[cb commit];[cb waitUntilCompleted];if(cb.status!=MTLCommandBufferStatusCompleted)return 5;
        }
        for(int i=0;i<16;i++)if(((float *)out.contents)[i]!=2.25f)return 6;
        for(int i=0;i<64;i++)if(((float *)textureReadback.contents)[i]!=2.25f){fprintf(stderr,"texture pixel[%d]=%g\n",i,((float *)textureReadback.contents)[i]);return 7;}
        puts("PASS buffers + texture pixels; 20 submissions");
        printf("input read references=%u write references=%u\n",inputReads,inputWrites);
        if(masks && !getenv("PVG_CONSERVATIVE_WRITABILITY") && (inputWrites || inputReads!=40))return 8;
        return 0;
    }
}
