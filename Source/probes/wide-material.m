// Bounded, offscreen width-boundary probe. No capture, user images or files.
#define GL_SILENCE_DEPRECATION
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>
#import <CoreImage/CoreImage.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#include <unistd.h>
#include <stdlib.h>

static BOOL Inspect(NSString *name,id<MTLTexture> texture,NSUInteger width,NSUInteger height) {
    NSMutableData *data=[NSMutableData dataWithLength:width*height*4];
    [texture getBytes:data.mutableBytes bytesPerRow:width*4 fromRegion:MTLRegionMake2D(0,0,width,height) mipmapLevel:0];
    const uint8_t *p=data.bytes;NSUInteger y=height/2,left=(y*width+32)*4,bad=0;
    for(NSUInteger x=32;x<width-32;x++) {
        NSUInteger i=(y*width+x)*4;
        for(NSUInteger c=0;c<4;c++)if(abs((int)p[i+c]-(int)p[left+c])>2){bad++;break;}
    }
    BOOL nonzero=p[left]||p[left+1]||p[left+2];
    printf("%s %lux%lu left=%u,%u,%u,%u boundary=%u,%u,%u,%u right=%u,%u,%u,%u bad=%lu\n",
        name.UTF8String,(unsigned long)width,(unsigned long)height,p[left],p[left+1],p[left+2],p[left+3],
        p[(y*width+MIN(width-1,4096UL))*4],p[(y*width+MIN(width-1,4096UL))*4+1],
        p[(y*width+MIN(width-1,4096UL))*4+2],p[(y*width+MIN(width-1,4096UL))*4+3],
        p[(y*width+width-33)*4],p[(y*width+width-33)*4+1],p[(y*width+width-33)*4+2],p[(y*width+width-33)*4+3],(unsigned long)bad);
    return nonzero&&bad==0;
}
int main(int argc,const char **argv){@autoreleasepool {
    alarm(30);setbuf(stdout,NULL);
    NSUInteger width=argc==2?(NSUInteger)strtoul(argv[1],NULL,10):5120,height=64;
    if(argc>2||width<256||width>8192)return 64;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,0};CGLPixelFormatObj pf=NULL;CGLContextObj gl=NULL;GLint count=0;
    if(!CGLChoosePixelFormat(attrs,&pf,&count)&&pf&&!CGLCreateContext(pf,NULL,&gl)&&!CGLSetCurrentContext(gl)) {
        GLint maximum=0,rectangle=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);glGetIntegerv(GL_MAX_RECTANGLE_TEXTURE_SIZE_ARB,&rectangle);
        printf("GL renderer=%s maxTexture=%d maxRectangle=%d error=0x%x\n",glGetString(GL_RENDERER),maximum,rectangle,glGetError());
    }else puts("GL context unavailable");
    if(gl){CGLSetCurrentContext(NULL);CGLDestroyContext(gl);}if(pf)CGLDestroyPixelFormat(pf);
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();if(!device)return 2;
    MTLTextureDescriptor *descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:width height:height mipmapped:NO];
    // Intel-style PVG textures do not advertise Shared storage. Use Managed
    // and explicitly synchronize GPU output before inspecting CPU bytes.
    descriptor.storageMode=MTLStorageModeManaged;
    descriptor.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite|MTLTextureUsageRenderTarget;
    id<MTLTexture> input=[device newTextureWithDescriptor:descriptor],output=[device newTextureWithDescriptor:descriptor];
    if(!input||!output)return 3;
    NSMutableData *bytes=[NSMutableData dataWithLength:width*height*4];uint8_t *p=bytes.mutableBytes;
    for(NSUInteger i=0;i<width*height;i++){p[i*4]=51;p[i*4+1]=102;p[i*4+2]=153;p[i*4+3]=255;}
    [input replaceRegion:MTLRegionMake2D(0,0,width,height) mipmapLevel:0 withBytes:p bytesPerRow:width*4];
    id<MTLCommandQueue> queue=[device newCommandQueue];BOOL good=YES;
    CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();if(!color)return 3;
    for(NSString *kind in @[@"metal-copy",@"mps-blur",@"ci-copy",@"ci-blur"]) {
        memset(bytes.mutableBytes,0,bytes.length);
        [output replaceRegion:MTLRegionMake2D(0,0,width,height) mipmapLevel:0 withBytes:bytes.bytes bytesPerRow:width*4];
        id<MTLCommandBuffer> command=[queue commandBuffer];
        if([kind isEqual:@"metal-copy"]) {
            id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
            [blit copyFromTexture:input sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(width,height,1)
                toTexture:output destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];[blit endEncoding];
        }else if([kind isEqual:@"mps-blur"]) {
            MPSImageGaussianBlur *blur=[[MPSImageGaussianBlur alloc] initWithDevice:device sigma:2];blur.edgeMode=MPSImageEdgeModeClamp;
            [blur encodeToCommandBuffer:command sourceTexture:input destinationTexture:output];
        }else {
            CIContext *context=[CIContext contextWithMTLDevice:device options:@{kCIContextWorkingColorSpace:NSNull.null}];
            CIImage *image=[[CIImage imageWithColor:[CIColor colorWithRed:.2 green:.4 blue:.6 alpha:1]] imageByCroppingToRect:CGRectMake(0,0,width,height)];
            if([kind isEqual:@"ci-blur"])image=[[[image imageByClampingToExtent] imageByApplyingFilter:@"CIGaussianBlur" withInputParameters:@{kCIInputRadiusKey:@4}] imageByCroppingToRect:CGRectMake(0,0,width,height)];
            [context render:image toMTLTexture:output commandBuffer:command bounds:CGRectMake(0,0,width,height) colorSpace:color];
        }
        id<MTLBlitCommandEncoder> sync=[command blitCommandEncoder];[sync synchronizeResource:output];[sync endEncoding];
        [command commit];[command waitUntilCompleted];
        if(command.status!=MTLCommandBufferStatusCompleted){printf("%s GPU error: %s\n",kind.UTF8String,command.error.description.UTF8String);good=NO;continue;}
        good=Inspect(kind,output,width,height)&&good;
    }
    CGColorSpaceRelease(color);return good?0:1;
}}
