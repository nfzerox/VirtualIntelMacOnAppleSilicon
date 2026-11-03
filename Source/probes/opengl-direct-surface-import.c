/* CPU imports into the real GPU backing: no staging texture or row image. */
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
static int run(unsigned profile)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)profile,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;struct DGHostContext c={0};
    if(CGLChoosePixelFormat(attrs,&pf,&count) || CGLCreateContext(pf,NULL,&c.cgl))return 0;
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(c.cgl);c.profile=profile;
    c.draw_mode=c.read_mode=GL_FRONT;
    int good=DGCreateAliases(&c,NULL) && DGSelectDrawable(&c,1,16,12);
    dg_surface_import=1;
    struct {struct DGMessage m;unsigned char data[80*12];} packet={0};
    packet.m.op=DG_IMPORT_DRAWABLE_VIEW;packet.m.args[0]=16;packet.m.args[1]=12;
    packet.m.args[2]=80;packet.m.args[3]=1;packet.m.bytes=80*11+64;
    for(unsigned y=0;y<12;++y)for(unsigned x=0;x<16;++x) {
        unsigned char *p=packet.data+y*80+x*4;p[0]=x;p[1]=y;p[2]=63;p[3]=255;
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,c.framebuffer);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    for(unsigned iteration=0;iteration<3;++iteration) {
        glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,7);glPixelStorei(GL_UNPACK_SKIP_ROWS,2);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS,3);
        good=DGDrawablePixels(&c,&packet.m,0)==0 && !c.error && good;
        GLint value=0;glGetIntegerv(GL_UNPACK_ROW_LENGTH,&value);good=value==7 && good;
        glGetIntegerv(GL_UNPACK_SKIP_ROWS,&value);good=value==2 && good;
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&value);good=value==3 && good;
        unsigned char actual[16*12*4];glReadPixels(0,0,16,12,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,actual);
        for(unsigned y=0;y<12;++y)for(unsigned x=0;x<16;++x) {
            unsigned char *p=actual+(y*16+x)*4;
            good=p[0]==x && p[1]==11-y && p[2]==63 && p[3]==255 && good;
        }
        struct {struct DGMessage m;unsigned char data[8];} patch={0};
        patch.m.op=DG_IMPORT_DRAWABLE_PATCH;patch.m.args[0]=2;patch.m.args[1]=1;
        patch.m.args[2]=4;patch.m.args[3]=5;patch.m.bytes=8;
        memset(patch.data,127,sizeof(patch.data));
        good=DGDrawablePixels(&c,&patch.m,0)==0 && !c.error && good;
        glReadPixels(0,0,16,12,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,actual);
        for(unsigned y=0;y<12;++y)for(unsigned x=0;x<16;++x) {
            unsigned char *p=actual+(y*16+x)*4;
            good=(y==5 && (x==4 || x==5)?p[0]==127 && p[1]==127 && p[2]==127 && p[3]==127:
                p[0]==x && p[1]==11-y && p[2]==63 && p[3]==255) && good;
        }
        good=glGetError()==GL_NO_ERROR && good;
    }
    DGDestroyDrawables(&c);DGDestroyAliases(&c);CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
    printf("%s profile=%x CPU/GPU imports, pitched flip, partial preservation, caller unpack state\n",good?"PASS":"FAIL",profile);
    return good;
}
int main(void) {int legacy=run(kCGLOGLPVersion_Legacy),core=run(kCGLOGLPVersion_3_2_Core);return !(legacy && core);}
