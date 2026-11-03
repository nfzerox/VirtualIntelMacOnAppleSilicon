/* A producer owns a private shared CGL context for completion waits.
 * Versions retain it independently of the producer connection. Context-free
 * kernel/service threads never borrow a live application context or map its
 * pixels. Cleanup must not queue behind a wait for unrelated newer GPU work:
 * use a separately serialized lazy cleanup context. It never acknowledges
 * completion or releases RAM early, and does not disturb a caller's GL state.
 */
#ifdef VIM_BACKING_ASYNC_EXPERIMENT
#include <dispatch/dispatch.h>
#endif
struct DGCompletionQueue {
    CGLContextObj context;
    pthread_mutex_t lock;
    CGLContextObj cleanup_context;
    pthread_mutex_t cleanup_lock;
    unsigned refs;
#ifdef VIM_BACKING_ASYNC_EXPERIMENT
    /* Lazy, serial per producer: no per-frame thread creation and no global
     * head-of-line wait between unrelated producers. Jobs retain this queue.
     * No worker/timer exists for an idle completion context. */
    dispatch_queue_t backing_notifications;
#endif
};
/* Same-process diagnostic A/B only; no environment lookup or polling service.
 * A pending client wait spins in Apple's GLEngine. Retain the exact producer
 * dependency, materialize it in this private queue and let the native Finish
 * block instead. Qualified on actual current/period Intel, not inferred from
 * the word "server" in glWaitSync. */
/* Bit0: background completion queue. Bit1: writing/render queue. Both default
 * on: actual Tiger Finder drags on Intel more than doubled their presented
 * frame rate when the synchronous shared-backing copies stopped spinning in
 * GLEngine. The writing queue finishes its OWN submitted fence, then verifies
 * that fence; neither bit grants CPU access to unfinished GPU work. */
static volatile int dg_completion_wait_policy=3;
static GLenum DGCompletionQueueBlock(GLsync fence)
{
    GLenum status=glClientWaitSync(fence,0,0);
    if(status!=GL_TIMEOUT_EXPIRED)return status;
    glWaitSync(fence,0,GL_TIMEOUT_IGNORED);
    /* Bare WaitSync+Finish on an otherwise empty context returns early on
     * both tested Intel drivers. A REAL queued end fence is essential; never
     * acknowledge producer completion based on Finish's return alone. */
    GLsync end=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    if(!end)return GL_WAIT_FAILED;
    glFlush();glFinish();
    GLenum error=glGetError();
    if(!error)status=glClientWaitSync(fence,0,0);
    glDeleteSync(end);
    return error?GL_WAIT_FAILED:status;
}
static struct DGCompletionQueue *DGCompletionQueueGet(struct DGHostContext *c)
{
    if(!c->completion_queue){
        struct DGCompletionQueue *q=calloc(1,sizeof(*q));if(!q)return NULL;
        if(pthread_mutex_init(&q->lock,NULL)){free(q);return NULL;}
        if(pthread_mutex_init(&q->cleanup_lock,NULL)){
            pthread_mutex_destroy(&q->lock);free(q);return NULL;
        }
        if(CGLCreateContext(CGLGetPixelFormat(c->cgl),c->cgl,&q->context)){
            pthread_mutex_destroy(&q->cleanup_lock);pthread_mutex_destroy(&q->lock);free(q);return NULL;
        }
        q->refs=1;c->completion_queue=q;
    }
    __sync_add_and_fetch(&c->completion_queue->refs,1);return c->completion_queue;
}
static void DGCompletionQueuePut(struct DGCompletionQueue *q)
{
    if(!q || __sync_sub_and_fetch(&q->refs,1))return;
#ifdef VIM_BACKING_ASYNC_EXPERIMENT
    if(q->backing_notifications)dispatch_release(q->backing_notifications);
#endif
    if(q->cleanup_context)CGLDestroyContext(q->cleanup_context);
    CGLDestroyContext(q->context);pthread_mutex_destroy(&q->cleanup_lock);
    pthread_mutex_destroy(&q->lock);free(q);
}
/* The caller retains q and fence. A timeout means still pending, not failure
 * or permission to release storage. There is no polling when no job exists. */
static GLenum DGCompletionQueueWait(struct DGCompletionQueue *q,GLsync fence)
{
    if(!q || !fence)return GL_WAIT_FAILED;
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
    struct DGCommandProfileStamp profile=DGCommandProfileBegin();
#endif
    pthread_mutex_lock(&q->lock);CGLContextObj previous=CGLGetCurrentContext();
    GLenum status=GL_WAIT_FAILED;
    if(!CGLSetCurrentContext(q->context)){
        status=(dg_completion_wait_policy&1)?DGCompletionQueueBlock(fence):
            glClientWaitSync(fence,0,20000000);
        CGLSetCurrentContext(NULL);
    }
    if(previous)CGLSetCurrentContext(previous);
    pthread_mutex_unlock(&q->lock);
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
    DGCommandProfileEnd(profile,DG_PROFILE_COMPLETION_WAIT,0,0);
#endif
    return status;
}
struct DGCompletionCleanup {
    CGLContextObj previous;
    pthread_mutex_t *lock;
};
/* Caller retains q and the GL objects. These are share-group objects, not
 * context-local FBO/VAO names. Never borrow a producer, even on this thread:
 * a caller may have an unfinished Begin/End or a pending native error.
 * At most ONE extra context per producer, created only if context-free or
 * foreign-group cleanup actually needs it. Native creation failure keeps the
 * old conservative wait-context path rather than losing resource ownership.
 */
static int DGCompletionCleanupBegin(struct DGCompletionQueue *q,struct DGCompletionCleanup *s)
{
    *s=(struct DGCompletionCleanup){CGLGetCurrentContext(),NULL};
    if(!q)return 0;
    pthread_mutex_lock(&q->cleanup_lock);
    if(!q->cleanup_context)
        CGLCreateContext(CGLGetPixelFormat(q->context),q->context,&q->cleanup_context);
    if(q->cleanup_context && !CGLSetCurrentContext(q->cleanup_context)){
        s->lock=&q->cleanup_lock;return 1;
    }
    CGLSetCurrentContext(s->previous);pthread_mutex_unlock(&q->cleanup_lock);
    pthread_mutex_lock(&q->lock);
    if(!CGLSetCurrentContext(q->context)){s->lock=&q->lock;return 1;}
    CGLSetCurrentContext(s->previous);pthread_mutex_unlock(&q->lock);return 0;
}
static void DGCompletionCleanupEnd(struct DGCompletionCleanup *s)
{
    if(!s->lock)return;
    CGLSetCurrentContext(NULL);
    if(s->previous)CGLSetCurrentContext(s->previous);
    pthread_mutex_unlock(s->lock);
}
static void DGCompletionQueueDelete(struct DGCompletionQueue *q,GLsync fence)
{
    if(!q || !fence)return;
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
    struct DGCommandProfileStamp profile=DGCommandProfileBegin();
#endif
    struct DGCompletionCleanup cleanup;
    if(DGCompletionCleanupBegin(q,&cleanup)){
        glDeleteSync(fence);DGCompletionCleanupEnd(&cleanup);
    }
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
    DGCommandProfileEnd(profile,DG_PROFILE_COMPLETION_DELETE,0,0);
#endif
}
