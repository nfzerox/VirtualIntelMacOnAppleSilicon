/* Public OpenGL control for the Tiger compositor's object wait calls.
 * The settled-texture cases have no new rendering commands. The dirty
 * controls queue a small update or an actual draw before each wait.
 * These are caller wall times, not GPU timestamps or a desktop FPS measure.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <mach/mach_time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { size=16, samples=512, warm=32 };
enum operation { test, finish, error, dirty_finish, draw_finish, flushed_draw_finish };

static double ticks_to_us;
static unsigned char pixels[size*size*4];

static void draw(void)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex2f(-1,-1);
    glTexCoord2f(1,0);glVertex2f(1,-1);
    glTexCoord2f(1,1);glVertex2f(1,1);
    glTexCoord2f(0,1);glVertex2f(-1,1);
    glEnd();
}

static int compare(const void *a,const void *b)
{
    uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b;
    return (x>y)-(x<y);
}

static int run(GLuint texture,enum operation op,const char *name)
{
    uint64_t elapsed[samples],total=0,with_submit=0;
    unsigned good=0;
    for(unsigned i=0;i<warm+samples;++i){
        uint64_t begin=mach_absolute_time();
        if(op==error)glPixelStorei(GL_UNPACK_ALIGNMENT,4);
        if(op==dirty_finish){
            pixels[0]=(unsigned char)i;
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,size,size,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        }
        if(op==draw_finish || op==flushed_draw_finish)draw();
        if(op==flushed_draw_finish)glFlush();
        uint64_t start=mach_absolute_time();
        int valid=1;
        if(op==test)valid=glTestObjectAPPLE(GL_TEXTURE,texture)==GL_TRUE;
        else if(op==error)valid=glGetError()==GL_NO_ERROR;
        else glFinishObjectAPPLE(GL_TEXTURE,texture);
        uint64_t end=mach_absolute_time(),duration=end-start;
        if(i>=warm){elapsed[i-warm]=duration;total+=duration;with_submit+=end-begin;good+=valid;}
        else if(!valid)return 0;
    }
    GLenum status=glGetError();
    qsort(elapsed,samples,sizeof(*elapsed),compare);
    printf("%s n=%u valid=%u error=%x mean_us=%.3f p50_us=%.3f p95_us=%.3f max_us=%.3f submit_and_wait_mean_us=%.3f\n",
        name,(unsigned)samples,good,(unsigned)status,ticks_to_us*total/samples,ticks_to_us*elapsed[samples/2-1],
        ticks_to_us*elapsed[(samples*95+99)/100-1],ticks_to_us*elapsed[samples-1],ticks_to_us*with_submit/samples);
    return good==samples && status==GL_NO_ERROR;
}

/* Each exact texture wait still happens before the fixture uses its results.
 * This only compares order for work with no CPU dependency between draws.
 * It is not permission to defer waits across an application's GL calls. */
