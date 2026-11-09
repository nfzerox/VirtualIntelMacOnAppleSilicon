/* Included by passthrough-opengl-host.c before any IOSurface texture import.
 *
 * A texture bound to an IOSurface with FEWER columns than the surface is a
 * "view". Intel and Apple GL drivers address its rows with the surface's
 * bytesPerRow. The AMD GL driver ignores bytesPerRow for a view and uses its
 * own pitch, round_up(columns*bytes_per_element,alignment), for both sampling
 * and rendering.
 * Every row after the first is then read and written at the wrong offset:
 * horizontal row corruption, sheared/repeated window images and trails.
 * Full-width imports are unaffected, as is a view whose own aligned pitch
 * happens to equal the surface's bytesPerRow.
 *
 * Measure the behavior, including the driver's alignment, once per native
 * renderer with real draws instead of matching vendor strings. Then:
 *  - host allocations choose a capacity whose bytesPerRow equals the view
 *    pitch the driver assumes, so logical sizes stay exact (see callers);
 *  - any remaining incompatible view is imported at the surface's full width.
 *    Rectangle textures address texels, so [0,width) still names the same
 *    pixels; only the extra columns become addressable. That is preferable
 *    to every row being misaddressed.
 * A driver which misaddresses in a pattern other than one consistent
 * alignment gets exact-width allocations and full-width imports only.
 * Renderers which honor bytesPerRow take exactly the previous native path,
 * as does any renderer whose measurement cannot complete.
 */
#ifndef VIM_IOSURFACE_VIEW_PITCH_HOST_H
#define VIM_IOSURFACE_VIEW_PITCH_HOST_H
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLIOSurface.h>
#include <IOSurface/IOSurface.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

/* DGIOSurfaceViewPitch results: 0 honors bytesPerRow; DG_VIEW_PITCH_UNKNOWN
 * misaddresses views in an unrecognized pattern; otherwise the measured
 * power-of-two row alignment in bytes. */
#define DG_VIEW_PITCH_UNKNOWN ((size_t)1)
#define DG_VIEW_PITCH_RENDERERS 16u
#define DG_VIEW_PITCH_PROBE_COLUMNS 2048u
#define DG_VIEW_PITCH_PROBE_ROW (DG_VIEW_PITCH_PROBE_COLUMNS*4u)
struct DGViewPitchRenderer { GLint renderer;size_t pitch; };
static pthread_mutex_t dg_view_pitch_lock=PTHREAD_MUTEX_INITIALIZER;
static struct DGViewPitchRenderer dg_view_pitch_renderers[DG_VIEW_PITCH_RENDERERS];
static unsigned dg_view_pitch_count,dg_view_pitch_next;
static unsigned dg_view_pitch_widened;

static size_t DGViewPitchAlign(size_t bytes,size_t alignment)
{ return (bytes+alignment-1)/alignment*alignment; }

/* Byte offset at which the driver reads row 1 of a `columns`-wide view of
 * the probe surface, or 0 if the draw could not be measured. Each probe
 * pixel holds its own element index, so the sample names its address. */
static size_t DGViewPitchRowOffset(CGLContextObj context,IOSurfaceRef surface,GLsizei columns)
{
    GLuint texture=0,target=0,framebuffer=0;uint32_t read=0;size_t offset=0;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    if(!CGLTexImageIOSurface2D(context,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,columns,2,
        GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,surface,0)){
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glGenTextures(1,&target);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,target);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,columns,2,0,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,NULL);
        glGenFramebuffersEXT(1,&framebuffer);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,framebuffer);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,target,0);
        if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT){
            // Sample the view in a real draw: the GPU addressing used by the
            // compositor. glGetTexImage and glReadPixels of the attachment
            // take a different path and do not show the misaddressing.
            glViewport(0,0,columns,2);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,columns,0,2,-1,1);
            glMatrixMode(GL_MODELVIEW);glLoadIdentity();
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);glEnable(GL_TEXTURE_RECTANGLE_ARB);
            glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
            glBegin(GL_QUADS);
            glTexCoord2f(0,0);glVertex2f(0,0);glTexCoord2f(columns,0);glVertex2f(columns,0);
            glTexCoord2f(columns,2);glVertex2f(columns,2);glTexCoord2f(0,2);glVertex2f(0,2);
            glEnd();
            glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
            glReadPixels(0,1,1,1,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,&read);
            if(!glGetError() && (read>>24)==0xff && (read&0xffffffu)<2*DG_VIEW_PITCH_PROBE_COLUMNS)
                offset=(size_t)(read&0xffffffu)*4;
        }
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&framebuffer);
        glDeleteTextures(1,&target);
    }
    glDeleteTextures(1,&texture);
    return offset;
}
/* Measure with a private context on the same renderer: 2048 columns (8192-byte
 * rows), 2 rows. Views of 1, 65 and 300 columns must read row 1 at byte 8192.
 * An aligning driver reads round_up(columns*4,alignment); the 1-column view
 * yields the alignment and the other widths must agree with it. */
