/* Exercise the actual loaded executor ABI, both worker and inline variants.
 * Transport/ownership controls, not VM pacing or a replacement GL renderer.
 * Includes caller-owned native CGL restoration, fragmented headers/bodies,
 * pipelined replies, malformed-terminal input and blocked-reader shutdown.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <pthread.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../PassthroughOpenGL/passthrough-opengl-wire.h"
#include "../vendor/qemu/include/hw/display/vim-passthrough-opengl-inprocess.h"
#include "direct-layout.h"
static const struct VIMGLInprocessAPI *api;
static int failed;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);failed=1;}}while(0)
struct Read {void *stream;struct DGReply reply;ssize_t count;};
static void *reader(void *p)
{
    struct Read *r=p;r->count=VIMGLInprocessReadExpected(api,r->stream,&r->reply,sizeof(r->reply));return NULL;
}
static void send_all(void *s,const void *buffer,size_t bytes)
{
    const unsigned char *p=buffer;
    while(bytes){ssize_t n=api->write(s,p,bytes);CHECK(n>0);if(n<=0)return;p+=n;bytes-=(size_t)n;}
}
static struct DGReply reply(void *s)
{
    struct DGReply r={0};CHECK(VIMGLInprocessReadExpected(api,s,&r,sizeof(r))==sizeof(r));return r;
}
int main(int argc,char **argv)
{
    if(argc!=2){fprintf(stderr,"usage: %s EXECUTOR_DYLIB\n",argv[0]);return 2;}
    void *library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);CHECK(library);if(!library)return 2;
    VIMGLGetInprocessAPI get=(VIMGLGetInprocessAPI)dlsym(library,"VIMPassthroughOpenGLInprocessGetAPI");CHECK(get);
    if(!get)return 2;api=get(VIM_GL_INPROCESS_ABI,sizeof(*api));CHECK(api);if(!api)return 2;
    CHECK(!get(VIM_GL_INPROCESS_ABI+1,sizeof(*api)) && !get(VIM_GL_INPROCESS_ABI,sizeof(*api)-1));
    CHECK(!get(1,sizeof(*api))); // old teardown semantics cannot negotiate backing loans
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj sentinel=NULL;GLint count=0;
    CHECK(!CGLChoosePixelFormat(attrs,&format,&count) && format);
    CHECK(!CGLCreateContext(format,NULL,&sentinel) && sentinel);if(format)CGLDestroyPixelFormat(format);
    CHECK(!CGLSetCurrentContext(sentinel));
    void *s=api->open();CHECK(s);if(!s)return 2;
    struct {struct DGMessage m;char layout[sizeof(DG_LAYOUT)];} hello={0};
    hello.m.op=DG_HELLO;hello.m.flags=DG_REPLY;hello.m.bytes=sizeof(hello.layout);
    hello.m.args[0]=DG_MAGIC;hello.m.args[1]=DG_VERSION;hello.m.args[2]=1;
    memcpy(hello.layout,DG_LAYOUT,sizeof(hello.layout));
    struct Read blocked={.stream=s};pthread_t thread;
    CHECK(!pthread_create(&thread,NULL,reader,&blocked));
    /* Single-byte fragments cannot accidentally rely on header/body writes
     * being atomic. Each input byte can be reused as soon as write returns. */
    for(size_t i=0;i<sizeof(hello.m)+sizeof(hello.layout);++i){
        unsigned char byte=((unsigned char *)&hello)[i];send_all(s,&byte,1);byte=0xa5;
        CHECK(CGLGetCurrentContext()==sentinel);
    }
    CHECK(!pthread_join(thread,NULL));CHECK(blocked.count==sizeof(struct DGReply));
    CHECK(!blocked.reply.error && blocked.reply.bytes==sizeof(hello.layout));
    unsigned char payload[sizeof(hello.layout)+16];memset(payload,0xa5,sizeof(payload));
    CHECK(VIMGLInprocessReadExpected(api,s,payload,sizeof(hello.layout))==sizeof(hello.layout));
    CHECK(!memcmp(payload,hello.layout,sizeof(hello.layout)));
    for(size_t i=sizeof(hello.layout);i<sizeof(payload);++i)CHECK(payload[i]==0xa5);
    struct DGMessage create={.op=DG_CREATE,.flags=DG_REPLY};
    send_all(s,&create,sizeof(create));struct DGReply created=reply(s);
    CHECK(!created.error && created.result && !created.bytes && CGLGetCurrentContext()==sentinel);
    struct DGMessage queries[2]={{.op=DG_ERROR,.flags=DG_REPLY,.context=(uint32_t)created.result},
        {.op=DG_ERROR,.flags=DG_REPLY,.context=(uint32_t)created.result}};
    struct iovec vectors[]={{queries,7},{NULL,0},{(unsigned char *)queries+7,sizeof(queries)-7}};
    CHECK(api->writev(s,vectors,3)==sizeof(queries));
    struct DGReply results[2]={{0}};
    CHECK(VIMGLInprocessReadExpected(api,s,results,sizeof(results))==sizeof(results));
    for(unsigned i=0;i<2;++i)CHECK(!results[i].error && !results[i].result && !results[i].bytes);
    CHECK(CGLGetCurrentContext()==sentinel);
    CHECK(api->writev(s,NULL,-1)==-1 && errno==EINVAL);
    CHECK(api->readv(s,NULL,-1)==-1 && errno==EINVAL);
    struct iovec invalid[]={{queries,sizeof(queries)},{NULL,1}};
    CHECK(api->writev(s,invalid,2)==-1 && errno==EINVAL); // validate ALL before a prefix can execute
    CHECK(api->write(s,NULL,0)==0 && api->read(s,NULL,0)==0);
    struct DGMessage destroy={.op=DG_DESTROY,.flags=DG_REPLY,.context=(uint32_t)created.result};
    send_all(s,&destroy,sizeof(destroy));struct DGReply destroyed=reply(s);CHECK(!destroyed.error);
    struct DGMessage malformed={.op=DG_ERROR,.bytes=DG_MAX_BYTES+1};
    ssize_t accepted=api->write(s,&malformed,sizeof(malformed));
    CHECK(accepted==sizeof(malformed) || accepted==-1); // a worker may reject after acceptance
    unsigned char byte=0xa5;CHECK(api->read(s,&byte,1)==0 && byte==0xa5);
    CHECK(!api->close(s) && CGLGetCurrentContext()==sentinel);
    CHECK(!api->live_count()); // close, not a later polling loop, owns retirement
    s=api->open();CHECK(s);if(s){
        blocked=(struct Read){.stream=s};CHECK(!pthread_create(&thread,NULL,reader,&blocked));
        CHECK(!api->shutdown(s));CHECK(!pthread_join(thread,NULL));
        CHECK(blocked.count==0 && !api->close(s));
        CHECK(!api->live_count());
    }
    for(unsigned i=0;i<400 && api->live_count();++i)usleep(5000);
    CHECK(!api->live_count());CHECK(!CGLSetCurrentContext(NULL));if(sentinel)CGLDestroyContext(sentinel);
    // Keep the module loaded if an unexpected worker failed to retire.
    if(!api->live_count())dlclose(library);
    puts(failed?"FAIL native stream ABI":"PASS actual native stream ABI: fragments, caller CGL, ordered replies, errors, cancellation and retirement");
    return failed?1:0;
}
