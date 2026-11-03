/* Real owning-task method writes, actual decoder and native GL state/pixels.
 * Compare bounded copy versus descriptor preparation in ONE guest boot. The
 * timed interval includes command consumption but NOT a GPU frame or query.
 * Never call these numbers desktop FPS. --baseline leaves policy untouched.
 */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include "direct-opcodes.h"
#include <sys/mman.h>
#include <signal.h>
#include <unistd.h>
static int failed;
static volatile sig_atomic_t cancelled;
static io_connect_t policy_device;
static uint64_t restore_mode;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);failed=1;}}while(0)
static int control(uint64_t mode,struct VIMGLFramebufferStats *stats)
{
    size_t size=sizeof(*stats);
    IOReturn error=IOConnectCallMethod(policy_device,kIOVM2DFramebufferBlitControl,
        &mode,1,NULL,0,NULL,NULL,stats,&size);
    return !error && size==sizeof(*stats) && stats->version==1;
}
static void restore(void)
{
    if(restore_mode){
        struct VIMGLFramebufferStats stats={0};
        if(!control(restore_mode,&stats)){fputs("STOP: original command-copy policy restoration failed\n",stderr);failed=1;}
        restore_mode=0;
    }
}
static void cancel(int number){(void)number;cancelled=1;}
static int compare(const void *a,const void *b)
{double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
static void viewport(struct DGContext *c,int x,int y)
{
    GLint actual[4]={0};c->common_state_valid=0; // Raw packets bypass client bookkeeping.
    glGetIntegerv(GL_VIEWPORT,actual);
    CHECK(actual[0]==x && actual[1]==y && actual[2]==16 && actual[3]==16);
    CHECK(glGetError()==GL_NO_ERROR && !c->connection->dead);
}
int main(int argc,char **argv)
{
    int baseline=argc==2 && !strcmp(argv[1],"--baseline");
    if(argc!=1 && !baseline)return 2;
    struct DGContext *c=DGCreateContext(NULL,NULL);
    if(!c || !c->connection->device){fputs("Requires the real guest graphics device\n",stderr);return 77;}
    unsigned char pixels[16*16*4]={0};CHECK(DGMakeCurrent(c,pixels,GL_UNSIGNED_BYTE,16,16));
    policy_device=c->connection->device;
    struct VIMGLFramebufferStats initial={0};CHECK(control(0,&initial));
    if(failed){DGDestroyContext(c);return 1;}
    unsigned initial_limit=((initial.flags>>16)&255u)*1024u;
    if(!baseline){
        if(initial_limit!=0 && initial_limit!=4096 && initial_limit!=32768 && initial_limit!=65536){
            fputs("Unknown original policy; no mutation made\n",stderr);DGDestroyContext(c);return 2;
        }
        restore_mode=initial_limit==0?9:initial_limit==4096?10:initial_limit==32768?11:12;
        atexit(restore);signal(SIGINT,cancel);signal(SIGTERM,cancel);
    }
    printf("renderer=%s original_limit=%u baseline=%d\n",glGetString(GL_RENDERER),initial_limit,baseline);
    mach_timebase_info_data_t tb;mach_timebase_info(&tb);
    const unsigned counts[]={1,28,29,64,227,228,455,456};
    const uint64_t modes[]={9,10,11,12,12,11,10,9};
    struct DGMessage *messages=calloc(456,sizeof(*messages));CHECK(messages);
    if(!messages){restore();DGDestroyContext(c);return 1;}
    for(unsigned phase=0;phase<(baseline?2u:8u) && !failed && !cancelled;++phase){
        struct VIMGLFramebufferStats selected={0};
        if(!baseline){CHECK(control(modes[phase],&selected));if(failed)break;}
        for(unsigned size=0;size<sizeof(counts)/sizeof(*counts) && !failed && !cancelled;++size){
            unsigned count=counts[size];
            for(unsigned i=0;i<count;++i){
                messages[i]=(struct DGMessage){.op=DG_OP_viewport,.context=c->id,.args={i%7,i%5,16,16}};
            }
            double times[32],total=0;unsigned measured=0;
            for(unsigned trial=0;trial<36 && !failed && !cancelled;++trial){
                uint64_t start=mach_absolute_time();
                int error=DGConnectionTransfer(c->connection,messages,count*sizeof(*messages),1);
                double us=(mach_absolute_time()-start)*(double)tb.numer/tb.denom/1000;
                CHECK(!error);viewport(c,(count-1)%7,(count-1)%5);
                if(trial>=4){times[measured++]=us;total+=us;}
            }
            if(measured){qsort(times,measured,sizeof(*times),compare);
                printf("phase=%u limit=%u bytes=%lu calls=%u mean_us=%.3f median_us=%.3f p95_us=%.3f\n",
                    phase,baseline?initial_limit:((selected.flags>>16)&255u)*1024u,
                    (unsigned long)(count*sizeof(*messages)),measured,total/measured,times[measured/2],times[(measured-1)*95/100]);}
        }
        glViewport(0,0,16,16);glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
        unsigned char pixel[4]={0};glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        CHECK(pixel[0]==255 && pixel[1]==0 && pixel[2]==255 && pixel[3]==255);
        CHECK(glGetError()==GL_NO_ERROR);
    }
    free(messages);DGFlushTransport(c);
    if(baseline){
        // Baseline qualifies timings/native results only. The old descriptor
        // path can consume a protected-page prefix before reporting failure;
        // it is NOT a passing oracle for the new pre-submission copyin gate.
        DGDestroyContext(c);policy_device=0;
        puts(failed?"FAIL real baseline state/pixels":"PASS real baseline state/pixels; source-fault gate is candidate-only");
        return failed?1:0;
    }
    struct VIMGLFramebufferStats selected={0};CHECK(control(11,&selected));
    struct VIMGLTransfer request={2,sizeof(struct DGMessage),1};
    struct VIMGLResult result={0};size_t bytes=sizeof(result);
    CHECK(IOConnectCallStructMethod(policy_device,kIOVM2DDirectGLTransfer,&request,sizeof(request),&result,&bytes)!=kIOReturnSuccess);
    size_t page=(size_t)getpagesize();
    char *guard=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(guard!=MAP_FAILED);
    if(guard!=MAP_FAILED){
        CHECK(!mprotect(guard+page,page,PROT_NONE));
        request.address=(uintptr_t)(guard+page-48);bytes=sizeof(result);
        CHECK(IOConnectCallStructMethod(policy_device,kIOVM2DDirectGLTransfer,&request,sizeof(request),&result,&bytes)!=kIOReturnSuccess);
        CHECK(!munmap(guard,page*2));
    }
    request.address=UINT64_MAX-7;bytes=sizeof(result);
    CHECK(IOConnectCallStructMethod(policy_device,kIOVM2DDirectGLTransfer,&request,sizeof(request),&result,&bytes)==kIOReturnBadArgument);
    // These failures must not submit even a prefix or corrupt the live stream.
    glViewport(2,3,16,16);viewport(c,2,3);
    restore();DGDestroyContext(c);policy_device=0;
    puts(failed?"FAIL real command-copy controls":"PASS real native state/pixels, unreadable/partial/wrapped sources, stream continuity and policy restoration");
    return cancelled?130:failed?1:0;
}
