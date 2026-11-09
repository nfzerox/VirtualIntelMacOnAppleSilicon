/* Does Apple's SDK-declared CGL execute dispatch preserve list recording? */
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLContext.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <string.h>
static unsigned failures;
static void check(int value,const char *label)
{printf("%s %s\n",value?"PASS":"FAIL",label);fflush(stdout);failures+=!value;}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;CGLDestroyPixelFormat(pf);
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    GLIFunctionDispatch execute=c->disp;GLIContext renderer=c->rend;
    const unsigned char initial[]={17,31,47,255},private_value[]={201,101,51,171},later[]={33,55,77,99};
    for(int named=0;named<2;++named)for(int mode=GL_COMPILE;mode<=GL_COMPILE_AND_EXECUTE;++mode){
        GLuint texture=0,list=glGenLists(1);if(named)glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,initial);
        glNewList(list,mode);glColor3f(.75f,.5f,.25f);
        printf("named=%d mode=%x same_dispatch=%d same_renderer=%d\n",named,mode,
            execute.tex_image2D==c->disp.tex_image2D,renderer==c->rend);
        execute.tex_image2D(renderer,GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,private_value);
        unsigned char out[4]={0};glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,out);
        check(!glGetError()&&!memcmp(out,private_value,4),"saved execute dispatch updates immediately inside list");
        glEndList();
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,later);
        glCallList(list);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,out);
        GLfloat color[4]={0};glGetFloatv(GL_CURRENT_COLOR,color);
        check(!glGetError()&&!memcmp(out,later,4),"private execute command is absent from compiled list");
        check(color[0]==.75f&&color[1]==.5f&&color[2]==.25f,"caller list commands remain intact");
        glDeleteLists(list,1);if(named)glDeleteTextures(1,&texture);
    }
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);printf("execute-dispatch failures=%u\n",failures);return failures?1:0;
}
