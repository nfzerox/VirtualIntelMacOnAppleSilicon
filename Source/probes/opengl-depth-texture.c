/* GL_ARB_depth_texture: discovery plus real depth-texture rendering in a
 * legacy CGL context, unchanged on a physical Mac and in a guest. Every
 * result is a native GL query, a float texture readback, or GPU pixels; a
 * depth renderbuffer or an accepted enum alone is not counted as support.
 * --exercise-unadvertised runs the rendering checks without discovery.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SIZE 16
static unsigned failures;
static GLuint color_target;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);fflush(stdout);failures+=!good;}
static int advertised(const char *wanted)
{
    const char *all=(const char *)glGetString(GL_EXTENSIONS);size_t n=strlen(wanted);
    for(const char *p=all;p&&(p=strstr(p,wanted));p+=n)
        if((p==all||p[-1]==' ')&&(p[n]==' '||!p[n]))return 1;
    return 0;
}
static float ramp(int x){return (float)x/(SIZE-1);}
static void quad(float z)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex3f(-1,-1,z);glTexCoord2f(1,0);glVertex3f(1,-1,z);
    glTexCoord2f(1,1);glVertex3f(1,1,z);glTexCoord2f(0,1);glVertex3f(-1,1,z);
    glEnd();
}
/* Sample the bound depth texture over the colour target; compare each
 * column's red/alpha with the expected depth, in 8-bit units. */
