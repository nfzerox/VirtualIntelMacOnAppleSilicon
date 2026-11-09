/* Compare real per-target and per-sample rendering, not just dispatch success. */
#ifdef VIM_RENDER_TARGETS_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define glGenVertexArrays glGenVertexArraysAPPLE
#define glBindVertexArray glBindVertexArrayAPPLE
#define glDeleteVertexArrays glDeleteVertexArraysAPPLE
#define glGenFramebuffers glGenFramebuffersEXT
#define glBindFramebuffer glBindFramebufferEXT
#define glDeleteFramebuffers glDeleteFramebuffersEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#define glFramebufferTextureLayer glFramebufferTextureLayerEXT
#define glBlitFramebuffer glBlitFramebufferEXT
#define glEnablei glEnableIndexedEXT
#define glDisablei glDisableIndexedEXT
#define glGetIntegeri_v glGetIntegerIndexedvEXT
#define glBindFragDataLocation glBindFragDataLocationEXT
#define glDrawBuffers glDrawBuffersARB
#define glUniform1i glUniform1iARB
#define glGetUniformLocation(program,name) glGetUniformLocationARB((GLhandleARB)(uintptr_t)(program),name)
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#endif
#include <stdio.h>
#include <stdlib.h>

enum { texture_ms=0x9100, texture_ms_array=0x9102, sample_position=0x8e50,
       sample_mask=0x8e51, sample_mask_value=0x8e52, color_sample_limit=0x910e,
       texture_samples=0x9106, max_dual_source_buffers=0x88fc, src1_color=0x88f9 };
static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)

static GLuint shader(GLenum type,const char *source)
{
    GLuint s=glCreateShader(type);GLint compiled=0;
    glShaderSource(s,1,&source,NULL);glCompileShader(s);glGetShaderiv(s,GL_COMPILE_STATUS,&compiled);
    if(!compiled){char log[1024]={0};glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(compiled);return s;
}

static GLuint program(int mode)
{
    GLuint v=shader(GL_VERTEX_SHADER,"#version 150\n"
        "void main(){gl_Position=vec4(gl_VertexID==1?3:-1,gl_VertexID==2?3:-1,0,1);}");
    const char *sources[]={
        "#version 150\nout vec4 first,second;void main(){first=vec4(1,0,0,1);second=vec4(0,1,0,.5);}",
        "#version 150\nout vec4 first,second;void main(){first=vec4(1);second=vec4(0,1,0,1);}",
        "#version 150\nuniform sampler2DMSArray samples;uniform int lastSample;out vec4 first;"
        "void main(){ivec3 p=ivec3(1,1,1);first=vec4(texelFetch(samples,p,0).rgb*.75+"
        "texelFetch(samples,p,lastSample).rgb*.25,1);}",
    };
    GLuint f=shader(GL_FRAGMENT_SHADER,sources[mode]);
    GLuint p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);
    if(mode==1){
        glBindFragDataLocationIndexed(p,0,0,"first");glBindFragDataLocationIndexed(p,0,1,"second");
    }else{
        glBindFragDataLocation(p,0,"first");glBindFragDataLocation(p,1,"second");
    }
    glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);
    GLint linked=0;glGetProgramiv(p,GL_LINK_STATUS,&linked);
    if(!linked){char log[1024]={0};glGetProgramInfoLog(p,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(linked);return p;
}

static void expect_pixel(const char *label,GLenum attachment,int r,int g,int b,int a,int tolerance)
{
    GLubyte px[4]={0};glReadBuffer(attachment);glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    GLenum error=glGetError();
    printf("%s pixel=%u,%u,%u,%u error=%04x\n",label,px[0],px[1],px[2],px[3],error);
    CHECK(error==GL_NO_ERROR && abs(px[0]-r)<=tolerance &&
          abs(px[1]-g)<=tolerance && abs(px[2]-b)<=tolerance && px[3]==a);
}

static void multisample(GLuint p,GLuint target,GLuint image,GLuint resolved)
{
    GLint limit=0,count=0;
    glGetIntegerv(color_sample_limit,&limit);CHECK(glGetError()==GL_NO_ERROR && limit>=2);
    if(limit<2)return;
    GLsizei samples=limit>=4?4:2;
    glBindTexture(GL_TEXTURE_2D,image);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glBindFramebuffer(GL_FRAMEBUFFER,resolved);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,image,0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);

    GLuint ms[2]={0};glGenTextures(2,ms);
    glBindTexture(texture_ms,ms[0]);glTexImage2DMultisample(texture_ms,samples,GL_RGBA8,4,4,GL_TRUE);
    glGetTexLevelParameteriv(texture_ms,0,texture_samples,&count);
    CHECK(glGetError()==GL_NO_ERROR && count==samples);
    glBindTexture(texture_ms_array,ms[1]);
    glTexImage3DMultisample(texture_ms_array,samples,GL_RGBA8,4,4,2,GL_TRUE);
    glGetTexLevelParameteriv(texture_ms_array,0,texture_samples,&count);
    CHECK(glGetError()==GL_NO_ERROR && count==samples);
    glBindFramebuffer(GL_FRAMEBUFFER,target);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,texture_ms,ms[0],0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    GLfloat position[3]={-1,-1,37};
    glGetMultisamplefv(sample_position,0,position);
    CHECK(glGetError()==GL_NO_ERROR && position[0]>=0 && position[0]<1 &&
          position[1]>=0 && position[1]<1 && position[2]==37);
    const GLfloat x=position[0],y=position[1];
    glGetMultisamplefv(sample_position,(GLuint)samples,position);
    CHECK(glGetError()==GL_INVALID_VALUE && position[0]==x && position[1]==y && position[2]==37);
    glGetMultisamplefv(GL_TEXTURE_2D,0,position);
    CHECK(glGetError()==GL_INVALID_ENUM && position[0]==x && position[1]==y && position[2]==37);
    GLuint array_target=0;glGenFramebuffers(1,&array_target);
    for(int array=0;array<2;++array){
        glBindFramebuffer(GL_FRAMEBUFFER,array?array_target:target);
        if(array)glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,ms[1],0,1);
        CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
        glDisable(sample_mask);glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
        GLuint half=(1u<<(samples/2))-1;
        glSampleMaski(0,half);glEnable(sample_mask);
        GLint mask[2]={-1,37};glGetIntegeri_v(sample_mask_value,0,mask);
        CHECK(glGetError()==GL_NO_ERROR && (GLuint)mask[0]==half && mask[1]==37);
        glDrawArrays(GL_TRIANGLES,0,3);glDisable(sample_mask);
        if(array){
            // Read real samples directly. Native Iris Pro's FBO blit from
            // this array layer returns zero even though it reports no error.
            GLuint reader=program(2);glUseProgram(reader);
            glUniform1i(glGetUniformLocation(reader,"lastSample"),samples-1);
            glBindFramebuffer(GL_FRAMEBUFFER,resolved);
            glDrawArrays(GL_TRIANGLES,0,3);
            expect_pixel("multisample array fetch",GL_COLOR_ATTACHMENT0,191,0,64,255,1);
            glUseProgram(p);glDeleteProgram(reader);
        }else{
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,resolved);
            glBlitFramebuffer(0,0,4,4,0,0,4,4,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER,resolved);
            expect_pixel("multisample 2D resolve",GL_COLOR_ATTACHMENT0,128,0,128,255,1);
        }
    }
    glDeleteFramebuffers(1,&array_target);
    glDeleteTextures(2,ms);
}

