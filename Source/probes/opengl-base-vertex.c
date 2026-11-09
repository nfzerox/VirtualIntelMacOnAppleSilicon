/* Public CGL and wire fixture: require native vertex IDs, not rebased input
 * pointers that only happen to fetch the right vertex positions. */
#ifdef VIM_BASE_VERTEX_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define glGenVertexArrays glGenVertexArraysAPPLE
#define glBindVertexArray glBindVertexArrayAPPLE
#define glBindAttribLocation(p,i,n) glBindAttribLocationARB((GLhandleARB)(uintptr_t)(p),(i),(n))
#define glVertexAttribPointer glVertexAttribPointerARB
#define glEnableVertexAttribArray glEnableVertexAttribArrayARB
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
extern void glGenVertexArrays(GLsizei,GLuint *);
extern void glBindVertexArray(GLuint);
extern void glPrimitiveRestartIndex(GLuint);
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
#define PRIMITIVE_RESTART 0x8f9du

static GLuint shader(GLenum type,const char *source)
{
    GLuint s=glCreateShader(type);glShaderSource(s,1,&source,NULL);glCompileShader(s);
    GLint good=0;glGetShaderiv(s,GL_COMPILE_STATUS,&good);
    if(!good){char log[2048]={0};glGetShaderInfoLog(s,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);}
    CHECK(good);return s;
}
static GLuint program(void)
{
    GLuint v=shader(GL_VERTEX_SHADER,"#version 150\nin vec2 point;flat out vec4 color;"
        "void main(){gl_Position=vec4(point,0,1);color=gl_VertexID<3?vec4(0,1,0,1):"
        "(gl_InstanceID==0?vec4(1,0,0,1):vec4(0,0,1,1));}");
    GLuint f=shader(GL_FRAGMENT_SHADER,"#version 150\nflat in vec4 color;out vec4 pixel;"
        "void main(){pixel=color;}");
    GLuint p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);
    glBindAttribLocation(p,0,"point");glLinkProgram(p);
    GLint linked=0;glGetProgramiv(p,GL_LINK_STATUS,&linked);CHECK(linked);
    glDeleteShader(v);glDeleteShader(f);return p;
}
static void pixel(unsigned x,unsigned y,unsigned channel)
{
    unsigned char px[4]={0};glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    GLenum err=glGetError();
    if(err || px[channel]!=255)fprintf(stderr,"draw error=%x pixel=%u,%u,%u,%u expected channel=%u\n",
                                      err,px[0],px[1],px[2],px[3],channel);
    CHECK(err==GL_NO_ERROR && px[channel]==255 && px[3]==255);
}
static void color(unsigned channel){pixel(8,8,channel);}
static void lists(int core,GLuint vbo,GLuint ebo)
{
    const GLfloat coords[]={-1,-1,0,-1,-.5,1,0,-1,1,-1,.5,1};
    const GLushort indices[]={0,1,2,3,4,5};
    const GLsizei count[]={3,0,3};const GLint first[]={0,0,3},base[]={0,0,3};
    const void *const raw[]={indices,(const void *)UINTPTR_MAX,indices+3};
    const void *const raw_base[]={indices,(const void *)UINTPTR_MAX,indices};
    const void *const offsets[]={NULL,(const void *)UINTPTR_MAX,(const void *)6};
    const void *const offset_base[]={NULL,(const void *)UINTPTR_MAX,NULL};
    if(core){
        glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(coords),coords,GL_STATIC_DRAW);
    }else glVertexPointer(2,GL_FLOAT,0,coords);
    glClear(GL_COLOR_BUFFER_BIT);glMultiDrawArrays(GL_TRIANGLES,first,count,3);
    pixel(4,6,core?1:0);pixel(12,6,0);
    if(!core){
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
        glClear(GL_COLOR_BUFFER_BIT);glMultiDrawElements(GL_TRIANGLES,count,GL_UNSIGNED_SHORT,raw,3);
        pixel(4,6,0);pixel(12,6,0);
        glClear(GL_COLOR_BUFFER_BIT);glMultiDrawElementsBaseVertex(GL_TRIANGLES,count,GL_UNSIGNED_SHORT,raw_base,3,base);
        pixel(4,6,0);pixel(12,6,0);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    glClear(GL_COLOR_BUFFER_BIT);glMultiDrawElements(GL_TRIANGLES,count,GL_UNSIGNED_SHORT,offsets,3);
    pixel(4,6,core?1:0);pixel(12,6,0);
    glClear(GL_COLOR_BUFFER_BIT);glMultiDrawElementsBaseVertex(GL_TRIANGLES,count,GL_UNSIGNED_SHORT,offset_base,3,base);
    pixel(4,6,core?1:0);pixel(12,6,0);
    const GLsizei skipped[]={3,-1,3};
    glClear(GL_COLOR_BUFFER_BIT);glMultiDrawArrays(GL_TRIANGLES,first,skipped,3);
    pixel(4,6,core?1:0);pixel(12,6,0);
    glClear(GL_COLOR_BUFFER_BIT);glMultiDrawElements(GL_TRIANGLES,skipped,GL_UNSIGNED_SHORT,offsets,3);
    pixel(4,6,core?1:0);pixel(12,6,0);
    glClear(GL_COLOR_BUFFER_BIT);glMultiDrawElementsBaseVertex(GL_TRIANGLES,skipped,GL_UNSIGNED_SHORT,offset_base,3,base);
    pixel(4,6,core?1:0);pixel(12,6,0);
}
static void draws(int core)
{
    GLuint tex=0,fbo=0;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,tex,0);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glViewport(0,0,16,16);glClearColor(0,0,0,0);
    const GLfloat coords[]={2,2,2,2,2,2,-1,-1,1,-1,0,1};
    const GLushort indices[]={0,1,2,6,7,8,0xffff,0,1,2,0xffff,3,4,5,0xffff};
    GLuint vbo=0,ebo=0,p=0;
    if(core){
        GLuint vao=0;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
        p=program();glUseProgram(p);
        glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,sizeof(coords),coords,GL_STATIC_DRAW);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL);glEnableVertexAttribArray(0);
    }else{
        glVertexPointer(2,GL_FLOAT,0,coords);glEnableClientState(GL_VERTEX_ARRAY);
        glColor4f(1,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawElementsBaseVertex(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,indices,3);color(0);
    }
    /* Core uses real VBO/EBO offsets. Compatibility exercises an EBO with
     * CPU vertex data as well as the prior CPU indices and CPU arrays. */
    glGenBuffers(1,&ebo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    CHECK(glGetError()==GL_NO_ERROR);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawElementsBaseVertex(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,NULL,3);color(0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawRangeElementsBaseVertex(GL_TRIANGLES,6,8,3,GL_UNSIGNED_SHORT,(void *)6,-3);color(0);
    if(core){
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawElementsInstancedBaseVertex(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,NULL,2,3);color(2);
        glClear(GL_COLOR_BUFFER_BIT);glPrimitiveRestartIndex(0xffff);glEnable(PRIMITIVE_RESTART);
        glDrawElementsBaseVertex(GL_TRIANGLE_STRIP,5,GL_UNSIGNED_SHORT,(void *)12,3);color(0);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawElements(GL_TRIANGLE_STRIP,5,GL_UNSIGNED_SHORT,(void *)20);color(0);
        glDisable(PRIMITIVE_RESTART);
    }
    glDrawRangeElementsBaseVertex(GL_TRIANGLES,8,6,3,GL_UNSIGNED_SHORT,(void *)6,-3);
    CHECK(glGetError()==GL_INVALID_VALUE);
    glDrawElementsBaseVertex(GL_TRIANGLES,3,GL_FLOAT,NULL,3);
    CHECK(glGetError()==GL_INVALID_ENUM);
    lists(core,vbo,ebo);
    if(p){glUseProgram(0);glDeleteProgram(p);}
    if(vbo)glDeleteBuffers(1,&vbo);
    glDeleteBuffers(1,&ebo);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&tex);
}

int main(int argc,char **argv)
{
#ifdef VIM_BASE_VERTEX_WIRE
    const uint32_t wrapped[]={UINT32_MAX,4};
    const uint8_t marker=0;
    CHECK(DGBaseVertexSpan(wrapped,2,4,0,1,0,0)==6);
    CHECK(DGBaseVertexSpan(&marker,1,1,0,-1,0,0)==UINT64_MAX);
    CHECK(DGBaseVertexSpan(&marker,1,1,0,-1,1,0)==0);
#endif
    for(int core=0;core<2;++core){
#ifdef VIM_BASE_VERTEX_WIRE
        (void)argc;(void)argv;
        unsigned char screen[16*16*4]={0};
        const int attrs[]={0x56494d02,core?0x3200:0x1000,0};
        struct DGContext *c=DGCreateContext(attrs,NULL);
        if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
        CHECK((c->connection->features&(DG_FEATURE_BASE_VERTEX|DG_FEATURE_MULTI_DRAW))==
              (DG_FEATURE_BASE_VERTEX|DG_FEATURE_MULTI_DRAW));
#else
        CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
            kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
            (CGLPixelFormatAttribute)(core?kCGLOGLPVersion_3_2_Core:kCGLOGLPVersion_Legacy),0,0,0};
        if(argc==2){attrs[5]=kCGLPFARendererID;attrs[6]=(CGLPixelFormatAttribute)strtoul(argv[1],NULL,16);}
        CGLPixelFormatObj format=NULL;GLint n=0;CGLContextObj c=NULL;
        if(CGLChoosePixelFormat(attrs,&format,&n)||!format||CGLCreateContext(format,NULL,&c)||
            CGLSetCurrentContext(c))return 2;
        CGLDestroyPixelFormat(format);
        if(argc==2 && !strstr((const char *)glGetString(GL_VENDOR),"Intel"))return 77;
#endif
        printf("base vertex renderer=%s core=%d\n",glGetString(GL_RENDERER),core);
        draws(core);
#ifdef VIM_BASE_VERTEX_WIRE
        CHECK(!c->connection->dead);DGDestroyContext(c);
#else
        CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    }
    printf("base vertex failures=%d\n",failures);return failures?1:0;
}
