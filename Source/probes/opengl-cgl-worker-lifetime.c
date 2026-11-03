/* Valid CGL sequence: a worker unbinds, another thread destroys that context,
 * then the same worker binds a DIFFERENT context which was created earlier.
 * MallocScribble=1 makes an unowned driver TLS cache reproducibly observable.
 * No desktop, private GLI, injection, quality workaround or performance claim.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct Trial { CGLContextObj first,second;pthread_mutex_t lock;pthread_cond_t condition;int phase,good; };
static void *worker(void *opaque)
{
    struct Trial *t=opaque;
    t->good=!CGLSetCurrentContext(t->first);
    const char *name=(const char *)glGetString(GL_RENDERER);
    if(!name || !strstr(name,"Intel")){fprintf(stderr,"REFUSED non-Intel renderer: %s\n",name?name:"none");t->good=0;}
    glDisable(GL_DITHER);
    CGLSetCurrentContext(NULL);
    pthread_mutex_lock(&t->lock);t->phase=1;pthread_cond_signal(&t->condition);
    while(t->phase<2)pthread_cond_wait(&t->condition,&t->lock);
    pthread_mutex_unlock(&t->lock);
    if(CGLSetCurrentContext(t->second))t->good=0;
    glDisable(GL_BLEND);
    GLuint texture=0,framebuffer=0;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&framebuffer);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,framebuffer);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)t->good=0;
    glViewport(0,0,2,2);glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
    unsigned char pixel[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    GLenum error=glGetError();
    if(error || pixel[0]!=0 || pixel[1]!=255 || pixel[2]!=0){
        fprintf(stderr,"FAIL real FBO pixel=%u,%u,%u error=%x\n",pixel[0],pixel[1],pixel[2],error);t->good=0;
    }
    glDeleteFramebuffersEXT(1,&framebuffer);glDeleteTextures(1,&texture);
    CGLSetCurrentContext(NULL);
    return NULL;
}
int main(void)
{
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0,0,0,0};
    const char *renderer=getenv("VIM_PASSTHROUGH_OPENGL_RENDERER_ID");
    if(renderer){attributes[2]=kCGLPFASupportsAutomaticGraphicsSwitching;
        attributes[3]=kCGLPFARendererID;attributes[4]=(CGLPixelFormatAttribute)strtoul(renderer,NULL,0);}
    CGLPixelFormatObj format=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count) || !count)return 2;
    for(unsigned i=0;i<16;++i){
        struct Trial trial={0};pthread_mutex_init(&trial.lock,NULL);pthread_cond_init(&trial.condition,NULL);
        if(CGLCreateContext(format,NULL,&trial.first) || CGLCreateContext(format,NULL,&trial.second))return 2;
        pthread_t thread;if(pthread_create(&thread,NULL,worker,&trial))return 2;
        pthread_mutex_lock(&trial.lock);
        while(trial.phase<1)pthread_cond_wait(&trial.condition,&trial.lock);
        pthread_mutex_unlock(&trial.lock);
        CGLDestroyContext(trial.first);
        pthread_mutex_lock(&trial.lock);trial.phase=2;pthread_cond_signal(&trial.condition);pthread_mutex_unlock(&trial.lock);
        pthread_join(thread,NULL);CGLDestroyContext(trial.second);
        pthread_cond_destroy(&trial.condition);pthread_mutex_destroy(&trial.lock);
        if(!trial.good){fprintf(stderr,"FAIL worker lifetime/pixel trial=%u\n",i);return 1;}
    }
    CGLDestroyPixelFormat(format);puts("PASS sixteen cross-worker context retire/rebind + actual native green pixels");return 0;
}
