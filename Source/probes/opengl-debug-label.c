/* Compare native object labels with the generated GLI bridge. */
#ifdef VIM_DEBUG_LABEL_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
static void label_query(GLuint object,GLsizei capacity,GLsizei expected,const char *prefix)
{
    char label[32];memset(label,'!',sizeof(label));
    GLsizei length=-1;
    glGetObjectLabelEXT(GL_TEXTURE,object,capacity,&length,label);
    GLenum error=glGetError();
    printf("label capacity=%d length=%d error=%04x first=%02x\n",capacity,length,error,(unsigned char)label[0]);
    CHECK(error==GL_NO_ERROR && length==expected);
    if(capacity>0)CHECK(!memcmp(label,prefix,(size_t)expected) && label[expected]==0 && label[expected+1]=='!');
    else CHECK(label[0]=='!');
}
static int advertised(int core)
{
    if(!core){
        const char *ext=(const char *)glGetString(GL_EXTENSIONS);
        return ext && strstr(ext,"GL_EXT_debug_label");
    }
#ifdef VIM_DEBUG_LABEL_WIRE
    const GLubyte *(*indexed)(GLenum,GLuint)=glGetStringi;
#else
    const GLubyte *(*indexed)(GLenum,GLuint)=dlsym(RTLD_DEFAULT,"glGetStringi");
#endif
    GLint count=0;glGetIntegerv(0x821d,&count); /* GL_NUM_EXTENSIONS */
    if(indexed)for(GLint i=0;i<count;++i){
        const char *ext=(const char *)indexed(GL_EXTENSIONS,(GLuint)i);
        if(ext && !strcmp(ext,"GL_EXT_debug_label"))return 1;
    }
    return 0;
}
int main(int argc,char **argv)
{
    setvbuf(stdout,NULL,_IONBF,0);
    int core=argc>1 && !strcmp(argv[1],"core");
#ifdef VIM_DEBUG_LABEL_WIRE
    const int attrs[]={0x56494d02,core?0x4100:0x1000,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(core?0x4100:0x1000),kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("label renderer=%s core=%d\n",glGetString(GL_RENDERER),core);
    if(!advertised(core))return 77;
    GLuint texture=0;
    glGenTextures(1,&texture);
    glBindTexture(GL_TEXTURE_2D,texture);
    label_query(texture,16,0,"");
    glLabelObjectEXT(GL_TEXTURE,texture,0,"abcdef");
    CHECK(glGetError()==GL_NO_ERROR);
    label_query(texture,16,6,"abcdef");
    label_query(texture,4,3,"abc");
    GLsizei length=-1;
    glGetObjectLabelEXT(GL_TEXTURE,texture,0,&length,NULL);
    GLenum error=glGetError();
    printf("label null length=%d error=%04x\n",length,error);
    CHECK(error==GL_NO_ERROR && length==6);
    char name[16]={0};
    glGetObjectLabelEXT(GL_TEXTURE,texture,sizeof(name),NULL,name);
    CHECK(glGetError()==GL_NO_ERROR && !strcmp(name,"abcdef"));
    glLabelObjectEXT(GL_TEXTURE,texture,3,"xyz-tail");
    label_query(texture,16,3,"xyz");
    glLabelObjectEXT(GL_TEXTURE,texture,0,NULL);
    label_query(texture,16,0,"");
    glDeleteTextures(1,&texture);
    name[0]='!';length=-1;
    glGetObjectLabelEXT(GL_TEXTURE,texture,sizeof(name),&length,name);
    error=glGetError();
    printf("label deleted length=%d error=%04x first=%02x\n",length,error,(unsigned char)name[0]);
    CHECK(error!=GL_NO_ERROR && length==-1 && name[0]=='!');
    return failures!=0;
}
