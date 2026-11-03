/* Default framebuffer contents belong to the drawable, not its dimensions. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <stdlib.h>
static int color(const char *name,unsigned r,unsigned g,unsigned b)
{
    unsigned char p[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    int good=p[0]==r && p[1]==g && p[2]==b;
    printf("%s %s pixel=%u,%u,%u\n",good?"PASS":"FAIL",name,p[0],p[1],p[2]);return good;
}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n) || CGLCreateContext(pf,NULL,&c) || CGLSetCurrentContext(c)) return 2;
    void *primary=calloc(64*64,4),*same=calloc(64*64,4),*small=calloc(32*32,4);
    if(!primary || !same || !small) return 3;
    CGLSetOffScreen(c,64,64,256,primary);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    CGLSetOffScreen(c,64,64,256,same);glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
    CGLSetOffScreen(c,64,64,256,primary);int good=color("same-size drawables remain distinct",255,0,0);
    CGLSetOffScreen(c,32,32,128,small);glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    CGLSetOffScreen(c,64,64,256,primary);good=color("primary survives size switches",255,0,0) && good;
    CGLSetOffScreen(c,64,64,256,same);good=color("secondary contents retained",0,255,0) && good;
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);CGLDestroyPixelFormat(pf);
    free(primary);free(same);free(small);return !good;
}
