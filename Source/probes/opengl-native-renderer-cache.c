// Exercise the actual cold-cache implementation with one transient failure.
// No VM, GPU, injected library or production fault-injection switch required.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../PassthroughOpenGL/native-renderer-properties.h"

static struct VIMNativeRendererCache cache=VIM_NATIVE_RENDERER_CACHE_INIT;
static unsigned queries;
static int query(int32_t *video,int32_t *texture,struct DGNativeRendererInfo *native,int *gl4)
{
    ++queries;
    *video=1337; // Failed replies must not publish partial metadata.
    if(queries==1)return 0;
    *video=512;*texture=1024;*gl4=1;
    if(native)*native=(struct DGNativeRendererInfo){DG_NATIVE_HARDWARE|DG_NATIVE_SOFTWARE,1};
    return 1;
}
static void *read_cache(void *unused)
{
    (void)unused;
    for(unsigned i=0;i<16;i++) {
        struct VIMNativeRendererProperties p=VIMReadNativeRendererProperties(&cache,1,query);
        assert(p.valid&&p.recovery&&p.gl4&&p.video_mb==512&&p.texture_mb==1024);
    }
    return NULL;
}
int main(void)
{
    struct VIMNativeRendererProperties p=VIMReadNativeRendererProperties(&cache,1,query);
    assert(!p.valid&&!p.recovery&&!p.gl4&&!p.video_mb&&!p.texture_mb&&queries==1);
    read_cache(NULL);
    pthread_t threads[4];
    for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,read_cache,NULL));
    for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));
    assert(queries==2);
    puts("PASS renderer cache: failed query retries, partial failure discarded, successful concurrent reads reuse metadata");
    return 0;
}