static void verify(void)
{
    GLuint vao=0,fb[2]={0},tex[2]={0};
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenFramebuffers(2,fb);glGenTextures(2,tex);
    glViewport(0,0,4,4);
    GLuint p=program(0);glUseProgram(p);
    multisample(p,fb[0],tex[0],fb[1]);

    glBindFramebuffer(GL_FRAMEBUFFER,fb[0]);
    for(int i=0;i<2;++i){
        glBindTexture(GL_TEXTURE_2D,tex[i]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0+i,GL_TEXTURE_2D,tex[i],0);
    }
    const GLenum targets[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT0+1};
    glDrawBuffers(2,targets);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnablei(GL_BLEND,0);glEnablei(GL_BLEND,1);
    glBlendFunci(0,GL_ONE,GL_ONE);glBlendEquationi(0,GL_FUNC_SUBTRACT);
    glBlendFuncSeparatei(1,GL_ZERO,GL_ONE,GL_ONE,GL_ZERO);
    glBlendEquationSeparatei(1,GL_FUNC_ADD,GL_MAX);
    GLint factor[2]={-1,37};
    glGetIntegeri_v(GL_BLEND_DST_RGB,1,factor);
    CHECK(glGetError()==GL_NO_ERROR && factor[0]==GL_ONE && factor[1]==37);
    glGetIntegeri_v(GL_BLEND_EQUATION_ALPHA,1,factor);
    CHECK(glGetError()==GL_NO_ERROR && factor[0]==GL_MAX && factor[1]==37);
    glDrawArrays(GL_TRIANGLES,0,3);
    expect_pixel("indexed blend target 0",targets[0],255,0,0,0,0);
    expect_pixel("indexed blend target 1",targets[1],0,0,255,255,0);
    glDisablei(GL_BLEND,1);
    glBlendFunci(~0u,GL_ONE,GL_ZERO);CHECK(glGetError()==GL_INVALID_VALUE);

    GLint dual=0;glGetIntegerv(max_dual_source_buffers,&dual);
    CHECK(glGetError()==GL_NO_ERROR && dual>=1);
    if(dual>0){
        GLuint d=program(1);glUseProgram(d);
        CHECK(glGetFragDataIndex(d,"first")==0 && glGetFragDataIndex(d,"second")==1 &&
              glGetFragDataIndex(d,"notPresent")==-1);
        glDrawBuffers(1,targets);glBlendFunci(0,src1_color,GL_ZERO);glBlendEquationi(0,GL_FUNC_ADD);
        glDrawArrays(GL_TRIANGLES,0,3);
        expect_pixel("dual-source blend",targets[0],0,255,0,255,0);
        glDeleteProgram(d);
    }
    glUseProgram(0);glDeleteProgram(p);glDeleteFramebuffers(2,fb);glDeleteTextures(2,tex);
    glDeleteVertexArrays(1,&vao);CHECK(glGetError()==GL_NO_ERROR);
}

int main(void)
{
#ifdef VIM_RENDER_TARGETS_WIRE
    const int attrs[]={0x56494d02,0x3200,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    const unsigned features=DG_FEATURE_MULTISAMPLE_TEXTURE|DG_FEATURE_DUAL_SOURCE_BLEND|DG_FEATURE_INDEXED_BLEND;
    CHECK((c->connection->features&features)==features);
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf||CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("render targets renderer=%s\n",glGetString(GL_RENDERER));
    verify();
#ifdef VIM_RENDER_TARGETS_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("render target failures=%d\n",failures);return failures?1:0;
}
