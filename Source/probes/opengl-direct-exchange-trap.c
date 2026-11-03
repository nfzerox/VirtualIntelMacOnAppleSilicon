/* Actual device qualification, not a replacement transport implementation.
 * Native GL values/errors/rendering must match through the old method and
 * negotiated trap. Timings are ordered query round trips, NOT desktop FPS.
 */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include <sys/mman.h>
#include <unistd.h>
static int failed;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);failed=1;} } while(0)
static int compare_double(const void *a,const void *b)
{ double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y); }
int main(void)
{
    struct DGContext *c=DGCreateContext(NULL,NULL);
    if(!c || !c->connection->device){fputs("Requires the real guest graphics device\n",stderr);return 77;}
    unsigned char pixels[16*16*4]={0};CHECK(DGMakeCurrent(c,pixels,GL_UNSIGNED_BYTE,16,16));
    printf("transport=%s trap=%u renderer=%s\n",DGTransportName(),
        c->connection->exchange_trap_supported,glGetString(GL_RENDERER));
    GLint expected=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&expected);CHECK(expected>=1024);
    mach_timebase_info_data_t timebase;mach_timebase_info(&timebase);
    for(int phase=0;phase<4;++phase){
        dg_exchange_trap_override=phase&1;
        for(int i=0;i<20;++i){GLint value=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&value);CHECK(value==expected);}
        double times[200],total=0;
        for(int i=0;i<200;++i){
            uint64_t before=mach_absolute_time();GLint value=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&value);
            times[i]=(mach_absolute_time()-before)*(double)timebase.numer/timebase.denom/1000.0;
            total+=times[i];CHECK(value==expected);
        }
        qsort(times,200,sizeof(*times),compare_double);
        printf("phase=%d trap=%d count=200 mean_us=%.3f median_us=%.3f p95_us=%.3f\n",
            phase,dg_exchange_trap_override,total/200,times[100],times[190]);
        glEnable(0xffffffffu);CHECK(glGetError()==GL_INVALID_ENUM && glGetError()==GL_NO_ERROR);
        glViewport(0,0,16,16);glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
        unsigned char pixel[4]={0};glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        CHECK(pixel[0]==255 && pixel[1]==0 && pixel[2]==255 && pixel[3]==255);
        CHECK(glGetError()==GL_NO_ERROR && !c->connection->dead);
    }
    if(c->connection->exchange_trap_supported){
        DGFlushTransport(c); // Invalid requests below must consume no stream bytes.
        struct {struct VIMGLExchange header;struct DGMessage message;} input={0};
        struct DGReply output={0};
        input.header=(struct VIMGLExchange){2,sizeof(input.message),sizeof(output),0};
        io_connect_t device=c->connection->device;
        CHECK(IOConnectTrap4(device,VIM_GL_EXCHANGE_TRAP,(uintptr_t)&input,sizeof(input),
            (uintptr_t)&output,sizeof(output))==kIOReturnBadArgument);
        input.header.version=1;input.header.reserved=1;
        CHECK(IOConnectTrap4(device,VIM_GL_EXCHANGE_TRAP,(uintptr_t)&input,sizeof(input),
            (uintptr_t)&output,sizeof(output))==kIOReturnBadArgument);
        input.header.reserved=0;
        CHECK(IOConnectTrap4(device,VIM_GL_EXCHANGE_TRAP,1,sizeof(input),
            (uintptr_t)&output,sizeof(output))==kIOReturnVMError);
        CHECK(IOConnectTrap4(device,VIM_GL_EXCHANGE_TRAP,(uintptr_t)&input,sizeof(input),
            (uintptr_t)&output,sizeof(output)+1)==kIOReturnBadArgument);
        CHECK(IOConnectTrap4(device,VIM_GL_EXCHANGE_TRAP,(uintptr_t)&input,SIZE_MAX,
            (uintptr_t)&output,sizeof(output))==kIOReturnBadArgument);
        CHECK(IOConnectTrap6(device,VIM_GL_EXCHANGE_TRAP,(uintptr_t)&input,sizeof(input),
            (uintptr_t)&output,sizeof(output),1,0)==kIOReturnBadArgument);
        CHECK(IOConnectTrap4(device,VIM_GL_EXCHANGE_TRAP+1,(uintptr_t)&input,sizeof(input),
            (uintptr_t)&output,sizeof(output))==kIOReturnBadArgument);
        size_t page=(size_t)getpagesize();
        char *guard=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
        CHECK(guard!=MAP_FAILED);
        if(guard!=MAP_FAILED){
            CHECK(!mprotect(guard+page,page,PROT_NONE));
            memcpy(guard+page-sizeof(input.header),&input.header,sizeof(input.header));
            CHECK(IOConnectTrap4(device,VIM_GL_EXCHANGE_TRAP,(uintptr_t)(guard+page-sizeof(input.header)),
                sizeof(input),(uintptr_t)&output,sizeof(output))==kIOReturnVMError);
            CHECK(!munmap(guard,page*2));
        }
        GLint value=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&value);
        CHECK(value==expected && glGetError()==GL_NO_ERROR && !c->connection->dead);
        puts("invalid shape, unreadable header/payload, reserved args and index preserve stream ordering");
        // A bad output pointer fails AFTER execution. Demonstrate why the
        // production client must fail closed, never replay via the method.
        // This raw fault is confined to a separately owned test connection.
        struct DGContext *fault=DGCreateContext(NULL,NULL);CHECK(fault);
        if(fault){
            CHECK(DGMakeCurrent(fault,pixels,GL_UNSIGNED_BYTE,16,16));
            glEnable(0xffffffffu);DGFlushTransport(fault);
            memset(&input,0,sizeof(input));
            input.header=(struct VIMGLExchange){1,sizeof(input.message),sizeof(output),0};
            input.message.op=DG_ERROR;input.message.flags=DG_REPLY;input.message.context=fault->id;
            CHECK(IOConnectTrap4(fault->connection->device,VIM_GL_EXCHANGE_TRAP,(uintptr_t)&input,
                sizeof(input),1,sizeof(output))==kIOReturnVMError);
            CHECK(glGetError()==GL_NO_ERROR); // the actual first error was consumed
            DGDestroyContext(fault);CHECK(DGMakeCurrent(c,pixels,GL_UNSIGNED_BYTE,16,16));
            puts("unwritable output fails after consuming the actual error; no replay is safe");
        }
    } else puts("Old-provider method fallback exercised; trap unavailable, not a trap-runtime pass.");
    DGDestroyContext(c);return failed?1:0;
}
