/* Real device comparison: larger request, SAME native GL execution/replies.
 * Batch+query wall time is not GPU execution, desktop FPS or input latency.
 * Old providers must retain the4KiB path without failure/replay negotiation.
 */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include <sys/mman.h>
#include <unistd.h>
static int failed;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);failed=1;}}while(0)
static int compare(const void *a,const void *b)
{double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
int main(int argc,char **argv)
{
    int require=argc==2 && !strcmp(argv[1],"--require-large");
    if(argc>1 && !require)return 2;
    struct DGContext *c=DGCreateContext(NULL,NULL);
    if(!c || !c->connection->device)return 77;
    unsigned char pixels[16*16*4]={0};CHECK(DGMakeCurrent(c,pixels,GL_UNSIGNED_BYTE,16,16));
    printf("renderer=%s large_request=%u\n",glGetString(GL_RENDERER),c->connection->large_exchange_supported);
    if(require && !c->connection->large_exchange_supported){DGDestroyContext(c);return 77;}
    const unsigned counts[]={28,29,64,227,228,453,454,455};
    mach_timebase_info_data_t tb;mach_timebase_info(&tb);
    for(unsigned phase=0;phase<4 && !failed;++phase){
        dg_large_exchange_override=phase&1;
        for(unsigned size=0;size<sizeof(counts)/sizeof(*counts) && !failed;++size){
            unsigned n=counts[size];double times[40],total=0;
            for(unsigned trial=0;trial<44 && !failed;++trial){
                GLint actual[4]={0};uint64_t at=mach_absolute_time();
                for(unsigned i=0;i<n;++i)glViewport(i%7,i%5,16,16);
                glGetIntegerv(GL_VIEWPORT,actual);
                double us=(mach_absolute_time()-at)*(double)tb.numer/tb.denom/1000;
                CHECK(actual[0]==(GLint)((n-1)%7) && actual[1]==(GLint)((n-1)%5) && actual[2]==16 && actual[3]==16);
                CHECK(!c->connection->dead && glGetError()==GL_NO_ERROR);
                if(trial>=4){times[trial-4]=us;total+=us;}
            }
            if(!failed){qsort(times,40,sizeof(*times),compare);
                printf("phase=%u large=%d pending_bytes=%lu count=40 mean_us=%.3f median_us=%.3f p95_us=%.3f\n",
                    phase,dg_large_exchange_override,(unsigned long)(n*sizeof(struct DGMessage)),total/40,times[20],times[38]);}
        }
        glViewport(0,0,16,16);glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
        unsigned char pixel[4]={0};glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        CHECK(pixel[0]==255 && !pixel[1] && pixel[2]==255 && pixel[3]==255);
        glEnable(0xffffffffu);CHECK(glGetError()==GL_INVALID_ENUM && glGetError()==GL_NO_ERROR);
    }
    DGFlushTransport(c);
    /* Rejected bounds/faults must consume NO prefix of this actual stream. */
    size_t page=(size_t)getpagesize(),bytes=VIM_GL_LARGE_EXCHANGE_LIMIT+page;
    char *input=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    CHECK(input!=MAP_FAILED);
    if(input!=MAP_FAILED){
        struct VIMGLExchange *header=(struct VIMGLExchange*)input;
        struct DGReply reply={0};
        *header=(struct VIMGLExchange){1,8192-sizeof(*header),sizeof(reply),0};
        CHECK(IOConnectTrap4(c->connection->device,VIM_GL_EXCHANGE_TRAP,(uintptr_t)input,
            VIM_GL_LARGE_EXCHANGE_LIMIT+1,(uintptr_t)&reply,sizeof(reply))==kIOReturnBadArgument);
        CHECK(IOConnectTrap4(c->connection->device,VIM_GL_EXCHANGE_TRAP,(uintptr_t)input,
            8192,(uintptr_t)&reply,VIM_GL_EXCHANGE_LIMIT+1)==kIOReturnBadArgument);
        CHECK(!mprotect(input+page,page,PROT_NONE));
        IOReturn error=IOConnectTrap4(c->connection->device,VIM_GL_EXCHANGE_TRAP,
            (uintptr_t)input,8192,(uintptr_t)&reply,sizeof(reply));
        CHECK(error==(c->connection->large_exchange_supported?kIOReturnVMError:kIOReturnBadArgument));
        CHECK(!munmap(input,bytes));
    }
    glViewport(2,3,16,16);GLint actual[4]={0};glGetIntegerv(GL_VIEWPORT,actual);
    CHECK(actual[0]==2 && actual[1]==3 && glGetError()==GL_NO_ERROR && !c->connection->dead);
    dg_large_exchange_override=1;DGDestroyContext(c);
    puts(failed?"FAIL actual large-exchange controls":"PASS actual native state/error/pixels, bounds/source faults, ordered continuity and restored policy");
    return failed?1:0;
}