static int sampled(float (*expected)(int),int alpha_too,const char *name)
{
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,color_target);
    glDisable(GL_DEPTH_TEST);glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);quad(0);glDisable(GL_TEXTURE_2D);
    GLubyte pixels[SIZE*SIZE*4];memset(pixels,0x5a,sizeof(pixels));
    glReadPixels(0,0,SIZE,SIZE,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    GLenum error=glGetError();int good=!error;
    for(int y=0;y<SIZE&&good;++y)for(int x=0;x<SIZE;++x){
        const GLubyte *p=pixels+4*(y*SIZE+x);int want=(int)lround(expected(x)*255);
        if(abs(p[0]-want)>2||abs(p[1]-want)>2||abs(p[2]-want)>2||(alpha_too&&abs(p[3]-want)>2)){
            printf("pixel %d,%d=%u,%u,%u,%u expected %d error=%x\n",x,y,p[0],p[1],p[2],p[3],want,error);good=0;break;}
    }
    check(good,name);return good;
}
static float half(int x){(void)x;return .5f;}
static float three_quarters(int x){(void)x;return .75f;}
static float shadow(int x){return ramp(x)>=.5f?1.f:0.f;}
static int readback(GLuint texture,float (*expected)(int),float tolerance,const char *name)
{
    glBindTexture(GL_TEXTURE_2D,texture);
    float values[SIZE*SIZE+1];values[SIZE*SIZE]=1234.5f;
    glGetTexImage(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT,GL_FLOAT,values);
    GLenum error=glGetError();int good=!error&&values[SIZE*SIZE]==1234.5f;
    for(int i=0;i<SIZE*SIZE&&good;++i)if(fabsf(values[i]-expected(i%SIZE))>tolerance){
        printf("depth[%d]=%.7f expected %.7f\n",i,values[i],expected(i%SIZE));good=0;}
    if(error)printf("readback error=%x\n",error);
    check(good,name);return good;
}
static GLuint depth_texture(const float *data)
{
    GLuint t=0;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH_COMPONENT24_ARB,SIZE,SIZE,0,GL_DEPTH_COMPONENT,GL_FLOAT,data);
    glTexParameteri(GL_TEXTURE_2D,GL_DEPTH_TEXTURE_MODE_ARB,GL_LUMINANCE);
    return t;
}
int main(int argc,char **argv)
{
    int exercise=argc==2&&!strcmp(argv[1],"--exercise-unadvertised");
    if(argc!=1&&!exercise)return 64;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,(CGLPixelFormatAttribute)0};
    CGLPixelFormatObj format=NULL;CGLContextObj ctx=NULL;GLint count=0,hardware=0;
    CGLError status=CGLChoosePixelFormat(attrs,&format,&count);
    if(!status&&format)status=CGLDescribePixelFormat(format,0,kCGLPFAAccelerated,&hardware);
    if(status||!format||!count||!hardware){printf("SKIP accelerated format CGL=%d\n",status);return 77;}
    status=CGLCreateContext(format,NULL,&ctx);CGLDestroyPixelFormat(format);
    if(status||!ctx||CGLSetCurrentContext(ctx))return 2;
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    int has=advertised("GL_ARB_depth_texture");
    check(has,"GL_ARB_depth_texture is advertised");
    if(!has&&!exercise)return 1;
    if(!advertised("GL_EXT_framebuffer_object")){printf("SKIP framebuffer objects unavailable\n");return failures?1:77;}

    /* Colour target for sampling depth textures. */
    GLuint fbo[2]={0},color=0;glGenFramebuffersEXT(2,fbo);
    color_target=fbo[0];
    glGenRenderbuffersEXT(1,&color);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,color);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,SIZE,SIZE);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo[0]);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,color);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT&&!glGetError(),"colour target complete");
    glViewport(0,0,SIZE,SIZE);glDisable(GL_DITHER);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();

    /* 1. Upload a per-column depth ramp and inspect it natively. */
    float data[SIZE*SIZE];for(int i=0;i<SIZE*SIZE;++i)data[i]=ramp(i%SIZE);
    GLuint uploaded=depth_texture(data);
    GLint bits=0,internal=0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_DEPTH_SIZE_ARB,&bits);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&internal);
    printf("depth bits=%d internal=%x\n",bits,internal);
    check(!glGetError()&&bits>=16,"TEXTURE_DEPTH_SIZE reports a depth texture");
    GLint mode=0;glGetTexParameteriv(GL_TEXTURE_2D,GL_DEPTH_TEXTURE_MODE_ARB,&mode);
    check(!glGetError()&&mode==GL_LUMINANCE,"DEPTH_TEXTURE_MODE set/query");
    readback(uploaded,ramp,1e-5f,"float upload reads back exactly");

    /* 2. Sample it on the GPU as luminance, then as intensity. */
    glBindTexture(GL_TEXTURE_2D,uploaded);sampled(ramp,0,"luminance depth sampling");
    glBindTexture(GL_TEXTURE_2D,uploaded);glTexParameteri(GL_TEXTURE_2D,GL_DEPTH_TEXTURE_MODE_ARB,GL_INTENSITY);
    sampled(ramp,1,"intensity depth sampling");

    /* 3. Render into a depth texture (the shadow-map path), then sample. */
    GLuint target=depth_texture(NULL);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo[1]);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_DEPTH_ATTACHMENT_EXT,GL_TEXTURE_2D,target,0);
    glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);
    GLenum complete=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
    check(complete==GL_FRAMEBUFFER_COMPLETE_EXT&&!glGetError(),"depth texture is a complete depth attachment");
    glClearDepth(1);glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_ALWAYS);glDepthMask(GL_TRUE);
    quad(.5f); /* NDC z=0.5 -> window depth 0.75 */
    glDisable(GL_DEPTH_TEST);glFlush();
    readback(target,three_quarters,1e-4f,"rendered depth reads back");
    glBindTexture(GL_TEXTURE_2D,target);sampled(three_quarters,0,"rendered depth texture sampling");

    /* 4. Copy a depth buffer into a texture. */
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo[1]);
    glClearDepth(.5);glClear(GL_DEPTH_BUFFER_BIT);
    GLuint copied=depth_texture(NULL);glBindTexture(GL_TEXTURE_2D,copied);
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,SIZE,SIZE);
    check(!glGetError(),"depth CopyTexSubImage accepted");
    readback(copied,half,1e-4f,"copied depth reads back");

    /* 5. Depth comparison, only where the guest also sees ARB_shadow. */
    if(advertised("GL_ARB_shadow")||exercise){
        glBindTexture(GL_TEXTURE_2D,uploaded);
        glTexParameteri(GL_TEXTURE_2D,GL_DEPTH_TEXTURE_MODE_ARB,GL_LUMINANCE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE_ARB,GL_COMPARE_R_TO_TEXTURE_ARB);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_FUNC_ARB,GL_LEQUAL);
        /* r=0.5 passes where the stored depth is at least 0.5. */
        glMatrixMode(GL_TEXTURE);glLoadIdentity();glTranslatef(0,0,.5f);glMatrixMode(GL_MODELVIEW);
        sampled(shadow,0,"ARB_shadow comparison");
        glMatrixMode(GL_TEXTURE);glLoadIdentity();glMatrixMode(GL_MODELVIEW);
    }else printf("SKIP ARB_shadow comparison (not advertised)\n");

    printf("%s %u failure(s)\n",failures?"FAIL":"PASS",failures);
    CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);
    return failures?1:0;
}
