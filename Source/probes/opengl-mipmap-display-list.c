/* Mipmap corrections execute immediately without contaminating caller lists. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <string.h>
#include "../PassthroughOpenGL/passthrough-opengl-mipmap-host.h"
static struct DGMipmapExecution execution;
static unsigned checks,failures;
static int executor,ordinary;
static void check(int ok,const char *what)
{++checks;failures+=!ok;printf("%s %s\n",ok?"PASS":"FAIL",what);fflush(stdout);}
static GLenum generate(GLenum target)
{
    if(executor){glGenerateMipmapEXT(target);return glGetError();}
    GLenum error=ordinary?DGGenerateSrgbMipmap(target,1,0):
        DGGenerateSrgbMipmapExecute(target,1,0,&execution);
    return error?error:glGetError();
}
static void define(GLenum target,GLint level,int size,const unsigned char *pixels)
{
    if(target==GL_TEXTURE_1D)glTexImage1D(target,level,GL_SRGB8_ALPHA8,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    else if(target==GL_TEXTURE_3D)glTexImage3D(target,level,GL_SRGB8_ALPHA8,size,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    else for(unsigned f=0;f<(target==GL_TEXTURE_CUBE_MAP?6u:1u);++f)
        glTexImage2D(target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X+f:target,
            level,GL_SRGB8_ALPHA8,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
}
static int pixels_match(GLenum target,GLint level,int size,const unsigned char *seed)
{
    unsigned char out[256];unsigned count=(unsigned)size;
    if(target!=GL_TEXTURE_1D)count*=size;if(target==GL_TEXTURE_3D)count*=size;
    for(unsigned f=0;f<(target==GL_TEXTURE_CUBE_MAP?6u:1u);++f){
        GLint width=0;GLenum image=target==GL_TEXTURE_CUBE_MAP?GL_TEXTURE_CUBE_MAP_POSITIVE_X+f:target;
        glGetTexLevelParameteriv(image,level,GL_TEXTURE_WIDTH,&width);
        if(width!=size)return 0;
        memset(out,0,sizeof(out));glGetTexImage(image,level,GL_RGBA,GL_UNSIGNED_BYTE,out);
        if(glGetError())return 0;
        for(unsigned i=0;i<count*4;++i)if(out[i]!=seed[i%4])return 0;
    }return 1;
}
static void run(GLenum target,int named,int base,GLenum mode,GLenum policy)
{
    GLuint texture=0,list=glGenLists(1);unsigned char input[256];
    const unsigned char first[]={17,71,131,191},later[]={201,101,51,171};
    for(unsigned i=0;i<sizeof(input);++i)input[i]=first[i%4];
    if(named)glGenTextures(1,&texture);glBindTexture(target,texture);
    define(target,base,4,input);
    glTexParameteri(target,GL_TEXTURE_BASE_LEVEL,base);glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,base+1);
    glTexParameteri(target,GL_TEXTURE_SRGB_DECODE_EXT,policy);
    glColor4f(.125f,.25f,.375f,.5f);glEnable(GL_FRAMEBUFFER_SRGB_EXT);
    printf("case target=%x named=%d base=%d listmode=%x policy=%x\n",target,named,base,mode,policy);
    check(!glGetError(),"list source accepted");
    glNewList(list,mode);glColor4f(.75f,.5f,.25f,1);
    check(!generate(target),"generation executes inside caller list");
    GLint value=0;glGetIntegerv(GL_LIST_INDEX,&value);check(value==(GLint)list,"caller list remains active");
    glGetIntegerv(GL_LIST_MODE,&value);check(value==(GLint)mode,"caller list mode preserved");
    glGetIntegerv(DGMipmapBinding(target),&value);check(value==(GLint)texture,"actual texture binding preserved");
    check(glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT),"caller sRGB state preserved");
    check(pixels_match(target,base+1,2,first),"children generated before EndList");
    GLfloat color[4]={0};glGetFloatv(GL_CURRENT_COLOR,color);
    check(color[0]==(mode==GL_COMPILE?.125f:.75f),"caller compile versus execute behavior preserved");
    glColor4f(.25f,.5f,.75f,1);glEndList();
    for(unsigned i=0;i<sizeof(input);++i)input[i]=later[i%4];
    define(target,base+1,2,input);glColor4f(0,0,0,0);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    glCallList(list);
    check(pixels_match(target,base+1,2,later),"playing caller list does not replay internal generation or uploads");
    check(!glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT),"playing caller list does not replay private state restoration");
    glGetFloatv(GL_CURRENT_COLOR,color);
    check(color[0]==.25f&&color[1]==.5f&&color[2]==.75f&&color[3]==1,"caller list color commands survive");
    check(!glGetError(),"list leaves no error");
    glDeleteLists(list,1);if(named)glDeleteTextures(1,&texture);
}
static void pending_bind(void)
{
    GLuint textures[2],list=glGenLists(1);glGenTextures(2,textures);unsigned char pixels[64];
    const unsigned char seeds[2][4]={{17,71,131,191},{201,101,51,171}};
    for(unsigned t=0;t<2;++t){
        for(unsigned i=0;i<64;++i)pixels[i]=seeds[t][i%4];
        glBindTexture(GL_TEXTURE_2D,textures[t]);define(GL_TEXTURE_2D,2,4,pixels);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,2);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,3);
    }
    glBindTexture(GL_TEXTURE_2D,textures[0]);glNewList(list,GL_COMPILE);
    glBindTexture(GL_TEXTURE_2D,textures[1]);
    check(!generate(GL_TEXTURE_2D),"immediate generator ignores a merely compiled bind");glEndList();
    check(pixels_match(GL_TEXTURE_2D,3,2,seeds[0]),"generation uses actual execution binding");
    glCallList(list);GLint value=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&value);
    check(value==(GLint)textures[1],"caller compiled bind survives correction");
    glGetTexLevelParameteriv(GL_TEXTURE_2D,3,GL_TEXTURE_WIDTH,&value);
    check(value==0&&!glGetError(),"playing list does not generate on its deferred binding");
    glDeleteLists(list,1);glDeleteTextures(2,textures);
}
static void transfer_state(GLenum mode)
{
    GLuint texture=0,list=glGenLists(1);glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    unsigned char pixels[64];const unsigned char seed[]={17,71,131,191};
    for(unsigned i=0;i<64;++i)pixels[i]=seed[i%4];define(GL_TEXTURE_2D,2,4,pixels);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,2);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,3);
    glPixelTransferf(GL_RED_SCALE,.25f);glPixelTransferf(GL_ALPHA_BIAS,.25f);
    glPixelTransferi(GL_MAP_COLOR,GL_TRUE);
    int imaging=DGMipmapImaging();GLfloat matrix[16]={0};matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;matrix[12]=.125f;
    if(imaging){glMatrixMode(GL_COLOR);glLoadMatrixf(matrix);glMatrixMode(GL_PROJECTION);}
    check(!glGetError(),"caller transfer state accepted");glNewList(list,mode);
    check(!generate(GL_TEXTURE_2D),"generation with noncanonical transfer state inside list");
    GLfloat value=0;GLint integer=0;glGetFloatv(GL_RED_SCALE,&value);check(value==.25f,"red transfer scale preserved during list");
    glGetFloatv(GL_ALPHA_BIAS,&value);check(value==.25f,"alpha transfer bias preserved during list");
    glGetIntegerv(GL_MAP_COLOR,&integer);check(integer==GL_TRUE,"color mapping preserved during list");
    if(imaging){GLfloat got[16];glGetFloatv(GL_COLOR_MATRIX,got);check(!memcmp(got,matrix,sizeof(got)),"caller color matrix preserved during list");
        glGetIntegerv(GL_MATRIX_MODE,&integer);check(integer==GL_PROJECTION,"caller matrix mode preserved during list");}
    glEndList();glPixelTransferf(GL_RED_SCALE,1);glPixelTransferf(GL_ALPHA_BIAS,0);glPixelTransferi(GL_MAP_COLOR,GL_FALSE);
    if(imaging){glMatrixMode(GL_COLOR);glLoadIdentity();glMatrixMode(GL_MODELVIEW);}
    check(pixels_match(GL_TEXTURE_2D,3,2,seed),"private generation ignores caller transfer transforms");
    glCallList(list);glGetFloatv(GL_RED_SCALE,&value);check(value==1,"list playback does not restore stale scale");
    glGetFloatv(GL_ALPHA_BIAS,&value);check(value==0,"list playback does not restore stale bias");
    glGetIntegerv(GL_MAP_COLOR,&integer);check(integer==GL_FALSE,"list playback does not restore stale color mapping");
    if(imaging){GLfloat got[16];glGetFloatv(GL_COLOR_MATRIX,got);check(got[12]==0,"list playback does not restore stale color matrix");
        glGetIntegerv(GL_MATRIX_MODE,&integer);check(integer==GL_MODELVIEW,"list playback does not change matrix mode");}
    check(!glGetError(),"transfer list leaves no error");glDeleteLists(list,1);glDeleteTextures(1,&texture);
}
int main(int argc,char **argv)
{
    for(int i=1;i<argc;++i)if(!strcmp(argv[i],"--executor"))executor=1;
        else if(!strcmp(argv[i],"--ordinary-control"))ordinary=1;else return 2;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(pf);if(!executor)DGMipmapCaptureExecution(context,&execution);
    printf("renderer=%s executor=%d\n",glGetString(GL_RENDERER),executor);
    const GLenum targets[]={GL_TEXTURE_1D,GL_TEXTURE_2D,GL_TEXTURE_3D,GL_TEXTURE_CUBE_MAP};
    for(unsigned t=0;t<4;++t)for(int base=0;base<=2;base+=2)
        for(GLenum mode=GL_COMPILE;mode<=GL_COMPILE_AND_EXECUTE;++mode)
            for(GLenum policy=GL_DECODE_EXT;policy<=GL_SKIP_DECODE_EXT;++policy){
                run(targets[t],1,base,mode,policy);
                if(targets[t]==GL_TEXTURE_2D)run(targets[t],0,base,mode,policy);
            }
    pending_bind();transfer_state(GL_COMPILE);transfer_state(GL_COMPILE_AND_EXECUTE);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("mipmap-display-list checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