static int grouped_draws(int deferred)
{
    enum { tiles=4 };
    GLuint textures[tiles]={0};
    unsigned char expected[tiles][4];
    uint64_t total=0,wait=0;
    int good=1;
    glGenTextures(tiles,textures);
    for(unsigned j=0;j<tiles;++j){
        expected[j][0]=29+j*37;
        expected[j][1]=151-j*33;
        expected[j][2]=44+j*46;
        expected[j][3]=255;
        for(size_t i=0;i<sizeof(pixels);i+=4)memcpy(pixels+i,expected[j],4);
        glBindTexture(GL_TEXTURE_2D,textures[j]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    }
    glClear(GL_COLOR_BUFFER_BIT);glFinish();
    if(glGetError()!=GL_NO_ERROR){good=0;goto done;}
    for(unsigned i=0;i<warm+samples;++i){
        uint64_t begin=mach_absolute_time(),waiting=0;
        for(unsigned j=0;j<tiles;++j){
            glViewport(j*size/tiles,0,size/tiles,size);
            glBindTexture(GL_TEXTURE_2D,textures[j]);
            draw();
            if(!deferred){
                uint64_t start=mach_absolute_time();
                glFinishObjectAPPLE(GL_TEXTURE,textures[j]);
                waiting+=mach_absolute_time()-start;
            }
        }
        if(deferred){
            uint64_t start=mach_absolute_time();
            for(unsigned j=0;j<tiles;++j)glFinishObjectAPPLE(GL_TEXTURE,textures[j]);
            waiting=mach_absolute_time()-start;
        }
        if(i>=warm){total+=mach_absolute_time()-begin;wait+=waiting;}
    }
    for(unsigned j=0;j<tiles;++j){
        unsigned char rgba[4]={0};
        glReadPixels((2*j+1)*size/(2*tiles),size/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
        good=good && !memcmp(rgba,expected[j],sizeof(rgba));
    }
    good=good && glGetError()==GL_NO_ERROR;
    printf("%s groups=%u draws=%u exact_waits=%u wait_mean_us=%.3f submit_and_wait_mean_us=%.3f pixel_pass=%d\n",
        deferred?"grouped":"interleaved",(unsigned)samples,(unsigned)tiles,(unsigned)tiles,
        ticks_to_us*wait/samples,ticks_to_us*total/samples,good);
done:
    glDeleteTextures(tiles,textures);
    return good;
}

int main(void)
{
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;CGLPBufferObj pbuffer=NULL;
    GLint count=0, hardware=0;
    GLuint texture=0,target=0,fbo=0;
    int good=0;
    mach_timebase_info_data_t timebase;
    if(mach_timebase_info(&timebase) || !timebase.denom)return 1;
    ticks_to_us=(double)timebase.numer/timebase.denom/1000;
    CGLError result=CGLChoosePixelFormat(attributes,&format,&count);
    if(!result && format && count)result=CGLDescribePixelFormat(format,0,kCGLPFAAccelerated,&hardware);
    if(!result && hardware)result=CGLCreateContext(format,NULL,&context);
    if(!result && context)result=CGLSetCurrentContext(context);
    if(result || !context || !hardware){
        fprintf(stderr,"An accelerated legacy context is required (CGL=%d).\n",(int)result);goto done;
    }
    // Match the period guest's real drawable. A current host can submit and
    // finish texture work on its native context without a legacy pbuffer.
#if defined(__i386__)
    result=CGLCreatePBuffer(size,size,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,0,&pbuffer);
    if(!result)result=CGLSetPBuffer(context,pbuffer,0,0,0);
    if(result){
        fprintf(stderr,"An accelerated period pbuffer is required (CGL=%d).\n",(int)result);goto done;
    }
#endif
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
    if(!renderer || !extensions || !strstr(extensions,"GL_APPLE_fence")){
        fprintf(stderr,"GL_APPLE_fence is unavailable.\n");goto done;
    }
    printf("renderer=%s hardware=%d\n",renderer,(int)hardware);
    glGenTextures(1,&texture);
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glFinish();
    if(!texture || !glIsTexture(texture) || glGetError()!=GL_NO_ERROR)goto done;
    good=run(texture,test,"settled-test") && run(texture,finish,"settled-finish") &&
         run(texture,error,"state-plus-error") && run(texture,dirty_finish,"updated-finish");
    if(!good)goto done;
    // Separate drawable from the sampled texture; validate the actual draw.
    // The period system already has its pbuffer. Current CGL needs an FBO.
#if !defined(__i386__)
    glGenTextures(1,&target);glBindTexture(GL_TEXTURE_2D,target);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,target,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT){good=0;goto done;}
#endif
    const unsigned char expected[4]={63,127,191,255};
    for(size_t i=0;i<sizeof(pixels);i+=4)memcpy(pixels+i,expected,4);
    glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glViewport(0,0,size,size);glEnable(GL_TEXTURE_2D);glDisable(GL_DITHER);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    good=glGetError()==GL_NO_ERROR && run(texture,draw_finish,"drawn-finish") &&
         run(texture,flushed_draw_finish,"drawn-flush-finish") &&
         run(texture,draw_finish,"drawn-finish-repeat");
    unsigned char observed[4]={0};
    glReadPixels(size/2,size/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,observed);
    good=good && glGetError()==GL_NO_ERROR && !memcmp(observed,expected,sizeof(observed));
    printf("draw-pixel=%u,%u,%u,%u pass=%d\n",observed[0],observed[1],observed[2],observed[3],good);
    good=good && grouped_draws(0) && grouped_draws(1) && grouped_draws(0);
done:
    if(fbo)glDeleteFramebuffersEXT(1,&fbo);
    if(target)glDeleteTextures(1,&target);
    if(texture)glDeleteTextures(1,&texture);
    if(context){CGLSetCurrentContext(NULL);CGLDestroyContext(context);}
    if(pbuffer)CGLDestroyPBuffer(pbuffer);
    if(format)CGLDestroyPixelFormat(format);
    return good?0:1;
}
