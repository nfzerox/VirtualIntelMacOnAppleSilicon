/* Diskless device test only. Deliberately partial vectored I/O and EINTR;
 * this is not a GL renderer and is never bundled in the application. */
#include "../vendor/qemu/include/hw/display/vim-passthrough-opengl-inprocess.h"
#include <stdlib.h>
typedef struct { size_t read,write;int interrupted_read,interrupted_write; } Stream;
static void *create(void) { return calloc(1,sizeof(Stream)); }
static unsigned char pattern(size_t i) { return (i*17+3)&255; }
static ssize_t receive(void *opaque,void *buffer,size_t bytes)
{
    Stream *s=opaque;unsigned char *p=buffer;
    for (size_t i=0;i<bytes;i++) p[i]=pattern(s->read++);
    return bytes;
}
static ssize_t send(void *opaque,const void *buffer,size_t bytes)
{
    Stream *s=opaque;const unsigned char *p=buffer;
    for (size_t i=0;i<bytes;i++) if (p[i]!=pattern(s->write++)) { errno=EIO;return -1; }
    return bytes;
}
static ssize_t receive_vectors(void *opaque,const struct iovec *v,int count)
{
    Stream *s=opaque;
    if (!s->interrupted_read++) { errno=EINTR;return -1; }
    size_t done=0;
    for (int i=0;i<count && done<8191;i++) {
        size_t bytes=v[i].iov_len<8191-done?v[i].iov_len:8191-done;
        receive(s,v[i].iov_base,bytes);done+=bytes;
    }
    return done;
}
static ssize_t send_vectors(void *opaque,const struct iovec *v,int count)
{
    Stream *s=opaque;
    if (!s->interrupted_write++) { errno=EINTR;return -1; }
    size_t done=0;
    for (int i=0;i<count && done<12289;i++) {
        size_t bytes=v[i].iov_len<12289-done?v[i].iov_len:12289-done;
        ssize_t n=send(s,v[i].iov_base,bytes);if (n<0) return n;done+=n;
    }
    return done;
}
static int shutdown_stream(void *stream) { return 0; }
static int close_stream(void *stream) { free(stream);return 0; }
static unsigned live(void) { return 0; }
const struct VIMGLInprocessAPI *VIMPassthroughOpenGLInprocessGetAPI(uint32_t version,uint32_t bytes)
{
    static const struct VIMGLInprocessAPI api={VIM_GL_INPROCESS_ABI,sizeof(api),create,
        receive,send,receive_vectors,send_vectors,shutdown_stream,close_stream,live};
    return version==api.version && bytes==sizeof(api)?&api:NULL;
}
