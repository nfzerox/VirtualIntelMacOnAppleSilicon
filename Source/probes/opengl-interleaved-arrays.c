/* Native and bridge: all 14 interleaved layouts and retained offsets. */
#ifdef VIM_INTERLEAVED_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct Layout {
    GLenum format;
    int texture,color,normal,vertex,stride;
    int color_offset,normal_offset,vertex_offset;
    GLenum color_type;
};
static const struct Layout layouts[]={
    {GL_V2F,0,0,0,2,8,0,0,0,0},
    {GL_V3F,0,0,0,3,12,0,0,0,0},
    {GL_C4UB_V2F,0,4,0,2,12,0,0,4,GL_UNSIGNED_BYTE},
    {GL_C4UB_V3F,0,4,0,3,16,0,0,4,GL_UNSIGNED_BYTE},
    {GL_C3F_V3F,0,3,0,3,24,0,0,12,GL_FLOAT},
    {GL_N3F_V3F,0,0,3,3,24,0,0,12,0},
    {GL_C4F_N3F_V3F,0,4,3,3,40,0,16,28,GL_FLOAT},
    {GL_T2F_V3F,2,0,0,3,20,0,0,8,0},
    {GL_T4F_V4F,4,0,0,4,32,0,0,16,0},
    {GL_T2F_C4UB_V3F,2,4,0,3,24,8,0,12,GL_UNSIGNED_BYTE},
    {GL_T2F_C3F_V3F,2,3,0,3,32,8,0,20,GL_FLOAT},
    {GL_T2F_N3F_V3F,2,0,3,3,32,0,8,20,0},
    {GL_T2F_C4F_N3F_V3F,2,4,3,3,48,8,24,36,GL_FLOAT},
    {GL_T4F_C4F_N3F_V4F,4,4,3,4,60,16,32,44,GL_FLOAT},
};
static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
static void verify_array(GLenum enable,GLenum pointer,GLenum size,GLenum type,GLenum stride,
                         int n,GLenum storage,GLint expected_stride,uintptr_t expected_pointer)
{
    CHECK(glIsEnabled(enable)==(n!=0));
    if(!n)return;
    void *p=NULL;GLint value=0;
    glGetPointerv(pointer,&p);CHECK((uintptr_t)p==expected_pointer);
    if(size){glGetIntegerv(size,&value);CHECK(value==n);}
    glGetIntegerv(type,&value);CHECK(value==(GLint)storage);
    glGetIntegerv(stride,&value);CHECK(value==expected_stride);
}
static void draw_arrays(GLuint vbo)
{
    struct Vertex { GLubyte rgba[4]; GLfloat xy[2]; };
    const struct { GLuint padding; struct Vertex v[3]; } triangle={
        0,{{{255,0,0,255},{2,2}},{{255,0,0,255},{13,2}},{{255,0,0,255},{2,13}}}
    };
    GLuint rb=0,fb=0;
    glGenRenderbuffersEXT(1,&rb);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rb);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,16,16);
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,rb);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glViewport(0,0,16,16);glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glOrtho(0,16,0,16,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glClientActiveTexture(GL_TEXTURE1);glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTexture(GL_TEXTURE0);glDisableClientState(GL_SECONDARY_COLOR_ARRAY);
    glDisableClientState(GL_FOG_COORDINATE_ARRAY);
    const GLubyte red[]={255,0,0,255};
    const GLboolean edge[]={GL_FALSE,GL_TRUE,GL_FALSE};
    const GLfloat index[]={1,2,3};
    struct { GLboolean flags[4]; GLfloat indices[3]; } aux_data={
        {GL_FALSE,GL_TRUE,GL_FALSE,GL_FALSE},{1,2,3}
    };
    GLuint aux=0;glGenBuffers(1,&aux);
    glClearColor(0,0,0,1);
    for(int buffered=0;buffered<=1;++buffered){
        glBindBuffer(GL_ARRAY_BUFFER,buffered?vbo:0);
        if(buffered)glBufferData(GL_ARRAY_BUFFER,sizeof(triangle),&triangle,GL_STATIC_DRAW);
        glInterleavedArrays(GL_C4UB_V2F,0,buffered?(const void *)sizeof(GLuint):triangle.v);
        glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,3);
        GLubyte pixels[16][16][4]={0};
        glReadPixels(0,0,16,16,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        CHECK(glGetError()==GL_NO_ERROR && !memcmp(pixels[4][4],red,sizeof(red)));
        glBindBuffer(GL_ARRAY_BUFFER,buffered?aux:0);
        if(buffered)glBufferData(GL_ARRAY_BUFFER,sizeof(aux_data),&aux_data,GL_STATIC_DRAW);
        glEdgeFlagPointer(0,buffered?NULL:edge);glEnableClientState(GL_EDGE_FLAG_ARRAY);
        glIndexPointer(GL_FLOAT,0,buffered?(const void *)sizeof(aux_data.flags):index);glEnableClientState(GL_INDEX_ARRAY);
        glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
        glDisableClientState(GL_EDGE_FLAG_ARRAY);glDisableClientState(GL_INDEX_ARRAY);
        glPopClientAttrib();
        CHECK(glIsEnabled(GL_EDGE_FLAG_ARRAY) && glIsEnabled(GL_INDEX_ARRAY));
        glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,3);
        glReadPixels(0,0,16,16,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        unsigned horizontal=0,diagonal=0;
        for(unsigned y=1;y<15;++y)for(unsigned x=1;x<15;++x)if(pixels[y][x][0]){
            if(y<=2 && x>=4 && x<=9)++horizontal;
            if(y>=4 && y<=9 && x>=4 && x<=11)++diagonal;
        }
        printf("interleaved draw buffer=%d edge horizontal=%u diagonal=%u\n",buffered,horizontal,diagonal);
        CHECK(glGetError()==GL_NO_ERROR && horizontal==0 && diagonal>=3);
        glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    }
    glDeleteBuffers(1,&aux);
    glDeleteFramebuffersEXT(1,&fb);glDeleteRenderbuffersEXT(1,&rb);
}
int main(void)
{
#ifdef VIM_INTERLEAVED_WIRE
    const int attrs[]={0x56494d02,0x1000,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(c->connection->features2&DG_FEATURE2_AUX_VERTEX_ARRAYS);
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("interleaved renderer=%s\n",glGetString(GL_RENDERER));
    GLfloat client_data[32]={0};GLuint vbo=0;glGenBuffers(1,&vbo);
    int observed=-1;
    for(int buffered=0;buffered<=1;++buffered){
        glBindBuffer(GL_ARRAY_BUFFER,buffered?vbo:0);
        uintptr_t pointer=buffered?16:(uintptr_t)client_data;
        for(unsigned i=0;i<sizeof(layouts)/sizeof(layouts[0]);++i){
            const struct Layout *l=&layouts[i];GLint stride=buffered?64:l->stride;
            glClientActiveTexture(GL_TEXTURE1);glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glClientActiveTexture(GL_TEXTURE0);
            glEnableClientState(GL_EDGE_FLAG_ARRAY);glEnableClientState(GL_INDEX_ARRAY);
            glEnableClientState(GL_SECONDARY_COLOR_ARRAY);glEnableClientState(GL_FOG_COORDINATE_ARRAY);
            glInterleavedArrays(l->format,buffered?stride:0,(const void *)pointer);
            verify_array(GL_VERTEX_ARRAY,GL_VERTEX_ARRAY_POINTER,GL_VERTEX_ARRAY_SIZE,GL_VERTEX_ARRAY_TYPE,
                GL_VERTEX_ARRAY_STRIDE,l->vertex,GL_FLOAT,stride,pointer+l->vertex_offset);
            verify_array(GL_COLOR_ARRAY,GL_COLOR_ARRAY_POINTER,GL_COLOR_ARRAY_SIZE,GL_COLOR_ARRAY_TYPE,
                GL_COLOR_ARRAY_STRIDE,l->color,l->color_type,stride,pointer+l->color_offset);
            verify_array(GL_NORMAL_ARRAY,GL_NORMAL_ARRAY_POINTER,0,GL_NORMAL_ARRAY_TYPE,
                GL_NORMAL_ARRAY_STRIDE,l->normal,GL_FLOAT,stride,pointer+l->normal_offset);
            verify_array(GL_TEXTURE_COORD_ARRAY,GL_TEXTURE_COORD_ARRAY_POINTER,GL_TEXTURE_COORD_ARRAY_SIZE,
                GL_TEXTURE_COORD_ARRAY_TYPE,GL_TEXTURE_COORD_ARRAY_STRIDE,l->texture,GL_FLOAT,stride,pointer);
            int auxiliary=glIsEnabled(GL_EDGE_FLAG_ARRAY) | (glIsEnabled(GL_INDEX_ARRAY)<<1) |
                (glIsEnabled(GL_SECONDARY_COLOR_ARRAY)<<2) | (glIsEnabled(GL_FOG_COORDINATE_ARRAY)<<3);
            if(observed<0)observed=auxiliary;
            CHECK(observed==auxiliary);
            glClientActiveTexture(GL_TEXTURE1);CHECK(glIsEnabled(GL_TEXTURE_COORD_ARRAY));
            CHECK(glGetError()==GL_NO_ERROR);
        }
    }
    const GLboolean edge[]={GL_FALSE,GL_TRUE,GL_FALSE};
    const GLfloat index[]={1,2,3};
    glBindBuffer(GL_ARRAY_BUFFER,0);
    glEdgeFlagPointer(0,edge);glIndexPointer(GL_FLOAT,0,index);
    GLint edge_stride=-1,index_stride=-1;
    glGetIntegerv(GL_EDGE_FLAG_ARRAY_STRIDE,&edge_stride);glGetIntegerv(GL_INDEX_ARRAY_STRIDE,&index_stride);
    printf("auxiliary zero-stride query=%d,%d\n",edge_stride,index_stride);
    CHECK(edge_stride==1 && index_stride==4);
    void *pointer=NULL;
    glGetPointerv(GL_EDGE_FLAG_ARRAY_POINTER,&pointer);CHECK(pointer==edge);
    glGetPointerv(GL_INDEX_ARRAY_POINTER,&pointer);CHECK(pointer==index);
    CHECK(glGetError()==GL_NO_ERROR);
    draw_arrays(vbo);
    glDeleteBuffers(1,&vbo);
    printf("interleaved remaining auxiliary enable mask=%x\n",observed);
    CHECK(observed==0xc);
#ifdef VIM_INTERLEAVED_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("interleaved failures=%d\n",failures);return failures?1:0;
}
