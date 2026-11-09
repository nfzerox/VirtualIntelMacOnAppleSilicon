/* Check independently addressed viewport state and layered native rasterization. */
#ifdef VIM_VIEWPORT_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define glGenVertexArrays glGenVertexArraysAPPLE
#define glBindVertexArray glBindVertexArrayAPPLE
#define glGenFramebuffers glGenFramebuffersEXT
#define glBindFramebuffer glBindFramebufferEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#define glEnablei glEnableIndexedEXT
#define glDisablei glDisableIndexedEXT
#define glIsEnabledi glIsEnabledIndexedEXT
#define glGetIntegeri_v glGetIntegerIndexedvEXT
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#endif
#include <stdio.h>
#include <string.h>

enum { max_viewports=0x825b,viewport_bounds_range=0x825d,geometry_shader=0x8dd9 };
static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)

static GLuint shader(GLenum type,const char *source)
{
    GLuint s=glCreateShader(type);GLint ok=0;
    glShaderSource(s,1,&source,NULL);glCompileShader(s);glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[1024]={0};glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(ok);return s;
}
static void pixel(int x,int r,int g)
{
    GLubyte p[4]={0};glReadPixels(x,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    printf("viewport pixel x=%d rgba=%u,%u,%u,%u\n",x,p[0],p[1],p[2],p[3]);
    CHECK(glGetError()==GL_NO_ERROR && p[0]==r && p[1]==g && p[2]==0 && p[3]==255);
}

static void verify(void)
{
    GLint max=0;GLfloat limits[3]={0,0,37};
    glGetIntegerv(max_viewports,&max);glGetFloatv(viewport_bounds_range,limits);
    CHECK(glGetError()==GL_NO_ERROR && max>=2 && limits[0]<0 && limits[1]>8 && limits[2]==37);
    if(max<2)return;
    /* Native limits used to size the arrays/loops in core state reset. */
    const GLenum selectors[]={0x8869,0x8e59,0x8b4d,0x8824,0x8a2f,0x8c8b,0x0d32};
    for(unsigned i=0;i<sizeof(selectors)/sizeof(selectors[0]);++i){
        GLint value[2]={-1,37};
        glGetIntegerv(selectors[i],value);
        CHECK(glGetError()==GL_NO_ERROR && value[0]>0 && value[1]==37);
    }
    GLuint vao=0,fb=0,image=0;
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenTextures(1,&image);glBindTexture(GL_TEXTURE_2D,image);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,8,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,image,0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);

    const GLfloat views[]={0,0,4,4,4,0,4,4};
    const GLfloat fractional[]={4.25f,.25f,3.5f,3.5f};
    const GLint boxes[]={1,0,3,4,4,0,3,4};
    const GLdouble depths[]={.25,.75,.125,.875};
    glViewportArrayv(0,2,views);glScissorArrayv(0,2,boxes);glDepthRangeArrayv(0,2,depths);
    GLint unindexed[4]={-1};glGetIntegerv(GL_VIEWPORT,unindexed);
    CHECK(glGetError()==GL_NO_ERROR && unindexed[0]==0 && unindexed[2]==4);
    glViewportIndexedf(0,1,0,3,4);
    glGetIntegerv(GL_VIEWPORT,unindexed);CHECK(unindexed[0]==1 && unindexed[2]==3);
    glViewportIndexedfv(1,fractional);
    GLfloat f[5]={0,0,0,0,37};GLdouble d[5]={0,0,0,0,37};
    glGetFloati_v(GL_VIEWPORT,1,f);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(f,fractional,sizeof(fractional)) && f[4]==37);
    glGetDoublei_v(GL_DEPTH_RANGE,1,d);
    CHECK(glGetError()==GL_NO_ERROR && d[0]==.125 && d[1]==.875 && d[2]==0 && d[4]==37);
    glGetDoublei_v(GL_VIEWPORT,1,d);
    CHECK(glGetError()==GL_NO_ERROR && d[0]==4.25 && d[1]==.25 && d[2]==3.5 && d[3]==3.5 && d[4]==37);
    glGetFloati_v(GL_VIEWPORT,(GLuint)max,f);
    CHECK(glGetError()==GL_INVALID_VALUE && !memcmp(f,fractional,sizeof(fractional)) && f[4]==37);
    glGetDoublei_v(GL_TEXTURE_2D,0,d);
    CHECK(glGetError()==GL_INVALID_ENUM && d[0]==4.25 && d[4]==37);
    glDepthRangeIndexed(1,-.5,1.5);glGetDoublei_v(GL_DEPTH_RANGE,1,d);
    CHECK(glGetError()==GL_NO_ERROR && d[0]==0 && d[1]==1 && d[2]==3.5);
    glViewportArrayv(0,0,NULL);glScissorArrayv(0,0,NULL);glDepthRangeArrayv(0,0,NULL);
    CHECK(glGetError()==GL_NO_ERROR);
    glViewportArrayv(0,-1,NULL);CHECK(glGetError()==GL_INVALID_VALUE);
    glScissorIndexed(1,4,0,2,4);
    GLint box[5]={0,0,0,0,37};glGetIntegeri_v(GL_SCISSOR_BOX,1,box);
    CHECK(glGetError()==GL_NO_ERROR && box[0]==4 && box[2]==2 && box[4]==37);
    glScissorIndexedv(1,boxes+4);
    glGetIntegeri_v(GL_SCISSOR_BOX,1,box);
    CHECK(glGetError()==GL_NO_ERROR && box[2]==3 && box[4]==37);
    glEnablei(GL_SCISSOR_TEST,1);
    CHECK(!glIsEnabledi(GL_SCISSOR_TEST,0) && glIsEnabledi(GL_SCISSOR_TEST,1));
