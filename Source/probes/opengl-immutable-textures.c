/* Native core-profile immutable allocation used by El Capitan VectorKit.
 * Run unchanged against the period Intel driver and the installed GLI. */
#ifdef VIM_IMMUTABLE_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define glGenFramebuffers glGenFramebuffersEXT
#define glBindFramebuffer glBindFramebufferEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#define glDeleteFramebuffers glDeleteFramebuffersEXT
#define GL_TEXTURE_IMMUTABLE_FORMAT 0x912f
#define GL_TEXTURE_2D_ARRAY 0x8c1a
#define GL_NUM_EXTENSIONS 0x821d
#define GL_R32I 0x8235
#define GL_RED_INTEGER 0x8d94
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#endif
#include <stdio.h>
#include <string.h>
static int failures;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);failures+=!good;fflush(stdout);}
/* Run with the probe's bound test framebuffer. */
static void integer_transfers(void)
{
    GLuint textures[2]={0};glGenTextures(2,textures);
    glBindTexture(GL_TEXTURE_2D,textures[0]);
    glTexStorage2D(GL_TEXTURE_2D,1,GL_R32I,2,2);
    const GLint data[]={99,-2147483647,0x12345678,99,99,-1,17};
    const GLint expected[]={-2147483647,0x12345678,-1,17};
    glPixelStorei(GL_UNPACK_ROW_LENGTH,4);glPixelStorei(GL_UNPACK_SKIP_PIXELS,1);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,2,2,GL_RED_INTEGER,GL_INT,data);
    check(!glGetError(),"signed R32I upload with padded rows and skip");
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[0],0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"R32I framebuffer complete");
    GLint result[5]={0,0,0,0,37};
    glReadPixels(0,0,2,2,GL_RED_INTEGER,GL_INT,result);
    check(!glGetError() && !memcmp(result,expected,sizeof(expected)) && result[4]==37,
          "signed R32I framebuffer readback preserves exact values and guard");
    glBindTexture(GL_TEXTURE_2D_ARRAY,textures[1]);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY,1,GL_R32I,2,1,2);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY,0,0,0,0,2,1,2,GL_RED_INTEGER,GL_INT,expected);
    check(!glGetError(),"signed array texture upload");
    glGetTexImage(GL_TEXTURE_2D_ARRAY,0,GL_RED_INTEGER,GL_INT,result);
    check(!glGetError() && !memcmp(result,expected,sizeof(expected)) && result[4]==37,
          "signed array texture readback preserves both layers and guard");
    GLuint pbo=0;glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);
    const GLint buffered[]={-99,123456789,-123456789,2147483647,-37};
    glBufferData(GL_PIXEL_UNPACK_BUFFER,sizeof(buffered),buffered,GL_STATIC_DRAW);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY,0,0,0,0,2,1,2,GL_RED_INTEGER,GL_INT,(const void *)sizeof(GLint));
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glDeleteBuffers(1,&pbo);
    check(!glGetError(),"signed array texture upload from a nonzero PBO offset");
    glGetTexImage(GL_TEXTURE_2D_ARRAY,0,GL_RED_INTEGER,GL_INT,result);
    check(!glGetError() && !memcmp(result,buffered+1,sizeof(expected)) && result[4]==37,
          "PBO upload preserves both layers and guard");
    glDeleteTextures(2,textures);
}
int main(int argc,char **argv)
{
    int gl4=argc>1 && !strcmp(argv[1],"--gl4");
#ifdef VIM_IMMUTABLE_WIRE
    const int attrs[]={0x56494d02,gl4?DG_CGL_PROFILE_GL4:0x3200,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    check(!!(c->connection->features2&DG_FEATURE2_CORE_EXTENSIONS),"indexed discovery negotiated");
    uint32_t features=c->connection->features2;
    c->connection->features2&=~DG_FEATURE2_CORE_EXTENSIONS;
    GLint old_count=-1;glGetIntegerv(GL_NUM_EXTENSIONS,&old_count);
    check(old_count==0 && !glGetError() && !glGetStringi(GL_EXTENSIONS,0) &&
          glGetError()==GL_INVALID_VALUE && !c->connection->dead && !c->core_extensions_cached,
          "old host keeps empty enumeration without a new request");
    c->connection->features2=features;
#else
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(gl4?kCGLOGLPVersion_GL4_Core:kCGLOGLPVersion_3_2_Core),
        kCGLPFAColorSize,32,kCGLPFADepthSize,32,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0,depth=-1;
    CGLError e=CGLChoosePixelFormat(attrs,&pf,&n);
    if(!e&&pf)e=CGLDescribePixelFormat(pf,0,kCGLPFADepthSize,&depth);
    if(!e&&pf)e=CGLCreateContext(pf,NULL,&c);
    if(pf)CGLDestroyPixelFormat(pf);
    printf("native DepthSize32 selection=%d\n",depth);
    check(!e&&c&&depth>=24,"core context with native DepthSize32 selection");
    if(!c)return 1;CGLSetCurrentContext(c);
#endif
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    GLint count[2]={-1,37};GLint64 wide[2]={-1,37};GLfloat f[2]={-1,37};
    GLdouble d[2]={-1,37};GLboolean b[2]={GL_FALSE,37};
    glGetIntegerv(GL_NUM_EXTENSIONS,count);glGetInteger64v(GL_NUM_EXTENSIONS,wide);
    glGetFloatv(GL_NUM_EXTENSIONS,f);glGetDoublev(GL_NUM_EXTENSIONS,d);glGetBooleanv(GL_NUM_EXTENSIONS,b);
    check(!glGetError() && count[0]>=0 && wide[0]==count[0] && f[0]==count[0] &&
          d[0]==count[0] && b[0]==(count[0]?GL_TRUE:GL_FALSE) &&
          count[1]==37 && wide[1]==37 && f[1]==37 && d[1]==37 && b[1]==37,
          "indexed extension count is consistent across query types and leaves guards intact");
    const GLubyte *storage=NULL;GLuint storage_index=0;
    for(GLint i=0;i<count[0];++i){
        const GLubyte *ext=glGetStringi(GL_EXTENSIONS,(GLuint)i);
        check(ext!=NULL && !glGetError(),"indexed extension name");
        if(ext && !strcmp((const char *)ext,"GL_ARB_texture_storage")){storage=ext;storage_index=(GLuint)i;}
    }
    check(!glGetStringi(GL_EXTENSIONS,(GLuint)count[0]) && glGetError()==GL_INVALID_VALUE,"extension index upper bound");
    check(!glGetStringi(GL_RENDERER,0) && glGetError()==GL_INVALID_ENUM,"wrong indexed string selector");
    check(!glGetString(GL_EXTENSIONS) && glGetError()==GL_INVALID_ENUM,"compatibility string is invalid in core");
    if(!storage){puts("SKIP: native context does not advertise GL_ARB_texture_storage");return failures?1:77;}
    check(storage==glGetStringi(GL_EXTENSIONS,storage_index) && !glGetError(),"stable immutable extension string");
#ifdef VIM_IMMUTABLE_WIRE
    check(count[0]==__builtin_popcountll(c->core_extension_mask),"count matches qualified extension mask");
#endif
    GLuint texture[5]={0};glGenTextures(5,texture);
    const GLenum targets[]={GL_TEXTURE_1D,GL_TEXTURE_2D,GL_TEXTURE_3D,GL_TEXTURE_CUBE_MAP,GL_TEXTURE_2D_ARRAY};
    for(unsigned i=0;i<5;i++){
        GLenum target=targets[i];glBindTexture(target,texture[i]);
        if(i==0)glTexStorage1D(target,3,GL_RGBA8,8);
        else if(i==2 || i==4)glTexStorage3D(target,3,GL_RGBA8,8,8,8);
        else glTexStorage2D(target,3,GL_RGBA8,8,8);
        check(!glGetError(),"native immutable storage allocation");
        GLint immutable=0;glGetTexParameteriv(target,GL_TEXTURE_IMMUTABLE_FORMAT,&immutable);
        check(immutable==GL_TRUE&&!glGetError(),"immutable-format query");
        GLenum face=i==3?GL_TEXTURE_CUBE_MAP_POSITIVE_X:target;
        for(GLint level=0;level<3;level++){
            GLint width=0;glGetTexLevelParameteriv(face,level,GL_TEXTURE_WIDTH,&width);
            check(width==(8>>level)&&!glGetError(),"complete mip-chain dimensions");
            if(i==2 || i==4){
                GLint layers=0;glGetTexLevelParameteriv(face,level,GL_TEXTURE_DEPTH,&layers);
                check(layers==(i==2?(8>>level):8)&&!glGetError(),"3D depth shrinks; array layers stay fixed");
            }
        }
    }
    glBindTexture(GL_TEXTURE_2D,texture[1]);
    unsigned char pixels[8*8*4],readback[sizeof(pixels)];
    for(unsigned i=0;i<sizeof(pixels);i++)pixels[i]=(unsigned char)(i*37+11);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,8,8,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    GLuint fbo=0;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture[1],0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"immutable texture FBO");
    glReadPixels(0,0,8,8,GL_RGBA,GL_UNSIGNED_BYTE,readback);
    check(!glGetError()&&!memcmp(pixels,readback,sizeof(pixels)),"immutable texture upload/readback");
    glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(3,3,1,1,GL_RGBA,GL_UNSIGNED_BYTE,readback);
    check(!glGetError()&&readback[0]==255&&!readback[1]&&readback[2]==255&&readback[3]==255,"actual GPU clear/readback");
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    check(glGetError()==GL_INVALID_OPERATION,"immutable storage cannot be redefined");
    integer_transfers();
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);glDeleteTextures(5,texture);
    check(!glGetError(),"clean resource teardown");
#ifdef VIM_IMMUTABLE_WIRE
    check(!c->connection->dead,"extension and allocation requests left transport alive");DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    return failures?1:0;
}
