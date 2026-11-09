/* Native and bridge ArrayElement: immediate attributes, VBO offsets and lists. */
#ifdef VIM_ARRAY_ELEMENT_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <sys/sysctl.h>

static int failures;
static int big_endian;
static int signed_byte_biased;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
struct Vertex { GLfloat position[2]; GLubyte color[4]; };
static void encode_position(struct Vertex *v)
{
    if(big_endian)for(unsigned i=0;i<2;++i){unsigned char *p=(unsigned char *)&v->position[i];
        unsigned char a=p[0],b=p[1];p[0]=p[3];p[1]=p[2];p[2]=b;p[3]=a;}
}
static void scalar(void *destination,GLenum type,int value)
{
    unsigned char bytes[8];size_t n=0;
    switch(type){
    case GL_SHORT:{GLshort v=value;n=sizeof(v);memcpy(bytes,&v,n);break;}
    case GL_INT:{GLint v=value;n=sizeof(v);memcpy(bytes,&v,n);break;}
    case GL_FLOAT:{GLfloat v=value;n=sizeof(v);memcpy(bytes,&v,n);break;}
    case GL_DOUBLE:{GLdouble v=value;n=sizeof(v);memcpy(bytes,&v,n);break;}
    }
    if(big_endian)for(size_t i=0;i<n/2;++i){unsigned char v=bytes[i];bytes[i]=bytes[n-1-i];bytes[n-1-i]=v;}
    memcpy(destination,bytes,n);
}
static void typed_vbo(void)
{
    const GLenum types[]={GL_SHORT,GL_INT,GL_FLOAT,GL_DOUBLE};
    const unsigned sizes[]={2,4,4,8};GLuint vbo=0;glGenBuffers(1,&vbo);
    for(unsigned t=0;t<4;++t){
        unsigned char bytes[96];memset(bytes,0xa5,sizeof(bytes));
        scalar(bytes+8,types[t],9);scalar(bytes+8+sizes[t],types[t],11);
        scalar(bytes+56,types[t],3);scalar(bytes+56+sizes[t],types[t],5);
        glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(bytes),bytes,GL_DYNAMIC_DRAW);
        glClientActiveTexture(GL_TEXTURE0);glTexCoordPointer(2,types[t],48,(const void *)8);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);glBindBuffer(GL_ARRAY_BUFFER,0);
        glArrayElement(1);GLfloat value[4]={0};glGetFloatv(GL_CURRENT_TEXTURE_COORDS,value);
        CHECK(glGetError()==GL_NO_ERROR&&value[0]==3&&value[1]==5&&value[2]==0&&value[3]==1);
        glBegin(GL_POINTS);glArrayElement(0);glArrayElement(1);glEnd();
        glGetFloatv(GL_CURRENT_TEXTURE_COORDS,value);
        CHECK(glGetError()==GL_NO_ERROR&&value[0]==3&&value[1]==5&&value[2]==0&&value[3]==1);
        GLint bound=-1;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&bound);CHECK(bound==0);
        scalar(bytes+56,types[t],7);scalar(bytes+56+sizes[t],types[t],9);
        glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(bytes),bytes);
        glBindBuffer(GL_ARRAY_BUFFER,0);glArrayElement(1);glGetFloatv(GL_CURRENT_TEXTURE_COORDS,value);
        CHECK(glGetError()==GL_NO_ERROR&&value[0]==7&&value[1]==9);
        printf("array element scalar type=%x value=%g,%g\n",(unsigned)types[t],value[0],value[1]);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    glDeleteBuffers(1,&vbo);
}
static void verify(void)
{
    struct Vertex vertices[]={{{1,1},{0,0,255,255}},{{3,3},{255,0,0,255}},
        {{12,3},{255,0,0,255}},{{3,12},{255,0,0,255}}};
    for(unsigned i=0;i<4;++i)encode_position(vertices+i);
    const GLubyte blue[]={0,0,255,255},red[]={255,0,0,255};
    GLuint rb=0,fb=0,vbo=0;
    glGenRenderbuffersEXT(1,&rb);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rb);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,16,16);
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,rb);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glViewport(0,0,16,16);glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glOrtho(0,16,0,16,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glClearColor(0,0,0,1);glGenBuffers(1,&vbo);
    for(unsigned layout=0;layout<4;++layout){
        glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
        if(!(layout&1))glBindBuffer(GL_ARRAY_BUFFER,0);
        glVertexPointer(2,GL_FLOAT,sizeof(struct Vertex),
            (layout&1)?(const void *)offsetof(struct Vertex,position):vertices[0].position);
        glBindBuffer(GL_ARRAY_BUFFER,(layout&2)?vbo:0);
        glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(struct Vertex),
            (layout&2)?(const void *)offsetof(struct Vertex,color):vertices[0].color);
        glEnableClientState(GL_COLOR_ARRAY);glDisableClientState(GL_VERTEX_ARRAY);
        glArrayElement(0);
        GLfloat current[4]={0};glGetFloatv(GL_CURRENT_COLOR,current);
        CHECK(glGetError()==GL_NO_ERROR && current[0]==0 && current[2]==1 && current[3]==1);
        glEnableClientState(GL_VERTEX_ARRAY);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_TRIANGLES);glArrayElement(1);glArrayElement(2);glArrayElement(3);glEnd();
        GLubyte pixel[4]={0};glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        glGetFloatv(GL_CURRENT_COLOR,current);
        CHECK(glGetError()==GL_NO_ERROR && !memcmp(pixel,red,4) && current[0]==1 && current[2]==0);
        GLint mapped=-1;glBindBuffer(GL_ARRAY_BUFFER,vbo);glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_MAPPED,&mapped);
        struct Vertex copy[4];memset(copy,0,sizeof(copy));glGetBufferSubData(GL_ARRAY_BUFFER,0,sizeof(copy),copy);
        CHECK(!mapped&&!memcmp(copy,vertices,sizeof(copy))&&glGetError()==GL_NO_ERROR);
        printf("array element layout=%u pixel=%u,%u,%u,%u\n",layout,pixel[0],pixel[1],pixel[2],pixel[3]);
    }
    glDisableClientState(GL_VERTEX_ARRAY);glDisableClientState(GL_COLOR_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER,0);
    const GLbyte attr[]={-128,0,127};
    glVertexAttribPointerARB(3,3,GL_BYTE,GL_TRUE,0,attr);glEnableVertexAttribArrayARB(3);
    glArrayElement(0);
    GLfloat generic[4]={-2,-2,-2,-2};
    glGetVertexAttribfvARB(3,GL_CURRENT_VERTEX_ATTRIB_ARB,generic);
    printf("array element normalized generic=%a,%a,%a,%a\n",generic[0],generic[1],generic[2],generic[3]);
    GLfloat expected_zero=signed_byte_biased?1.0f/255.0f:0;
    CHECK(glGetError()==GL_NO_ERROR && generic[0]==-1 &&
        (signed_byte_biased?fabsf(generic[1]-expected_zero)<=2*FLT_EPSILON*expected_zero:generic[1]==0) && generic[2]==1 && generic[3]==1);
    glDisableVertexAttribArrayARB(3);
    glColorPointer(3,GL_BYTE,0,attr);glEnableClientState(GL_COLOR_ARRAY);
    glArrayElement(0);glGetFloatv(GL_CURRENT_COLOR,generic);
    printf("array element signed color=%a,%a,%a,%a\n",generic[0],generic[1],generic[2],generic[3]);
    CHECK(glGetError()==GL_NO_ERROR && generic[0]==-1 &&
        (signed_byte_biased?fabsf(generic[1]-expected_zero)<=2*FLT_EPSILON*expected_zero:generic[1]==0) && generic[2]==1 && generic[3]==1);
    glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_COLOR_ARRAY);
    /* Lists capture at compilation, not at playback, for either source. */
    for(int buffered=0;buffered<=1;++buffered){
        struct Vertex point={{6,6},{0,0,255,255}};
        encode_position(&point);
        glBindBuffer(GL_ARRAY_BUFFER,buffered?vbo:0);
        if(buffered)glBufferData(GL_ARRAY_BUFFER,sizeof(point),&point,GL_STATIC_DRAW);
        glVertexPointer(2,GL_FLOAT,sizeof(point),buffered?NULL:point.position);
        glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(point),buffered?(const void *)offsetof(struct Vertex,color):point.color);
        GLuint list=glGenLists(1);glNewList(list,GL_COMPILE);
        glBegin(GL_POINTS);glArrayElement(0);glEnd();glEndList();
        point.color[0]=255;point.color[2]=0;point.position[0]=10;
        if(big_endian){unsigned char *p=(unsigned char *)&point.position[0];
            unsigned char a=p[0],b=p[1];p[0]=p[3];p[1]=p[2];p[2]=b;p[3]=a;}
        if(buffered)glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(point),&point);
        glClear(GL_COLOR_BUFFER_BIT);glCallList(list);
        GLubyte point_pixels[3][3][4];memset(point_pixels,0,sizeof(point_pixels));glReadPixels(5,5,3,3,GL_RGBA,GL_UNSIGNED_BYTE,point_pixels);
        unsigned hits=0;
        for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x)
            hits+=!memcmp(point_pixels[y][x],blue,4);
        printf("array element display list buffer=%d captured=%u\n",buffered,hits);
        CHECK(glGetError()==GL_NO_ERROR && hits==1);
        glDeleteLists(list,1);
    }
