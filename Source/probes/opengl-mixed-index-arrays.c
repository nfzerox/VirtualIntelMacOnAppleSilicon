/* Public legacy CGL: CPU/VBO vertex attributes with GPU element indices.
 * Sparse indices require the real index range, not count vertices. Lists
 * must retain the values compiled before CPU/VBO/EBO source mutation.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
struct Vertex {GLfloat xy[2];GLubyte color[4];};
static unsigned failures,draws,draw_failures;
static int cpu_list_control;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %u: %s\n",(unsigned)__LINE__,#x);++failures;}} while(0)
static void pixel(unsigned layout,GLenum type,unsigned list_mode,const char *phase)
{
    GLubyte rgba[4]={0};glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    GLenum error=glGetError();++draws;
    printf("DRAW layout=%u type=%x list=%x phase=%s pixel=%u,%u,%u,%u error=%x\n",
        layout,(unsigned)type,list_mode,phase,rgba[0],rgba[1],rgba[2],rgba[3],(unsigned)error);
    int correct=error==GL_NO_ERROR && rgba[0]==255 && rgba[1]==0 && rgba[2]==0 && rgba[3]==255;
    if(!correct)++draw_failures;CHECK(correct);
}
static void setup(struct Vertex *vertices,GLuint vbo,GLuint ebo,unsigned layout,GLenum type)
{
    const struct Vertex initial[]={
        {{2,2},{0,0,255,255}},{{2,2},{0,0,255,255}},{{2,2},{0,0,255,255}},
        {{-1,-1},{255,0,0,255}},{{3,-1},{255,0,0,255}},{{-1,3},{255,0,0,255}}
    };
    memcpy(vertices,initial,sizeof(initial));
    glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(initial),vertices,GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER,layout&1?vbo:0);
    glVertexPointer(2,GL_FLOAT,sizeof(*vertices),layout&1?(const void *)offsetof(struct Vertex,xy):vertices[0].xy);
    glBindBuffer(GL_ARRAY_BUFFER,layout&2?vbo:0);
    glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(*vertices),layout&2?(const void *)offsetof(struct Vertex,color):vertices[0].color);
    glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_COLOR_ARRAY);
    union {uint32_t alignment;unsigned char bytes[64];} indices;
    memset(&indices,0x5a,sizeof(indices));
    unsigned width=type==GL_UNSIGNED_BYTE?1:type==GL_UNSIGNED_SHORT?2:4;
    for(unsigned i=0;i<3;++i){
        unsigned char *p=indices.bytes+16+i*width;
        if(width==1){GLubyte v=3+i;memcpy(p,&v,1);}
        else if(width==2){GLushort v=3+i;memcpy(p,&v,2);}
        else {GLuint v=3+i;memcpy(p,&v,4);}
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),&indices,GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER,vbo); /* The draw must restore this binding. */
    CHECK(glGetError()==GL_NO_ERROR);
}
static void state(const struct Vertex *vertices,GLuint vbo,GLuint ebo,unsigned layout)
{
    GLint value=-1;void *pointer=NULL;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&value);CHECK(value==(GLint)vbo);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&value);CHECK(value==(GLint)ebo);
    glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING,&value);CHECK(value==(GLint)(layout&1?vbo:0));
    glGetIntegerv(GL_COLOR_ARRAY_BUFFER_BINDING,&value);CHECK(value==(GLint)(layout&2?vbo:0));
    glGetPointerv(GL_VERTEX_ARRAY_POINTER,&pointer);
    CHECK(pointer==(layout&1?(const void *)offsetof(struct Vertex,xy):(const void *)vertices[0].xy));
    glGetPointerv(GL_COLOR_ARRAY_POINTER,&pointer);
    CHECK(pointer==(layout&2?(const void *)offsetof(struct Vertex,color):(const void *)vertices[0].color));
    CHECK(glGetError()==GL_NO_ERROR);
}
static void changed_source(struct Vertex *vertices,GLuint vbo,GLuint ebo)
{
    for(unsigned i=0;i<6;++i){vertices[i].xy[0]=vertices[i].xy[1]=2;vertices[i].color[0]=0;vertices[i].color[2]=255;}
    glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferSubData(GL_ARRAY_BUFFER,0,6*sizeof(*vertices),vertices);
    unsigned char zero[64]={0};glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,0,sizeof(zero),zero);CHECK(glGetError()==GL_NO_ERROR);
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--cpu-list-control"))cpu_list_control=1;
    else if(argc!=1)return 2;
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);
    printf("renderer=%s version=%s bits=%u cpu_list_control=%d\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),(unsigned)sizeof(void *)*8,cpu_list_control);
    GLuint framebuffer=0,renderbuffer=0,vbo=0,ebo=0;
    glGenRenderbuffersEXT(1,&renderbuffer);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,renderbuffer);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,16,16);
    glGenFramebuffersEXT(1,&framebuffer);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,framebuffer);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,renderbuffer);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glViewport(0,0,16,16);glDisable(GL_DITHER);glDisable(GL_BLEND);glClearColor(0,0,0,0);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glGenBuffers(1,&vbo);glGenBuffers(1,&ebo);
    const GLenum types[]={GL_UNSIGNED_BYTE,GL_UNSIGNED_SHORT,GL_UNSIGNED_INT};
    for(unsigned layout=0;layout<4;++layout)for(unsigned t=0;t<3;++t){
        struct Vertex vertices[6];
        setup(vertices,vbo,ebo,layout,types[t]);
        glClear(GL_COLOR_BUFFER_BIT);glDrawElements(GL_TRIANGLES,3,types[t],(const void *)16);
        pixel(layout,types[t],0,"elements");state(vertices,vbo,ebo,layout);
        glClear(GL_COLOR_BUFFER_BIT);glDrawRangeElements(GL_TRIANGLES,3,5,3,types[t],(const void *)16);
        pixel(layout,types[t],0,"range");state(vertices,vbo,ebo,layout);
        for(unsigned mode=0;mode<2;++mode){
            setup(vertices,vbo,ebo,layout,types[t]);glClear(GL_COLOR_BUFFER_BIT);
            struct Vertex expanded[3];for(unsigned i=0;i<3;++i)expanded[i]=vertices[3+i];
            if(cpu_list_control){
                glBindBuffer(GL_ARRAY_BUFFER,0);glVertexPointer(2,GL_FLOAT,sizeof(*expanded),expanded[0].xy);
                glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(*expanded),expanded[0].color);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
            }
            GLenum list_mode=mode?GL_COMPILE_AND_EXECUTE:GL_COMPILE;
            GLuint list=glGenLists(1);CHECK(list!=0);glNewList(list,list_mode);
            if(cpu_list_control)glDrawArrays(GL_TRIANGLES,0,3);
            else glDrawElements(GL_TRIANGLES,3,types[t],(const void *)16);
            glEndList();
            CHECK(glGetError()==GL_NO_ERROR && glIsList(list));
            if(mode)pixel(layout,types[t],list_mode,"initial");
            if(cpu_list_control){
                glBindBuffer(GL_ARRAY_BUFFER,layout&1?vbo:0);
                glVertexPointer(2,GL_FLOAT,sizeof(*vertices),layout&1?(const void *)offsetof(struct Vertex,xy):vertices[0].xy);
                glBindBuffer(GL_ARRAY_BUFFER,layout&2?vbo:0);
                glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(*vertices),layout&2?(const void *)offsetof(struct Vertex,color):vertices[0].color);
            }
            changed_source(vertices,vbo,ebo);glClear(GL_COLOR_BUFFER_BIT);
            glCallList(list);
            pixel(layout,types[t],list_mode,"playback");state(vertices,vbo,ebo,layout);glDeleteLists(list,1);
        }
    }
    /* Error controls run AFTER rendering, so a native invalid-call defect
     * cannot be mistaken for a failure in the ordinary draw/list stream. */
    for(unsigned layout=0;layout<4;++layout)for(unsigned t=0;t<3;++t){
        struct Vertex vertices[6];setup(vertices,vbo,ebo,layout,types[t]);
        glDrawElements(GL_TRIANGLES,0,types[t],(const void *)UINTPTR_MAX);CHECK(glGetError()==GL_NO_ERROR);
        glDrawRangeElements(GL_TRIANGLES,5,3,3,types[t],(const void *)16);CHECK(glGetError()==GL_INVALID_VALUE);
        void *mapped=glMapBuffer(GL_ELEMENT_ARRAY_BUFFER,GL_READ_ONLY);CHECK(mapped!=NULL);
        if(mapped){
            glDrawElements(GL_TRIANGLES,3,types[t],(const void *)16);CHECK(glGetError()==GL_INVALID_OPERATION);
            CHECK(glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER)==GL_TRUE && glGetError()==GL_NO_ERROR);
        }
    }
    glDisableClientState(GL_VERTEX_ARRAY);glDisableClientState(GL_COLOR_ARRAY);
    glDeleteBuffers(1,&vbo);glDeleteBuffers(1,&ebo);
    glDeleteFramebuffersEXT(1,&framebuffer);glDeleteRenderbuffersEXT(1,&renderbuffer);
    CHECK(glGetError()==GL_NO_ERROR);CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("mixed-index-arrays draws=%u draw_failures=%u failures=%u\n",draws,draw_failures,failures);return failures?1:0;
}
