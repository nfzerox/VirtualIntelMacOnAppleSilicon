/* Finite GPU-pixel qualification of the core 1.3 texture-combine operations
 * Leopard Core Animation requires under the older ARB extension spelling.
 * Run unchanged on native hardware and the forwarded context. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int failures;
static int has_extension(const char *extensions,const char *name)
{
    size_t n=strlen(name);const char *p=extensions;
    while(p&&(p=strstr(p,name))){
        if((p==extensions||p[-1]==' ')&&(p[n]==' '||!p[n]))return 1;
        p+=n;
    }
    return 0;
}
int main(int argc,char **argv)
{
    CGLPixelFormatAttribute a[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj f=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(a,&f,&n)||!f||CGLCreateContext(f,NULL,&c)||!c)return 2;
    CGLDestroyPixelFormat(f);if(CGLSetCurrentContext(c))return 2;
    printf("renderer=%s version=%s bits=%u\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),(unsigned)(8*sizeof(void*)));
    const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
    printf("combine3=%d combine4=%d imaging=%d\n",has_extension(extensions,"GL_ATI_texture_env_combine3"),
        has_extension(extensions,"GL_NV_texture_env_combine4"),has_extension(extensions,"GL_ARB_imaging"));
    const char *required[]={"GL_ARB_multitexture","GL_ARB_texture_env_combine","GL_ARB_texture_cube_map",
        "GL_ARB_texture_env_dot3","GL_EXT_blend_color","GL_NV_blend_square","GL_ATI_texture_env_combine3"};
    for(unsigned i=0;i<sizeof(required)/sizeof(required[0]);++i){
        int found=has_extension(extensions,required[i]);
        printf("discovery %s=%d\n",required[i],found);
        if(argc==2&&!strcmp(argv[1],"--require-discovery")&&!found)++failures;
    }
    GLint units=0;glGetIntegerv(GL_MAX_TEXTURE_UNITS,&units);
    printf("texture_units=%d\n",units);if(units<2)return 2;
    GLuint source,target,fb;glGenTextures(1,&source);glGenTextures(1,&target);
    glBindTexture(GL_TEXTURE_2D,target);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,target,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 2;
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glViewport(0,0,4,4);
    glBindTexture(GL_TEXTURE_2D,source);unsigned char texel[]={64,128,192,255};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,texel);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glEnable(GL_TEXTURE_2D);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_COMBINE);
    glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB,GL_TEXTURE);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_RGB,GL_PRIMARY_COLOR);
    glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_RGB,GL_CONSTANT);glTexEnvi(GL_TEXTURE_ENV,GL_OPERAND2_RGB,GL_SRC_COLOR);
    GLfloat constant[]={.25f,.25f,.25f,1};
    glTexEnvfv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_COLOR,constant);
    const GLenum modes[]={GL_REPLACE,GL_MODULATE,GL_ADD,GL_ADD_SIGNED,GL_INTERPOLATE,GL_SUBTRACT,
        GL_DOT3_RGB,GL_DOT3_RGBA,GL_MODULATE_ADD_ATI,GL_MODULATE_SIGNED_ADD_ATI,GL_MODULATE_SUBTRACT_ATI};
    const int expected[][3]={{64,128,192},{32,64,96},{192,255,255},{64,128,192},{112,128,144},{0,0,64},
        {128,128,128},{128,128,128},{144,160,176},{16,32,48},{0,16,32}};
    for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);++i){
        glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB,modes[i]);
        if(i==6||i==7)glColor4f(.25f,.5f,.75f,1);
        else if(i==10)glColor4f(.0625f,.0625f,.0625f,1);else glColor4f(.5f,.5f,.5f,1);
        glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();
        unsigned char p[4]={0};glReadPixels(2,2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);GLenum e=glGetError();
        int good=!e;for(unsigned k=0;k<3;++k)if(abs((int)p[k]-expected[i][k])>1)good=0;
        if(i>=6&&abs((int)p[3]-(i==7?128:255))>1)good=0;
        printf("%s mode=%x pixel=%u,%u,%u error=%x\n",good?"PASS":"FAIL",modes[i],p[0],p[1],p[2],e);failures+=!good;
    }
    constant[3]=.5f;glTexEnvfv(GL_TEXTURE_ENV,GL_TEXTURE_ENV_COLOR,constant);
    glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB,GL_REPLACE);
    glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA,GL_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_ALPHA,GL_PRIMARY_COLOR);
    glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE2_ALPHA,GL_CONSTANT);
    const int expected_alpha[]={191,64,96};
    for(unsigned i=0;i<3;++i){
        glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA,modes[8+i]);glColor4f(1,1,1,i==2?.125f:.25f);
        glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();
        unsigned char p[4]={0};glReadPixels(2,2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);GLenum e=glGetError();
        int good=!e&&p[0]==64&&p[1]==128&&p[2]==192&&abs((int)p[3]-expected_alpha[i])<=1;
        printf("%s combine3_alpha mode=%x alpha=%u error=%x\n",good?"PASS":"FAIL",modes[8+i],p[3],e);failures+=!good;
    }
    glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_RGB,GL_MODULATE_ADD_ATI);
    glTexEnvi(GL_TEXTURE_ENV,GL_COMBINE_ALPHA,GL_MODULATE_ADD_ATI);
    glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_RGB,GL_ONE);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_RGB,GL_ZERO);
    glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE0_ALPHA,GL_ONE);glTexEnvi(GL_TEXTURE_ENV,GL_SOURCE1_ALPHA,GL_ZERO);
    glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_QUADS);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();
    unsigned char selectors[4]={0};GLint source_selector=-1;
    glGetTexEnviv(GL_TEXTURE_ENV,GL_SOURCE0_RGB,&source_selector);
    glReadPixels(2,2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,selectors);GLenum selector_error=glGetError();
    int selector_good=!selector_error&&source_selector==GL_ONE&&abs((int)selectors[0]-64)<=1&&
        abs((int)selectors[1]-64)<=1&&abs((int)selectors[2]-64)<=1&&abs((int)selectors[3]-128)<=1;
    printf("%s combine3_zero_one pixel=%u,%u,%u,%u selector=%x error=%x\n",selector_good?"PASS":"FAIL",
        selectors[0],selectors[1],selectors[2],selectors[3],source_selector,selector_error);failures+=!selector_good;
    /* Select DIFFERENT texels on each unit. Identical constant textures would
     * miss a dropped active-unit selector or second coordinate stream. */
    GLuint second;glGenTextures(1,&second);
    const unsigned char pixels[2][16]={{128,255,128,255,0,0,0,255,0,0,0,255,0,0,0,255},
        {0,0,0,255,0,0,0,255,0,0,0,255,255,128,128,255}};
    for(unsigned unit=0;unit<2;++unit){
        glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,unit?second:source);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels[unit]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,unit?GL_MODULATE:GL_REPLACE);glEnable(GL_TEXTURE_2D);
    }
    static const GLfloat vertices[]={-1,-1,1,-1,1,1,-1,1};
    static const GLfloat coords[2][8]={{.25f,.25f,.25f,.25f,.25f,.25f,.25f,.25f},
        {.75f,.75f,.75f,.75f,.75f,.75f,.75f,.75f}};
    GLuint buffers[3]={0};glGenBuffers(3,buffers);
    const GLushort indices[]={0,1,3,2};
    for(unsigned arrays=0;arrays<3;++arrays){
        glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
        if(arrays){
            if(arrays==2){
                glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);
                glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
            }
            glVertexPointer(2,GL_FLOAT,0,arrays==2?NULL:vertices);glEnableClientState(GL_VERTEX_ARRAY);
            for(unsigned unit=0;unit<2;++unit){
                if(arrays==2){
                    glBindBuffer(GL_ARRAY_BUFFER,buffers[unit+1]);
                    glBufferData(GL_ARRAY_BUFFER,sizeof(coords[unit]),coords[unit],GL_STATIC_DRAW);
                }
                glClientActiveTexture(GL_TEXTURE0+unit);glTexCoordPointer(2,GL_FLOAT,0,arrays==2?NULL:coords[unit]);
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            }
            if(arrays==2){
                GLuint element;glGenBuffers(1,&element);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,element);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
                glDrawRangeElements(GL_TRIANGLE_STRIP,0,3,4,GL_UNSIGNED_SHORT,NULL);
                glDeleteBuffers(1,&element);
            }else glDrawArrays(GL_QUADS,0,4);
        }else{
            glBegin(GL_QUADS);
            for(unsigned v=0;v<4;++v){
                glMultiTexCoord2f(GL_TEXTURE0,.25f,.25f);glMultiTexCoord2f(GL_TEXTURE1,.75f,.75f);
                glVertex2f(vertices[2*v],vertices[2*v+1]);
            }
            glEnd();
        }
        unsigned char p[4]={0};glReadPixels(2,2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);GLenum e=glGetError();
        int good=!e&&abs((int)p[0]-128)<=1&&abs((int)p[1]-128)<=1&&abs((int)p[2]-64)<=1;
        printf("%s multitexture_%s pixel=%u,%u,%u error=%x\n",good?"PASS":"FAIL",
            arrays==2?"indexed_vbo":arrays?"arrays":"immediate",p[0],p[1],p[2],e);failures+=!good;
    }
    glDeleteBuffers(3,buffers);
    for(unsigned unit=0;unit<2;++unit){glActiveTexture(GL_TEXTURE0+unit);glDisable(GL_TEXTURE_2D);}
    glEnable(GL_BLEND);glBlendColor(.25f,.5f,.75f,.5f);glBlendFunc(GL_CONSTANT_COLOR,GL_ONE_MINUS_CONSTANT_COLOR);
    glClearColor(.8f,.4f,.2f,1);glClear(GL_COLOR_BUFFER_BIT);glColor4f(.4f,.8f,.6f,1);
    glBegin(GL_QUADS);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();
    unsigned char blended[4]={0};glReadPixels(2,2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,blended);GLenum blend_error=glGetError();
    int blend_good=!blend_error&&abs((int)blended[0]-179)<=1&&abs((int)blended[1]-153)<=1&&abs((int)blended[2]-128)<=1;
    printf("%s constant_blend pixel=%u,%u,%u error=%x\n",blend_good?"PASS":"FAIL",blended[0],blended[1],blended[2],blend_error);
    failures+=!blend_good;
    glBlendFunc(GL_SRC_COLOR,GL_DST_COLOR);
    glClearColor(.8f,.4f,.2f,1);glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_QUADS);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();
    glReadPixels(2,2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,blended);blend_error=glGetError();
    blend_good=!blend_error&&abs((int)blended[0]-204)<=1&&abs((int)blended[1]-204)<=1&&abs((int)blended[2]-102)<=1;
    printf("%s square_blend pixel=%u,%u,%u error=%x\n",blend_good?"PASS":"FAIL",blended[0],blended[1],blended[2],blend_error);
    failures+=!blend_good;
    glDeleteTextures(1,&second);
    glDeleteFramebuffersEXT(1,&fb);glDeleteTextures(1,&source);glDeleteTextures(1,&target);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);return failures?1:0;
}
