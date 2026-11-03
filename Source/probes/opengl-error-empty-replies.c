/* Real native-query replies and pending errors, through the production
 * interpreter. Build hidden/export-only-main like opengl-direct-inprocess.
 * No simulated renderer, fabricated empty-error result or guest mutation. */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include <assert.h>
int main(void)
{
    unsigned char pixels[2][32*32*4]={{0}};
    struct DGContext *a=DGCreateContext(NULL,NULL),*b=DGCreateContext(NULL,NULL);
    assert(a && b && (a->connection->capabilities&DG_CAP_ERROR_EMPTY_REPLY));
    assert(DGMakeCurrent(a,pixels[0],GL_UNSIGNED_BYTE,32,32));
    GLint maximum=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);
    assert(maximum>=1024 && a->error_observed_empty && !glGetError());
    /* An existing drawable flush can carry the same native observation after
     * batched drawing. A prior error must survive that ownership boundary. */
    glClearColor(.25f,.5f,.75f,1);glClear(GL_COLOR_BUFFER_BIT);
    uint32_t framebuffer=UINT32_MAX;
    assert(DGFlushDrawableState(1,&framebuffer) && a->error_observed_empty);
    assert(!glGetError());
    glEnable(0xffffffffu);
    assert(DGFlushDrawableState(0,&framebuffer) && !a->error_observed_empty);
    assert(glGetError()==GL_INVALID_ENUM && !glGetError());
    /* Batched native viewport/matrix observations must not throw away the
     * witness and reintroduce a separate GetError exchange. */
    glViewport(1,2,20,21);
    GLint viewport[4]={0};glGetIntegerv(GL_VIEWPORT,viewport);
    assert(viewport[0]==1 && viewport[1]==2 && viewport[2]==20 && viewport[3]==21);
    assert(a->error_observed_empty && !glGetError());
    /* A prior native error must NOT discard a valid query's real output. */
    glEnable(0xffffffffu);assert(!a->error_observed_empty);
    GLint observed=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&observed);
    assert(observed==maximum && !a->error_observed_empty);
    assert(glGetError()==GL_INVALID_ENUM && !glGetError());
    /* Distinct native error flags remain observable, not silently drained
     * by the extra query. Native GL may retain either first or both flags. */
    glEnable(0xffffffffu);glViewport(0,0,-1,20);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&observed);assert(observed==maximum);
    GLenum first=glGetError(),second=glGetError(),third=glGetError();
    assert((first==GL_INVALID_ENUM || first==GL_INVALID_VALUE) &&
           (!second || second==GL_INVALID_ENUM || second==GL_INVALID_VALUE) && !third);
    /* Illegal queries inside Begin/End must never acquire an empty witness. */
    glBegin(GL_POINTS);glGetIntegerv(GL_MAX_TEXTURE_SIZE,&observed);
    assert(!a->error_observed_empty);glEnd();
    assert(glGetError()==GL_INVALID_OPERATION && !glGetError());
    /* Error observations belong to an exact context, not a shared stream. */
    assert(DGMakeCurrent(b,pixels[1],GL_UNSIGNED_BYTE,32,32));
    glEnable(0xffffffffu);glGetIntegerv(GL_MAX_TEXTURE_SIZE,&observed);
    assert(observed==maximum && !b->error_observed_empty);
    assert(DGMakeCurrent(a,pixels[0],GL_UNSIGNED_BYTE,32,32));assert(!glGetError());
    assert(DGMakeCurrent(b,pixels[1],GL_UNSIGNED_BYTE,32,32));
    assert(glGetError()==GL_INVALID_ENUM && !glGetError());
    /* An unnegotiated peer retains the original request/reply semantics. */
    b->connection->capabilities&=~DG_CAP_ERROR_EMPTY_REPLY;
    glEnable(0xffffffffu);glGetIntegerv(GL_MAX_TEXTURE_SIZE,&observed);
    assert(observed==maximum && !b->error_observed_empty);
    assert(glGetError()==GL_INVALID_ENUM && !glGetError());
    DGDestroyContext(b);DGDestroyContext(a);
    puts("PASS ordered native empty-error observations, valid query outputs with pending errors, async invalidation, multiple flags, Begin/End, context isolation and old-peer fallback");
    return 0;
}
