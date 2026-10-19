/* Ownership of native PP companions between generation and native adoption.
 * A scope lives on the legacy-update wrapper's stack; no global map or
 * allocation is needed. The caller must leave every entered scope. */
#ifndef PVG_GL_COMPANION_H
#define PVG_GL_COMPANION_H
#include <stddef.h>

typedef struct PVGGLCompanionScope {
    struct PVGGLCompanionScope *parent;
    void *context;
    void *ir;
    void *pp;
    void (*release)(void *);
    unsigned stage;
} PVGGLCompanionScope;

void PVGGLCompanionEnter(PVGGLCompanionScope *, void *context,
                         unsigned stage, void (*release)(void *));
/* On success ownership of pp moves to the scope. On failure caller owns pp.
 * NULL IR is rejected, allowing immediate cleanup of failed compilation. */
int PVGGLCompanionPut(void *ir, void *pp);
/* Native ownership begins only on successful take; mismatches do not consume.
 * The caller passes this to native creation or stores it before PP remapping. */
void *PVGGLCompanionTake(void *context, unsigned stage, void *ir);
void PVGGLCompanionLeave(PVGGLCompanionScope *);
#endif
