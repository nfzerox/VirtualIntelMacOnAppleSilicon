/* Host-only ownership regression; run with ASan/UBSan. No Apple ABI mocks
 * are claimed to validate generation, remapping, or rendered pixels. */
#include "../compat/PVGOpenGLProbe/PVGGLCompanion.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

static _Thread_local unsigned released;
static void release(void *p) { ++released;free(p); }
static void *exercise(void *arg) {
    (void)arg;
    int context,other,ir,otherIR;
    for (unsigned i=0;i<10000;++i) {
        PVGGLCompanionScope outer,inner;
        unsigned before=released;
        assert(!PVGGLCompanionPut(&ir,&context));
        PVGGLCompanionEnter(&outer,&context,0,release);
        void *pp=malloc(16),*duplicate=malloc(16);
        assert(pp && duplicate);
        assert(!PVGGLCompanionPut(NULL,pp));
        assert(PVGGLCompanionPut(&ir,pp));
        assert(!PVGGLCompanionPut(&ir,duplicate));free(duplicate);
        assert(!PVGGLCompanionTake(&other,0,&ir));
        assert(!PVGGLCompanionTake(&context,4,&ir));
        assert(!PVGGLCompanionTake(&context,0,&otherIR));
        PVGGLCompanionEnter(&inner,&other,4,release);
        assert(!PVGGLCompanionTake(&context,0,&ir));
        void *abandoned=malloc(16);assert(abandoned);
        assert(PVGGLCompanionPut(&otherIR,abandoned));
        PVGGLCompanionLeave(&inner);
        assert(released==before+1);
        assert(PVGGLCompanionTake(&context,0,&ir)==pp);
        assert(!PVGGLCompanionTake(&context,0,&ir));
        PVGGLCompanionLeave(&outer);
        assert(released==before+1); /* Native now owns pp, not the scope. */
        release(pp);
        assert(released==before+2);
    }
    return NULL;
}
int main(void) {
    pthread_t threads[4];
    for(unsigned i=0;i<4;++i)assert(!pthread_create(&threads[i],NULL,exercise,NULL));
    for(unsigned i=0;i<4;++i)assert(!pthread_join(threads[i],NULL));
    puts("companion ownership: 40000 nested scopes; transfer, discard and thread isolation passed");
}
