/* Actual native GL sync retirement, using the production completion helper.
 * The diagnostic holds the real wait-context mutex while another thread
 * deletes an older, actually completed fence. This must not need permission
 * from a wait on later work. No fake GPU completion or production timeout.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

struct DGHostContext {CGLContextObj cgl;struct DGCompletionQueue *completion_queue;};
#include "../PassthroughOpenGL/passthrough-opengl-completion-host.c"

struct retirement {
    struct DGCompletionQueue *queue;
    CGLContextObj caller;
    GLsync fence;
    pthread_mutex_t lock;
    pthread_cond_t condition;
    int entered,done;
};
static void *retire(void *opaque)
{
    struct retirement *r=opaque;
    assert(!CGLSetCurrentContext(r->caller));
    /* Cleanup must not consume or contaminate a caller's existing GL error. */
    if(r->caller)glEnable(0xffffffffu);
    pthread_mutex_lock(&r->lock);r->entered=1;
    pthread_cond_signal(&r->condition);pthread_mutex_unlock(&r->lock);
    DGCompletionQueueDelete(r->queue,r->fence);
    assert(CGLGetCurrentContext()==r->caller);
    if(r->caller){assert(glGetError()==GL_INVALID_ENUM);assert(!glGetError());}
    assert(!CGLSetCurrentContext(NULL));
    pthread_mutex_lock(&r->lock);r->done=1;
    pthread_cond_signal(&r->condition);pthread_mutex_unlock(&r->lock);
    return NULL;
}
static int retire_while_wait_locked(struct DGCompletionQueue *queue,CGLContextObj caller,GLsync fence)
{
    struct retirement r={.queue=queue,.caller=caller,.fence=fence,
        .lock=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
    pthread_mutex_lock(&queue->lock);
    pthread_t thread;assert(!pthread_create(&thread,NULL,retire,&r));
    pthread_mutex_lock(&r.lock);
    while(!r.entered)pthread_cond_wait(&r.condition,&r.lock);
    struct timeval now;assert(!gettimeofday(&now,NULL));
    struct timespec until={now.tv_sec+2,now.tv_usec*1000};
    int status=0;
    while(!r.done && !status)status=pthread_cond_timedwait(&r.condition,&r.lock,&until);
    assert(!status || status==ETIMEDOUT);int independent=r.done;
    pthread_mutex_unlock(&r.lock);
    /* Even a failed old implementation is unblocked/joined before reporting;
     * the diagnostic never leaves a worker or borrowed native context live. */
    pthread_mutex_unlock(&queue->lock);assert(!pthread_join(thread,NULL));
    pthread_cond_destroy(&r.condition);pthread_mutex_destroy(&r.lock);
    return independent;
}
int main(int argc,char **argv)
{
    int core=argc==2&&!strcmp(argv[1],"--core");
    assert(argc==1 || core);
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)(core?kCGLOGLPVersion_3_2_Core:kCGLOGLPVersion_Legacy),
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj format=NULL;GLint formats=0;
    assert(!CGLChoosePixelFormat(attributes,&format,&formats) && format && formats);
    struct DGHostContext producer={0};CGLContextObj shared=NULL,foreign=NULL;
    assert(!CGLCreateContext(format,NULL,&producer.cgl));
    assert(!CGLCreateContext(format,producer.cgl,&shared));
    assert(!CGLCreateContext(format,NULL,&foreign));CGLDestroyPixelFormat(format);
    assert(!CGLSetCurrentContext(producer.cgl));
    const char *renderer=(const char*)glGetString(GL_RENDERER);
    printf("renderer=%s profile=%s\n",renderer,core?"core":"legacy");
    assert(renderer && strstr(renderer,"Intel"));
    struct DGCompletionQueue *q=DGCompletionQueueGet(&producer);assert(q);
    CGLContextObj callers[]={producer.cgl,shared,foreign,NULL};
    const char *names[]={"producer","sharing","foreign","context-free"};
    unsigned failures=0;
    for(unsigned i=0;i<4;++i){
        assert(!CGLSetCurrentContext(producer.cgl));
        GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);assert(fence);glFlush();
        GLenum status=DGCompletionQueueWait(q,fence);
        assert(status==GL_ALREADY_SIGNALED || status==GL_CONDITION_SATISFIED);
        assert(glClientWaitSync(fence,0,0)==GL_ALREADY_SIGNALED && !glGetError());
        assert(!CGLSetCurrentContext(NULL));
        int independent=retire_while_wait_locked(q,callers[i],fence);
        printf("%s %s completed-fence retirement while wait-context mutex held\n",independent?"PASS":"FAIL",names[i]);
        failures+=!independent;
        assert(!CGLSetCurrentContext(producer.cgl));assert(!glIsSync(fence) && !glGetError());
    }
    /* Both the sync and auxiliary contexts outlive their producer connection. */
    GLsync last=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);assert(last);glFlush();
    GLenum status=DGCompletionQueueWait(q,last);
    assert(status==GL_ALREADY_SIGNALED || status==GL_CONDITION_SATISFIED);
    assert(!CGLSetCurrentContext(NULL));CGLDestroyContext(producer.cgl);producer.cgl=NULL;
    int independent=retire_while_wait_locked(q,NULL,last);failures+=!independent;
    printf("%s producer-closed context-free retirement\n",independent?"PASS":"FAIL");
    assert(!CGLSetCurrentContext(shared));assert(!glIsSync(last) && !glGetError());
    assert(!CGLSetCurrentContext(NULL));CGLDestroyContext(shared);CGLDestroyContext(foreign);
    DGCompletionQueuePut(q);DGCompletionQueuePut(producer.completion_queue);
    printf("%s native completion/retirement isolation, caller restoration and producer-close lifetime\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
