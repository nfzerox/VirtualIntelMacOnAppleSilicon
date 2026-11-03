/* Finite ordinary-GL capability audit. This is NOT a CGL-pbuffer test or
 * conformance suite. Run the same binary on Intel Iris Pro and our guest. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
static int checkPixel(const char *name,unsigned r,unsigned g,unsigned b) {
    unsigned char p[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    GLenum error=glGetError();int ok=!error&&p[0]==r&&p[1]==g&&p[2]==b&&p[3]==255;
    printf("%s %s pixel=%u,%u,%u,%u error=%x\n",ok?"PASS":"FAIL",name,p[0],p[1],p[2],p[3],error);return ok;
}
static void quad(float z) {
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex3f(-1,-1,z);glTexCoord2f(1,0);glVertex3f(1,-1,z);
    glTexCoord2f(1,1);glVertex3f(1,1,z);glTexCoord2f(0,1);glVertex3f(-1,1,z);glEnd();
}
static void cubequad(const float *v) {
    glBegin(GL_QUADS);
    glTexCoord3fv(v);glVertex2f(-1,-1);glTexCoord3fv(v);glVertex2f(1,-1);
    glTexCoord3fv(v);glVertex2f(1,1);glTexCoord3fv(v);glVertex2f(-1,1);glEnd();
}
int main(int argc,char **argv) {
    if(argc>2||(argc==2&&strcmp(argv[1],"--intel")&&strcmp(argv[1],"--desktop")))return 64;
    int intel=argc==2&&!strcmp(argv[1],"--intel");
    if(argc==2&&!strcmp(argv[1],"--desktop")){
        int accelerated=CGDisplayUsesOpenGLAcceleration(CGMainDisplayID());
        printf("desktop accelerated=%d\n",accelerated);
        // An app-owned GPU context is not evidence that WindowServer uses
        // its accelerated compositor. Never retire installation media on
        // the strength of the offscreen pixel tests alone.
        if(!accelerated)return 4;
    }
    // AutomaticGraphicsSwitching was added in 10.8. Lion's native chooser
    // rejects it before consulting any renderer; it is not a GL requirement.
    char kernel[80]={0};size_t bytes=sizeof(kernel);
    if(sysctlbyname("kern.osrelease",kernel,&bytes,NULL,0)||bytes>=sizeof(kernel))return 1;
    CGLPixelFormatAttribute attrs[6]={0};unsigned attribute=0;
    attrs[attribute++]=kCGLPFAAccelerated;attrs[attribute++]=kCGLPFANoRecovery;
    if(strtoul(kernel,NULL,10)>=12)attrs[attribute++]=kCGLPFASupportsAutomaticGraphicsSwitching;
    if(intel){attrs[attribute++]=kCGLPFARendererID;attrs[attribute++]=(CGLPixelFormatAttribute)0x1024502;}
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint count=0,renderer=0;
    CGLError status=CGLChoosePixelFormat(attrs,&pf,&count);
    if(status||!pf){printf("pixel format error=%d count=%d\n",status,count);return 2;}
    status=CGLCreateContext(pf,NULL,&context);
    if(status||!context){printf("context creation error=%d\n",status);CGLDestroyPixelFormat(pf);return 2;}
    status=CGLSetCurrentContext(context);
    if(status){printf("current context error=%d\n",status);CGLDestroyContext(context);CGLDestroyPixelFormat(pf);return 2;}
    CGLGetParameter(context,kCGLCPCurrentRendererID,&renderer);
    printf("renderer=%s id=%x version=%s\n",glGetString(GL_RENDERER),renderer,glGetString(GL_VERSION));
    if(intel&&(renderer&0xffffff00)!=0x1024500)return 77;
    GLuint fbo,color,depth,texture,cube;
    glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,color,0);
    glGenRenderbuffersEXT(1,&depth);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,depth);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_DEPTH_COMPONENT24,16,16);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_DEPTH_ATTACHMENT_EXT,GL_RENDERBUFFER_EXT,depth);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 3;
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glViewport(0,0,1,1);
    glClearDepth(1);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);
    glColor4f(1,0,0,1);quad(0);glColor4f(0,0,1,1);quad(.5f);
    int ok=checkPixel("depth24 occludes farther geometry",255,0,0);
    glColor4f(0,1,0,1);quad(-.5f);ok=checkPixel("depth24 admits nearer geometry",0,255,0)&&ok;glDisable(GL_DEPTH_TEST);
    unsigned char red[16*16*4],green[8*8*4];
    for(unsigned i=0;i<sizeof(red);i+=4){red[i]=255;red[i+1]=red[i+2]=0;red[i+3]=255;}
    for(unsigned i=0;i<sizeof(green);i+=4){green[i]=green[i+2]=0;green[i+1]=green[i+3]=255;}
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,1);glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_LOD,1);glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_LOD,1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,red);
    glTexImage2D(GL_TEXTURE_2D,1,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,green);
    quad(0);ok=checkPixel("explicit ordinary 2D mip level",0,255,0)&&ok;
    glGenerateMipmapEXT(GL_TEXTURE_2D);quad(0);ok=checkPixel("generated ordinary 2D mip level",255,0,0)&&ok;
    // NPOT is a real compositor-admission requirement on 10.8, not just a
    // texture-size query. Exercise full repeat sampling and generated levels
    // before advertising GL_ARB_texture_non_power_of_two to native cgls.
    unsigned char npot[3*5*4];
    for(unsigned y=0;y<5;++y)for(unsigned x=0;x<3;++x){
        unsigned i=(y*3+x)*4;npot[i]=x==0?255:0;npot[i+1]=x==1?255:0;npot[i+2]=x==2?255:0;npot[i+3]=255;
    }
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_LOD,0);glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_LOD,0);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,2);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,3,5,0,GL_RGBA,GL_UNSIGNED_BYTE,npot);
    glBegin(GL_QUADS);
    glTexCoord2f(1,1);glVertex2f(-1,-1);glTexCoord2f(2,1);glVertex2f(1,-1);
    glTexCoord2f(2,2);glVertex2f(1,1);glTexCoord2f(1,2);glVertex2f(-1,1);glEnd();
    ok=checkPixel("NPOT 3x5 repeat texture",0,255,0)&&ok;
    for(unsigned i=0;i<sizeof(npot);i+=4){npot[i]=npot[i+3]=255;npot[i+1]=npot[i+2]=0;}
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,3,5,GL_RGBA,GL_UNSIGNED_BYTE,npot);
    glGenerateMipmapEXT(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST);
    glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_LOD,2);glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_LOD,2);
    quad(0);ok=checkPixel("NPOT generated 1x1 mip level",255,0,0)&&ok;
    glDisable(GL_TEXTURE_2D);
    glGenTextures(1,&cube);glBindTexture(GL_TEXTURE_CUBE_MAP,cube);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    const unsigned char colors[6][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255},{0,255,255,255},{255,0,255,255}};
    const float directions[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for(unsigned i=0;i<6;i++)glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+i,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,colors[i]);
    glEnable(GL_TEXTURE_CUBE_MAP);
    for(unsigned i=0;i<6;i++){char name[64];snprintf(name,sizeof(name),"ordinary cube-map face %u",i);cubequad(directions[i]);ok=checkPixel(name,colors[i][0],colors[i][1],colors[i][2])&&ok;}
    glDisable(GL_TEXTURE_CUBE_MAP);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteRenderbuffersEXT(1,&depth);
    glDeleteTextures(1,&cube);glDeleteTextures(1,&texture);glDeleteTextures(1,&color);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(pf);return ok?0:1;
}