static size_t DGViewPitchMeasure(GLint renderer)
{
    CGLPixelFormatAttribute attributes[]={kCGLPFAAllowOfflineRenderers,kCGLPFASupportsAutomaticGraphicsSwitching,
        kCGLPFARendererID,(CGLPixelFormatAttribute)renderer,0};
    CGLPixelFormatObj format=NULL;GLint count=0;CGLContextObj context=NULL;
    if(CGLChoosePixelFormat(attributes,&format,&count) || !format)return 0;
    CGLError created=CGLCreateContext(format,NULL,&context);CGLDestroyPixelFormat(format);
    if(created || !context)return 0;
    int64_t values[]={DG_VIEW_PITCH_PROBE_COLUMNS,2,4,DG_VIEW_PITCH_PROBE_ROW,2*DG_VIEW_PITCH_PROBE_ROW,0x42475241};
    const CFStringRef keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,
        kIOSurfaceBytesPerRow,kIOSurfaceAllocSize,kIOSurfacePixelFormat};
    CFMutableDictionaryRef properties=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    IOSurfaceRef surface=NULL;size_t pitch=0;
    if(properties){
        int good=1;
        for(unsigned i=0;i<6;++i){
            CFNumberRef number=CFNumberCreate(NULL,kCFNumberSInt64Type,&values[i]);
            if(!number){good=0;break;}CFDictionarySetValue(properties,keys[i],number);CFRelease(number);
        }
        if(good)surface=IOSurfaceCreate(properties);CFRelease(properties);
    }
    if(surface && IOSurfaceGetBytesPerRow(surface)==DG_VIEW_PITCH_PROBE_ROW && !IOSurfaceLock(surface,0,NULL)){
        uint32_t *pixels=IOSurfaceGetBaseAddress(surface);
        for(uint32_t i=0;i<2*DG_VIEW_PITCH_PROBE_COLUMNS;++i)pixels[i]=0xff000000u|i;
        IOSurfaceUnlock(surface,0,NULL);
        CGLContextObj previous=CGLGetCurrentContext();
        if(!CGLSetCurrentContext(context)){
            static const GLsizei widths[]={1,65,300};size_t offsets[3];int measured=1,honored=1;
            for(unsigned i=0;i<3;++i){
                offsets[i]=DGViewPitchRowOffset(context,surface,widths[i]);
                measured=measured && offsets[i];honored=honored && offsets[i]==DG_VIEW_PITCH_PROBE_ROW;
            }
            glFinish();CGLSetCurrentContext(previous);
            // A failed measurement keeps the native path: no evidence of a
            // misaddressing driver is not a reason to change semantics.
            if(measured && !honored){
                size_t alignment=offsets[0];
                int consistent=alignment>=4 && alignment<DG_VIEW_PITCH_PROBE_ROW && !(alignment&(alignment-1));
                for(unsigned i=0;consistent && i<3;++i)
                    consistent=offsets[i]==DGViewPitchAlign((size_t)widths[i]*4,alignment);
                pitch=consistent?alignment:DG_VIEW_PITCH_UNKNOWN;
            }
        }
    }
    if(surface)CFRelease(surface);
    CGLDestroyContext(context);
    return pitch;
}
/* View pitch behavior of the renderer currently executing ctx; see results. */
static size_t DGIOSurfaceViewPitch(CGLContextObj ctx)
{
    GLint renderer=0;
    if(!ctx || CGLGetParameter(ctx,kCGLCPCurrentRendererID,&renderer) || !renderer)return 0;
    pthread_mutex_lock(&dg_view_pitch_lock);
    for(unsigned i=0;i<dg_view_pitch_count;++i)if(dg_view_pitch_renderers[i].renderer==renderer){
        size_t pitch=dg_view_pitch_renderers[i].pitch;pthread_mutex_unlock(&dg_view_pitch_lock);return pitch;
    }
    // Measured once per renderer, outside any caller GL state (own context).
    // A full table replaces its oldest entry rather than measuring again.
    size_t pitch=DGViewPitchMeasure(renderer);
    unsigned slot=dg_view_pitch_count<DG_VIEW_PITCH_RENDERERS?dg_view_pitch_count++:
        dg_view_pitch_next++%DG_VIEW_PITCH_RENDERERS;
    dg_view_pitch_renderers[slot]=(struct DGViewPitchRenderer){renderer,pitch};
    pthread_mutex_unlock(&dg_view_pitch_lock);
    if(!pitch)fprintf(stderr,"PassthroughOpenGL renderer=0x%x IOSurface view pitch=surface\n",renderer);
    else if(pitch==DG_VIEW_PITCH_UNKNOWN)fprintf(stderr,"PassthroughOpenGL renderer=0x%x IOSurface view pitch=unrecognized (full-width only)\n",renderer);
    else fprintf(stderr,"PassthroughOpenGL renderer=0x%x IOSurface view pitch=aligned to %zu bytes\n",renderer,pitch);
    return pitch;
}
/* A view of `width` columns is addressed correctly under `pitch`. */
static int DGIOSurfaceViewPitchMatches(IOSurfaceRef surface,size_t width,size_t pitch)
{
    if(width>=IOSurfaceGetWidth(surface) || !pitch)return 1;
    size_t element=IOSurfaceGetBytesPerElement(surface),columns=IOSurfaceGetElementWidth(surface);
    if(pitch==DG_VIEW_PITCH_UNKNOWN || !element || !columns)return 0;
    return DGViewPitchAlign((width+columns-1)/columns*element,pitch)==IOSurfaceGetBytesPerRow(surface);
}
/* Row bytes for a new surface whose views must match `pitch`. */
static size_t DGIOSurfaceViewRowBytes(size_t row,size_t pitch)
{ return pitch>DG_VIEW_PITCH_UNKNOWN?DGViewPitchAlign(row,pitch):row; }
/* Capacity for host-chosen storage: keep the caller's rounding when the
 * renderer honors bytesPerRow; otherwise round to the driver's own pitch so a
 * logical-size view of the allocation remains correctly addressed, or use the
 * exact width when that pitch is not a whole number of elements or unknown. */
