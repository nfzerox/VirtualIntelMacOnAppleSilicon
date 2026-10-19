/* Offline reproduction of captured Mojave compositor pipeline compilation.
 * Includes a small textured draw/readback; no VM or persistent host changes. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
extern dispatch_data_t PVGHostRepairLibrary(dispatch_data_t) __attribute__((weak_import));
static id<MTLLibrary> load(id<MTLDevice> device, const char *path) {
    NSData *bytes = [NSData dataWithContentsOfFile:@(path)];
    if (!bytes) return nil;
    dispatch_data_t data = dispatch_data_create(bytes.bytes, bytes.length,
        dispatch_get_global_queue(0, 0), DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    NSError *error = nil;
    dispatch_data_t repaired = PVGHostRepairLibrary ? PVGHostRepairLibrary(data) : NULL;
    id<MTLLibrary> library = [device newLibraryWithData:repaired ?: data error:&error];
    if (repaired) dispatch_release(repaired);
    dispatch_release(data);
    fprintf(stderr, "library %s: %s\n", path,
        library ? library.functionNames.description.UTF8String : error.description.UTF8String);
    return library;
}
int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc != 3) return 64;
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        id<MTLLibrary> vertex = load(device, argv[1]), fragment = load(device, argv[2]);
        if (!vertex || !fragment) return 1;
        MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
        d.vertexFunction = [vertex newFunctionWithName:@"UberCompositeVertex"];
        d.fragmentFunction = [fragment newFunctionWithName:@"UberCompositeFragment"];
        d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        d.vertexDescriptor = [MTLVertexDescriptor vertexDescriptor];
        for (unsigned i = 0; i < 3; ++i) {
            d.vertexDescriptor.attributes[i].format = i == 2 ? MTLVertexFormatFloat : MTLVertexFormatFloat2;
            d.vertexDescriptor.attributes[i].bufferIndex = 2;
            d.vertexDescriptor.attributes[i].offset = i * 8;
        }
        d.vertexDescriptor.layouts[2].stride = 20;
        NSError *error = nil;
        id pipeline = [device newRenderPipelineStateWithDescriptor:d error:&error];
        fprintf(stderr, "pipeline=%s error=%s\n", pipeline ? "PASS" : "FAIL",
            error ? error.description.UTF8String : "none");
        if (!pipeline) return 2;
        MTLTextureDescriptor *td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
            width:8 height:8 mipmapped:NO];
        td.storageMode = MTLStorageModeShared;
        td.usage = MTLTextureUsageRenderTarget;
        id<MTLTexture> target = [device newTextureWithDescriptor:td];
        td.width=2; td.height=2; td.usage=MTLTextureUsageShaderRead;
        id<MTLTexture> texture = [device newTextureWithDescriptor:td];
        const uint8_t colors[16]={0,0,255,255, 0,255,0,255,
                                 255,0,0,255, 255,255,255,255};
        [texture replaceRegion:MTLRegionMake2D(0,0,2,2) mipmapLevel:0
            withBytes:colors bytesPerRow:8];
        const float vertices[]={-1,1,0,0,1, -1,-1,0,1,1,
                                 1,1,1,0,1,  1,-1,1,1,1};
        const float matrix[]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        const float args[]={1,0,1};
        MTLSamplerDescriptor *sd=[MTLSamplerDescriptor new];
        sd.minFilter=MTLSamplerMinMagFilterNearest;
        sd.magFilter=MTLSamplerMinMagFilterNearest;
        id<MTLSamplerState> sampler=[device newSamplerStateWithDescriptor:sd];
        id<MTLCommandQueue> queue=[device newCommandQueue];
        id<MTLCommandBuffer> command=[queue commandBuffer];
        MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture=target;
        pass.colorAttachments[0].loadAction=MTLLoadActionClear;
        pass.colorAttachments[0].storeAction=MTLStoreActionStore;
        id<MTLRenderCommandEncoder> encoder=[command renderCommandEncoderWithDescriptor:pass];
        [encoder setRenderPipelineState:pipeline];
        [encoder setVertexBytes:vertices length:sizeof(vertices) atIndex:2];
        [encoder setVertexBytes:matrix length:sizeof(matrix) atIndex:1];
        [encoder setFragmentBytes:args length:sizeof(args) atIndex:0];
        [encoder setFragmentTexture:texture atIndex:0];
        [encoder setFragmentSamplerState:sampler atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        [encoder endEncoding]; [command commit]; [command waitUntilCompleted];
        if(command.status!=MTLCommandBufferStatusCompleted) {
            fprintf(stderr,"draw failed: %s\n",command.error.description.UTF8String);
            return 3;
        }
        uint8_t result[8*8*4];
        [target getBytes:result bytesPerRow:32 fromRegion:MTLRegionMake2D(0,0,8,8) mipmapLevel:0];
        unsigned mismatches=0;
        for(unsigned y=0;y<8;y++) for(unsigned x=0;x<8;x++)
            if(memcmp(result+(y*8+x)*4,colors+((y/4)*2+x/4)*4,4)) mismatches++;
        fprintf(stderr,"textured draw: mismatched pixels=%u/64\n",mismatches);
        return mismatches ? 4 : 0;
    }
}
