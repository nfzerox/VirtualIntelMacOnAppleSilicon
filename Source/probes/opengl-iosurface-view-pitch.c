/* Native GPU regression for IOSurface views. Build on a modern macOS host:
 * xcrun clang -O2 -Wall -Wextra -Werror -Wno-unused-function \
 *   -Wno-deprecated-declarations -Wno-misleading-indentation \
 *   Source/probes/opengl-iosurface-view-pitch.c -framework OpenGL \
 *   -framework IOSurface -framework CoreFoundation -o /tmp/iosurface-view-pitch
 * Run without renderer overrides: exercises each available accelerated GPU.
 */
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <OpenGL/CGLIOSurface.h>
#include <IOSurface/IOSurface.h>
static CGLError raw_import(CGLContextObj c,GLsizei width,GLsizei height,IOSurfaceRef surface)
{return CGLTexImageIOSurface2D(c,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,width,height,
    GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,surface,0);}
#include "../PassthroughOpenGL/iosurface-view-pitch-host.h"

static unsigned failures;
static void check(int good,const char *label)
{printf("%s %s\n",good?"PASS":"FAIL",label);failures+=!good;}
/* The 200x1 Core Image lookup exposed a second tile-layout discontinuity:
 * a full-width view can still be wrong when its backing is taller. */
