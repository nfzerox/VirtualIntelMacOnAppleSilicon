/* Actual production auxiliary completion queue, isolated public-GL work.
 * Compare complete wall AND calling-thread CPU time, never infer desktop FPS.
 * Each wait must observe the actual producer fence, preserve the previous
 * context, and permit exact producer-pixel validation. No guest/driver install.
 * Build universally on current host; also build with period Xcode5 on the
 * physical Mavericks Intel reference. Reject Float/Nvidia as the oracle.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <mach/mach.h>
#include <mach/mach_time.h>

struct DGHostContext {CGLContextObj cgl;struct DGCompletionQueue *completion_queue;};
#include "../PassthroughOpenGL/passthrough-opengl-completion-host.c"

/* Research alternatives deliberately live ONLY in the probe. They are not
 * production switches or assumed safe substitutes for CPU ownership. */
static GLenum alternative_wait(struct DGCompletionQueue *q,GLsync fence,GLuint texture,unsigned mode)
{
    pthread_mutex_lock(&q->lock);CGLContextObj previous=CGLGetCurrentContext();
    GLenum status=GL_WAIT_FAILED;
    if(!CGLSetCurrentContext(q->context)){
        status=glClientWaitSync(fence,0,0);
        if(status==GL_TIMEOUT_EXPIRED){
            if(mode==1){
                /* Finish alone on an unrelated context is insufficient. */
                glWaitSync(fence,0,GL_TIMEOUT_IGNORED);glFinish();
            }else if(mode==2){
                assert(mode==2);glFinishObjectAPPLE(GL_TEXTURE,texture);
            }else if(mode==3){
                glWaitSync(fence,0,GL_TIMEOUT_IGNORED);
                GLsync end=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
                assert(end);glFlush();glFinish();glDeleteSync(end);
            }else {
                assert(mode==4);glWaitSync(fence,0,GL_TIMEOUT_IGNORED);
                GLuint end=0;glGenFencesAPPLE(1,&end);assert(end);
                glSetFenceAPPLE(end);glFlush();glFinishFenceAPPLE(end);glDeleteFencesAPPLE(1,&end);
            }
            GLenum error=glGetError();
            /* Still require the ACTUAL producer fence to signal. */
            status=error?GL_WAIT_FAILED:glClientWaitSync(fence,0,0);
        }
        CGLSetCurrentContext(NULL);
    }
    if(previous)CGLSetCurrentContext(previous);
    pthread_mutex_unlock(&q->lock);return status;
}

static double now_us(void)
{
    static mach_timebase_info_data_t t;
    if(!t.denom)assert(!mach_timebase_info(&t));
    return (double)mach_absolute_time()*t.numer/t.denom/1000;
}
static double thread_us(void)
{
    thread_basic_info_data_t info;mach_msg_type_number_t n=THREAD_BASIC_INFO_COUNT;
    mach_port_t thread=mach_thread_self();
    kern_return_t k=thread_info(thread,THREAD_BASIC_INFO,(thread_info_t)&info,&n);
    mach_port_deallocate(mach_task_self(),thread);assert(!k);
    return (info.user_time.seconds+info.system_time.seconds)*1000000.0+
        info.user_time.microseconds+info.system_time.microseconds;
}
static int compare_double(const void *a,const void *b)
{double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}

