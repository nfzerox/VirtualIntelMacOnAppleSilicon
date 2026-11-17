/* Finite native Metal -> CGL IOSurface ownership comparison. Run unchanged
 * on the Intel reference and mixed PVG/Passthrough OpenGL guest. Read CGL
 * A separate Metal buffer verifies GPU pixels without synchronizing the
 * source texture's CPU shadow. No texture.getBytes masks stale storage. */
#define GL_SILENCE_DEPRECATION
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#import <OpenGL/CGLIOSurface.h>
#include <unistd.h>
static int exercise(id<MTLDevice> device,int synchronize_cpu)
{
    IOSurfaceRef surface=IOSurfaceCreate((CFDictionaryRef)@{(id)kIOSurfaceWidth:@64,
        (id)kIOSurfaceHeight:@64,(id)kIOSurfaceBytesPerElement:@4,
        (id)kIOSurfaceBytesPerRow:@256,(id)kIOSurfacePixelFormat:@(0x42475241)});
    if(!surface || IOSurfaceLock(surface,0,NULL))return 2;
    uint8_t *base=IOSurfaceGetBaseAddress(surface);
    for(unsigned i=0;i<64*64;i++){base[i*4]=17;base[i*4+1]=34;base[i*4+2]=51;base[i*4+3]=255;}
    IOSurfaceUnlock(surface,0,NULL);
    MTLTextureDescriptor *description=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
        MTLPixelFormatBGRA8Unorm width:64 height:64 mipmapped:NO];
    // Native Intel Mojave accepts managed, not shared, texture storage.
    description.storageMode=MTLStorageModeManaged;
    description.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
    id<MTLTexture> texture=[device newTextureWithDescriptor:description iosurface:surface plane:0];
    id<MTLCommandQueue> queue=[device newCommandQueue];
    if(!texture || !queue){fprintf(stderr,"Metal allocation failed\n");return 2;}
    id<MTLCommandBuffer> commands=[queue commandBuffer];
    MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture=texture;
    pass.colorAttachments[0].loadAction=MTLLoadActionClear;
    pass.colorAttachments[0].storeAction=MTLStoreActionStore;
    pass.colorAttachments[0].clearColor=MTLClearColorMake(0,1,0,1);
    [[commands renderCommandEncoderWithDescriptor:pass] endEncoding];
    id<MTLBuffer> gpu_readback=[device newBufferWithLength:64*64*4 options:MTLResourceStorageModeShared];
    if(!gpu_readback)return 2;
    id<MTLBlitCommandEncoder> sync=[commands blitCommandEncoder];
    [sync copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(64,64,1) toBuffer:gpu_readback destinationOffset:0
        destinationBytesPerRow:256 destinationBytesPerImage:0];
    if(synchronize_cpu)[sync synchronizeResource:texture];
    [sync endEncoding];
    [commands commit];[commands waitUntilCompleted];
    if(commands.status!=MTLCommandBufferStatusCompleted){
        fprintf(stderr,"Metal command failed: %s\n",commands.error.description.UTF8String);return 2;
    }
    uint8_t cpu[4]={0},gl[4]={0},gpu[4]={0};memcpy(gpu,gpu_readback.contents,4);
    if(IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL))return 2;
    memcpy(cpu,IOSurfaceGetBaseAddress(surface),4);
    IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
    GLuint name=0,framebuffer=0;glGenTextures(1,&name);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,name);
    CGLError imported=CGLTexImageIOSurface2D(CGLGetCurrentContext(),GL_TEXTURE_RECTANGLE_ARB,
        GL_RGBA,64,64,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,surface,0);
    glGenFramebuffersEXT(1,&framebuffer);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,framebuffer);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,name,0);
    GLenum status=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,gl);GLenum error=glGetError();
    int good=!imported && status==GL_FRAMEBUFFER_COMPLETE_EXT && !error &&
        gl[0]==0 && gl[1]==255 && gl[2]==0 && gl[3]==255 &&
        gpu[0]==0 && gpu[1]==255 && gpu[2]==0 && gpu[3]==255;
    printf("%s managed-sync=%d CPU-BGRA=%u,%u,%u,%u GL-RGBA=%u,%u,%u,%u GPU-BGRA=%u,%u,%u,%u import=%d FBO=%x error=%x\n",
        good?"PASS":"FAIL",synchronize_cpu,cpu[0],cpu[1],cpu[2],cpu[3],
        gl[0],gl[1],gl[2],gl[3],gpu[0],gpu[1],gpu[2],gpu[3],imported,status,error);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&framebuffer);glDeleteTextures(1,&name);
    [gpu_readback release];[queue release];[texture release];CFRelease(surface);return !good;
}
int main(int argc,char **argv)
{
    alarm(30);
    @autoreleasepool {
        int intel=argc==2&&!strcmp(argv[1],"--intel");
        NSArray *devices=MTLCopyAllDevices();id<MTLDevice> device=nil;
        for(id<MTLDevice> candidate in devices)
            if(!intel || [candidate.name containsString:@"Intel"]){device=candidate;break;}
        if(!device)return 77;
        CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
            kCGLPFASupportsAutomaticGraphicsSwitching,
            intel?kCGLPFARendererID:0,(CGLPixelFormatAttribute)0x1024500,0};
        CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
        if(CGLChoosePixelFormat(attributes,&format,&count) || !format ||
           CGLCreateContext(format,NULL,&context) || CGLSetCurrentContext(context))return 2;
        const char *renderer=(const char *)glGetString(GL_RENDERER);
        printf("Metal=%s GL=%s\n",device.name.UTF8String,renderer);
        if(intel && !strstr(renderer,"Intel"))return 77;
        int result=exercise(device,0);
        result|=exercise(device,1);
        CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(format);[devices release];
        return result;
    }
}
