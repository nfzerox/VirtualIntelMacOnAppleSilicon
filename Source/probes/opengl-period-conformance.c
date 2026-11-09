/* Public compatibility-profile tests that run on a genuine 10.4u SDK.
 * Run unchanged on a physical Mac and the VM. No desktop/session injection. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#ifdef VIM_TEST_IOSURFACE
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <OpenGL/CGLIOSurface.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failed;
#ifdef VIM_TEST_IOSURFACE
static IOSurfaceRef color_surface(void)
{
    const CFStringRef keys[]={kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,
        kIOSurfaceBytesPerRow,kIOSurfaceAllocSize,kIOSurfacePixelFormat};
    const int values[]={16,16,4,64,1024,0x42475241};
    CFNumberRef numbers[6];
    for(unsigned i=0;i<6;++i)numbers[i]=CFNumberCreate(NULL,kCFNumberIntType,&values[i]);
    CFDictionaryRef properties=CFDictionaryCreate(NULL,(const void **)keys,
        (const void **)numbers,6,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    IOSurfaceRef surface=IOSurfaceCreate(properties);
    CFRelease(properties);
    for(unsigned i=0;i<6;++i)CFRelease(numbers[i]);
    return surface;
}
#endif
static void outcome(const char *test,int pass)
{
    printf("%s %s\n",pass?"PASS":"FAIL",test);
    if(!pass)++failed;
}

static void quad(GLfloat z)
{
    glBegin(GL_QUADS);
    glVertex3f(-1,-1,z);glVertex3f(1,-1,z);glVertex3f(1,1,z);glVertex3f(-1,1,z);
    glEnd();
}

static int pixel(GLint x,GLint y,unsigned r,unsigned g,unsigned b,unsigned tolerance)
{
    GLubyte p[4]={0};
    glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    GLenum error=glGetError();
    if(!error && p[0]+tolerance>=r && p[0]<=r+tolerance &&
       p[1]+tolerance>=g && p[1]<=g+tolerance &&
       p[2]+tolerance>=b && p[2]<=b+tolerance)return 1;
    printf("  pixel %d,%d expected=%u,%u,%u got=%u,%u,%u,%u error=%04x\n",
        (int)x,(int)y,r,g,b,p[0],p[1],p[2],p[3],(unsigned)error);
    return 0;
}

int main(int argc,char **argv)
{
    unsigned long renderer=0;
    if(argc==2&&!strcmp(argv[1],"--list-renderers")){
        CGLRendererInfoObj info=NULL;GLint n=0;
        if(CGLQueryRendererInfo(~0u,&info,&n)||!info)return 2;
        for(GLint i=0;i<n;++i){
            GLint id=0,accelerated=0;
            if(!CGLDescribeRenderer(info,i,kCGLRPRendererID,&id)&&
               !CGLDescribeRenderer(info,i,kCGLRPAccelerated,&accelerated))
                printf("renderer-id=0x%x accelerated=%d\n",(unsigned)id,(int)accelerated);
        }
        CGLDestroyRendererInfo(info);return 0;
    }
    int fboMode=argc==2&&!strcmp(argv[1],"--fbo");
#ifdef VIM_TEST_IOSURFACE
    int surfaceMode=argc==2&&!strcmp(argv[1],"--iosurface-fbo");
    fboMode|=surfaceMode;
#endif
    if(argc==3&&!strcmp(argv[1],"--renderer-id")){
        char *end=NULL;renderer=strtoul(argv[2],&end,0);
        if(!renderer||renderer>0xffffffffu||*end)return 64;
    }else if(argc!=1&&!fboMode)return 64;
    CGLPixelFormatAttribute attrs[9]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFADepthSize,24};
    unsigned end=4;
    if(renderer){attrs[end++]=kCGLPFARendererID;attrs[end++]=(CGLPixelFormatAttribute)renderer;}
    CGLPixelFormatObj format=NULL;CGLContextObj ctx=NULL;CGLPBufferObj pbuffer=NULL;
    GLint count=0,hardware=0;
    CGLError status=CGLChoosePixelFormat(attrs,&format,&count);
    if(!status && (!format || !count)){
        if(format)CGLDestroyPixelFormat(format);
        attrs[end++]=kCGLPFARemotePBuffer;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1050
        if(renderer)attrs[end++]=kCGLPFAAllowOfflineRenderers;
#endif
        format=NULL;count=0;status=CGLChoosePixelFormat(attrs,&format,&count);
    }
    if(!status && format)status=CGLDescribePixelFormat(format,0,kCGLPFAAccelerated,&hardware);
    if(status || !format || !count || !hardware){
        printf("SKIP accelerated format CGL=%d count=%d hardware=%d\n",status,(int)count,(int)hardware);return 77;
    }
    status=CGLCreateContext(format,NULL,&ctx);CGLDestroyPixelFormat(format);
    if(status || !ctx || CGLSetCurrentContext(ctx))return 2;
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    GLuint fbo=0,renderbuffers[2]={0};
#ifdef VIM_TEST_IOSURFACE
    GLuint color=0;
#endif
    if(fboMode){
        glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
        glGenRenderbuffersEXT(2,renderbuffers);
#ifdef VIM_TEST_IOSURFACE
        IOSurfaceRef surface=surfaceMode?color_surface():NULL;
        if(surfaceMode){
            if(!surface)return 2;
            glGenTextures(1,&color);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,color);
            status=CGLTexImageIOSurface2D(ctx,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,16,16,
                GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,surface,0);
            CFRelease(surface);
            if(status){printf("FAIL IOSurface color attachment CGL=%d\n",status);return 1;}
            glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
                GL_TEXTURE_RECTANGLE_ARB,color,0);
        }else
#endif
        {
            glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,renderbuffers[0]);
            glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,16,16);
            glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,renderbuffers[0]);
        }
        glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,renderbuffers[1]);
        glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_DEPTH_COMPONENT24,16,16);
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_DEPTH_ATTACHMENT_EXT,GL_RENDERBUFFER_EXT,renderbuffers[1]);
        GLenum complete=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT),error=glGetError();
        if(complete!=GL_FRAMEBUFFER_COMPLETE_EXT||error){
            printf("FAIL accelerated framebuffer status=%04x error=%04x\n",(unsigned)complete,(unsigned)error);return 1;
        }
    }else{
        status=CGLCreatePBuffer(16,16,GL_TEXTURE_RECTANGLE_ARB,GL_RGBA8,0,&pbuffer);
        if(!status)status=CGLSetPBuffer(ctx,pbuffer,0,0,0);
    }
    if(status){
        printf("FAIL accelerated pbuffer attachment CGL=%d\n",status);
        CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);if(pbuffer)CGLDestroyPBuffer(pbuffer);return 1;
    }
    glViewport(0,0,16,16);glDisable(GL_DITHER);
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(0,0,8,8);
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    outcome("scissored clear preserves outside pixels",pixel(2,2,255,0,0,0)&pixel(10,10,0,0,255,0));

    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1,0,0,0.5f);quad(0);glDisable(GL_BLEND);
    outcome("source-alpha blending",pixel(2,2,128,0,128,1));

    glClearDepth(1);glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);
    glColor4ub(255,0,0,255);quad(0);
    glColor4ub(0,0,255,255);quad(0.5f);
    int farther=pixel(2,2,255,0,0,0);
    glColor4ub(0,255,0,255);quad(-0.5f);
    outcome("depth ordering",farther&pixel(2,2,0,255,0,0));
    glDisable(GL_DEPTH_TEST);

    glColorMask(GL_TRUE,GL_FALSE,GL_FALSE,GL_TRUE);
    glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    outcome("color write mask",pixel(2,2,255,255,0,0));

    GLuint list=glGenLists(1);
    glNewList(list,GL_COMPILE);glColor4ub(0,255,255,255);quad(0);glEndList();
    int before=pixel(2,2,255,255,0,0);
    glCallList(list);
    outcome("display list compiles without drawing and replays",list&&before&&pixel(2,2,0,255,255,0));
    glDeleteLists(list,1);

    const GLfloat verts[]={-1,-1, 1,-1, 1,1, -1,1};
    glColor4ub(255,0,255,255);
    glVertexPointer(2,GL_FLOAT,0,verts);glEnableClientState(GL_VERTEX_ARRAY);
    glDrawArrays(GL_QUADS,0,4);glDisableClientState(GL_VERTEX_ARRAY);
    outcome("CPU vertex-array draw",pixel(2,2,255,0,255,0));

    GLubyte packed[48];memset(packed,0xa5,sizeof(packed));
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,4);
    glPixelStorei(GL_PACK_SKIP_ROWS,1);glPixelStorei(GL_PACK_SKIP_PIXELS,1);
    glReadPixels(2,2,2,2,GL_RGBA,GL_UNSIGNED_BYTE,packed);
    GLenum packError=glGetError();
    GLint state[4]={0};
    glGetIntegerv(GL_PACK_ALIGNMENT,&state[0]);glGetIntegerv(GL_PACK_ROW_LENGTH,&state[1]);
    glGetIntegerv(GL_PACK_SKIP_ROWS,&state[2]);glGetIntegerv(GL_PACK_SKIP_PIXELS,&state[3]);
    int safe=packError==GL_NO_ERROR&&glGetError()==GL_NO_ERROR&&
        state[0]==1&&state[1]==4&&state[2]==1&&state[3]==1;
    for(unsigned i=0;i<sizeof(packed);++i){
        int touched=(i>=20&&i<28)||(i>=36&&i<44);
        if(packed[i]!=(touched?(i%4==1?0:255):0xa5))safe=0;
    }
    if(!safe){
        printf("  pack error=%04x state=%d,%d,%d,%d; changed bytes:",(unsigned)packError,
            (int)state[0],(int)state[1],(int)state[2],(int)state[3]);
        for(unsigned i=0;i<sizeof(packed);++i)
            if(packed[i]!=0xa5)printf(" %u:%02x",i,packed[i]);
        putchar('\n');
    }
    outcome("pixel pack skips and untouched padding",safe);
    glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);

    GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    const GLubyte texels[]={255,0,0,255,0,255,0,255,0,0,255,255,255,255,0,255};
    GLubyte reply[24];memset(reply,0xa5,sizeof(reply));
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,texels);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,reply);
    outcome("texture upload and exact readback",glGetError()==GL_NO_ERROR&&
        !memcmp(texels,reply,sizeof(texels))&&reply[sizeof(texels)]==0xa5);
    glDeleteTextures(1,&texture);

    glEnable(0xffffffffu);
    outcome("invalid capability and error reset",glGetError()==GL_INVALID_ENUM&&glGetError()==GL_NO_ERROR);
    if(fbo){glDeleteFramebuffersEXT(1,&fbo);glDeleteRenderbuffersEXT(2,renderbuffers);}
#ifdef VIM_TEST_IOSURFACE
    if(color)glDeleteTextures(1,&color);
#endif
    CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);if(pbuffer)CGLDestroyPBuffer(pbuffer);
    return failed?1:0;
}
