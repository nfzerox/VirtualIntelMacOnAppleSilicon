/* Observe command-specific signed normalization on the same public binary.
 * No formula is silently accepted as "a driver bug". Queries and rendered
 * behavior must be kept distinct; this first matrix observes current state.
 * --shader checks whether native generic ArrayElement requires a program.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
static unsigned errors,observations;
static int direct_only;
static void report(const char *kind,GLenum type,unsigned sample,const GLfloat *value,unsigned n)
{
    GLenum error=glGetError();errors+=error!=GL_NO_ERROR;++observations;
    printf("OBS %s type=%x sample=%u value=",kind,type,sample);
    for(unsigned i=0;i<n;++i)printf("%s%a",i?",":"",(double)value[i]);
    printf(" error=%x\n",error);fflush(stdout);
}
static void current(const char *kind,GLenum type,unsigned sample,GLenum key,unsigned n)
{GLfloat value[4]={-9,-9,-9,-9};glGetFloatv(key,value);report(kind,type,sample,value,n);}
static void generic(const char *kind,GLenum type,unsigned sample)
{GLfloat value[4]={-9,-9,-9,-9};glGetVertexAttribfvARB(3,GL_CURRENT_VERTEX_ATTRIB_ARB,value);report(kind,type,sample,value,4);}
static GLhandleARB program(void)
{
    const GLcharARB *vertex="#version 110\nattribute vec4 probeValue;void main(){gl_Position=gl_Vertex;gl_FrontColor=probeValue;}";
    const GLcharARB *fragment="#version 110\nvoid main(){gl_FragColor=gl_Color;}";
    GLhandleARB shaders[]={glCreateShaderObjectARB(GL_VERTEX_SHADER_ARB),glCreateShaderObjectARB(GL_FRAGMENT_SHADER_ARB)};
    GLhandleARB object=glCreateProgramObjectARB();
    for(unsigned i=0;i<2;++i){const GLcharARB *source=i?fragment:vertex;
        glShaderSourceARB(shaders[i],1,&source,NULL);glCompileShaderARB(shaders[i]);
        GLint ok=0;glGetObjectParameterivARB(shaders[i],GL_OBJECT_COMPILE_STATUS_ARB,&ok);
        if(!ok){char text[1024];GLsizei n=0;glGetInfoLogARB(shaders[i],sizeof(text),&n,text);
            fprintf(stderr,"shader compilation failed: %.*s\n",n,text);return 0;}
        glAttachObjectARB(object,shaders[i]);glDeleteObjectARB(shaders[i]);
    }
    glBindAttribLocationARB(object,3,"probeValue");glLinkProgramARB(object);
    GLint ok=0;glGetObjectParameterivARB(object,GL_OBJECT_LINK_STATUS_ARB,&ok);
    if(!ok)return 0;glUseProgramObjectARB(object);return object;
}
static void run(GLenum type)
{
    union {GLbyte b[4];GLshort s[4];GLint i[4];} source;
    const int64_t limits[]={127,32767,2147483647};
    unsigned which=type==GL_BYTE?0:type==GL_SHORT?1:2;
    int64_t maximum=limits[which];
    int64_t values[]={-maximum-1,-maximum,-1,0,1,maximum-1,maximum};
    GLuint buffer=0;glGenBuffers(1,&buffer);
    for(unsigned sample=0;sample<7;++sample){
        for(unsigned c=0;c<4;++c){
            if(type==GL_BYTE)source.b[c]=(GLbyte)values[sample];
            else if(type==GL_SHORT)source.s[c]=(GLshort)values[sample];
            else source.i[c]=(GLint)values[sample];
        }
        if(type==GL_BYTE){glColor4bv(source.b);current("direct-color",type,sample,GL_CURRENT_COLOR,4);
            glNormal3bv(source.b);current("direct-normal",type,sample,GL_CURRENT_NORMAL,3);
            glSecondaryColor3bvEXT(source.b);current("direct-secondary",type,sample,GL_CURRENT_SECONDARY_COLOR,3);
            glVertexAttrib4NbvARB(3,source.b);generic("direct-generic",type,sample);}
        else if(type==GL_SHORT){glColor4sv(source.s);current("direct-color",type,sample,GL_CURRENT_COLOR,4);
            glNormal3sv(source.s);current("direct-normal",type,sample,GL_CURRENT_NORMAL,3);
            glSecondaryColor3svEXT(source.s);current("direct-secondary",type,sample,GL_CURRENT_SECONDARY_COLOR,3);
            glVertexAttrib4NsvARB(3,source.s);generic("direct-generic",type,sample);}
        else {glColor4iv(source.i);current("direct-color",type,sample,GL_CURRENT_COLOR,4);
            glNormal3iv(source.i);current("direct-normal",type,sample,GL_CURRENT_NORMAL,3);
            glSecondaryColor3ivEXT(source.i);current("direct-secondary",type,sample,GL_CURRENT_SECONDARY_COLOR,3);
            glVertexAttrib4NivARB(3,source.i);generic("direct-generic",type,sample);}
        for(unsigned vbo=0;!direct_only&&vbo<2;++vbo){
            glBindBuffer(GL_ARRAY_BUFFER,vbo?buffer:0);
            if(vbo)glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(4*(which==0?1:which==1?2:4)),&source,GL_STREAM_DRAW);
            const void *pointer=vbo?NULL:&source;
            glColorPointer(4,type,0,pointer);glEnableClientState(GL_COLOR_ARRAY);
            glColor4f(.25f,.5f,.75f,1);glArrayElement(0);
            current(vbo?"vbo-color":"cpu-color",type,sample,GL_CURRENT_COLOR,4);glDisableClientState(GL_COLOR_ARRAY);
            glNormalPointer(type,0,pointer);glEnableClientState(GL_NORMAL_ARRAY);
            glNormal3f(.25f,.5f,.75f);glArrayElement(0);
            current(vbo?"vbo-normal":"cpu-normal",type,sample,GL_CURRENT_NORMAL,3);glDisableClientState(GL_NORMAL_ARRAY);
            glSecondaryColorPointerEXT(3,type,0,pointer);glEnableClientState(GL_SECONDARY_COLOR_ARRAY);
            glSecondaryColor3fEXT(.25f,.5f,.75f);glArrayElement(0);
            current(vbo?"vbo-secondary":"cpu-secondary",type,sample,GL_CURRENT_SECONDARY_COLOR,3);glDisableClientState(GL_SECONDARY_COLOR_ARRAY);
            glVertexAttribPointerARB(3,4,type,GL_TRUE,0,pointer);glEnableVertexAttribArrayARB(3);
            glVertexAttrib4fARB(3,9,8,7,6);glArrayElement(0);
            generic(vbo?"vbo-generic":"cpu-generic",type,sample);glDisableVertexAttribArrayARB(3);
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER,0);glDeleteBuffers(1,&buffer);
}
int main(int argc,char **argv)
{
    int shader=0;for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--shader"))shader=1;
        else if(!strcmp(argv[i],"--direct-only"))direct_only=1;
        else return 2;
    }
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&context)||CGLSetCurrentContext(context))return 2;CGLDestroyPixelFormat(pf);
    int translated=-1;size_t size=sizeof(translated);if(sysctlbyname("sysctl.proc_native",&translated,&size,NULL,0))translated=-1;
    printf("renderer=%s version=%s bits=%u proc_native=%d shader=%d direct_only=%d\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),
        (unsigned)sizeof(void *)*8,translated,shader,direct_only);
    GLuint framebuffer=0,renderbuffer=0;
    glGenRenderbuffersEXT(1,&renderbuffer);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,renderbuffer);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,16,16);
    glGenFramebuffersEXT(1,&framebuffer);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,framebuffer);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,renderbuffer);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT||glGetError())return 2;
    GLhandleARB object=shader?program():0;if(shader&&!object)return 2;
    run(GL_BYTE);run(GL_SHORT);run(GL_INT);
    if(object){glUseProgramObjectARB(0);glDeleteObjectARB(object);}
    glDeleteFramebuffersEXT(1,&framebuffer);glDeleteRenderbuffersEXT(1,&renderbuffer);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("signed-normalization observations=%u api_errors=%u\n",observations,errors);return errors?1:0;
}
