/* Native shader subroutine reflection and selection, including array slots. */
#ifdef VIM_SUBROUTINES_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define glGenVertexArrays glGenVertexArraysAPPLE
#define glBindVertexArray glBindVertexArrayAPPLE
#define glGenFramebuffers glGenFramebuffersEXT
#define glBindFramebuffer glBindFramebufferEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#endif
#include <stdio.h>
#include <string.h>

enum { subroutines=0x8de5, uniforms=0x8de6, locations=0x8e47,
    compatible_count=0x8e4a, compatible=0x8e4b };
static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)

static GLuint shader(GLenum stage,const char *text)
{
    GLuint s=glCreateShader(stage);
    glShaderSource(s,1,&text,NULL);glCompileShader(s);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[1024]={0};glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(ok);return s;
}
static void color(int x,int red,int green)
{
    GLubyte rgba[4]={0};
    glReadPixels(x,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    printf("subroutine pixel %d=%u,%u,%u,%u\n",x,rgba[0],rgba[1],rgba[2],rgba[3]);
    CHECK(glGetError()==GL_NO_ERROR && rgba[0]==red && rgba[1]==green && rgba[2]==0 && rgba[3]==255);
}
static void verify(void)
{
    GLuint p=glCreateProgram();
    glAttachShader(p,shader(GL_VERTEX_SHADER,"#version 410\n"
        "void main(){gl_Position=vec4(gl_VertexID==1?3:-1,gl_VertexID==2?3:-1,0,1);}"));
    glAttachShader(p,shader(GL_FRAGMENT_SHADER,"#version 410\n"
        "subroutine vec4 Paint();"
        "subroutine(Paint) vec4 red(){return vec4(1,0,0,1);}"
        "subroutine(Paint) vec4 green(){return vec4(0,1,0,1);}"
        "subroutine uniform Paint choose[2];"
        "out vec4 color;void main(){color=gl_FragCoord.x<2?choose[0]():choose[1]();}"));
    glLinkProgram(p);
    GLint linked=0;glGetProgramiv(p,GL_LINK_STATUS,&linked);
    if(!linked){char log[1024]={0};glGetProgramInfoLog(p,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(linked);if(!linked)return;
    const GLenum stage=GL_FRAGMENT_SHADER;
    GLint counts[]={-1,-1,-1,37};
    glGetProgramStageiv(p,stage,subroutines,counts);
    glGetProgramStageiv(p,stage,uniforms,counts+1);
    glGetProgramStageiv(p,stage,locations,counts+2);
    CHECK(glGetError()==GL_NO_ERROR && counts[0]==2 && counts[1]==1 && counts[2]==2 && counts[3]==37);
    GLint first=glGetSubroutineUniformLocation(p,stage,"choose[0]");
    GLint second=glGetSubroutineUniformLocation(p,stage,"choose[1]");
    GLuint red=glGetSubroutineIndex(p,stage,"red"),green=glGetSubroutineIndex(p,stage,"green");
    CHECK(glGetError()==GL_NO_ERROR && first>=0 && first<2 && second>=0 && second<2 &&
        first!=second && red<2 && green<2 && red!=green);
    CHECK(glGetSubroutineUniformLocation(p,stage,"unknown")==-1 && glGetError()==GL_NO_ERROR);
    CHECK(glGetSubroutineIndex(p,stage,"unknown")==~0u && glGetError()==GL_NO_ERROR);
    if(first<0 || first>=2 || second<0 || second>=2 || red>=2 || green>=2)return;

    GLint choices[]={-1,-1,37};
    glGetActiveSubroutineUniformiv(p,stage,0,compatible_count,choices);
    CHECK(glGetError()==GL_NO_ERROR && choices[0]==2 && choices[1]==-1 && choices[2]==37);
    glGetActiveSubroutineUniformiv(p,stage,0,compatible,choices);
    CHECK(glGetError()==GL_NO_ERROR && choices[2]==37 &&
        ((choices[0]==(GLint)red && choices[1]==(GLint)green) ||
         (choices[1]==(GLint)red && choices[0]==(GLint)green)));
    GLint guard=37;glGetActiveSubroutineUniformiv(p,stage,0,GL_TEXTURE_2D,&guard);
    GLenum invalid_pname=glGetError();
    printf("invalid subroutine selector error=%x output=%d\n",invalid_pname,guard);
    /* The period Apple driver ignores this unknown pname without a write. */
    CHECK(invalid_pname==GL_NO_ERROR && guard==37);
    guard=37;
    glGetActiveSubroutineUniformiv(p,stage,~0u,compatible,&guard);
    CHECK(glGetError()==GL_INVALID_VALUE && guard==37);

    char name[16];memset(name,'?',sizeof(name));GLsizei len=-1;
    glGetActiveSubroutineName(p,stage,green,4,&len,name);
    CHECK(glGetError()==GL_NO_ERROR && len==3 && !memcmp(name,"gre",4) && name[4]=='?');
    glGetActiveSubroutineUniformName(p,stage,0,sizeof(name),NULL,name);
    CHECK(glGetError()==GL_NO_ERROR && !strncmp(name,"choose",6));
    glGetActiveSubroutineName(p,stage,red,0,&len,NULL);
    CHECK(glGetError()==GL_NO_ERROR && len==0);

    GLuint vao=0,fb=0,tex=0;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glViewport(0,0,4,2);glUseProgram(p);
    GLuint assignments[2];assignments[first]=red;assignments[second]=green;
    glUniformSubroutinesuiv(stage,2,assignments);
    GLuint assigned[]={~0u,~0u,37};
    glGetUniformSubroutineuiv(stage,first,assigned);
    glGetUniformSubroutineuiv(stage,second,assigned+1);
    CHECK(glGetError()==GL_NO_ERROR && assigned[0]==red && assigned[1]==green && assigned[2]==37);
    glUniformSubroutinesuiv(stage,1,assignments);
    CHECK(glGetError()==GL_INVALID_VALUE);
    glDrawArrays(GL_TRIANGLES,0,3);color(0,255,0);color(3,0,255);
    assignments[first]=green;assignments[second]=red;
    glUniformSubroutinesuiv(stage,2,assignments);
    glDrawArrays(GL_TRIANGLES,0,3);color(0,0,255);color(3,255,0);
}

int main(void)
{
#ifdef VIM_SUBROUTINES_WIRE
    const int attrs[]={0x56494d02,DG_CGL_PROFILE_GL4,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(c->connection->features2&DG_FEATURE2_SUBROUTINES);
    uint32_t features=c->connection->features2;
    c->connection->features2&=~DG_FEATURE2_SUBROUTINES;
    CHECK(glGetSubroutineUniformLocation(0,GL_FRAGMENT_SHADER,"unused")==-1 &&
        glGetError()==GL_INVALID_OPERATION && !c->connection->dead);
    c->connection->features2=features;
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("subroutines renderer=%s\n",glGetString(GL_RENDERER));verify();
#ifdef VIM_SUBROUTINES_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("subroutine failures=%d\n",failures);return failures?1:0;
}
