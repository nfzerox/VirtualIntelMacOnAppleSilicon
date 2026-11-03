/* Exercise real drawable allocation/reuse with hostile caller GL state. */
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
static int run(unsigned profile)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)profile,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;struct DGHostContext c={0};
    if(CGLChoosePixelFormat(attrs,&pf,&count) || CGLCreateContext(pf,NULL,&c.cgl))return 0;
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(c.cgl);c.profile=profile;
    if(!strstr((const char*)glGetString(GL_RENDERER),"Intel"))return 0;
    dg_storage_reuse=1;int good=1;
    for(unsigned iteration=0;iteration<3;++iteration) {
        glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);
        glColorMaskIndexedEXT(0,GL_FALSE,GL_TRUE,GL_FALSE,GL_FALSE);
        glColorMaskIndexedEXT(1,GL_TRUE,GL_FALSE,GL_TRUE,GL_FALSE);
        glClearColor(.2f,.4f,.6f,.8f);
        if(profile!=kCGLOGLPVersion_Legacy)glEnable(GL_RASTERIZER_DISCARD_EXT);
        GLuint texture=0;
        IOSurfaceRef surface=DGDrawableStorage(&c,32,24,&texture);
        GLboolean mask[4],other[4];GLfloat color[4];
        glGetBooleanIndexedvEXT(GL_COLOR_WRITEMASK,0,mask);
        glGetBooleanIndexedvEXT(GL_COLOR_WRITEMASK,1,other);
        glGetFloatv(GL_COLOR_CLEAR_VALUE,color);
        good=good && surface && glIsEnabled(GL_SCISSOR_TEST) &&
            !mask[0] && mask[1] && !mask[2] && !mask[3] &&
            other[0] && !other[1] && other[2] && !other[3] &&
            color[0]==.2f && color[1]==.4f && color[2]==.6f && color[3]==.8f;
        if(profile!=kCGLOGLPVersion_Legacy) {
            good=glIsEnabled(GL_RASTERIZER_DISCARD_EXT) && good;
            glDisable(GL_RASTERIZER_DISCARD_EXT);
        }
        GLuint fbo=0;glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
        glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
        unsigned char pixels[32*24*4];memset(pixels,255,sizeof(pixels));
        glReadPixels(0,0,32,24,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        for(unsigned i=0;i<sizeof(pixels);++i)good=good && !pixels[i];
        glDisable(GL_SCISSOR_TEST);glColorMaskIndexedEXT(0,GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
        DGRetireStorageTexture(&c,surface,texture,32,24,1);
        good=glGetError()==GL_NO_ERROR && good;
    }
    good=c.storage_allocations==1 && c.storage_reuses==2 && c.storage_texture_reuses==2 &&
        c.storage_registrations==1 && good;
    DGDestroyDrawables(&c);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
    printf("%s profile=%x GPU clear, recycled contents, indexed masks and scissor preservation\n",good?"PASS":"FAIL",profile);
    return good;
}
int main(void)
{
    int legacy=run(kCGLOGLPVersion_Legacy),core=run(kCGLOGLPVersion_3_2_Core);
    return !(legacy && core);
}
