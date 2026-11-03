/* Typed fixed-function vector/state codecs used by stock Mavericks Chess. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <math.h>
static int failures;
static void values(const char *name,const GLfloat *actual,const GLfloat *expected,int count)
{
    GLenum error=glGetError();int pass=!error;
    for(int i=0;i<count;++i) if(fabsf(actual[i]-expected[i])>0.0001f) pass=0;
    printf("%s %s error=%x\n",pass?"PASS":"FAIL",name,error);if(!pass) ++failures;
}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint count=0;
    CGLError error=CGLChoosePixelFormat(attrs,&pf,&count);
    if(!error && pf) error=CGLCreateContext(pf,NULL,&c);
    if(error || !c) return 2;CGLSetCurrentContext(c);
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    const GLfloat diffuse[]={0.25f,0.5f,0.75f,1},direction[]={0,0,-1},shine=37;
    GLfloat actual[4]={0};
    glLightfv(GL_LIGHT0,GL_DIFFUSE,diffuse);glGetLightfv(GL_LIGHT0,GL_DIFFUSE,actual);
    values("light vector",actual,diffuse,4);
    glLightfv(GL_LIGHT0,GL_SPOT_DIRECTION,direction);glGetLightfv(GL_LIGHT0,GL_SPOT_DIRECTION,actual);
    values("light direction",actual,direction,3);
    glMaterialfv(GL_FRONT,GL_AMBIENT_AND_DIFFUSE,diffuse);glGetMaterialfv(GL_FRONT,GL_DIFFUSE,actual);
    values("material vector",actual,diffuse,4);
    glMaterialfv(GL_FRONT,GL_SHININESS,&shine);glGetMaterialfv(GL_FRONT,GL_SHININESS,actual);
    values("material scalar",actual,&shine,1);
    GLint state=-1;glGetIntegerv(GL_UNPACK_LSB_FIRST,&state);
    if(glGetError() || state) {puts("FAIL unpack scalar query");++failures;}else puts("PASS unpack scalar query");
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);CGLDestroyPixelFormat(pf);return !!failures;
}
