/* Host transport tests only, not native GL/pixel or real-VM qualification. */
#include "../PassthroughOpenGL/passthrough-opengl-memory-stream.c"
#include "../vendor/qemu/include/hw/display/vim-passthrough-opengl-inprocess.h"
#include <stdio.h>
#include <unistd.h>
static int failed;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);__sync_fetch_and_add(&failed,1);} } while(0)
static int exact(struct DGMemoryStream *s,void *data,size_t bytes,int server,int writing)
{
    char *p=data;
    while(bytes){ssize_t n=server?(writing?DGMemoryServerWrite(s,p,bytes):DGMemoryServerRead(s,p,bytes)):
        (writing?DGMemoryClientWrite(s,p,bytes):DGMemoryClientRead(s,p,bytes));
        if(n<=0)return 0;p+=n;bytes-=(size_t)n;}
    return 1;
}
static int vector_exact(struct DGMemoryStream *s,struct iovec *v,int count,int writing)
{
    while(count){
        if(!v->iov_len){++v;--count;continue;}
        ssize_t n=writing?DGMemoryClientWritev(s,v,count):DGMemoryClientReadv(s,v,count);
        if(n<=0)return 0;
        while(count && (size_t)n>=v->iov_len){n-=(ssize_t)v->iov_len;++v;--count;}
        if(count && n){v->iov_base=(char *)v->iov_base+n;v->iov_len-=(size_t)n;}
    }return 1;
}
struct Header {uint32_t bytes,seed;};
static void server(struct DGMemoryStream *s)
{
    struct Header header;
    while(exact(s,&header,sizeof(header),1,0)){
        if(header.bytes>1024*1024)return;
        unsigned char *bytes=malloc(header.bytes?header.bytes:1);if(!bytes)return;
        if(!exact(s,bytes,header.bytes,1,0)){free(bytes);return;}
        for(uint32_t i=0;i<header.bytes;++i){
            if(bytes[i]!=(unsigned char)(i*17+header.seed))CHECK(0);
            bytes[i]^=0x5a;
        }
        int good=exact(s,bytes,header.bytes,1,1);free(bytes);if(!good)return;
    }
}
static void *client(void *opaque)
{
    unsigned seed=(unsigned)(uintptr_t)opaque;
    struct DGMemoryStream *s=DGMemoryStreamOpen(server);CHECK(s);if(!s)return NULL;
    size_t lengths[]={1,13,65535,65536,65537,262177,1048576};
    for(unsigned round=0;round<3;++round)for(unsigned index=0;index<7;++index){
        size_t bytes=lengths[index];struct Header header={(uint32_t)bytes,seed+round};
        unsigned char *input=malloc(bytes),*output=malloc(bytes+32);CHECK(input && output);
        if(!input || !output)exit(2);
        for(size_t i=0;i<bytes;++i)input[i]=(unsigned char)(i*17+header.seed);
        memset(output,0xa5,bytes+32);
        CHECK(exact(s,&header,sizeof(header),0,1));
        size_t first=bytes<19?bytes:19;
        struct iovec send[]={{input,first},{NULL,0},{input+first,bytes-first}};
        CHECK(vector_exact(s,send,3,1));
        struct iovec receive[]={{output+16,first},{NULL,0},{output+16+first,bytes-first}};
        CHECK(vector_exact(s,receive,3,0));
        for(size_t i=0;i<bytes;++i)CHECK(output[16+i]==(unsigned char)(input[i]^0x5a));
        for(unsigned i=0;i<16;++i)CHECK(output[i]==0xa5 && output[16+bytes+i]==0xa5);
        free(input);free(output);
    }
    CHECK(!DGMemoryStreamClose(s));return NULL;
}
static void blocked_reader(struct DGMemoryStream *s)
{unsigned char byte;CHECK(DGMemoryServerRead(s,&byte,1)==0);}
static volatile int writer_filled,writer_done;
static pthread_mutex_t drain_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t drain_ready=PTHREAD_COND_INITIALIZER;
static int start_drain;
static volatile int close_drained;
static void graceful_reader(struct DGMemoryStream *s)
{
    pthread_mutex_lock(&drain_lock);
    while(!start_drain)pthread_cond_wait(&drain_ready,&drain_lock);
    pthread_mutex_unlock(&drain_lock);
    uint32_t value=0;CHECK(exact(s,&value,sizeof(value),1,0) && value==0x12345678);
    CHECK(DGMemoryServerWrite(s,&value,sizeof(value))==-1 && errno==EPIPE);
    __atomic_store_n(&close_drained,1,__ATOMIC_RELEASE);
}
static void blocked_writer(struct DGMemoryStream *s)
{
    unsigned char *data=calloc(1,DG_MEMORY_RING_BYTES);CHECK(data);if(!data)return;
    CHECK(DGMemoryServerWrite(s,data,DG_MEMORY_RING_BYTES)==DG_MEMORY_RING_BYTES);
    __atomic_store_n(&writer_filled,1,__ATOMIC_RELEASE);
    CHECK(DGMemoryServerWrite(s,data,1)==-1 && errno==EPIPE);
    __atomic_store_n(&writer_done,1,__ATOMIC_RELEASE);free(data);
}
static void *waiting_client(void *opaque)
{unsigned char byte;CHECK(DGMemoryClientRead(opaque,&byte,1)==0);return NULL;}
static int drained(void)
{
    // Finite test observation only, never compiled into the production wait.
    for(unsigned i=0;i<400;++i){if(!DGMemoryStreamLiveCount())return 1;usleep(5000);}
    return 0;
}
struct WaitingClose {struct DGMemoryStream *stream;int result,finished;};
static void *waiting_close(void *opaque)
{
    struct WaitingClose *p=opaque;p->result=DGMemoryStreamCloseAndWait(p->stream);
    __atomic_store_n(&p->finished,1,__ATOMIC_RELEASE);return NULL;
}
static void retirement_worker(struct DGMemoryStream *s)
{
    unsigned char byte;
    // Model retirement still outstanding AFTER shutdown has woken I/O.
    CHECK(DGMemoryServerRead(s,&byte,1)==0);
    pthread_mutex_lock(&drain_lock);
    while(!start_drain)pthread_cond_wait(&drain_ready,&drain_lock);
    pthread_mutex_unlock(&drain_lock);
    __atomic_store_n(&close_drained,1,__ATOMIC_RELEASE);
}
static void qualified_close(int cancelled)
{
    start_drain=0;__atomic_store_n(&close_drained,0,__ATOMIC_RELEASE);
    struct DGMemoryStream *s=DGMemoryStreamOpen(cancelled?retirement_worker:graceful_reader);CHECK(s);
    if(!s)return;
    if(cancelled)CHECK(!DGMemoryStreamShutdown(s));
    else {uint32_t value=0x12345678;CHECK(exact(s,&value,sizeof(value),0,1));}
    struct WaitingClose p={.stream=s};pthread_t thread;
    CHECK(!pthread_create(&thread,NULL,waiting_close,&p));
    pthread_mutex_lock(&s->lock);
    while(!s->client_closed)pthread_cond_wait(&s->readable[0],&s->lock);
    pthread_mutex_unlock(&s->lock);
    CHECK(!__atomic_load_n(&p.finished,__ATOMIC_ACQUIRE));
    CHECK(!__atomic_load_n(&close_drained,__ATOMIC_ACQUIRE));
    pthread_mutex_lock(&drain_lock);start_drain=1;pthread_cond_signal(&drain_ready);pthread_mutex_unlock(&drain_lock);
    CHECK(!pthread_join(thread,NULL));CHECK(!p.result && p.finished);
    CHECK(__atomic_load_n(&close_drained,__ATOMIC_ACQUIRE) && !DGMemoryStreamLiveCount());
}
static unsigned expected_progress;
static ssize_t expected_read(void *s,void *p,size_t n)
{
    ssize_t got=DGMemoryClientRead(s,p,n);
    if(got>0)__atomic_fetch_add(&expected_progress,(unsigned)got,__ATOMIC_RELEASE);
    return got;
}
static void expected_writer(struct DGMemoryStream *s)
{
    unsigned char bytes[4093];
    for(unsigned i=0;i<sizeof(bytes);++i)bytes[i]=(unsigned char)(i*7+19);
    for(unsigned i=0;i<73;++i)if(!exact(s,bytes,sizeof(bytes),1,1))return;
}
static void expected_reply(void)
{
    const struct VIMGLInprocessAPI api={.read=expected_read};
    struct DGMemoryStream *s=DGMemoryStreamOpen(expected_writer);CHECK(s);if(!s)return;
    enum {N=73*4093};unsigned char *bytes=malloc(N+32);CHECK(bytes);if(!bytes)exit(2);
    memset(bytes,0xa5,N+32);
    CHECK(VIMGLInprocessReadExpected(&api,s,bytes+16,262144)==262144);
    // A known-span read after actual EOF must report only the bytes received.
    CHECK(VIMGLInprocessReadExpected(&api,s,bytes+16+262144,N-262144+1)==N-262144);
    for(unsigned i=0;i<N;++i)CHECK(bytes[16+i]==(unsigned char)((i%4093)*7+19));
    for(unsigned i=0;i<16;++i)CHECK(bytes[i]==0xa5 && bytes[16+N+i]==0xa5);
    CHECK(VIMGLInprocessReadExpected(&api,s,bytes,1)==0);
    CHECK(!DGMemoryStreamClose(s));free(bytes);CHECK(drained());
}
static void expected_cancel_writer(struct DGMemoryStream *s)
{
    unsigned char byte=0x6b;CHECK(DGMemoryServerWrite(s,&byte,1)==1);
    CHECK(DGMemoryServerRead(s,&byte,1)==0);
}
static void *expected_cancel_client(void *s)
{
    const struct VIMGLInprocessAPI api={.read=expected_read};
    unsigned char bytes[4]={0xa5,0xa5,0xa5,0xa5};
    CHECK(VIMGLInprocessReadExpected(&api,s,bytes,sizeof(bytes))==1);
    CHECK(bytes[0]==0x6b && bytes[1]==0xa5 && bytes[2]==0xa5 && bytes[3]==0xa5);
    return NULL;
}
static void expected_cancel(void)
{
    __atomic_store_n(&expected_progress,0,__ATOMIC_RELEASE);
    struct DGMemoryStream *s=DGMemoryStreamOpen(expected_cancel_writer);CHECK(s);if(!s)return;
    pthread_t reader;CHECK(!pthread_create(&reader,NULL,expected_cancel_client,s));
    for(unsigned i=0;i<400 && !__atomic_load_n(&expected_progress,__ATOMIC_ACQUIRE);++i)usleep(5000);
    CHECK(__atomic_load_n(&expected_progress,__ATOMIC_ACQUIRE)==1);
    CHECK(!DGMemoryStreamShutdown(s));CHECK(!pthread_join(reader,NULL));
    CHECK(!DGMemoryStreamClose(s));CHECK(drained());
}
int main(void)
{
    CHECK(!DGMemoryStreamOpen(NULL) && errno==EINVAL);
    qualified_close(0);qualified_close(1);start_drain=0;
    expected_reply();
    expected_cancel();
    pthread_t clients[4];
    for(unsigned i=0;i<4;++i)CHECK(!pthread_create(&clients[i],NULL,client,(void *)(uintptr_t)(i+1)));
    for(unsigned i=0;i<4;++i)CHECK(!pthread_join(clients[i],NULL));
    CHECK(drained());
    struct DGMemoryStream *graceful=DGMemoryStreamOpen(graceful_reader);CHECK(graceful);
    uint32_t value=0x12345678;CHECK(exact(graceful,&value,sizeof(value),0,1));
    CHECK(!DGMemoryStreamClose(graceful));
    pthread_mutex_lock(&drain_lock);start_drain=1;pthread_cond_signal(&drain_ready);pthread_mutex_unlock(&drain_lock);
    CHECK(drained() && __atomic_load_n(&close_drained,__ATOMIC_ACQUIRE));
    struct DGMemoryStream *s=DGMemoryStreamOpen(blocked_reader);CHECK(s);
    pthread_t reader;CHECK(!pthread_create(&reader,NULL,waiting_client,s));
    usleep(20000);CHECK(!DGMemoryStreamShutdown(s));
    CHECK(!pthread_join(reader,NULL));CHECK(!DGMemoryStreamClose(s));CHECK(drained());
    s=DGMemoryStreamOpen(blocked_writer);CHECK(s);
    for(unsigned i=0;i<400 && !__atomic_load_n(&writer_filled,__ATOMIC_ACQUIRE);++i)usleep(5000);
    CHECK(__atomic_load_n(&writer_filled,__ATOMIC_ACQUIRE));usleep(20000);
    CHECK(!__atomic_load_n(&writer_done,__ATOMIC_ACQUIRE));
    CHECK(!DGMemoryStreamShutdown(s));CHECK(!DGMemoryStreamClose(s));CHECK(drained());
    CHECK(__atomic_load_n(&writer_done,__ATOMIC_ACQUIRE));
    printf("%s concurrent independent streams, partial/vector I/O, bounded expected reads/EOF, wraparound, 1-MiB replies, guards, graceful drain, cancellation and synchronous worker retirement\n",failed?"FAIL":"PASS");
    return failed?1:0;
}
