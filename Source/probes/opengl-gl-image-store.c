/* Exact scalar Get shapes used by Core Image; same binary on physical Intel
 * and the real GLI guest route. Guards catch an accidentally vector-sized copy.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <string.h>
static int failures;
#define QUERY(TYPE,GET,EXPECTED) do { \
    struct { unsigned char before[16];TYPE value;unsigned char after[16]; } v; \
    memset(&v,0xa5,sizeof(v));GET(key,&v.value); \
    int pass=glGetError()==GL_NO_ERROR && v.value==(TYPE)(EXPECTED); \
    for(unsigned i=0;i<16;++i)pass&=v.before[i]==0xa5 && v.after[i]==0xa5; \
    printf("%s %s scalar %x expected=%d actual=%g\n",pass?"PASS":"FAIL",#GET,key,(int)(EXPECTED),(double)v.value); \
    failures+=!pass; \
} while(0)
static void state(GLenum key,int expected)
{
    QUERY(GLint,glGetIntegerv,expected);QUERY(GLfloat,glGetFloatv,expected);
    QUERY(GLdouble,glGetDoublev,expected);QUERY(GLboolean,glGetBooleanv,expected!=0);
}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&format,&count)||!format||CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    const char *renderer=(const char *)glGetString(GL_RENDERER);printf("renderer=%s\n",renderer?renderer:"none");
    if(!renderer||!strstr(renderer,"Intel"))return 2;
    const GLenum keys[]={GL_PACK_IMAGE_HEIGHT,GL_PACK_SKIP_IMAGES,GL_UNPACK_IMAGE_HEIGHT,GL_UNPACK_SKIP_IMAGES};
    for(unsigned i=0;i<4;++i){
        GLenum key=keys[i];state(key,0);int value=11+i;
        glPixelStorei(key,value);state(key,value);
        glPixelStorei(key,-1);int pass=glGetError()==GL_INVALID_VALUE;
        printf("%s negative pixel-store value rejected\n",pass?"PASS":"FAIL");failures+=!pass;state(key,value);
        glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);glPixelStorei(key,0);state(key,0);
        glPopClientAttrib();state(key,value);glPixelStorei(key,0);
    }
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(format);
    printf("image-store query failures=%d\n",failures);return failures?1:0;
}
