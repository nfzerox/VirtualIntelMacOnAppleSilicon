/* Native actual-wire control of source-level timing. State changes below are
 * only this standalone process's quiescent diagnostic metadata, not GL state. */
#define VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE 1
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include <assert.h>
static void arm(unsigned ms)
{
    assert(!dg_client_profile_active && !dg_client_profile_inflight);
    memset(&dg_client_profile_report,0,sizeof(dg_client_profile_report));dg_client_profile_ms=ms;
    __atomic_store_n(&dg_client_profile_active,++dg_client_profile_serial,__ATOMIC_RELEASE);
}
static void verify_rows(const struct DGClientProfileRow *rows,unsigned count)
{for(unsigned i=0;i<count;++i)assert(rows[i].entered==rows[i].completed && rows[i].maximum<=rows[i].ticks);}
int main(void)
{
    unsigned char pixels[32*32*4]={0};struct DGContext *c=DGCreateContext(NULL,NULL);assert(c);
    assert(DGMakeCurrent(c,pixels,GL_UNSIGNED_BYTE,32,32));
    const char *name=(const char *)glGetString(GL_RENDERER);assert(name && strstr(name,"Intel"));puts(name);
    arm(10000);
    glViewport(0,0,32,32);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    GLfloat vertex[]={-1,-1,1,-1,0,1};glEnableClientState(GL_VERTEX_ARRAY);glVertexPointer(2,GL_FLOAT,0,vertex);
    glColor3f(0,1,0);glDrawArrays(GL_TRIANGLES,0,3);
    unsigned char p[4]={0};glReadPixels(16,12,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    assert(!glGetError() && p[0]==0 && p[1]==255 && p[2]==0 && p[3]==255);
    __atomic_store_n(&dg_client_profile_active,0,__ATOMIC_RELEASE);assert(!dg_client_profile_inflight);
    assert(dg_client_profile_report.version==1 && dg_client_profile_report.generation==1);
    assert(dg_client_profile_report.ended>dg_client_profile_report.started);
    assert(dg_client_profile_report.draws.entered==1 && dg_client_profile_report.draws.completed==1);
    assert(dg_client_profile_report.submissions[0][DGCommandProfileIndex(DG_DRAW)].completed==1);
    assert(dg_client_profile_report.pointers[0].completed && dg_client_profile_report.pointers[2].completed);
    assert(dg_client_profile_report.locks.entered==dg_client_profile_report.locks.completed && dg_client_profile_report.locks.completed);
    verify_rows(dg_client_profile_report.submissions[0],DG_PROFILE_ROWS);
    verify_rows(dg_client_profile_report.submissions[1],DG_PROFILE_ROWS);
    verify_rows(dg_client_profile_report.transfers,2);verify_rows(dg_client_profile_report.pointers,3);
    struct DGClientProfileReport saved=dg_client_profile_report;
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);assert(!glGetError());
    assert(!memcmp(&saved,&dg_client_profile_report,sizeof(saved)));
    puts("PASS actual decoder/native GPU pixels, exact counts, balanced nested spans, inactive metadata unchanged");
    arm(1);struct DGClientProfileStamp s=DGClientProfileBegin(0,4,0);assert(s.generation);
    usleep(5000);DGClientProfileEnd(s);
    assert(!dg_client_profile_active && !dg_client_profile_inflight && dg_client_profile_report.excluded==1);
    assert(dg_client_profile_report.submissions[0][4].entered==1 && !dg_client_profile_report.submissions[0][4].completed);
    arm(10000);s=DGClientProfileBegin(0,4,0);assert(s.generation);
    __atomic_store_n(&dg_client_profile_active,0,__ATOMIC_RELEASE);DGClientProfileEnd(s);
    assert(!dg_client_profile_inflight && dg_client_profile_report.excluded==1);
    puts("PASS finite expiry/reset and stopped-span exclusion; no fabricated completion or rendering-policy change");
    dg_client_profile_backing_only=1;arm(10000);dg_client_backing_slow_count=0;
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);assert(!glGetError());
    assert(!dg_client_profile_report.started && !dg_client_profile_inflight);
    s=DGClientProfileBegin(1,DG_PROFILE_CLIENT_BACKING_BEGIN,64);assert(s.generation);
    usleep(25000);DGClientProfileEnd(s); // positive metadata control, not a GPU/performance test
    __atomic_store_n(&dg_client_profile_active,0,__ATOMIC_RELEASE);
    assert(!dg_client_profile_inflight && dg_client_backing_slow_count==1);
    assert(dg_client_profile_report.submissions[1][DGCommandProfileIndex(DG_PROFILE_CLIENT_BACKING_BEGIN)].completed==1);
    assert(!dg_client_profile_report.draws.entered && !dg_client_profile_report.locks.entered);
    for(unsigned i=0;i<DG_PROFILE_ROWS;++i){
        assert(!dg_client_profile_report.submissions[0][i].entered);
        if(i!=DGCommandProfileIndex(DG_PROFILE_CLIENT_BACKING_BEGIN))assert(!dg_client_profile_report.submissions[1][i].entered);
    }
    assert(!dg_client_profile_report.transfers[0].entered && !dg_client_profile_report.transfers[1].entered);
    for(unsigned i=0;i<3;++i)assert(!dg_client_profile_report.pointers[i].entered);
    dg_client_profile_backing_only=0;
    puts("PASS backing-only clock: native drawing/error observation unchanged; all ordinary hot-path rows remain empty");
    DGDestroyContext(c);return 0;
}