#ifdef VIM_ARRAY_ELEMENT_WIRE
    glBegin(GL_POINTS);glArrayElement(99);glEnd();CHECK(glGetError()==GL_INVALID_OPERATION);
    glBegin(GL_POINTS);glArrayElement(-1);glEnd();CHECK(glGetError()==GL_INVALID_VALUE);
#endif
    glBufferData(GL_ARRAY_BUFFER,0,NULL,GL_STATIC_DRAW);
    glBegin(GL_POINTS);glColor3f(1,0,0);glVertex2f(2,2);glEnd();
    CHECK(glGetError()==GL_NO_ERROR);
    glDisableClientState(GL_VERTEX_ARRAY);glDisableClientState(GL_COLOR_ARRAY);typed_vbo();
    glDeleteBuffers(1,&vbo);
    glDeleteFramebuffersEXT(1,&fb);glDeleteRenderbuffersEXT(1,&rb);
}
int main(int argc,char **argv)
{
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--big-endian"))big_endian=1;
        else if(!strcmp(argv[i],"--signed-byte-biased"))signed_byte_biased=1;
        else return 2;
    }
#ifdef VIM_ARRAY_ELEMENT_WIRE
    const int attrs[]={0x56494d02,0x1000,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(c->connection->features2&DG_FEATURE2_ARRAY_ELEMENT);
    if(big_endian){CHECK(c->connection->features3&DG_FEATURE3_BIG_ENDIAN_ARRAY_ELEMENT);c->big_endian_arrays=1;}
#else
    if(big_endian)return 2; /* A native little-endian CGL client is not Rosetta1. */
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    int native=-1;size_t native_bytes=sizeof(native);sysctlbyname("sysctl.proc_native",&native,&native_bytes,NULL,0);
    printf("array element pointer_bits=%lu proc_native=%d big_endian_test=%d signed_byte_biased=%d renderer=%s\n",
        (unsigned long)sizeof(void *)*8,native,big_endian,signed_byte_biased,glGetString(GL_RENDERER));
#if defined(__ppc__)
    CHECK(native==0);
#endif
    verify();
#ifdef VIM_ARRAY_ELEMENT_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("array element failures=%d\n",failures);return failures?1:0;
}
