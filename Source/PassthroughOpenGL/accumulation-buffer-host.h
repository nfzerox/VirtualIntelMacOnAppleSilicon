/* GPU accumulation storage for a virtual default framebuffer. The executor
 * supplies its real selected read texture and draw attachments; it remains
 * responsible for GL errors, pixel-format selection and drawable ownership.
 *
 * A private shared context leaves application state and native attribute
 * stacks untouched. Shared GL fences order both queues without CPU readback
 * or glFinish. RGBA32F ping-pong avoids depending on float32 blending support.
 * Private texture registration retains the existing implicit-name protection.
 */
#ifndef VIM_ACCUMULATION_BUFFER_HOST_H
#define VIM_ACCUMULATION_BUFFER_HOST_H
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <IOSurface/IOSurface.h>
#include <CoreVideo/CVPixelBuffer.h>
#include <stdint.h>
#include <string.h>
#include "private-textures-host.h"

struct DGAccumulationBuffer {
    IOSurfaceRef storage[2];
    GLuint textures[2];
    struct DGPrivateTextureWatch watches[2];
    GLsizei width,height;
    unsigned current;
};
struct DGAccumulationEngine {
    CGLContextObj context;
    GLuint framebuffer,program;
    GLint mode,value,clear,bounds;
};
struct DGAccumulationTarget {GLuint texture;GLboolean mask[4];};
/* Internal operation, not an extra public GL enum. */
#define DG_ACCUMULATION_CLEAR 0u

