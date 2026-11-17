/* Cold renderer enumeration. Cache only a successful host query: transport
 * readiness during login must not permanently disable GL in an application.
 * No timer, polling loop or per-draw work; a failed call remains a real error
 * and the next explicit enumeration can try again. */
#ifndef VIM_NATIVE_RENDERER_PROPERTIES_H
#define VIM_NATIVE_RENDERER_PROPERTIES_H
#include <pthread.h>
#include "direct-native-renderers.h"

struct VIMNativeRendererProperties {
    int valid,recovery,gl4;
    int32_t video_mb,texture_mb;
};
struct VIMNativeRendererCache {
    pthread_mutex_t mutex;
    struct VIMNativeRendererProperties properties;
};
#define VIM_NATIVE_RENDERER_CACHE_INIT {PTHREAD_MUTEX_INITIALIZER,{0}}

static struct VIMNativeRendererProperties VIMReadNativeRendererProperties(
    struct VIMNativeRendererCache *cache,int recovery,
    int (*query)(int32_t *,int32_t *,struct DGNativeRendererInfo *,int *))
{
    pthread_mutex_lock(&cache->mutex);
    if(!cache->properties.valid) {
        struct VIMNativeRendererProperties candidate={0};
        struct DGNativeRendererInfo native={0};
        if(query(&candidate.video_mb,&candidate.texture_mb,recovery?&native:NULL,&candidate.gl4)) {
            candidate.valid=1;
            candidate.recovery=(native.flags&(DG_NATIVE_HARDWARE|DG_NATIVE_SOFTWARE))==
                (DG_NATIVE_HARDWARE|DG_NATIVE_SOFTWARE);
            cache->properties=candidate;
        }
    }
    struct VIMNativeRendererProperties result=cache->properties;
    pthread_mutex_unlock(&cache->mutex);
    return result;
}
#endif
