/* Finite actual guest/native-GL transfer probe. No replacement renderer.
 * Compare copied and prepared rows on the same boot, including pitched gaps,
 * reversed rows and >256KiB payloads in both client architectures. */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include <unistd.h>
static int failed;
#define CHECK(x) do { if (!(x) && failed++<16) fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); } while(0)
static double milliseconds(uint64_t start)
{
    mach_timebase_info_data_t clock;mach_timebase_info(&clock);
    return (mach_absolute_time()-start)*(double)clock.numer/clock.denom/1e6;
}
static int compare_time(const void *a,const void *b)
{ double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y); }
int main(void)
{
    enum { W=1025,H=513,ROW=W*4,STRIDE=ROW+64 };
    size_t span=(size_t)STRIDE*H+32;
    unsigned char *storage=malloc(span),*shadow=calloc(W*H,4),*tight=malloc(W*H*4);
    if (!storage || !shadow || !tight) return 2;
    struct DGContext *c=DGCreateContext(NULL,NULL);
    CHECK(c && DGMakeCurrent(c,shadow,GL_UNSIGNED_BYTE,W,H));if (!c) return 2;
    printf("transport=%s renderer=%s scatter=%d upload=%d\n",DGTransportName(),
        glGetString(GL_RENDERER),DGSupportsRAMScatterReads(),c->connection->row_upload_supported);
    CHECK(DGSupportsRAMScatterReads() && c->connection->row_upload_supported);
    if (!DGSupportsRAMScatterReads()) return 2;
    glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(0,0,W,H/2);glClearColor(0,1,1,1);
    glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    for (int reverse=0;reverse<2;reverse++) {
        memset(storage,0xa5,span);
        CHECK(DGReadDrawableRectRows(storage+16,STRIDE,reverse,0,0,W,H)==1);
        for (unsigned y=0;y<H;y++) {
            unsigned source=reverse?H-1-y:y;
            unsigned char *p=storage+16+y*STRIDE;
            for (unsigned x=0;x<W;x++,p+=4)
                CHECK(p[0]==255 && p[1]==(source<H/2?255:0) && p[2]==(source<H/2?0:255) && p[3]==255);
            for (unsigned x=ROW;x<STRIDE;x++) CHECK(storage[16+y*STRIDE+x]==0xa5);
        }
        for (unsigned i=0;i<16;i++) CHECK(storage[i]==0xa5 && storage[span-1-i]==0xa5);
        CHECK(DGImportDrawableRows(storage+16,W,H,STRIDE,reverse)==1);
        CHECK(DGReadDrawable(tight,W,H));
        for (unsigned y=0;y<H;y++) for (unsigned x=0;x<W;x++) {
            unsigned char *p=tight+(y*W+x)*4;
            CHECK(p[0]==255 && p[1]==(y<H/2?255:0) && p[2]==(y<H/2?0:255) && p[3]==255);
        }
    }
    double times[4][24];
    for (int trial=-4;trial<24;trial++) {
        for (unsigned mode=0;mode<4;mode++) {
            uint64_t start=mach_absolute_time();
            if (mode==1) CHECK(DGReadDrawableRectRows(tight,ROW,0,0,0,W,H)==1);
            else if (mode==2) CHECK(DGReadDrawableRectRows(storage+16,STRIDE,1,0,0,W,H)==1);
            else {
                CHECK(DGReadDrawableRect(tight,0,0,W,H));
                if (mode==3) for (unsigned y=0;y<H;y++)
                    memcpy(storage+16+y*STRIDE,tight+(H-1-y)*ROW,ROW);
            }
            if(trial>=0) times[mode][trial]=milliseconds(start);
        }
    }
    const char *names[]={"copied-tight","prepared-tight","prepared-pitched-reverse","copied-plus-row-flip"};
    for (unsigned mode=0;mode<4;mode++) {
        double total=0;for(unsigned i=0;i<24;i++) total+=times[mode][i];
        qsort(times[mode],24,sizeof(double),compare_time);
        printf("%s bytes=%u samples=24 mean_ms=%.3f median_ms=%.3f p95_ms=%.3f\n",
            names[mode],W*H*4,total/24,times[mode][12],times[mode][22]);
    }
    // The real AppKit frontBuffer callback writes imported IOSurfaces, not
    // only default window drawables. Qualify the same prepared receive path
    // against its copied baseline and reject stale serials without writes.
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    CHECK(DGImportIOSurfaceTextureFormat(GL_RGBA8,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,W,H,tight,ROW)==1);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(0,0,W,H/2);glClearColor(1,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    struct DGIosurfaceWrite writes[DG_IOSURFACE_INLINE_WRITES];unsigned count=0;uint32_t framebuffer=0;
    CHECK(DGFlushIOSurfaceState(1,&framebuffer,writes,DG_IOSURFACE_INLINE_WRITES,&count)==1);
    uint64_t serial=0;for(unsigned i=0;i<count;i++)if(writes[i].texture==texture)serial=writes[i].serial;
    CHECK(serial);
    unsigned char *reference=malloc(span);if(!reference)return 2;
    memset(storage,0xa5,span);memset(reference,0xa5,span);
    unsigned supported=c->connection->ram_scatter_supported;
    c->connection->ram_scatter_supported=0;
    CHECK(DGReadIOSurfaceTextureElements(texture,serial,W,H,reference+16,STRIDE,4)==1);
    c->connection->ram_scatter_supported=supported;
    CHECK(DGReadIOSurfaceTextureElements(texture,serial,W,H,storage+16,STRIDE,4)==1);
    CHECK(!memcmp(storage,reference,span));
    for(unsigned y=0;y<H;y++) {
        const unsigned char *p=storage+16+y*STRIDE;
        for(unsigned x=0;x<W;x++,p+=4)
            CHECK(!p[0]&&p[1]==(y<H/2?0:255)&&p[2]==(y<H/2?255:0)&&p[3]==255);
        for(unsigned x=ROW;x<STRIDE;x++)CHECK(storage[16+y*STRIDE+x]==0xa5);
    }
    memset(storage,0xa5,span);
    CHECK(DGReadIOSurfaceTextureElements(texture,serial+1,W,H,storage+16,STRIDE,4)==0);
    for(size_t i=0;i<span;i++)CHECK(storage[i]==0xa5);
    double imported[2][24];
    for(int trial=-4;trial<24;trial++)for(unsigned mode=0;mode<2;mode++) {
        c->connection->ram_scatter_supported=mode?supported:0;
        uint64_t start=mach_absolute_time();
        CHECK(DGReadIOSurfaceTextureElements(texture,serial,W,H,storage+16,STRIDE,4)==1);
        if(trial>=0)imported[mode][trial]=milliseconds(start);
    }
    c->connection->ram_scatter_supported=supported;
    for(unsigned mode=0;mode<2;mode++) {
        qsort(imported[mode],24,sizeof(double),compare_time);
        printf("IOSurface-%s bytes=%u samples=24 median_ms=%.3f p95_ms=%.3f\n",
            mode?"prepared-pitched":"copied-pitched",W*H*4,imported[mode][12],imported[mode][22]);
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    free(reference);
    CHECK(!c->connection->dead && glGetError()==GL_NO_ERROR);
    DGDestroyContext(c);free(storage);free(shadow);free(tight);
    puts(failed?"FAIL prepared RAM rows":"PASS prepared receive/upload, pitch guards, row reversal and retirement");
    return failed?1:0;
}
