/* Isolated Intel-host ownership/performance experiment. Native GL still
 * renders; Metal only blits its IOSurface into an existing shared CPU buffer.
 * Not a runtime switch. No IOSurfaceAddress, CPU pixel copy or file backing.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include <assert.h>
#include <mach/mach_time.h>
static uint32_t background(unsigned x,unsigned y,unsigned round)
{return 0xff000000u|((x*13+y*3+round*7)&255)|(((x*7+y*17)&255)<<8)|(((x+y*11)&255)<<16);}
static void exercise(id<MTLDevice> device,id<MTLCommandQueue> queue,int width,int height,int sparse,int aligned)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;struct DGHostContext c={0};
    assert(!CGLChoosePixelFormat(attrs,&pf,&count) && pf && !CGLCreateContext(pf,NULL,&c.cgl));
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(c.cgl);c.profile=kCGLOGLPVersion_Legacy;
    c.draw_mode=c.read_mode=GL_FRONT;
    assert(strstr((const char *)glGetString(GL_RENDERER),"Intel") && DGCreateAliases(&c,NULL));
    assert(DGSelectDrawable(&c,1,width,height));
    MTLTextureDescriptor *descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
        width:IOSurfaceGetWidth(c.color_storage) height:IOSurfaceGetHeight(c.color_storage) mipmapped:NO];
    descriptor.storageMode=MTLStorageModeShared;descriptor.usage=MTLTextureUsageShaderRead;
    id<MTLTexture> texture=[device newTextureWithDescriptor:descriptor iosurface:c.color_storage plane:0];
    assert(texture);
    unsigned xbase=aligned?0:3,ybase=aligned?0:7;
    unsigned pitch=((width+(aligned?0:15))*4+255)&~255u,rows=height+(aligned?0:11);
    size_t length=((size_t)pitch*rows+vm_page_size-1)&~(size_t)(vm_page_size-1);
    mach_vm_address_t memory=0;assert(!mach_vm_allocate(mach_task_self(),&memory,length,VM_FLAGS_ANYWHERE));
    id<MTLBuffer> buffer=[device newBufferWithBytesNoCopy:(void *)(uintptr_t)memory length:length
        options:MTLResourceStorageModeShared deallocator:nil];
    assert(buffer && buffer.contents==(void *)(uintptr_t)memory);
    struct DGVRAMRect rects[3]={{0,0,width,height},{0},{0}};unsigned rectangles=1;
    if(sparse){
        rects[0]=(struct DGVRAMRect){11,13,101,67};rects[1]=(struct DGVRAMRect){131,77,41,39};
        rects[2]=(struct DGVRAMRect){53,height-27,51,23};rectangles=3;
    }
    uint64_t gl_ticks=0,metal_ticks=0;double gpu_seconds=0;unsigned gpu_samples=0;
    mach_timebase_info_data_t clock;mach_timebase_info(&clock);
    for(unsigned round=0;round<40;++round){
        @autoreleasepool {
            uint32_t *pixels=(void *)(uintptr_t)memory;
            for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<pitch/4;++x)pixels[y*(pitch/4)+x]=background(x,y,round);
            unsigned red=(round*17)&255,green=(round*29)&255,blue=(round*43)&255,alpha=(round*11)&255;
            glClearColor(red/255.f,green/255.f,blue/255.f,alpha/255.f);glClear(GL_COLOR_BUFFER_BIT);
            uint64_t began=mach_absolute_time();glFinish();uint64_t ready=mach_absolute_time();
            id<MTLCommandBuffer> commands=[queue commandBuffer];id<MTLBlitCommandEncoder> blit=[commands blitCommandEncoder];
            assert(commands && blit);
            for(unsigned r=0;r<rectangles;++r){
                struct DGVRAMRect rect=rects[r];NSUInteger offset=(ybase+rect.y)*(NSUInteger)pitch+(xbase+rect.x)*4;
                [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(rect.x,rect.y,0)
                    sourceSize:MTLSizeMake(rect.width,rect.height,1) toBuffer:buffer destinationOffset:offset
                    destinationBytesPerRow:pitch destinationBytesPerImage:0];
            }
            [blit endEncoding];[commands commit];[commands waitUntilCompleted];uint64_t completed=mach_absolute_time();
            if(commands.status!=MTLCommandBufferStatusCompleted){
                fprintf(stderr,"Metal copy failed: %s\n",commands.error.description.UTF8String);abort();
            }
            uint32_t published=(alpha<<24)|(red<<16)|(green<<8)|blue;
            // Exactly one first volatile read per location; a second read or
            // diagnostic GPU download must never hide stale CPU contents.
            for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<pitch/4;++x){
                int changed=0;for(unsigned r=0;r<rectangles;++r)
                    if(x>=xbase+(unsigned)rects[r].x && x<xbase+(unsigned)(rects[r].x+rects[r].width) &&
                       y>=ybase+(unsigned)rects[r].y && y<ybase+(unsigned)(rects[r].y+rects[r].height))changed=1;
                uint32_t expected=changed?published:background(x,y,round);
                uint32_t observed=((volatile uint32_t *)pixels)[y*(pitch/4)+x];
                if(observed!=expected){
                    fprintf(stderr,"FAIL first CPU read round=%u xy=%u,%u got=%08x expected=%08x\n",round,x,y,observed,expected);abort();
                }
            }
            if(round>=8){
                gl_ticks+=ready-began;metal_ticks+=completed-ready;
                if(commands.GPUStartTime>0 && commands.GPUEndTime>=commands.GPUStartTime){
                    gpu_seconds+=commands.GPUEndTime-commands.GPUStartTime;++gpu_samples;
                }
            }
        }
    }
    double scale=(double)clock.numer/clock.denom/1000/32;
    printf("PASS GL IOSurface -> same Metal shared pages: %dx%d sparse=%d aligned=%d GL-finish-us=%.3f Metal-copy-wait-us=%.3f Metal-GPU-us=%.3f samples=%u\n",
        width,height,sparse,aligned,gl_ticks*scale,metal_ticks*scale,gpu_samples?gpu_seconds*1e6/gpu_samples:0,gpu_samples);fflush(stdout);
    [texture release];[buffer release];assert(!mach_vm_deallocate(mach_task_self(),memory,length));
    assert(!glGetError());DGDestroyDrawables(&c);DGDestroyAliases(&c);CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
}
int main(void)
{
    @autoreleasepool {
        NSArray<id<MTLDevice>> *devices=MTLCopyAllDevices();
        if(devices.count!=1 || ![devices[0].name containsString:@"Intel"]){
            fputs("SKIP: requires one native Intel Metal device\n",stderr);[devices release];return 77;
        }
        id<MTLDevice> device=devices[0];
        id<MTLCommandQueue> queue=[device newCommandQueue];assert(queue);
        printf("Metal device=%s unified=%d\n",device.name.UTF8String,device.hasUnifiedMemory);fflush(stdout);
        exercise(device,queue,251,197,1,0);exercise(device,queue,1280,800,0,1);
        exercise(device,queue,2560,1600,0,1);exercise(device,queue,2560,1600,0,0);
        [queue release];[devices release];return 0;
    }
}
