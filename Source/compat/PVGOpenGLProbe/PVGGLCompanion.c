#include "PVGGLCompanion.h"
#include <stdlib.h>
#include <string.h>

static _Thread_local PVGGLCompanionScope *current;

void PVGGLCompanionEnter(PVGGLCompanionScope *scope, void *context,
                         unsigned stage, void (*release)(void *)) {
    if (!scope || !context || !release || (stage != 0 && stage != 4)) abort();
    /* Reject reentering an active stack object before touching it. */
    for (PVGGLCompanionScope *p=current;p;p=p->parent)
        if (p==scope) abort();
    memset(scope,0,sizeof(*scope));
    scope->parent=current;scope->context=context;
    scope->stage=stage;scope->release=release;current=scope;
}

int PVGGLCompanionPut(void *ir, void *pp) {
    if (!current || !ir || !pp || current->pp) return 0;
    current->ir=ir;current->pp=pp;
    return 1;
}

void *PVGGLCompanionTake(void *context, unsigned stage, void *ir) {
    if (!current || current->context!=context || current->stage!=stage ||
        !ir || current->ir!=ir) return NULL;
    void *pp=current->pp;
    current->pp=NULL;current->ir=NULL;
    return pp;
}

void PVGGLCompanionLeave(PVGGLCompanionScope *scope) {
    if (!scope || scope!=current) abort();
    void *pp=scope->pp;
    void (*release)(void *)=scope->release;
    current=scope->parent;
    memset(scope,0,sizeof(*scope));
    /* Unconsumed output (e.g. failed native creation) remains ours. Native
     * PPStreamTokenFree, not free(), must be supplied by the wrapper. */
    if (pp) release(pp);
}