#ifdef VIM_VIEWPORT_WIRE
    /* The client swap helper, like the host blit helper, must preserve index 1. */
    DGClientSetBlitScissor(GL_FALSE);
    CHECK(!glIsEnabledi(GL_SCISSOR_TEST,0) && glIsEnabledi(GL_SCISSOR_TEST,1));
#endif
    glEnablei(GL_SCISSOR_TEST,0);
    glViewportArrayv(0,2,views);
    GLuint v=shader(GL_VERTEX_SHADER,"#version 410\n"
        "void main(){gl_Position=vec4(gl_VertexID==1?3:-1,gl_VertexID==2?3:-1,0,1);}");
    GLuint g=shader(geometry_shader,"#version 410\n"
        "layout(triangles) in;layout(triangle_strip,max_vertices=6) out;flat out vec4 tint;\n"
        "void main(){for(int v=0;v<2;++v){for(int i=0;i<3;++i){gl_Position=gl_in[i].gl_Position;"
        "gl_ViewportIndex=v;tint=v==0?vec4(1,0,0,1):vec4(0,1,0,1);EmitVertex();}EndPrimitive();}}");
    GLuint fshader=shader(GL_FRAGMENT_SHADER,"#version 410\nflat in vec4 tint;out vec4 color;void main(){color=tint;}");
    GLuint p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,g);glAttachShader(p,fshader);glLinkProgram(p);
    GLint linked=0;glGetProgramiv(p,GL_LINK_STATUS,&linked);
    if(!linked){char log[1024]={0};glGetProgramInfoLog(p,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(linked);
    if(linked){
        glUseProgram(p);glDrawArrays(GL_TRIANGLES,0,3);
        pixel(0,0,0);pixel(1,255,0);pixel(5,0,255);pixel(7,0,0);
    }
    glDisablei(GL_SCISSOR_TEST,1);
    CHECK(glIsEnabledi(GL_SCISSOR_TEST,0) && !glIsEnabledi(GL_SCISSOR_TEST,1) && glGetError()==GL_NO_ERROR);
}

int main(int argc,char **argv)
{
    const int gl4=argc>1 && !strcmp(argv[1],"--gl4");
#ifdef VIM_VIEWPORT_WIRE
    const int attrs[]={0x56494d02,gl4?DG_CGL_PROFILE_GL4:0x3200,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(c->connection->features&DG_FEATURE_VIEWPORT_ARRAY);
    if(gl4){
        CHECK(c->connection->features&DG_FEATURE_GL4_PROFILE);
        uint32_t features=c->connection->features;
        c->connection->features&=~DG_FEATURE_GL4_PROFILE;
        struct DGContext *unsupported=DGCreateContext(attrs,c);
        CHECK(!unsupported && !c->connection->dead);
        if(unsupported)DGDestroyContext(unsupported);
        c->connection->features=features;
    }
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(gl4?kCGLOGLPVersion_GL4_Core:kCGLOGLPVersion_3_2_Core),0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return gl4?77:2;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("viewport renderer=%s\n",glGetString(GL_RENDERER));
    verify();
    if(gl4){
        GLboolean state[2]={GL_TRUE,37};
        glGetBooleanv(0x8e24,state); /* GL_TRANSFORM_FEEDBACK_ACTIVE */
        CHECK(glGetError()==GL_NO_ERROR && state[0]==GL_FALSE && state[1]==37);
        glGetBooleanv(0x8e23,state); /* GL_TRANSFORM_FEEDBACK_PAUSED */
        CHECK(glGetError()==GL_NO_ERROR && state[0]==GL_FALSE && state[1]==37);
    }
#ifdef VIM_VIEWPORT_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("viewport failures=%d\n",failures);return failures?1:0;
}