static size_t DGIOSurfaceViewCapacityWidth(size_t width,size_t preferred,size_t element_bytes,size_t pitch)
{
    if(!pitch)return preferred;
    if(pitch==DG_VIEW_PITCH_UNKNOWN || !element_bytes || pitch%element_bytes)return width;
    size_t columns=pitch/element_bytes;
    return (width+columns-1)/columns*columns;
}
static CGLError DGTexImageIOSurface2D(CGLContextObj ctx,GLenum target,GLenum internal,GLsizei width,GLsizei height,
    GLenum format,GLenum type,IOSurfaceRef surface,GLuint plane)
{
    if(surface && !plane && !IOSurfaceGetPlaneCount(surface) && width>0 &&
       (size_t)width<IOSurfaceGetWidth(surface) && IOSurfaceGetWidth(surface)<=INT32_MAX &&
       !DGIOSurfaceViewPitchMatches(surface,(size_t)width,DGIOSurfaceViewPitch(ctx))){
        if(__atomic_fetch_add(&dg_view_pitch_widened,1,__ATOMIC_RELAXED)<8)
            fprintf(stderr,"PassthroughOpenGL widened IOSurface view %dx%d of %zux%zu row=%zu\n",width,height,
                IOSurfaceGetWidth(surface),IOSurfaceGetHeight(surface),IOSurfaceGetBytesPerRow(surface));
        width=(GLsizei)IOSurfaceGetWidth(surface);
    }
    return CGLTexImageIOSurface2D(ctx,target,internal,width,height,format,type,surface,plane);
}
/* Every later host import goes through the wrapper. */
#define CGLTexImageIOSurface2D DGTexImageIOSurface2D
#endif
