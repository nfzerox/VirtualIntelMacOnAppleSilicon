/* Owned offscreen pixels only. Qualify compact readback coordinates, bounds,
 * canonical transfer/state preservation and actual shared-VRAM cost. */
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../PassthroughOpenGL/passthrough-opengl-surfaces.h"
extern void *DGCreateContext(const int *,void *);
extern GLboolean DGMakeCurrent(void *,void *,GLenum,GLsizei,GLsizei);
extern void DGDestroyContext(void *);
extern int DGReadDrawable(void *,int,int);
extern const char *DGTransportName(void);
static double seconds(void)
{
    mach_timebase_info_data_t t;mach_timebase_info(&t);
    return mach_absolute_time()*(double)t.numer/t.denom/1e9;
}
int main(void)
{
    enum { W=2560,H=1600 };
    const char *setting=getenv("VIM_READ_RECT_FRAMES");char *end=NULL;
    long frames=setting?strtol(setting,&end,10):10;
    if(frames<1 || frames>500 || (setting && (!*setting || *end)))return 64;
    unsigned char *shadow=calloc((size_t)W*H,4),*out=malloc((size_t)W*H*4+32);
    void *c=DGCreateContext(NULL,NULL);
    if(!shadow || !out || !c || !DGMakeCurrent(c,shadow,GL_UNSIGNED_BYTE,W,H))return 2;
    printf("renderer=%s transport=%s rect_capability=%d\n",glGetString(GL_RENDERER),
        DGTransportName(),DGSupportsDrawableReadRects());fflush(stdout);
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(12,17,7,3);
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    GLuint pbo=0;glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER,4096,NULL,GL_STREAM_DRAW);
    glPixelStorei(GL_PACK_ROW_LENGTH,37);glPixelStorei(GL_PACK_SKIP_PIXELS,3);
    glPixelStorei(GL_PACK_SKIP_ROWS,2);glPixelStorei(GL_PACK_ALIGNMENT,8);
    glPixelStorei(GL_PACK_SWAP_BYTES,GL_TRUE);glPixelStorei(GL_PACK_LSB_FIRST,GL_TRUE);
    glPixelTransferf(GL_RED_SCALE,0);glPixelTransferf(GL_ALPHA_BIAS,.25f);
    memset(out,0xa5,7*5*4+32);
    int read=DGReadDrawableRect(out+16,12,16,7,5),pixels=read,guards=1;
    for(int y=0;y<5;++y)for(int x=0;x<7;++x) {
        unsigned char *p=out+16+((size_t)y*7+x)*4;int red=y>=1 && y<=3;
        pixels=pixels && p[0]==(red?0:255) && !p[1] && p[2]==(red?255:0) && p[3]==255;
    }
    for(int i=0;i<16;++i)guards=guards && out[i]==0xa5 && out[16+7*5*4+i]==0xa5;
    GLint binding=0,row=0,skip=0,alignment=0,swap=0,lsb=0;GLfloat scale=1,bias=0;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&binding);glGetIntegerv(GL_PACK_ROW_LENGTH,&row);
    glGetIntegerv(GL_PACK_SKIP_ROWS,&skip);glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
    glGetIntegerv(GL_PACK_SWAP_BYTES,&swap);glGetIntegerv(GL_PACK_LSB_FIRST,&lsb);
    glGetFloatv(GL_RED_SCALE,&scale);glGetFloatv(GL_ALPHA_BIAS,&bias);
    int state=binding==(GLint)pbo && row==37 && skip==2 && alignment==8 && swap && lsb && scale==0 && bias==.25f;
    int bounds=!DGReadDrawableRect(out,-1,0,1,1) && !DGReadDrawableRect(out,0,-1,1,1) &&
        !DGReadDrawableRect(out,W-1,0,2,1) && !DGReadDrawableRect(out,0,H-1,1,2) &&
        !DGReadDrawableRect(out,0,0,0,1) && !DGReadDrawableRect(NULL,0,0,1,1);
    unsigned char edge[4];int last=DGReadDrawableRect(edge,W-1,H-1,1,1) &&
        edge[0]==255 && !edge[1] && !edge[2] && edge[3]==255;
    printf("%s compact nonzero-origin readback and orientation\n",pixels?"PASS":"FAIL");
    printf("%s output guards; %s public PBO/pack/transfer state; %s invalid bounds; %s last pixel\n",
        guards?"PASS":"FAIL",state?"PASS":"FAIL",bounds?"PASS":"FAIL",last?"PASS":"FAIL");fflush(stdout);
    const int regions[][4]={{0,0,W,H},{600,300,1280,880},{0,H-44,W,44}};
    int fresh=1;
    for(int frame=0;frame<100;++frame) {
        // No Finish: this private export owns flush/CPU visibility. Change
        // every frame so a stale surface mapping cannot accidentally pass.
        int r=!!(frame&1),g=!!(frame&2),b=!!(frame&4);
        glClearColor(r,g,b,1);glClear(GL_COLOR_BUFFER_BIT);
        memset(out,0xa5,7*5*4+32);
        fresh=DGReadDrawableRect(out+16,12,16,7,5) && fresh;
        for(int i=0;i<7*5;++i) {
            unsigned char *p=out+16+i*4;
            fresh=fresh && p[0]==b*255 && p[1]==g*255 && p[2]==r*255 && p[3]==255;
        }
        for(int i=0;i<16;++i)fresh=fresh && out[i]==0xa5 && out[16+7*5*4+i]==0xa5;
    }
    printf("%s 100 fresh GPU writes without explicit Finish, cropped BGRA and guards\n",fresh?"PASS":"FAIL");fflush(stdout);
    int good=pixels && guards && state && bounds && last && fresh;
    for(unsigned i=0;i<sizeof(regions)/sizeof(*regions);++i) {
        const int *r=regions[i];double start=seconds();
        for(long frame=0;frame<frames;++frame) {
            glClear(GL_COLOR_BUFFER_BIT);
            good=(i ? DGReadDrawableRect(out,r[0],r[1],r[2],r[3]) : DGReadDrawable(out,W,H)) && good;
        }
        printf("%s readback %d,%d %dx%d bytes=%u: %.3f ms/frame\n",i?"compact":"full",
            r[0],r[1],r[2],r[3],(unsigned)r[2]*r[3]*4,(seconds()-start)*1000/frames);fflush(stdout);
    }
    GLenum error=glGetError();printf("%s final GL error=%x\n",error?"FAIL":"PASS",error);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glDeleteBuffers(1,&pbo);DGDestroyContext(c);
    free(out);free(shadow);return !(good && !error);
}
