/* Native and wire Apple element arrays and vertex-range hints. */
#ifdef VIM_APPLE_ARRAYS_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
static void pixels(int left,int right)
{
    GLubyte l[4]={0},r[4]={0};
    glReadPixels(2,3,1,1,GL_RGBA,GL_UNSIGNED_BYTE,l);
    glReadPixels(10,3,1,1,GL_RGBA,GL_UNSIGNED_BYTE,r);
    CHECK(glGetError()==GL_NO_ERROR && l[0]==left && r[0]==right);
    printf("APPLE element pixels=%u,%u\n",l[0],r[0]);
}
static void range_state(const void *data)
{
    GLint length=-1,hint=-1,maximum=-1;void *pointer=NULL;
    glGetIntegerv(GL_VERTEX_ARRAY_RANGE_LENGTH_APPLE,&length);
    glGetIntegerv(GL_VERTEX_ARRAY_STORAGE_HINT_APPLE,&hint);
    glGetIntegerv(GL_MAX_VERTEX_ARRAY_RANGE_ELEMENT_APPLE,&maximum);
    printf("APPLE range defaults length=%d hint=%04x max=%d enabled=%d\n",length,hint,maximum,
           glIsEnabled(GL_VERTEX_ARRAY_RANGE_APPLE));
    CHECK(glGetError()==GL_NO_ERROR && length==0 && hint==GL_STORAGE_CLIENT_APPLE);
#ifdef VIM_APPLE_ARRAYS_WIRE
    CHECK(maximum>=65535);
#endif
    glEnableClientState(GL_VERTEX_ARRAY_RANGE_APPLE);
    printf("APPLE range enabled without pointer=%d\n",glIsEnabled(GL_VERTEX_ARRAY_RANGE_APPLE));
    CHECK(glIsEnabled(GL_VERTEX_ARRAY_RANGE_APPLE));
    glVertexArrayRangeAPPLE(32,data);
    glGetIntegerv(GL_VERTEX_ARRAY_RANGE_LENGTH_APPLE,&length);
    glGetPointerv(GL_VERTEX_ARRAY_RANGE_POINTER_APPLE,&pointer);
    CHECK(glGetError()==GL_NO_ERROR && length==32 && pointer==data);
    glFlushVertexArrayRangeAPPLE(8,(const char *)data+4);CHECK(glGetError()==GL_NO_ERROR);
    glDisableClientState(GL_VERTEX_ARRAY_RANGE_APPLE);
    glVertexArrayParameteriAPPLE(GL_VERTEX_ARRAY_STORAGE_HINT_APPLE,GL_STORAGE_SHARED_APPLE);
    glGetIntegerv(GL_VERTEX_ARRAY_STORAGE_HINT_APPLE,&hint);
    printf("APPLE shared hint enabled=%d\n",glIsEnabled(GL_VERTEX_ARRAY_RANGE_APPLE));
    CHECK(glGetError()==GL_NO_ERROR && hint==GL_STORAGE_SHARED_APPLE &&
          !glIsEnabled(GL_VERTEX_ARRAY_RANGE_APPLE));
    glVertexArrayRangeAPPLE(0,NULL);
    glGetPointerv(GL_VERTEX_ARRAY_RANGE_POINTER_APPLE,&pointer);
    CHECK(glGetError()==GL_NO_ERROR && pointer==NULL);
    glEnableClientState(GL_VERTEX_ARRAY_RANGE_APPLE);
    glVertexArrayRangeAPPLE(0,data);
    glGetPointerv(GL_VERTEX_ARRAY_RANGE_POINTER_APPLE,&pointer);
    glGetIntegerv(GL_VERTEX_ARRAY_RANGE_LENGTH_APPLE,&length);
    printf("APPLE zero range length=%d pointer=%d enabled=%d\n",length,pointer==data,
           glIsEnabled(GL_VERTEX_ARRAY_RANGE_APPLE));
    CHECK(length==0 && pointer==data && glIsEnabled(GL_VERTEX_ARRAY_RANGE_APPLE));
    glVertexArrayRangeAPPLE(32,NULL);
    glGetPointerv(GL_VERTEX_ARRAY_RANGE_POINTER_APPLE,&pointer);
    glGetIntegerv(GL_VERTEX_ARRAY_RANGE_LENGTH_APPLE,&length);
    printf("APPLE null range length=%d pointer=%d enabled=%d\n",length,pointer!=NULL,
           glIsEnabled(GL_VERTEX_ARRAY_RANGE_APPLE));
    CHECK(glGetError()==GL_NO_ERROR && length==32 && pointer==NULL &&
          glIsEnabled(GL_VERTEX_ARRAY_RANGE_APPLE));
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
#ifdef VIM_APPLE_ARRAYS_WIRE
    const int attrs[]={0x56494d02,0x1000,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(c->connection->features2&DG_FEATURE2_APPLE_ARRAYS);
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
    int advertised=extensions && strstr(extensions,"GL_APPLE_element_array")!=NULL;
    printf("APPLE element renderer=%s advertised=%d\n",glGetString(GL_RENDERER),advertised);
    if(!advertised){
#ifdef VIM_APPLE_ARRAYS_WIRE
        DGDestroyContext(c);
#else
        CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
        return 77;
    }
    GLuint rb=0,fb=0;glGenRenderbuffersEXT(1,&rb);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rb);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,16,16);
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,rb);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glViewport(0,0,16,16);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,16,0,16,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glClearColor(0,0,0,1);glColor4ub(255,0,0,255);
    const GLfloat positions[][2]={{0,0},{1,2},{7,2},{1,8},{9,2},{15,2},{9,8}};
    GLushort indices[]={0,1,2,3,4,5,6};
    glVertexPointer(2,GL_FLOAT,0,positions);glEnableClientState(GL_VERTEX_ARRAY);
    glElementPointerAPPLE(GL_UNSIGNED_SHORT,indices);
    glEnableClientState(GL_ELEMENT_ARRAY_APPLE);
    GLint type=0;void *pointer=NULL;
    glGetIntegerv(GL_ELEMENT_ARRAY_TYPE_APPLE,&type);glGetPointerv(GL_ELEMENT_ARRAY_POINTER_APPLE,&pointer);
    CHECK(glGetError()==GL_NO_ERROR && type==GL_UNSIGNED_SHORT && pointer==indices &&
          glIsEnabled(GL_ELEMENT_ARRAY_APPLE));
    glClear(GL_COLOR_BUFFER_BIT);glDrawElementArrayAPPLE(GL_TRIANGLES,1,3);pixels(255,0);
    glClear(GL_COLOR_BUFFER_BIT);glDrawRangeElementArrayAPPLE(GL_TRIANGLES,4,6,4,3);pixels(0,255);
    const GLint first[]={1,4};const GLsizei count[]={3,3};
    glClear(GL_COLOR_BUFFER_BIT);glMultiDrawElementArrayAPPLE(GL_TRIANGLES,first,count,2);pixels(255,255);
    glClear(GL_COLOR_BUFFER_BIT);glMultiDrawRangeElementArrayAPPLE(GL_TRIANGLES,1,6,first,count,2);pixels(255,255);
    indices[1]=4;indices[2]=5;indices[3]=6;
    glClear(GL_COLOR_BUFFER_BIT);glDrawElementArrayAPPLE(GL_TRIANGLES,1,3);pixels(0,255);
    indices[1]=1;indices[2]=2;indices[3]=3;
    for(int form=0;form<4;++form){
        GLuint glyph=glGenLists(1);glNewList(glyph,GL_COMPILE);
        if(form==0)glDrawElementArrayAPPLE(GL_TRIANGLES,1,3);
        if(form==1)glDrawRangeElementArrayAPPLE(GL_TRIANGLES,4,6,4,3);
        if(form==2)glMultiDrawElementArrayAPPLE(GL_TRIANGLES,first,count,2);
        if(form==3)glMultiDrawRangeElementArrayAPPLE(GL_TRIANGLES,1,6,first,count,2);
        glEndList();
        for(unsigned i=1;i<7;++i)indices[i]=0;
        glClear(GL_COLOR_BUFFER_BIT);glCallList(glyph);
        pixels(form!=1?255:0,form!=0?255:0);
        glDeleteLists(glyph,1);
        for(unsigned i=1;i<7;++i)indices[i]=(GLushort)i;
    }
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glElementPointerAPPLE(GL_UNSIGNED_SHORT,indices+1);glDisableClientState(GL_ELEMENT_ARRAY_APPLE);
    glPopClientAttrib();glGetPointerv(GL_ELEMENT_ARRAY_POINTER_APPLE,&pointer);
    CHECK(pointer==indices && glIsEnabled(GL_ELEMENT_ARRAY_APPLE));
    GLuint vao=0;glGenVertexArraysAPPLE(1,&vao);glBindVertexArrayAPPLE(vao);
    void *new_pointer=NULL;glGetPointerv(GL_ELEMENT_ARRAY_POINTER_APPLE,&new_pointer);
    int new_enabled=glIsEnabled(GL_ELEMENT_ARRAY_APPLE);
    glBindVertexArrayAPPLE(0);
    printf("APPLE element VAO retained pointer=%d enabled=%d\n",new_pointer==indices,new_enabled);
    CHECK(glGetError()==GL_NO_ERROR && !new_pointer && !new_enabled);
    glDeleteVertexArraysAPPLE(1,&vao);
    GLuint ebo=0;glGenBuffers(1,&ebo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
    const GLushort dummy[7]={0,4,5,6,1,2,3};
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(dummy),dummy,GL_STATIC_DRAW);
    glElementPointerAPPLE(GL_UNSIGNED_SHORT,NULL);
    glClear(GL_COLOR_BUFFER_BIT);glDrawElementArrayAPPLE(GL_TRIANGLES,1,3);pixels(0,255);
    GLint binding=0;glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&binding);CHECK(binding==(GLint)ebo);
    glDeleteBuffers(1,&ebo);
    glElementPointerAPPLE(GL_UNSIGNED_SHORT,indices);
    glDisableClientState(GL_ELEMENT_ARRAY_APPLE);
    glDrawElementArrayAPPLE(GL_TRIANGLES,1,3);
    GLenum disabled=glGetError();printf("APPLE element disabled error=%04x\n",disabled);
    CHECK(disabled==GL_INVALID_OPERATION);
    range_state(positions);
    glDeleteFramebuffersEXT(1,&fb);glDeleteRenderbuffersEXT(1,&rb);
#ifdef VIM_APPLE_ARRAYS_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("APPLE element failures=%d\n",failures);return failures?1:0;
}
