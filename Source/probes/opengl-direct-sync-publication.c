/* Run against the real in-process executor; no infinite host wait in a
 * regression fixture. A remote fence must be submitted before another
 * context can wait on it without the producer's flush flag. */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include <assert.h>
int main(void)
{
    unsigned char pixels[16*16*4]={0};
    struct DGContext *producer=DGCreateContext(NULL,NULL);
    assert(producer && DGMakeCurrent(producer,pixels,GL_UNSIGNED_BYTE,16,16));
    struct DGContext *consumer=DGCreateContext(NULL,producer);assert(consumer);
    for(unsigned i=0;i<16;++i){
        assert(DGMakeCurrent(producer,pixels,GL_UNSIGNED_BYTE,16,16));
        glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);assert(fence);
        assert(DGMakeCurrent(consumer,pixels,GL_UNSIGNED_BYTE,16,16));
        GLenum status=glClientWaitSync(fence,0,250000000);
        assert(status==GL_ALREADY_SIGNALED || status==GL_CONDITION_SATISFIED);
        glDeleteSync(fence);assert(!glGetError());
    }
    DGDestroyContext(consumer);DGDestroyContext(producer);
    puts("PASS submitted cross-context fences, flags=0, real GPU completion");
    return 0;
}
