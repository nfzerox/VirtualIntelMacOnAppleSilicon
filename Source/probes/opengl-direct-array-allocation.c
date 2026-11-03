/* Count actual client realloc calls, not guessed copied bytes or desktop FPS.
 * The same client reaches the guest device or the real in-process decoder.
 * Export only main on the host so its GL wrappers cannot interpose on native GL.
 */
#include <stdlib.h>
#include <stdint.h>
static unsigned array_realloc_calls;
static unsigned array_realloc_fail;
static void *array_count_realloc(void *p,size_t bytes)
{array_realloc_calls++;if(array_realloc_calls==array_realloc_fail)return NULL;return realloc(p,bytes);}
#define realloc array_count_realloc
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#undef realloc
#include <assert.h>
static void check_pixel(unsigned red,unsigned green,unsigned blue)
{
    unsigned char p[4]={0};glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    assert(!glGetError() && p[0]==red && p[1]==green && p[2]==blue && p[3]==255);
}
int main(int argc,char **argv)
{
    int bounded=argc==2 && !strcmp(argv[1],"--one-allocation");assert(argc==1 || bounded);
#ifndef VIM_ARRAY_ALLOCATION_BASELINE
    size_t reserve=0;assert(DGDrawReservePart(&reserve,1) && reserve==1);
    assert(DGDrawReservePart(&reserve,1) && reserve==9);
    assert(DGDrawReservePart(&reserve,8) && reserve==24);
    reserve=DG_MAX_BYTES;assert(!DGDrawReservePart(&reserve,1) && reserve==DG_MAX_BYTES);
    reserve=0;assert(!DGDrawReservePart(&reserve,SIZE_MAX) && !reserve);
    reserve=SIZE_MAX;assert(!DGDrawReservePart(&reserve,1) && reserve==SIZE_MAX);
#endif
    unsigned char pixels[32*32*4]={0};struct DGContext *c=DGCreateContext(NULL,NULL);assert(c);
    assert(DGMakeCurrent(c,pixels,GL_UNSIGNED_BYTE,32,32));
    const char *name=(const char*)glGetString(GL_RENDERER);assert(name && strstr(name,"Intel"));puts(name);
    GLfloat positions[]={-1,-1,1,-1,1,1,-1,1},uv[]={0,0,1,0,1,1,0,1};
    GLubyte colors[16];GLushort indices[]={0,1,2,0,2,3};
    for(unsigned i=0;i<4;++i){colors[i*4]=0;colors[i*4+1]=255;colors[i*4+2]=0;colors[i*4+3]=255;}
    glViewport(0,0,32,32);glDisable(GL_DITHER);glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2,GL_FLOAT,0,positions);glEnableClientState(GL_COLOR_ARRAY);glColorPointer(4,GL_UNSIGNED_BYTE,0,colors);
    for(unsigned unit=0;unit<2;++unit){glClientActiveTexture(GL_TEXTURE0+unit);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);glTexCoordPointer(2,GL_FLOAT,0,uv);}
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);assert(!glGetError());
    for(unsigned kind=0;kind<2;++kind){
        unsigned before=array_realloc_calls;uint64_t began=mach_absolute_time();
        for(unsigned i=0;i<64;++i){
            if(!kind)glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,indices);
            else glDrawArrays(GL_QUADS,0,4);
        }
        uint64_t ended=mach_absolute_time();unsigned allocations=array_realloc_calls-before;
        uint64_t hash=UINT64_C(1469598103934665603);
        for(size_t i=0;i<c->connection->used;++i)hash=(hash^c->connection->batch[i])*UINT64_C(1099511628211);
        mach_timebase_info_data_t clock;assert(!mach_timebase_info(&clock));
        printf("kind=%s draws=64 realloc_calls=%u per_draw=%.3f\n",kind?"arrays":"elements",allocations,allocations/64.0);
        printf("owned_batch_bytes=%zu hash=%016llx client_loop_us=%.3f (not FPS)\n",c->connection->used,
            (unsigned long long)hash,(ended-began)*(double)clock.numer/clock.denom/1000.0);
#ifdef VIM_ARRAY_ALLOCATION_BASELINE
        if(bounded)assert(allocations==64);
#else
        if(bounded)assert(allocations<=(kind?0u:1u));
        assert(c->draw_scratch && c->draw_scratch_capacity<=DG_DRAW_SCRATCH_LIMIT);
        // A different allocation's old contents must never leak into the next
        // wire packet. Every descriptor/array/alignment byte is overwritten.
        memset(c->draw_scratch,0xa5,c->draw_scratch_capacity);
#endif
        // Mutation after Draw must not change already snapshotted client bytes.
        for(unsigned i=0;i<4;++i){colors[i*4+1]=0;colors[i*4+2]=255;}
        if(!kind)check_pixel(0,255,0);else check_pixel(0,0,255);
    }
    unsigned before=array_realloc_calls;glDrawArrays(GL_QUADS,-1,4);
    assert(glGetError()==GL_INVALID_VALUE && array_realloc_calls==before && !glGetError());
#ifdef VIM_ARRAY_ALLOCATION_BASELINE
    array_realloc_fail=array_realloc_calls+1;glDrawArrays(GL_QUADS,0,4);array_realloc_fail=0;
    assert(glGetError()==GL_OUT_OF_MEMORY && !glGetError());check_pixel(0,0,255);
#else
    // Force real growth rather than pretending a cache hit should allocate.
    GLubyte wide_colors[4*512]={0};
    for(unsigned i=0;i<4;++i)memcpy(wide_colors+i*512,colors+i*4,4);
    glColorPointer(4,GL_UNSIGNED_BYTE,512,wide_colors);
    unsigned char *saved=c->draw_scratch;size_t capacity=c->draw_scratch_capacity;
    array_realloc_fail=array_realloc_calls+1;glDrawArrays(GL_QUADS,0,4);array_realloc_fail=0;
    assert(c->draw_scratch==saved && c->draw_scratch_capacity==capacity);
    assert(glGetError()==GL_OUT_OF_MEMORY && !glGetError());check_pixel(0,0,255);
    glColorPointer(4,GL_UNSIGNED_BYTE,0,colors);glDrawArrays(GL_QUADS,0,4);check_pixel(0,0,255);
    // Oversized packets never evict or grow the retained small allocation.
    struct DGPacket large=DGPacketNew(c,DG_DRAW);
    assert(DGDrawPacketReserve(c,&large,DG_DRAW_SCRATCH_LIMIT+1));
    assert(c->draw_scratch==saved && c->draw_scratch_capacity==capacity);
    DGDrawPacketRelease(c,&large);
    assert(c->draw_scratch==saved && c->draw_scratch_capacity==capacity);
#endif
    GLint limit=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&limit);assert(limit>=32 && !glGetError());
    DGDestroyContext(c);puts("PASS actual native pixels, draw-time client capture, rejected draw without allocation, live transport");return 0;
}