static void DGAccumulationDeleteBuffer(struct DGAccumulationBuffer *buffer)
{
    if(!buffer->textures[0] && !buffer->textures[1] && !buffer->storage[0] && !buffer->storage[1]){
        memset(buffer,0,sizeof(*buffer));return;
    }
    for(unsigned i=0;i<2;++i){
        DGPrivateTextureWatchForget(&buffer->watches[i]);
        DGPrivateDeleteTextures(1,&buffer->textures[i]);
        if(buffer->storage[i])CFRelease(buffer->storage[i]);
    }
    memset(buffer,0,sizeof(*buffer));
}
static void DGAccumulationDeleteEngine(struct DGAccumulationEngine *engine)
{
    if(!engine->context){memset(engine,0,sizeof(*engine));return;}
    CGLContextObj previous=CGLGetCurrentContext();
    if(engine->context && !CGLSetCurrentContext(engine->context)){
        if(engine->program)glDeleteProgram(engine->program);
        if(engine->framebuffer)glDeleteFramebuffersEXT(1,&engine->framebuffer);
        CGLSetCurrentContext(previous);
    }
    if(engine->context)CGLDestroyContext(engine->context);
    memset(engine,0,sizeof(*engine));
}
static GLuint DGAccumulationShader(GLenum type,const char *source)
{
    GLuint shader=glCreateShader(type);GLint compiled=0;
    if(shader){glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
        glGetShaderiv(shader,GL_COMPILE_STATUS,&compiled);}
    if(!compiled){if(shader)glDeleteShader(shader);return 0;}
    return shader;
}
/* Leaves the private context current. It is never exposed to an application. */
static int DGAccumulationInitialize(struct DGAccumulationEngine *engine,CGLContextObj parent)
{
    if(!engine->context && CGLCreateContext(CGLGetPixelFormat(parent),parent,&engine->context))return 0;
    if(CGLSetCurrentContext(engine->context))return 0;
    if(engine->program)return 1;
    const char *vertex="#version 120\nvoid main(){gl_Position=gl_Vertex;}\n";
    const char *fragment=
        "#version 120\n#extension GL_ARB_texture_rectangle : enable\n"
        "uniform sampler2DRect previousImage,sourceImage;\n"
        "uniform int operation;uniform float factor;uniform vec4 clearValue,bounds;\n"
        "void main(){vec2 p=gl_FragCoord.xy;vec4 old=texture2DRect(previousImage,p);\n"
        "bool inside=p.x>=bounds.x&&p.y>=bounds.y&&p.x<bounds.z&&p.y<bounds.w;\n"
        "vec4 next=old;\n"
        "if(inside){if(operation==0)next=clearValue;\n"
        "else if(operation==1)next=texture2DRect(sourceImage,p)*factor;\n"
        "else if(operation==2)next=old+texture2DRect(sourceImage,p)*factor;\n"
        "else if(operation==3)next=old*factor;\n"
        "else if(operation==4)next=old+vec4(factor);}\n"
        "if(operation==5)next=clamp(old*factor,0.0,1.0);\n"
        "gl_FragColor=next;}\n";
    GLuint vs=DGAccumulationShader(GL_VERTEX_SHADER,vertex);
    GLuint fs=DGAccumulationShader(GL_FRAGMENT_SHADER,fragment);
    GLuint program=vs&&fs?glCreateProgram():0;GLint linked=0;
    if(program){glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
        glGetProgramiv(program,GL_LINK_STATUS,&linked);}
    if(vs)glDeleteShader(vs);if(fs)glDeleteShader(fs);
    if(!linked){if(program)glDeleteProgram(program);return 0;}
    glGenFramebuffersEXT(1,&engine->framebuffer);
    if(!engine->framebuffer){glDeleteProgram(program);return 0;}
    engine->program=program;
    engine->mode=glGetUniformLocation(program,"operation");
    engine->value=glGetUniformLocation(program,"factor");
    engine->clear=glGetUniformLocation(program,"clearValue");
    engine->bounds=glGetUniformLocation(program,"bounds");
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program,"previousImage"),0);
    glUniform1i(glGetUniformLocation(program,"sourceImage"),1);
    glDisable(GL_DITHER);
    return engine->framebuffer && glGetError()==GL_NO_ERROR;
}
static int DGAccumulationAllocate(struct DGAccumulationEngine *engine,
    struct DGAccumulationBuffer *buffer,GLsizei width,GLsizei height)
{
    if(buffer->width==width && buffer->height==height && buffer->textures[0] && buffer->textures[1])return 1;
    DGAccumulationDeleteBuffer(buffer);
    if(width<1 || height<1 || width>8192 || height>8192)return 0;
    int64_t row=((int64_t)width*16+63)&~INT64_C(63);
    const CFStringRef keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,
        kIOSurfaceBytesPerRow,kIOSurfaceAllocSize,kIOSurfacePixelFormat};
    int64_t values[]={width,height,16,row,row*height,kCVPixelFormatType_128RGBAFloat};
    CFMutableDictionaryRef properties=CFDictionaryCreateMutable(NULL,0,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    if(!properties)return 0;
    for(unsigned i=0;i<6;++i){
        CFNumberRef value=CFNumberCreate(NULL,kCFNumberSInt64Type,values+i);
        if(!value){CFRelease(properties);return 0;}
        CFDictionarySetValue(properties,keys[i],value);CFRelease(value);
    }
    int good=1;glActiveTexture(GL_TEXTURE0);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,engine->framebuffer);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glClearColor(0,0,0,0);
    for(unsigned i=0;i<2 && good;++i){
        buffer->storage[i]=IOSurfaceCreate(properties);
        if(!buffer->storage[i]){good=0;break;}
        glGenTextures(1,buffer->textures+i);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,buffer->textures[i]);
        good=buffer->textures[i] && !DGPrivateTextureImport(engine->context,GL_TEXTURE_RECTANGLE_ARB,
            GL_RGBA32F_ARB,width,height,GL_RGBA,GL_FLOAT,buffer->storage[i],0);
        if(!good)break;
        DGPrivateTextureWatchName(buffer->watches+i,buffer->textures+i);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
            GL_TEXTURE_RECTANGLE_ARB,buffer->textures[i],0);
        good=glCheckFramebufferStatusEXT(GL_DRAW_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT;
        if(good)glClear(GL_COLOR_BUFFER_BIT);
    }
    glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,0,0);
    CFRelease(properties);
    good=good && glGetError()==GL_NO_ERROR;
    if(!good){DGAccumulationDeleteBuffer(buffer);return 0;}
    buffer->width=width;buffer->height=height;return 1;
}
static void DGAccumulationQuad(void)
{
    glBegin(GL_QUADS);glVertex2f(-1,-1);glVertex2f(1,-1);
    glVertex2f(1,1);glVertex2f(-1,1);glEnd();
}
/* An invalid operation must be rejected by the caller BEFORE this function.
 * clip is NULL when scissoring is disabled, otherwise {x,y,width,height}.
 * For RETURN, targets contains the actual selected color buffers and masks.
 * Other operations neither read nor modify application color write masks.
 */
