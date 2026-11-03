/* Same offscreen Core Image workload on Mavericks Intel and the guest.
 * Uses the public CI/CGL API and a generated, opaque coordinate grid. Output
 * is diagnostic evidence, never a replacement effect or software fallback.
 */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl.h>
#import <OpenGL/glext.h>
#include <sys/stat.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <dlfcn.h>
#if MAC_OS_X_VERSION_MIN_REQUIRED <= 1040
// Native Tiger's public CIFilter keys predate the exported string constants.
#define kCIInputImageKey @"inputImage"
#define kCIOutputImageKey @"outputImage"
#endif
int main(int argc,char **argv)
{
    int leopardReference=argc==4&&!strcmp(argv[3],"--leopard-discrete-behavior-reference");
    if(argc!=3&&!leopardReference){fprintf(stderr,"usage: %s FILTER NEW_OUTPUT_DIRECTORY [--leopard-discrete-behavior-reference]\n",argv[0]);return 2;}
    // This is a finite diagnostic, not setup or an application render limit.
    // A native filter compiler fault must not strand the diagnostic owner.
    alarm(30);
    NSAutoreleasePool *pool=[[NSAutoreleasePool alloc] init];
    {
        CIFilter *filter=[CIFilter filterWithName:@(argv[1])];
        if(!filter){fprintf(stderr,"Filter unavailable: %s\n",argv[1]);return 2;}
        [filter setDefaults];NSLog(@"filter attributes=%@",filter.attributes);
        if(mkdir(argv[2],0700)){perror("new output directory");return 2;}
        enum {W=384,H=256};unsigned char pixels[W*H*4];
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            unsigned char *p=pixels+(y*W+x)*4;
            p[0]=255;p[1]=x*255/(W-1);p[2]=y;p[3]=((x/16+y/16)&1)?240:24;
        }
        CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();
        CIImage *input=[CIImage imageWithBitmapData:[NSData dataWithBytes:pixels length:sizeof(pixels)]
            bytesPerRow:W*4 size:CGSizeMake(W,H) format:kCIFormatARGB8 colorSpace:color];
        if(![filter.inputKeys containsObject:kCIInputImageKey])return 2;
        [filter setValue:input forKey:kCIInputImageKey];
        if(!strcmp(argv[1],"CIShapedWaterRipple")){
            /* Put the actual distortion inside this small owned image and
             * observe a nonzero phase, not the identity frame at phase zero. */
            [filter setValue:[CIVector vectorWithX:144 Y:92] forKey:@"inputPoint0"];
            [filter setValue:[CIVector vectorWithX:240 Y:164] forKey:@"inputPoint1"];
            [filter setValue:@12 forKey:@"inputCornerRadius"];
            /* Dock's matching DBRippleInfo::run multiplies normalized
             * animation progress by 450. This is not a 0..1 phase. */
            const char *phaseText=getenv("VIM_CI_RIPPLE_PHASE");char *end=NULL;
            double phase=phaseText?strtod(phaseText,&end):225;
            if(!isfinite(phase)||phase<0||phase>1000||(phaseText&&(!end||*end)))return 2;
            [filter setValue:@(phase) forKey:@"inputPhase"];
        }
        CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
#if MAC_OS_X_VERSION_MIN_REQUIRED > 1060
            kCGLPFASupportsAutomaticGraphicsSwitching,
#endif
            0};
        CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
        CGLError contextError=CGLChoosePixelFormat(attrs,&format,&count);
        fprintf(stderr,"CGL choose=%d count=%d format=%p\n",(int)contextError,(int)count,(void *)format);
        if(!contextError&&format)contextError=CGLCreateContext(format,NULL,&context);
        fprintf(stderr,"CGL create=%d context=%p\n",(int)contextError,(void *)context);
        if(!contextError&&context)contextError=CGLSetCurrentContext(context);
        if(contextError||!context){fprintf(stderr,"CGL current/admission=%d\n",(int)contextError);return 2;}
        const char *renderer=(const char *)glGetString(GL_RENDERER);
        fprintf(stderr,"renderer=%s\n",renderer?renderer:"none");
        // Standard public names, not a private driver override. One extra
        // 2D object breaks the accidental texture/program-name coincidence
        // hiding Tiger CI's saved-state overwrite on physical Intel hardware.
        GLuint pressure=0;
        if(getenv("VIM_CI_NAME_PRESSURE")){
            glGenTextures(1,&pressure);glBindTexture(GL_TEXTURE_2D,pressure);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
            glBindTexture(GL_TEXTURE_2D,0);
        }
        const char *traceLibrary=getenv("VIM_CI_TRACE_LIBRARY");void *traceHandle=NULL;
        if(traceLibrary){
            traceHandle=dlopen(traceLibrary,RTLD_NOW|RTLD_LOCAL);
            void (*trace)(CGLContextObj)=traceHandle?dlsym(traceHandle,"VIMTraceContext"):NULL;
            if(!trace){fprintf(stderr,"Trace library unavailable: %s\n",dlerror());return 77;}trace(context);
        }
        if(leopardReference){
            /* Explicit supplied physical10.5 behavior/ABI reference only;
             * never present this discrete renderer as an Intel perf control. */
            char release[64]={0};size_t size=sizeof(release);
            if(sysctlbyname("kern.osrelease",release,&size,NULL,0)||strncmp(release,"9.",2)||
               !renderer||!strstr(renderer,"NVIDIA GeForce 9400M"))return 77;
        }else if(!renderer||!strstr(renderer,"Intel"))return 2;
        if(getenv("VIM_CI_NORMALIZE_INTEL_LABEL")) {
            /* Owned diagnostic only, not a public GL contract. Our verified
             * virtual renderer returns a mutable4096-byte per-context heap
             * cache. Never write a native driver's potentially const string.
             * This shortens the real Intel trademark spelling, retaining its
             * actual hardware name and every capability/limit unchanged. */
            GLint rendererID=0;CGLDescribePixelFormat(format,0,kCGLPFARendererID,&rendererID);
            if((rendererID&0x00fe7f00)!=(0x01036000&0x00fe7f00) || strncmp(renderer,"Intel(R) ",9))return 2;
            char *owned=(char *)renderer;memmove(owned+6,owned+9,strlen(owned+9)+1);owned[5]=' ';
            fprintf(stderr,"NORMALIZED owned renderer=%s\n",owned);
        }
        // PID-provider probes cannot inspect a renderer before CGL dlopens
        // it. Stop only on explicit request, after that load and before CI
        // policy creation; the operator attaches a finite trace then resumes.
        if(getenv("VIM_CI_TRACE_GATE")) {
            fprintf(stderr,"TRACE_GATE pid=%d\n",getpid());fflush(stderr);raise(SIGSTOP);
        }
