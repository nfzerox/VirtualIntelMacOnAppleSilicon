/* Actual native GL interpreter through its memory ABI or the existing TCP
 * comparison endpoint. No substitute renderer, guest window or FPS claim.
 * Build the host client hidden/export-only-main so its GL wrappers cannot
 * interpose on the library's real native OpenGL dlsym targets. */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include <unistd.h>
static int failed;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);failed=1;}}while(0)
static int compare_double(const void *a,const void *b)
{double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
struct Migration {struct DGContext *context;void *pixels;int begin;};
static void *migrated(void *opaque)
{
    struct Migration *m=opaque;
    CHECK(DGMakeCurrent(m->context,m->pixels,GL_UNSIGNED_BYTE,63,67));
    if(m->begin){
        // The actual host context changes native threads inside an open
        // immediate-mode primitive, with several real transport boundaries.
        for(unsigned i=0;i<2400;++i)glVertex2f(0,0);
        DGFlushTransport(m->context);
    }else{
        glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
        CHECK(glGetError()==GL_NO_ERROR);
    }
    DGMakeCurrent(NULL,NULL,GL_UNSIGNED_BYTE,0,0);return NULL;
}
int main(void)
{
    enum {W=63,H=67};unsigned char shadow[W*H*4]={0};
    struct DGContext *c=DGCreateContext(NULL,NULL);CHECK(c);if(!c)return 2;
    CHECK(DGMakeCurrent(c,shadow,GL_UNSIGNED_BYTE,W,H));
    printf("transport=%s renderer=%s\n",DGTransportName(),glGetString(GL_RENDERER));
    glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(0,0,W,H/2);glClearColor(0,1,1,1);
    glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    unsigned char pixels[W*H*4+32];memset(pixels,0xa5,sizeof(pixels));
    CHECK(DGReadDrawable(pixels+16,W,H));
    for(int y=0;y<H;++y)for(int x=0;x<W;++x){const unsigned char *p=pixels+16+(y*W+x)*4;
        CHECK(p[0]==255 && p[1]==(y<H/2?255:0) && p[2]==(y<H/2?0:255) && p[3]==255);}
    for(unsigned i=0;i<16;++i)CHECK(pixels[i]==0xa5 && pixels[16+W*H*4+i]==0xa5);
    CHECK(DGReadDrawableRect(pixels+16,1,1,31,29));
    for(int y=0;y<29;++y)for(int x=0;x<31;++x){const unsigned char *p=pixels+16+(y*31+x)*4;
        CHECK(p[0]==255 && p[1]==255 && p[2]==0 && p[3]==255);}
    glEnable(0xffffffffu);CHECK(glGetError()==GL_INVALID_ENUM && glGetError()==GL_NO_ERROR);
    GLint expected=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&expected);CHECK(expected>=1024);
    mach_timebase_info_data_t timebase;mach_timebase_info(&timebase);
    for(unsigned phase=0;phase<3;++phase){
        double times[500],total=0;
        for(unsigned i=0;i<500;++i){
            uint64_t began=mach_absolute_time();GLint value=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&value);
            times[i]=(mach_absolute_time()-began)*(double)timebase.numer/timebase.denom/1000.0;
            total+=times[i];CHECK(value==expected);
        }
        qsort(times,500,sizeof(*times),compare_double);
        printf("phase=%u queries=500 mean_us=%.3f median_us=%.3f p95_us=%.3f\n",
            phase,total/500,times[250],times[475]);
    }
    CHECK(glGetError()==GL_NO_ERROR && !c->connection->dead);
    int before_migration=failed;
    glViewport(0,0,W,H);glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glPointSize(1);glColor4f(1,1,1,1);glBegin(GL_POINTS);
    for(unsigned i=0;i<2400;++i)glVertex2f(0,0);
    DGFlushTransport(c);
    struct Migration migration={c,shadow,1};pthread_t thread;
    CHECK(!pthread_create(&thread,NULL,migrated,&migration));CHECK(!pthread_join(thread,NULL));
    glEnd();CHECK(glGetError()==GL_NO_ERROR && !c->connection->dead);
    CHECK(DGReadDrawableRect(pixels+16,W/2,H/2,1,1));
    CHECK(pixels[16]==255 && pixels[17]==255 && pixels[18]==255 && pixels[19]==255);
    migration.begin=0;
    for(unsigned round=0;round<3;++round){
        CHECK(!pthread_create(&thread,NULL,migrated,&migration));CHECK(!pthread_join(thread,NULL));
        CHECK(glGetError()==GL_NO_ERROR);
    }
    puts(failed==before_migration?"PASS context migration and open Begin/End across native threads and batch fragments":
        "FAIL context migration or open Begin/End");
    DGDestroyContext(c);
    if(dg_inprocess_api){
        for(unsigned i=0;i<400 && dg_inprocess_api->live_count();++i)usleep(5000);
        CHECK(!dg_inprocess_api->live_count());
        void *library=dlopen(getenv("VIM_PASSTHROUGH_OPENGL_INPROCESS_LIBRARY"),RTLD_NOW|RTLD_LOCAL);
        VIMGLGetInprocessAPI get=library?(VIMGLGetInprocessAPI)dlsym(library,"VIMPassthroughOpenGLInprocessGetAPI"):NULL;
        CHECK(get && !get(VIM_GL_INPROCESS_ABI+1,sizeof(struct VIMGLInprocessAPI)) &&
            !get(VIM_GL_INPROCESS_ABI,sizeof(struct VIMGLInprocessAPI)-1));
        if(library)dlclose(library); // original client load still owns its reference
    }
    puts(failed?"FAIL native interpreter transport":"PASS native values/errors, padded/cropped IOSurface rows, guards and retirement");
    return failed?1:0;
}