static GLenum DGAccumulationExecute(struct DGAccumulationEngine *engine,
    struct DGAccumulationBuffer *buffer,GLsizei width,GLsizei height,GLenum operation,
    GLfloat value,const GLfloat clear[4],const GLint *clip,GLuint source,
    const struct DGAccumulationTarget *targets,unsigned target_count,GLboolean dither)
{
    CGLContextObj parent=CGLGetCurrentContext();
    if(!parent)return GL_INVALID_OPERATION;
    int mode=operation==DG_ACCUMULATION_CLEAR?0:operation==GL_LOAD?1:operation==GL_ACCUM?2:
        operation==GL_MULT?3:operation==GL_ADD?4:operation==GL_RETURN?5:-1;
    if(mode<0)return GL_INVALID_ENUM;
    if((mode==1||mode==2)&&!source)return GL_INVALID_OPERATION;
    GLsync producer=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    if(!producer)return GL_OUT_OF_MEMORY;
    glFlush();
    GLenum error=GL_NO_ERROR;
    if(!DGAccumulationInitialize(engine,parent))error=GL_OUT_OF_MEMORY;
    if(!error){
        glWaitSync(producer,0,GL_TIMEOUT_IGNORED);glDeleteSync(producer);producer=NULL;
        if(!DGAccumulationAllocate(engine,buffer,width,height))error=GL_OUT_OF_MEMORY;
    }
    if(!error){
        GLfloat bounds[]={0,0,(GLfloat)width,(GLfloat)height};
        if(clip){bounds[0]=(GLfloat)clip[0];bounds[1]=(GLfloat)clip[1];
            bounds[2]=(GLfloat)((double)clip[0]+clip[2]);bounds[3]=(GLfloat)((double)clip[1]+clip[3]);}
        glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,engine->framebuffer);
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glViewport(0,0,width,height);
        glUseProgram(engine->program);glUniform1i(engine->mode,mode);glUniform1f(engine->value,value);
        glUniform4fv(engine->bounds,1,bounds);
        if(clear)glUniform4fv(engine->clear,1,clear);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,buffer->textures[buffer->current]);
        // Even an untaken shader branch can make the driver validate its
        // sampler. Keep a complete, non-destination texture on both units.
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,
            source?source:buffer->textures[buffer->current]);
        if(mode==5){
            if(clip){glEnable(GL_SCISSOR_TEST);glScissor(clip[0],clip[1],clip[2],clip[3]);}
            else glDisable(GL_SCISSOR_TEST);
            if(dither)glEnable(GL_DITHER);else glDisable(GL_DITHER);
            for(unsigned i=0;i<target_count && !error;++i){
                glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
                    GL_TEXTURE_RECTANGLE_ARB,targets[i].texture,0);
                if(glCheckFramebufferStatusEXT(GL_DRAW_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT){error=GL_INVALID_FRAMEBUFFER_OPERATION_EXT;break;}
                const GLboolean *mask=targets[i].mask;glColorMask(mask[0],mask[1],mask[2],mask[3]);
                DGAccumulationQuad();
            }
        }else{
            glDisable(GL_SCISSOR_TEST);glDisable(GL_DITHER);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            unsigned next=buffer->current^1;
            glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
                GL_TEXTURE_RECTANGLE_ARB,buffer->textures[next],0);
            if(glCheckFramebufferStatusEXT(GL_DRAW_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)error=GL_INVALID_FRAMEBUFFER_OPERATION_EXT;
            else {DGAccumulationQuad();buffer->current=next;}
        }
        glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,0,0);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
    }
    GLsync completed=NULL;
    if(CGLGetCurrentContext()==engine->context){
        GLenum native=glGetError();if(!error)error=native;
        completed=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);glFlush();
        // Failure may still have queued a prefix: drain before retirement.
        if(!completed)glFinish();
    }
    if(CGLSetCurrentContext(parent))return GL_INVALID_OPERATION;
    if(producer)glDeleteSync(producer);
    if(completed){glWaitSync(completed,0,GL_TIMEOUT_IGNORED);glDeleteSync(completed);}
    return error;
}
#endif