#if MAC_OS_X_VERSION_MIN_REQUIRED < 1060
        CIContext *ci=[CIContext contextWithCGLContext:context pixelFormat:format
            options:[NSDictionary dictionaryWithObject:(id)color forKey:kCIContextOutputColorSpace]];
#else
        CIContext *ci=[CIContext contextWithCGLContext:context pixelFormat:format colorSpace:color options:nil];
#endif
        CIImage *output=[filter valueForKey:kCIOutputImageKey];if(!ci||!output)return 2;
        if(traceHandle){void (*watch)(void *)=dlsym(traceHandle,"VIMTraceCIContext");if(watch)watch(ci);}
        GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
        if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 2;
        glViewport(0,0,W,H);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,W,0,H,-1,1);
        glMatrixMode(GL_MODELVIEW);glLoadIdentity();glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
        [ci drawImage:output inRect:CGRectMake(0,0,W,H) fromRect:CGRectMake(0,0,W,H)];
        if(traceHandle){void (*end)(void)=dlsym(traceHandle,"VIMTraceCIEnd");if(end)end();}
        glFinish();glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        GLenum error=glGetError();fprintf(stderr,"GL error=%x\n",(unsigned)error);if(error)return 1;
        NSBitmapImageRep *image=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:W pixelsHigh:H
            bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
            bytesPerRow:W*4 bitsPerPixel:32];
        if(!image)return 2;
        for(unsigned y=0;y<H;++y)memcpy(image.bitmapData+y*W*4,pixels+(H-1-y)*W*4,W*4);
        NSData *png=[image representationUsingType:NSPNGFileType properties:@{}];NSError *writeError=nil;
        /* mkdir above exclusively creates this output directory. The older
         * Foundation has no WithoutOverwriting option; Atomic still writes
         * only inside that new owned directory, never an existing result. */
        if(![png writeToFile:[@(argv[2]) stringByAppendingPathComponent:@"result.png"]
            options:NSAtomicWrite error:&writeError])return 2;
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
        if(pressure)glDeleteTextures(1,&pressure);
        // CI's autoreleased context/cache objects must finish teardown while
        // their caller-owned CGL context still exists. A native driver's heap
        // can conceal this fixture lifetime bug; a different allocation does not.
        ci=nil;[pool drain];CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(format);
        CGColorSpaceRelease(color);return 0;
    }
}
