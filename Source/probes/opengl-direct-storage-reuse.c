/* Exercise the real host drawable implementation, without changing a VM.
 * Build with the generated direct-layout.h include directory and the same
 * frameworks as passthrough-opengl-host. No substitute/software renderer is used. */
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main

static int check_pixel(unsigned char r,unsigned char g,unsigned char b)
{
    unsigned char p[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    if(p[0]!=r || p[1]!=g || p[2]!=b) {
        fprintf(stderr,"pixel mismatch %u,%u,%u expected %u,%u,%u\n",p[0],p[1],p[2],r,g,b);return 0;
    }
    return 1;
}
static int read_alias(GLuint alias,unsigned char pixel[4])
{
    GLint saved=0;GLuint framebuffer=0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&saved);
    glGenFramebuffersEXT(1,&framebuffer);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,framebuffer);
    glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,alias,0);
    GLenum status=glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)saved);glDeleteFramebuffersEXT(1,&framebuffer);
    return framebuffer && status==GL_FRAMEBUFFER_COMPLETE_EXT;
}
static void diagnose_alias(GLuint alias,const char *stage)
{
    unsigned char image[173*151*4]={0},pixel[4]={0};
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,alias);
    glGetTexImage(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA,GL_UNSIGNED_BYTE,image);
    int complete=read_alias(alias,pixel);
    fprintf(stderr,"alias diagnostic %s get_image=%u,%u,%u,%u framebuffer=%u,%u,%u,%u status=%x error=%x\n",
        stage,image[0],image[1],image[2],image[3],pixel[0],pixel[1],pixel[2],pixel[3],complete,glGetError());
}
static int exercise(int double_buffer,int depth)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;struct DGHostContext c={0};
    if(CGLChoosePixelFormat(attrs,&pf,&count) || CGLCreateContext(pf,NULL,&c.cgl))return 0;
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(c.cgl);
    if(!strstr((const char*)glGetString(GL_RENDERER),"Intel"))return 0;
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    c.profile=kCGLOGLPVersion_Legacy;c.double_buffer=double_buffer;
    c.depth_bits=depth?24:0;c.stencil_bits=depth?8:0;c.draw_mode=c.read_mode=GL_FRONT;
    if(!DGCreateAliases(&c,NULL))return 0;
    int good=1;
    /* Keep a different live drawable throughout: retiring another drawable
     * must not merge their contents even if both have matching dimensions. */
    good=DGSelectDrawable(&c,1,173,151) && good;
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    /* Exercise the actual generated public dispatch, not merely the helper.
     * Claim a private color name without GenTextures; both the current mirror
     * and canonical drawable must move, retaining the real rendered pixels. */
    GLuint claimed=c.color;struct DGMessage bind={.op=5};struct DGReply bound={0};
    bind.args[0]=GL_TEXTURE_2D;bind.args[1]=claimed;
    good=!DGExecute(&c,&bind,&bound) && !c.error && !glGetError() &&
        c.color!=claimed && c.drawable->color==c.color && check_pixel(255,0,0) && good;
    glBindTexture(GL_TEXTURE_2D,0);glDeleteTextures(1,&claimed);
    for(unsigned i=0;i<100 && good;++i) {
        int w=129+(i*17)%120,h=130+(i*23)%120;
        good=DGSelectDrawable(&c,2+i,w,h) && good;
        good=check_pixel(0,0,0) && good;
        GLint prior=0,tw=0,th=0;glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&prior);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,c.color);
        glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_WIDTH,&tw);
        glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_HEIGHT,&th);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,(GLuint)prior);
        good=(tw==w && th==h && glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT) && good;
        glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);good=check_pixel(0,255,0) && good;
        int sockets[2];
        if(socketpair(AF_UNIX,SOCK_STREAM,0,sockets))return 0;
        struct DGMessage read={.op=DG_READ_DRAWABLE_RECT,.bytes=16};
        read.args[0]=read.args[1]=2;read.args[2]=w-2;read.args[3]=h-2;
        struct DGReply read_reply={0};unsigned char edge[16]={0};
        good=DGSendDrawableSurface(&c,&read,sockets[0])==1 && good;
        good=!DGTransfer(sockets[1],&read_reply,sizeof(read_reply),0) && good;
        good=!DGTransfer(sockets[1],edge,sizeof(edge),0) && good;
        for(unsigned p=0;p<4;++p)good=edge[p*4]==0 && edge[p*4+1]==255 && edge[p*4+2]==0 && good;
        close(sockets[0]);close(sockets[1]);
        DGDropDrawable(&c,2+i);
        good=DGSelectDrawable(&c,1,173,151) && check_pixel(255,0,0) && good;
    }
    uint64_t hits=c.storage_texture_reuses,registrations=c.storage_registrations;
    for(unsigned i=0;i<100 && good;++i){
        good=DGSelectDrawable(&c,400+i,129,130) && good;
        good=check_pixel(0,0,0) && good;
        glClearColor(0,1,1,1);glClear(GL_COLOR_BUFFER_BIT);good=check_pixel(0,255,255) && good;
        DGDropDrawable(&c,400+i);
        good=DGSelectDrawable(&c,1,173,151) && check_pixel(255,0,0) && good;
    }
    unsigned planes=double_buffer?2:1;
    good=(dg_drawable_texture_reuse?c.storage_texture_reuses-hits>=98*planes:
        c.storage_texture_reuses==hits && c.storage_registrations-registrations==100*planes) && good;
    /* A live guest alias outlives its drawable and must veto recycling. */
    GLuint alias=0;glGenTextures(1,&alias);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,alias);
    struct DGMessage message={0};message.args[0]=GL_RGBA8;message.args[1]=173;message.args[2]=151;
    struct DGReply reply={0};DGBindDrawableTexture(&c,&message,&reply);
    good=!reply.error && good;
    if(getenv("VIM_TEST_ALIAS_DIAGNOSTICS"))diagnose_alias(alias,"before-drop");
    DGDropDrawable(&c,1);
    good=DGSelectDrawable(&c,200,173,151) && good;
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    /* Physical Mavericks Intel returns zero through GetTexImage for this
     * IOSurface alias even before retirement. Read its actual GPU attachment,
     * as in the native/current-host before/after diagnostic control. */
    unsigned char pixels[4]={0};good=read_alias(alias,pixels) && good;
    if(getenv("VIM_TEST_ALIAS_DIAGNOSTICS"))diagnose_alias(alias,"after-drop-and-blue-draw");
    if(!good || pixels[0]!=255 || pixels[1] || pixels[2])
        fprintf(stderr,"alias retirement control: earlier_checks=%d alias=%u pixel=%u,%u,%u,%u reply_error=%x\n",
            good,alias,pixels[0],pixels[1],pixels[2],pixels[3],reply.error);
    good=pixels[0]==255 && pixels[1]==0 && pixels[2]==0 && pixels[3]==255 && good;
    GLenum error=glGetError();good=!error && c.storage_reuses>=98 && good;
    printf("%s double=%d depth=%d allocations=%llu reuses=%llu cached=%zu registrations=%llu texture_reuses=%llu enabled=%d error=%x\n",
        good?"PASS":"FAIL",double_buffer,depth,c.storage_allocations,c.storage_reuses,c.spare_bytes,
        c.storage_registrations,c.storage_texture_reuses,dg_drawable_texture_reuse,error);
    DGForgetAliases(&c,1,&alias);glDeleteTextures(1,&alias);
    DGDestroyDrawables(&c);DGDestroyAliases(&c);CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
    for(unsigned i=0;i<4;++i)good=!c.spare_storage[i] && !c.spare_textures[i] && good;
    good=!c.spare_bytes && good;
    return good;
}
int main(void)
{
    dg_storage_reuse=1;
    const char *reuse=getenv("VIM_TEST_DRAWABLE_TEXTURE_REUSE");
    if(reuse){if(strcmp(reuse,"0") && strcmp(reuse,"1"))return 2;dg_drawable_texture_reuse=!strcmp(reuse,"1");}
    int good=exercise(0,0);good=exercise(1,1) && good;return !good;
}
