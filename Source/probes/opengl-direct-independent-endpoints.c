/* Actual guest device/native GL: an independent producer submits additive
 * rendering + Finish while another endpoint queries an unrelated native
 * limit. Compare serial/parallel doorbell policies without changing payloads.
 * API-return overlap and query latency are NOT desktop FPS or GPU timestamps.
 * Final producer pixels and both endpoints' native errors remain assertions.
 */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include <assert.h>

enum { Width=2048,Height=1024,Draws=32,Rounds=8,Queries=256 };
static unsigned workload_draws=Draws;
static struct {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    unsigned ready,go,finishing,done;
    uint64_t finish_start[Rounds],finish_end[Rounds];
} control={.lock=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};

static struct DGContext *context(unsigned width,unsigned height,void **pixels)
{
    *pixels=calloc((size_t)width*height,4);assert(*pixels);
    struct DGContext *c=DGCreateContext(NULL,NULL);assert(c);
    assert(DGMakeCurrent(c,*pixels,GL_UNSIGNED_BYTE,width,height));
    const char *renderer=(const char*)glGetString(GL_RENDERER);
    assert(renderer && strstr(renderer,"Intel"));
    printf("endpoint renderer=%s geometry=%ux%u\n",renderer,width,height);
    return c;
}
static void *producer(void *unused)
{
    (void)unused;void *pixels=NULL;struct DGContext *c=context(Width,Height,&pixels);
    glViewport(0,0,Width,Height);glDisable(GL_DITHER);
    pthread_mutex_lock(&control.lock);control.ready=1;pthread_cond_broadcast(&control.condition);
    pthread_mutex_unlock(&control.lock);
    for(unsigned round=0;round<Rounds;++round){
        pthread_mutex_lock(&control.lock);
        while(control.go<=round)pthread_cond_wait(&control.condition,&control.lock);
        pthread_mutex_unlock(&control.lock);
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);glColor4ub(1,2,(GLubyte)(1+round%3),0);
        for(unsigned i=0;i<workload_draws;++i){
            glBegin(GL_QUADS);glVertex2f(-1,-1);glVertex2f(1,-1);
            glVertex2f(1,1);glVertex2f(-1,1);glEnd();
        }
        glDisable(GL_BLEND);
        pthread_mutex_lock(&control.lock);control.finishing=round+1;
        control.finish_start[round]=mach_absolute_time();pthread_cond_broadcast(&control.condition);
        pthread_mutex_unlock(&control.lock);
        glFinish();uint64_t ended=mach_absolute_time();assert(!glGetError());
        unsigned char p[4]={0};glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
        unsigned expected[]={workload_draws,workload_draws*2,workload_draws*(1+round%3),255};
        for(unsigned channel=0;channel<4;++channel){
            if(expected[channel]>255)expected[channel]=255;
            assert(p[channel]==expected[channel]);
        }
        assert(!glGetError());
        pthread_mutex_lock(&control.lock);control.finish_end[round]=ended;control.done=round+1;
        pthread_cond_broadcast(&control.condition);pthread_mutex_unlock(&control.lock);
    }
    DGDestroyContext(c);free(pixels);return NULL;
}
static int compare(const void *a,const void *b)
{double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
int main(int argc,char **argv)
{
    if(argc!=2 || (strcmp(argv[1],"--real-device") && strcmp(argv[1],"--real-device-slow"))){
        fputs("usage: opengl-direct-independent-endpoints --real-device|--real-device-slow\n",stderr);return 2;
    }
    /* Positive coverage control for the explicitly armed >20-ms host I/O
     * diagnostic. Still bounded, with actual GPU work/error/pixel assertions.
     * Never mix this artificial load into desktop qualification runs. */
    if(!strcmp(argv[1],"--real-device-slow"))workload_draws=128;
    printf("producer_draws=%u\n",workload_draws);
    mach_timebase_info_data_t tb;assert(!mach_timebase_info(&tb));double us=(double)tb.numer/tb.denom/1000;
    void *pixels=NULL;struct DGContext *c=context(16,16,&pixels);
    assert(c->connection->device);GLint expected=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&expected);
    assert(expected>=Width && !glGetError());
    pthread_t thread;assert(!pthread_create(&thread,NULL,producer,NULL));
    pthread_mutex_lock(&control.lock);
    while(!control.ready)pthread_cond_wait(&control.condition,&control.lock);
    pthread_mutex_unlock(&control.lock);
    for(unsigned round=0;round<Rounds;++round){
        pthread_mutex_lock(&control.lock);control.go=round+1;pthread_cond_broadcast(&control.condition);
        while(control.finishing<=round)pthread_cond_wait(&control.condition,&control.lock);
        pthread_mutex_unlock(&control.lock);
        uint64_t start[Queries],end[Queries];double latency[Queries];
        for(unsigned i=0;i<Queries;++i){
            GLint value=0;start[i]=mach_absolute_time();glGetIntegerv(GL_MAX_TEXTURE_SIZE,&value);
            end[i]=mach_absolute_time();assert(value==expected && !c->connection->dead);
            latency[i]=(end[i]-start[i])*us;
        }
        assert(!glGetError());pthread_mutex_lock(&control.lock);
        while(control.done<=round)pthread_cond_wait(&control.condition,&control.lock);
        uint64_t began=control.finish_start[round],ended=control.finish_end[round];
        pthread_mutex_unlock(&control.lock);
        unsigned overlap=0;
        for(unsigned i=0;i<Queries;++i)overlap+=start[i]>=began && end[i]<ended;
        qsort(latency,Queries,sizeof(*latency),compare);
        printf("round=%u actual_finish_us=%.3f queries=%u query_median_us=%.3f p95_us=%.3f maximum_us=%.3f completed_within_other_finish_api_span=%u\n",
            round,(ended-began)*us,Queries,latency[Queries/2],latency[Queries*95/100],latency[Queries-1],overlap);
    }
    assert(!pthread_join(thread,NULL));DGDestroyContext(c);free(pixels);
    pthread_cond_destroy(&control.condition);pthread_mutex_destroy(&control.lock);
    puts("PASS independent real device streams: native limits/errors and all producer GPU pixels; timing/overlap alone is NOT desktop qualification");
    return 0;
}
