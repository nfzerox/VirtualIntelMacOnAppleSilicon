/* Bounded native-backend diagnostic, not an interposer or production patch.
 * Can a public Metal texture view select correct linear mip generation on
 * the same storage, without reading/converting/reuploading image pixels?
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
static int run(id<MTLDevice> device,id<MTLCommandQueue> queue,MTLPixelFormat format,MTLPixelFormat linear)
{
    MTLTextureDescriptor *desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:2 height:2 mipmapped:YES];
    desc.storageMode=MTLStorageModeShared;
    /* Deliberately omit PixelFormatView, as a native GL texture may do. */
    desc.usage=MTLTextureUsageShaderRead|MTLTextureUsageRenderTarget;
    id<MTLTexture> texture=[device newTextureWithDescriptor:desc];if(!texture)return 1;
    const uint8_t checker[]={0,0,0,255,255,255,255,255,255,255,255,255,0,0,0,255};
    id<MTLTexture> view=[texture newTextureViewWithPixelFormat:linear];
    printf("format=%lu view=%d source-levels=%lu view-levels=%lu same-parent=%d\n",(unsigned long)format,view!=nil,
        (unsigned long)texture.mipmapLevelCount,(unsigned long)view.mipmapLevelCount,view.parentTexture==texture);
    if(!view){[texture release];return 1;}
    int failed=0;
    for(unsigned mode=0;mode<2;++mode){
        [texture replaceRegion:MTLRegionMake2D(0,0,2,2) mipmapLevel:0 withBytes:checker bytesPerRow:8];
        id<MTLCommandBuffer> buffer=[queue commandBuffer];id<MTLBlitCommandEncoder> blit=[buffer blitCommandEncoder];
        [blit generateMipmapsForTexture:mode?view:texture];[blit endEncoding];[buffer commit];[buffer waitUntilCompleted];
        uint8_t pixel[4]={0};[texture getBytes:pixel bytesPerRow:4 fromRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:1];
        unsigned expected=mode?128:188;
        int good=buffer.status==MTLCommandBufferStatusCompleted&&abs((int)pixel[0]-(int)expected)<=1&&
            pixel[0]==pixel[1]&&pixel[1]==pixel[2]&&pixel[3]==255;
        printf("%s linear-view=%u pixel=%u,%u,%u,%u expected=%u gpu-ms=%g\n",good?"PASS":"FAIL",mode,
            pixel[0],pixel[1],pixel[2],pixel[3],expected,(buffer.GPUEndTime-buffer.GPUStartTime)*1000);
        if(buffer.error)fprintf(stderr,"%s\n",buffer.error.localizedDescription.UTF8String);failed+=!good;
    }
    [view release];[texture release];return failed;
}
int main(void){@autoreleasepool{
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();if(!device)return 77;
    id<MTLCommandQueue> queue=[device newCommandQueue];if(!queue){[device release];return 1;}
    printf("device=%s\n",device.name.UTF8String);
    int failed=run(device,queue,MTLPixelFormatRGBA8Unorm_sRGB,MTLPixelFormatRGBA8Unorm)+
        run(device,queue,MTLPixelFormatBGRA8Unorm_sRGB,MTLPixelFormatBGRA8Unorm);
    [queue release];[device release];return failed?1:0;
}}
