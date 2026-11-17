/* Host transport tests only, not native GL/pixel or real-VM qualification. */
#ifdef DG_MEMORY_STREAM_SOURCE
#include DG_MEMORY_STREAM_SOURCE /* Compare a prior transport, not a shipping switch. */
#else
#include "../PassthroughOpenGL/passthrough-opengl-memory-stream.c"
#endif
#include "../vendor/qemu/include/hw/display/vim-passthrough-opengl-inprocess.h"
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
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
enum {PIXEL_REPLY_BYTES=4*1024*1024,PIXEL_REPLY_ROUNDS=64};
static unsigned long pixel_reply_writes;
static void pixel_reply_worker(struct DGMemoryStream *s)
{
    unsigned char *pixels=malloc(PIXEL_REPLY_BYTES);CHECK(pixels);if(!pixels)return;
    memset(pixels,0x79,PIXEL_REPLY_BYTES);
    uint32_t ready=0x12345678;if(!exact(s,&ready,sizeof(ready),1,1)){free(pixels);return;}
    for(unsigned frame=0;frame<PIXEL_REPLY_ROUNDS;++frame){
        struct iovec vectors[]={{pixels,777},{NULL,0},{pixels+777,PIXEL_REPLY_BYTES-777}};
        int count=3;struct iovec *v=vectors;
        while(count){
            ssize_t n=DGMemoryServerWritev(s,v,count);++pixel_reply_writes;
            if(n<=0){free(pixels);return;}
            while(count && (size_t)n>=v->iov_len){n-=(ssize_t)v->iov_len;++v;--count;}
            if(count && n){v->iov_base=(char *)v->iov_base+n;v->iov_len-=(size_t)n;}
        }
    }
    free(pixels);
}
static void pixel_replies(void)
{
    unsigned char *pixels=malloc(PIXEL_REPLY_BYTES+32);CHECK(pixels);if(!pixels)exit(2);
    struct DGMemoryStream *s=DGMemoryStreamOpen(pixel_reply_worker);CHECK(s);if(!s)exit(2);
    uint32_t ready=0;CHECK(exact(s,&ready,sizeof(ready),0,0) && ready==0x12345678);
    struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);
    for(unsigned frame=0;frame<PIXEL_REPLY_ROUNDS;++frame){
        memset(pixels,0xa5,16);memset(pixels+16+PIXEL_REPLY_BYTES,0xa5,16);
        struct iovec receive[]={{pixels+16,1003},{NULL,0},{pixels+1019,PIXEL_REPLY_BYTES-1003}};
        CHECK(vector_exact(s,receive,3,0));
        for(unsigned i=0;i<16;++i)CHECK(pixels[i]==0xa5 && pixels[16+PIXEL_REPLY_BYTES+i]==0xa5);
        CHECK(pixels[16]==0x79 && pixels[1018]==0x79 && pixels[1019]==0x79 && pixels[15+PIXEL_REPLY_BYTES]==0x79);
    }
    clock_gettime(CLOCK_MONOTONIC,&end);CHECK(!DGMemoryStreamCloseAndWait(s));
    double seconds=end.tv_sec-begin.tv_sec+(end.tv_nsec-begin.tv_nsec)/1e9;
    printf("pixel reply stream: %u x %u bytes, %.3f ms, %.1f MiB/s, %lu server writes (transport only, not GPU/FPS)\n",
        PIXEL_REPLY_ROUNDS,PIXEL_REPLY_BYTES,seconds*1000,256.0/seconds,pixel_reply_writes);
    free(pixels);CHECK(!DGMemoryStreamLiveCount());
}
static ssize_t partial_pixel_write;
static void partial_pixel_worker(struct DGMemoryStream *s)
{
    unsigned char *pixels=malloc(PIXEL_REPLY_BYTES);CHECK(pixels);if(!pixels)return;
    memset(pixels,0x6c,PIXEL_REPLY_BYTES);
    struct iovec send[]={{NULL,0},{pixels,5},{NULL,0},{pixels+5,PIXEL_REPLY_BYTES-5}};
    partial_pixel_write=DGMemoryServerWritev(s,send,4);free(pixels);
}
static void cancel_pixel_reply(int shutdown)
{
    struct DGMemoryStream *s=DGMemoryStreamOpen(partial_pixel_worker);CHECK(s);if(!s)return;
    unsigned char bytes[33];memset(bytes,0xa5,sizeof(bytes));
    CHECK(DGMemoryClientRead(s,bytes+1,31)==31);
    for(unsigned i=1;i<32;++i)CHECK(bytes[i]==0x6c);
    CHECK(bytes[0]==0xa5 && bytes[32]==0xa5);
    if(shutdown)CHECK(!DGMemoryStreamShutdown(s));
    CHECK(!DGMemoryStreamCloseAndWait(s));CHECK(!DGMemoryStreamLiveCount());
    /* The legacy ring may have accepted a 64-KB prefix. Either transport must
     * report only its actual accepted bytes, not an unconsumed full reply. */
    CHECK(partial_pixel_write>=31 && partial_pixel_write<PIXEL_REPLY_BYTES);
}
/* The real prepared upload sends a small command before a large gathered
 * body. Its header must be consumed before borrowed vectors, even if the
 * server has not begun reading when the body writer arrives. */
