/* Exercise the generated CPU upload decoder and a shared-context GPU consumer.
 * No producer readback/Finish is allowed before the consumer samples the wave.
 * Build with the generated host-wire include directory and the same frameworks
 * as passthrough-opengl-host.c; pass an optional native renderer ID. */
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include <assert.h>
int main(int argc,char **argv)
{
    alarm(30);
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFAAllowOfflineRenderers,
        kCGLPFASupportsAutomaticGraphicsSwitching,argc>1?kCGLPFARendererID:0,
        argc>1?(CGLPixelFormatAttribute)strtol(argv[1],NULL,0):0,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;struct DGHostContext producer={0};CGLContextObj consumer=NULL;
    assert(!CGLChoosePixelFormat(attrs,&pf,&count) && pf);
    assert(!CGLCreateContext(pf,NULL,&producer.cgl) && !CGLCreateContext(pf,producer.cgl,&consumer));
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(producer.cgl);
    producer.profile=kCGLOGLPVersion_Legacy;
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    assert(DGCreateAliases(&producer,NULL));
    for(unsigned compact=0;compact<2;++compact){
        CGLSetCurrentContext(producer.cgl);
        GLuint source=0;glGenTextures(1,&source);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
        struct {struct DGMessage m;unsigned char pixels[800];} packet={0};
        packet.m.op=DG_OP_tex_image2D;packet.m.bytes=sizeof(packet.pixels);
        packet.m.args[0]=GL_TEXTURE_RECTANGLE_ARB;packet.m.args[2]=GL_RGBA8;
        packet.m.args[3]=200;packet.m.args[4]=1;packet.m.args[6]=GL_BGRA;
        packet.m.args[7]=GL_UNSIGNED_INT_8_8_8_8_REV;packet.m.args[8]=1;
        for(unsigned x=0;x<200;++x)memset(packet.pixels+4*x,35+x,4);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        if(compact){
            glPixelStorei(GL_UNPACK_ROW_LENGTH,317);glPixelStorei(GL_UNPACK_SKIP_PIXELS,7);
            packet.m.args[15]=2;packet.m.args[13]=800;
        }
        producer.error=GL_INVALID_ENUM;
        struct DGReply reply={0};assert(!DGExecute(&producer,&packet.m,&reply));
        assert(producer.error==GL_INVALID_ENUM && !reply.error && !glGetError());
        GLint row=0,skip=0;glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&skip);
        assert(row==(compact?317:0) && skip==(compact?7:0));
        /* Sampling, rather than querying the source, detects deferred uploads. */
        CGLSetCurrentContext(consumer);
        GLuint target=0,fbo=0;glGenTextures(1,&target);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,target);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,200,1,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,target,0);
        assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        glViewport(0,0,200,1);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,200,0,1,-1,1);
        glMatrixMode(GL_MODELVIEW);glLoadIdentity();glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
        glEnable(GL_TEXTURE_RECTANGLE_ARB);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
        glBegin(GL_QUADS);
        glTexCoord2i(0,0);glVertex2i(0,0);glTexCoord2i(200,0);glVertex2i(200,0);
        glTexCoord2i(200,1);glVertex2i(200,1);glTexCoord2i(0,1);glVertex2i(0,1);glEnd();
        unsigned char result[800]={0};glReadPixels(0,0,200,1,GL_RGBA,GL_UNSIGNED_BYTE,result);
        assert(!glGetError() && !memcmp(result,packet.pixels,sizeof(result)));
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
        glDeleteTextures(1,&target);glDeleteTextures(1,&source);
        printf("PASS shared 200x1 GPU lookup, compact=%u, all samples and unpack state\n",compact);
    }
    CGLSetCurrentContext(producer.cgl);DGDestroyAliases(&producer);
    CGLSetCurrentContext(NULL);CGLDestroyContext(consumer);CGLDestroyContext(producer.cgl);
    return 0;
}