static void workload(unsigned draws,unsigned phase,unsigned iteration,int core)
{
    if(core){
        for(unsigned j=0;j<draws;++j){
            glClearColor((j+1)/255.0f,(j+1)*2/255.0f,(j+1)*(1+(phase+iteration)%3)/255.0f,1);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        return;
    }
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);
    /* Exact UNORM increments (no dither). A different blue increment each
     * iteration catches old results. Saturation is intentional and checked. */
    unsigned blue=1+(phase+iteration)%3;
    glColor4ub(1,2,(GLubyte)blue,0);
    for(unsigned j=0;j<draws;++j){
        glBegin(GL_QUADS);glVertex2f(-1,-1);glVertex2f(1,-1);
        glVertex2f(1,1);glVertex2f(-1,1);glEnd();
    }
    glDisable(GL_BLEND);
}
static void verify_pixels(unsigned width,unsigned height,unsigned draws,unsigned phase,unsigned iteration)
{
    const unsigned positions[3][2]={{1,1},{width/2,height/2},{width-2,height-2}};
    unsigned expected[4]={draws,draws*2,draws*(1+(phase+iteration)%3),255};
    for(unsigned c=0;c<4;++c)if(expected[c]>255)expected[c]=255;
    for(unsigned j=0;j<3;++j){
        GLubyte p[4]={0};glReadPixels((GLint)positions[j][0],(GLint)positions[j][1],1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
        for(unsigned c=0;c<4;++c)if(p[c]!=expected[c]){
            fprintf(stderr,"FAIL phase=%u iteration=%u point=%u channel=%u value=%u expected=%u\n",
                phase,iteration,j,c,p[c],expected[c]);abort();
        }
    }
    assert(!glGetError());
}
int main(int argc,char **argv)
{
    enum {width=2048,height=1024,draws=32,runs=40};
    int core=argc==2&&!strcmp(argv[1],"--core");
    int compare_alternatives=argc==2&&!strcmp(argv[1],"--alternatives");
    if(argc>2 || (argc==2&&!compare_alternatives&&!core)){
        fprintf(stderr,"Usage: %s [--alternatives|--core]\n",argv[0]);return 2;
    }
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)(core?kCGLOGLPVersion_3_2_Core:kCGLOGLPVersion_Legacy),
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;struct DGHostContext c={0};
    assert(!CGLChoosePixelFormat(attrs,&pf,&count)&&pf&&count);
    assert(!CGLCreateContext(pf,NULL,&c.cgl));CGLDestroyPixelFormat(pf);
    assert(!CGLSetCurrentContext(c.cgl));
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    printf("renderer=%s profile=%s geometry=%ux%u draws=%u runs=%u\n",renderer,core?"core":"legacy",width,height,draws,runs);
    assert(renderer&&strstr(renderer,"Intel"));
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glViewport(0,0,width,height);glDisable(GL_DITHER);assert(!glGetError());
    struct DGCompletionQueue *q=DGCompletionQueueGet(&c);assert(q);
    const unsigned production_modes[]={0,5,5,0},alternatives[]={0,1,2,3,4,4,3,2,1,0};
    const unsigned *modes=compare_alternatives?alternatives:production_modes;
    unsigned phases=compare_alternatives?sizeof(alternatives)/sizeof(*alternatives):
        sizeof(production_modes)/sizeof(*production_modes);
    for(unsigned phase=0;phase<phases;++phase){
        double wall[runs],cpu[runs],total_wall=0,total_cpu=0;
        unsigned wait_calls=0,timeouts=0,alternative_incomplete=0;
        unsigned mode=modes[phase];
        dg_completion_wait_policy=mode==5?1:0;
        for(unsigned i=0;i<runs;++i){
            workload(draws,phase,i,core);
            GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);assert(fence);glFlush();assert(!glGetError());
            double before_cpu=thread_us(),before_wall=now_us();GLenum status;
            status=mode&&mode!=5?alternative_wait(q,fence,texture,mode):GL_TIMEOUT_EXPIRED;
            alternative_incomplete+=mode&&mode!=5&&status==GL_TIMEOUT_EXPIRED;
            while(status==GL_TIMEOUT_EXPIRED){
                /* A candidate that returns before the producer completes is
                 * NOT repeatedly spun. Record that rejection and fall back to
                 * the original real wait, never fabricated completion. */
                status=DGCompletionQueueWait(q,fence);
                ++wait_calls;timeouts+=status==GL_TIMEOUT_EXPIRED;
            }
            wall[i]=now_us()-before_wall;cpu[i]=thread_us()-before_cpu;
            assert(status==GL_ALREADY_SIGNALED||status==GL_CONDITION_SATISFIED);
            assert(CGLGetCurrentContext()==c.cgl);
            /* Verify after timing, BEFORE deleting the fence. ReadPixels is
             * an additional pixel check, not the evidence of completed wait. */
            assert(glClientWaitSync(fence,0,0)==GL_ALREADY_SIGNALED);
            verify_pixels(width,height,draws,phase,i);
            DGCompletionQueueDelete(q,fence);assert(CGLGetCurrentContext()==c.cgl);
            total_wall+=wall[i];total_cpu+=cpu[i];
        }
        qsort(wall,runs,sizeof(*wall),compare_double);qsort(cpu,runs,sizeof(*cpu),compare_double);
        printf("phase=%u wait_mode=%u wait_wall_mean_us=%.2f median_us=%.2f p90_us=%.2f wait_thread_cpu_mean_us=%.2f median_us=%.2f p90_us=%.2f wait_calls=%u timeouts=%u alternative_incomplete=%u\n",
            phase,mode,total_wall/runs,wall[runs/2],wall[runs*9/10],
            total_cpu/runs,cpu[runs/2],cpu[runs*9/10],wait_calls,timeouts,alternative_incomplete);fflush(stdout);
    }
    /* Repeat the already-ready path with no current caller context. */
    GLsync ready=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);assert(ready);glFinish();
    assert(!CGLSetCurrentContext(NULL));
    for(unsigned mode=0;mode<6;++mode){
        dg_completion_wait_policy=mode==5?1:0;
        GLenum status=mode&&mode!=5?alternative_wait(q,ready,texture,mode):DGCompletionQueueWait(q,ready);
        assert(status==GL_ALREADY_SIGNALED||status==GL_CONDITION_SATISFIED);
        assert(!CGLGetCurrentContext());
    }
    DGCompletionQueueDelete(q,ready);assert(!CGLGetCurrentContext());
    DGCompletionQueuePut(q);DGCompletionQueuePut(c.completion_queue);
    assert(!CGLSetCurrentContext(c.cgl));glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    assert(!glGetError());CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
    puts("PASS real shared-fence completion, independent queue, caller-context restoration, pixels, ready/null-context path and cleanup");
    return 0;
}