static void queued_upload_worker(struct DGMemoryStream *s)
{
    pthread_mutex_lock(&drain_lock);
    while(!start_drain)pthread_cond_wait(&drain_ready,&drain_lock);
    pthread_mutex_unlock(&drain_lock);
    struct Header h={0};CHECK(exact(s,&h,sizeof(h),1,0));
    CHECK(h.bytes==PIXEL_REPLY_BYTES && h.seed==0x79);
    unsigned char bytes[4093];size_t left=PIXEL_REPLY_BYTES;
    while(left){
        size_t n=left<sizeof(bytes)?left:sizeof(bytes);
        if(!exact(s,bytes,n,1,0)){CHECK(0);return;}
        for(size_t i=0;i<n;++i)CHECK(bytes[i]==0x79);
        left-=n;
    }
}
struct UploadWriter {struct DGMemoryStream *stream;unsigned char *pixels;ssize_t result;};
static void *upload_writer(void *opaque)
{
    struct UploadWriter *w=opaque;
    struct iovec v[]={{NULL,0},{w->pixels,5},{NULL,0},{w->pixels+5,PIXEL_REPLY_BYTES-5}};
    w->result=DGMemoryClientWritev(w->stream,v,4);return NULL;
}
static void queued_upload(void)
{
    start_drain=0;
    struct DGMemoryStream *s=DGMemoryStreamOpen(queued_upload_worker);CHECK(s);if(!s)return;
    struct Header h={PIXEL_REPLY_BYTES,0x79};CHECK(exact(s,&h,sizeof(h),0,1));
    struct UploadWriter w={.stream=s,.pixels=malloc(PIXEL_REPLY_BYTES)};
    CHECK(w.pixels);if(!w.pixels)exit(2);memset(w.pixels,0x79,PIXEL_REPLY_BYTES);
    pthread_t writer;CHECK(!pthread_create(&writer,NULL,upload_writer,&w));
    pthread_mutex_lock(&s->lock);
    while(!s->ring[0].borrowed)pthread_cond_wait(&s->readable[0],&s->lock);
    CHECK(s->ring[0].used==sizeof(h) && s->ring[0].borrowed_remaining==PIXEL_REPLY_BYTES);
    pthread_mutex_unlock(&s->lock);
    pthread_mutex_lock(&drain_lock);start_drain=1;pthread_cond_signal(&drain_ready);pthread_mutex_unlock(&drain_lock);
    CHECK(!pthread_join(writer,NULL));CHECK(w.result==PIXEL_REPLY_BYTES);
    CHECK(!DGMemoryStreamCloseAndWait(s));free(w.pixels);CHECK(!DGMemoryStreamLiveCount());
}
static int abort_upload;
static void partial_upload_worker(struct DGMemoryStream *s)
{
    unsigned char bytes[31];CHECK(exact(s,bytes,sizeof(bytes),1,0));
    for(unsigned i=0;i<sizeof(bytes);++i)CHECK(bytes[i]==0x79);
    if(abort_upload)CHECK(!DGMemoryStreamShutdown(s));
    /* Otherwise worker retirement itself wakes the outstanding upload. */
}
static void cancel_upload(int shutdown)
{
    abort_upload=shutdown;
    struct DGMemoryStream *s=DGMemoryStreamOpen(partial_upload_worker);CHECK(s);if(!s)return;
    unsigned char *pixels=malloc(PIXEL_REPLY_BYTES);CHECK(pixels);if(!pixels)exit(2);
    memset(pixels,0x79,PIXEL_REPLY_BYTES);
    struct iovec v[]={{NULL,0},{pixels,5},{NULL,0},{pixels+5,PIXEL_REPLY_BYTES-5}};
    CHECK(DGMemoryClientWritev(s,v,4)==31);
    CHECK(!DGMemoryStreamCloseAndWait(s));free(pixels);CHECK(!DGMemoryStreamLiveCount());
}
static void pixel_upload_worker(struct DGMemoryStream *s)
{
    unsigned char *pixels=malloc(PIXEL_REPLY_BYTES);CHECK(pixels);if(!pixels)return;
    for(unsigned frame=0;frame<PIXEL_REPLY_ROUNDS;++frame){
        struct Header h={0};
        if(!exact(s,&h,sizeof(h),1,0)||h.bytes!=PIXEL_REPLY_BYTES||h.seed!=frame){CHECK(0);break;}
        if(!exact(s,pixels,PIXEL_REPLY_BYTES,1,0)){CHECK(0);break;}
        CHECK(pixels[0]==0x79 && pixels[776]==0x79 && pixels[777]==0x79 && pixels[PIXEL_REPLY_BYTES-1]==0x79);
        unsigned char ack=0x6b;if(!exact(s,&ack,1,1,1)){CHECK(0);break;}
    }
    free(pixels);
}
static void pixel_uploads(void)
{
    unsigned char *pixels=malloc(PIXEL_REPLY_BYTES);CHECK(pixels);if(!pixels)exit(2);
    memset(pixels,0x79,PIXEL_REPLY_BYTES);
    struct DGMemoryStream *s=DGMemoryStreamOpen(pixel_upload_worker);CHECK(s);if(!s)exit(2);
    struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);
    for(unsigned frame=0;frame<PIXEL_REPLY_ROUNDS;++frame){
        struct Header h={PIXEL_REPLY_BYTES,frame};CHECK(exact(s,&h,sizeof(h),0,1));
        struct iovec v[]={{pixels,777},{NULL,0},{pixels+777,PIXEL_REPLY_BYTES-777}};
        CHECK(vector_exact(s,v,3,1));
        unsigned char ack=0;CHECK(exact(s,&ack,1,0,0) && ack==0x6b);
    }
    clock_gettime(CLOCK_MONOTONIC,&end);CHECK(!DGMemoryStreamCloseAndWait(s));
    double seconds=end.tv_sec-begin.tv_sec+(end.tv_nsec-begin.tv_nsec)/1e9;
    printf("pixel upload stream: %u x %u bytes, %.3f ms, %.1f MiB/s (transport only, not GPU/FPS)\n",
        PIXEL_REPLY_ROUNDS,PIXEL_REPLY_BYTES,seconds*1000,256.0/seconds);
    free(pixels);CHECK(!DGMemoryStreamLiveCount());
}
int main(int argc,char **argv)
{
    alarm(30); /* Test watchdog only; production transfer waits are unchanged. */
    if(argc==2 && !strcmp(argv[1],"--benchmark")){pixel_replies();return failed?1:0;}
    if(argc==2 && !strcmp(argv[1],"--benchmark-uploads")){pixel_uploads();return failed?1:0;}
    CHECK(!DGMemoryStreamOpen(NULL) && errno==EINVAL);
    qualified_close(0);qualified_close(1);start_drain=0;
    expected_reply();
    expected_cancel();
    pixel_replies();cancel_pixel_reply(0);cancel_pixel_reply(1);
    queued_upload();cancel_upload(0);cancel_upload(1);pixel_uploads();
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
    printf("%s concurrent independent streams, partial/vector I/O, bounded expected reads/EOF, wraparound, large vectors in both directions, queued-header ordering, guards, partial-transfer cancellation, graceful drain and synchronous worker retirement\n",failed?"FAIL":"PASS");
    return failed?1:0;
}