static void short_height(CGLContextObj context,size_t pitch)
{
    enum {W=200,H=128,ROW=832};
    const CFStringRef keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,
        kIOSurfaceBytesPerRow,kIOSurfaceAllocSize,kIOSurfacePixelFormat};
    int64_t values[]={W,H,4,ROW,ROW*H,0x42475241};
    CFMutableDictionaryRef properties=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    for(unsigned i=0;i<6;++i){
        CFNumberRef n=CFNumberCreate(NULL,kCFNumberSInt64Type,values+i);
        CFDictionarySetValue(properties,keys[i],n);CFRelease(n);
    }
    IOSurfaceRef surface=IOSurfaceCreate(properties);CFRelease(properties);
    check(surface!=NULL,"create one-row lookup backing");if(!surface)return;
    for(unsigned wrapped=0;wrapped<2;++wrapped){
        check(!IOSurfaceLock(surface,0,NULL),"lock lookup backing");
        memset(IOSurfaceGetBaseAddress(surface),0,IOSurfaceGetAllocSize(surface));IOSurfaceUnlock(surface,0,NULL);
        GLuint texture=0,framebuffer=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
        CGLError error=wrapped?CGLTexImageIOSurface2D(context,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,W,1,
            GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,surface,0):raw_import(context,W,1,surface);
        check(!error,"import one-row lookup");
        glGenFramebuffersEXT(1,&framebuffer);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,framebuffer);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
        check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"one-row framebuffer complete");
        glViewport(0,0,W,1);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,W,0,1,-1,1);
        glMatrixMode(GL_MODELVIEW);glLoadIdentity();glDisable(GL_TEXTURE_RECTANGLE_ARB);glColor4ub(17,34,51,255);
        glBegin(GL_QUADS);glVertex2i(0,0);glVertex2i(W,0);glVertex2i(W,1);glVertex2i(0,1);glEnd();glFinish();
        check(!glGetError() && !IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL),"read completed lookup backing");
        const uint32_t *pixels=IOSurfaceGetBaseAddress(surface);unsigned correct=0;
        for(unsigned x=0;x<W;++x)correct+=pixels[x]==0xff112233u;
        IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
        printf("lookup wrapped=%u correct_row_pixels=%u/%u\n",wrapped,correct,W);
        if(wrapped)check(correct==W,"all wrapped lookup pixels address the first row");
        else if(correct!=W)check(pitch==DG_VIEW_PITCH_UNKNOWN,"height-dependent native tiling detected");
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&framebuffer);glDeleteTextures(1,&texture);
    }
    check(pitch!=DG_VIEW_PITCH_UNKNOWN || !DGIOSurfaceViewPitchMatches(surface,W,1,pitch),"taller tiled backing cannot be reused for a short drawable");
    CFRelease(surface);
}
static void run(GLint renderer)
{
    CGLPixelFormatAttribute attributes[]={kCGLPFAAllowOfflineRenderers,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFARendererID,(CGLPixelFormatAttribute)renderer,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    CGLError error=CGLChoosePixelFormat(attributes,&format,&count);
    if(!error && format)error=CGLCreateContext(format,NULL,&context);
    if(format)CGLDestroyPixelFormat(format);
    check(!error && context,"create accelerated renderer context");if(!context)return;
    CGLSetCurrentContext(context);
    printf("renderer=%s id=%x\n",glGetString(GL_RENDERER),renderer);
    size_t pitch=DGIOSurfaceViewPitch(context);
    check(CGLGetCurrentContext()==context,"measurement restores caller context");
    check(DGIOSurfaceViewPitch(context)==pitch,"cached renderer measurement");
    const CFStringRef keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,
        kIOSurfaceBytesPerRow,kIOSurfaceAllocSize,kIOSurfacePixelFormat};
    int64_t values[]={DG_VIEW_PITCH_PROBE_COLUMNS,DG_VIEW_PITCH_PROBE_ROWS,4,
        DG_VIEW_PITCH_PROBE_ROW,DG_VIEW_PITCH_PROBE_ROW*DG_VIEW_PITCH_PROBE_ROWS,0x42475241};
    CFMutableDictionaryRef properties=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    for(unsigned i=0;i<6;++i){
        CFNumberRef n=CFNumberCreate(NULL,kCFNumberSInt64Type,values+i);
        CFDictionarySetValue(properties,keys[i],n);CFRelease(n);
    }
    IOSurfaceRef surface=IOSurfaceCreate(properties);CFRelease(properties);
    check(surface!=NULL,"create tiled-size surface");if(!surface)goto done;
    check(DGViewPitchRenderMatches(context,surface,DG_VIEW_PITCH_PROBE_COLUMNS)==1,"full-width native rendering addresses rows 1 and 33");
    const GLsizei widths[]={1,65,300,1000,1280};
    for(unsigned i=0;i<sizeof(widths)/sizeof(*widths);++i){
        int native=DGViewPitchRenderMatches(context,surface,widths[i]);
        check(native>=0,"native view render measured successfully");
        if(!native)check(pitch!=0,"misaddressed native view detected");
        GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
        error=CGLTexImageIOSurface2D(context,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,widths[i],DG_VIEW_PITCH_PROBE_ROWS,
            GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,surface,0);
        GLint imported=0;glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_WIDTH,&imported);
        check(!error && !glGetError(),"wrapped view imported successfully");
        printf("view=%d imported=%d native_rows_correct=%d\n",widths[i],imported,native);
        glDeleteTextures(1,&texture);
        check(DGViewPitchRenderMatches(context,surface,imported)==1,"wrapped dimensions render at the correct CPU addresses");
        size_t capacity=DGIOSurfaceViewCapacityWidth(widths[i],(widths[i]+127)&~127,4,pitch);
        check(pitch!=DG_VIEW_PITCH_UNKNOWN || capacity==(size_t)widths[i],"tiled-view fallback allocates exact logical width");
    }
    CFRelease(surface);
    short_height(context,pitch);
done:
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
}
int main(void)
{
    CGLRendererInfoObj info=NULL;GLint count=0;
    check(!CGLQueryRendererInfo(UINT32_MAX,&info,&count) && info,"enumerate native renderers");
    unsigned tested=0;GLint previous=0;
    for(GLint i=0;info && i<count;++i){
        GLint accelerated=0,renderer=0;
        CGLDescribeRenderer(info,i,kCGLRPAccelerated,&accelerated);
        CGLDescribeRenderer(info,i,kCGLRPRendererID,&renderer);
        if(accelerated && renderer!=previous){run(renderer);++tested;previous=renderer;}
    }
    if(info)CGLDestroyRendererInfo(info);
    check(tested>0,"at least one accelerated renderer tested");
    printf("failures=%u renderers=%u\n",failures,tested);return failures?1:0;
}
