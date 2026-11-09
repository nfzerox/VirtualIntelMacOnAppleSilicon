/* Public CGL list semantics for indexed strips, multi-draw boundaries and
 * signed base vertices. The center pixel must stay empty between two strips.
 * Invoke each family separately so an unsupported native API cannot hide it.
 */
#include <stddef.h>
#ifdef VIM_COMPILED_INDEX_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
typedef struct DGContext *ProbeContext;
static void close_context(ProbeContext c){DGDestroyContext(c);}
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
typedef CGLContextObj ProbeContext;
static void close_context(ProbeContext c){CGLSetCurrentContext(NULL);CGLDestroyContext(c);}
#endif
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef void (*ElementsBase)(GLenum,GLsizei,GLenum,const void *,GLint);
typedef void (*RangeBase)(GLenum,GLuint,GLuint,GLsizei,GLenum,const void *,GLint);
typedef void (*MultiBase)(GLenum,const GLsizei *,GLenum,const void *const *,GLsizei,const GLint *);
static ElementsBase elements_base;static RangeBase range_base;static MultiBase multi_base;
static const char *families[]={"multi","base","range-base","multi-base"};
static unsigned family,failures,cases,observations;
static int cpu_list_control;
static int safe_empty_pointer;
/* Base cases: positive offsets, negative offsets, and zero offsets. */
static GLint vertex_base(int variant,unsigned draw){return variant==2?0:variant?-4:draw?6:2;}
struct Vertex {GLfloat xy[2];GLubyte color[4];};
union Indices {uint32_t alignment;unsigned char bytes[64];};
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %u: %s\n",(unsigned)__LINE__,#x);++failures;}} while(0)
static void setup(struct Vertex *v,union Indices *indices,GLuint vbo,GLuint ebo,
    unsigned layout,GLenum type,int negative,int buffered)
{
    const struct Vertex seed[]={
        {{2,2},{0,255,0,255}},{{2,2},{0,255,0,255}},
        {{-1,-1},{255,0,0,255}},{{-.25f,-1},{255,0,0,255}},
        {{-1,1},{255,0,0,255}},{{-.25f,1},{255,0,0,255}},
        {{.25f,-1},{0,0,255,255}},{{1,-1},{0,0,255,255}},
        {{.25f,1},{0,0,255,255}},{{1,1},{0,0,255,255}}
    };
    memcpy(v,seed,sizeof(seed));glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(seed),v,GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER,layout&1?vbo:0);
    glVertexPointer(2,GL_FLOAT,sizeof(*v),layout&1?NULL:v[0].xy);
    glBindBuffer(GL_ARRAY_BUFFER,layout&2?vbo:0);
    glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(*v),layout&2?(const void *)offsetof(struct Vertex,color):v[0].color);
    glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_COLOR_ARRAY);
    unsigned width=type==GL_UNSIGNED_BYTE?1:type==GL_UNSIGNED_SHORT?2:4;
    memset(indices,0x5a,sizeof(*indices));
    for(unsigned draw=0;draw<2;++draw)for(unsigned vertex=0;vertex<4;++vertex){
        unsigned value=3-vertex;
        if(!family||negative==2)value+=draw?6:2;else if(negative)value+=draw?10:6;
        unsigned char *p=indices->bytes+16+draw*16+vertex*width;
        if(width==1){GLubyte n=value;memcpy(p,&n,1);}
        else if(width==2){GLushort n=value;memcpy(p,&n,2);}
        else {GLuint n=value;memcpy(p,&n,4);}
    }
    /* Mutation below also exercises CPU-index lists with a different EBO
     * bound at playback. Give that buffer real storage in every case. */
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(*indices),indices,GL_DYNAMIC_DRAW);
    if(!buffered)glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);CHECK(glGetError()==GL_NO_ERROR);
}
static void issue(GLenum type,const union Indices *data,int negative,int buffered)
{
    const void *indices[]={buffered?(const void *)16:data->bytes+16,
        (const void *)UINTPTR_MAX,buffered?(const void *)32:data->bytes+32};
    if(safe_empty_pointer)indices[1]=indices[0];
    const GLsizei counts[]={4,0,4};const GLint bases[]={vertex_base(negative,0),-12345,vertex_base(negative,1)};
    if(!family)glMultiDrawElements(GL_TRIANGLE_STRIP,counts,type,indices,3);
    else if(family==3)multi_base(GL_TRIANGLE_STRIP,counts,type,indices,3,bases);
    else for(unsigned i=0;i<3;i+=2){
        if(family==1)elements_base(GL_TRIANGLE_STRIP,4,type,indices[i],bases[i]);
        else {
            GLuint start=negative==2?(i?6:2):negative?(i?10:6):0;
            range_base(GL_TRIANGLE_STRIP,start,start+3,4,type,indices[i],bases[i]);
        }
    }
}
/* Diagnostic control only: supply the equivalent ordered CPU vertices to
 * the native list compiler, with one DrawArrays per independent strip. */
static void control(const struct Vertex *vertices,const union Indices *data,GLenum type,int negative)
{
    struct Vertex packed[8];unsigned width=type==GL_UNSIGNED_BYTE?1:type==GL_UNSIGNED_SHORT?2:4;
    GLint vertex_buffer=0,color_buffer=0,array_buffer=0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array_buffer);
    glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING,&vertex_buffer);
    glGetIntegerv(GL_COLOR_ARRAY_BUFFER_BINDING,&color_buffer);
    for(unsigned draw=0;draw<2;++draw)for(unsigned i=0;i<4;++i){
        GLuint raw=0;const void *p=data->bytes+16+draw*16+i*width;
        if(width==1){GLubyte v;memcpy(&v,p,1);raw=v;}
        else if(width==2){GLushort v;memcpy(&v,p,2);raw=v;}
        else memcpy(&raw,p,4);
        GLint base=family?vertex_base(negative,draw):0;
        packed[draw*4+i]=vertices[(GLuint)((int64_t)raw+base)];
    }
    glBindBuffer(GL_ARRAY_BUFFER,0);glVertexPointer(2,GL_FLOAT,sizeof(*packed),packed[0].xy);
    glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(*packed),packed[0].color);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);glDrawArrays(GL_TRIANGLE_STRIP,4,4);
    glBindBuffer(GL_ARRAY_BUFFER,vertex_buffer);
    glVertexPointer(2,GL_FLOAT,sizeof(*vertices),vertex_buffer?NULL:vertices[0].xy);
    glBindBuffer(GL_ARRAY_BUFFER,color_buffer);
    glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(*vertices),color_buffer?(const void *)offsetof(struct Vertex,color):vertices[0].color);
    glBindBuffer(GL_ARRAY_BUFFER,array_buffer);
}
static void pixels(unsigned layout,GLenum type,unsigned mode,int negative,int buffered,const char *phase)
{
    const unsigned xs[]={4,16,28};const GLubyte expected[][4]={{255,0,0,255},{0,0,0,0},{0,0,255,255}};
    unsigned before=failures;
    for(unsigned i=0;i<3;++i){
        GLubyte rgba[4]={0};glReadPixels(xs[i],8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
        if(memcmp(rgba,expected[i],4))fprintf(stderr,"pixel x=%u rgba=%u,%u,%u,%u\n",xs[i],rgba[0],rgba[1],rgba[2],rgba[3]);
        CHECK(!memcmp(rgba,expected[i],4));
    }
    CHECK(glGetError()==GL_NO_ERROR);++observations;
    printf("RESULT family=%s layout=%u type=%x mode=%x base_case=%d ebo=%d phase=%s failures=%u\n",
        families[family],layout,(unsigned)type,mode,negative,buffered,phase,failures-before);fflush(stdout);
}
int main(int argc,char **argv)
{
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--cpu-list-control"))cpu_list_control=1;
        else if(!strcmp(argv[i],"--safe-empty-pointer"))safe_empty_pointer=1;
        else if(!strcmp(argv[i],"--kind") && i+1<argc){
            for(family=0;family<4;++family)if(!strcmp(argv[i+1],families[family]))break;
            if(family==4)return 2;++i;
        }else return 2;
    }
    ProbeContext context=NULL;
#ifdef VIM_COMPILED_INDEX_WIRE
    unsigned char surface[32*16*4]={0};
    context=DGCreateContext(NULL,NULL);
    if(!context||!DGMakeCurrent(context,surface,GL_UNSIGNED_BYTE,32,16))return 2;
#else
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&format,&n)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;CGLDestroyPixelFormat(format);
#endif
    printf("family=%s renderer=%s version=%s bits=%u\n",families[family],glGetString(GL_RENDERER),glGetString(GL_VERSION),(unsigned)sizeof(void *)*8);fflush(stdout);
    const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
    unsigned major=0,minor=0;sscanf((const char *)glGetString(GL_VERSION),"%u.%u",&major,&minor);
    if(!family && major==1 && minor<4 && (!extensions||!strstr(extensions,"GL_EXT_multi_draw_arrays"))){
        puts("UNSUPPORTED public multi-draw family");close_context(context);return 77;
    }
    if(family){
#ifdef VIM_COMPILED_INDEX_WIRE
        elements_base=glDrawElementsBaseVertex;range_base=glDrawRangeElementsBaseVertex;multi_base=glMultiDrawElementsBaseVertex;
#else
        elements_base=(ElementsBase)dlsym(RTLD_DEFAULT,"glDrawElementsBaseVertex");
        range_base=(RangeBase)dlsym(RTLD_DEFAULT,"glDrawRangeElementsBaseVertex");
        multi_base=(MultiBase)dlsym(RTLD_DEFAULT,"glMultiDrawElementsBaseVertex");
#endif
        if(!elements_base||!range_base||!multi_base||!extensions||!strstr(extensions,"GL_ARB_draw_elements_base_vertex")){
            puts("UNSUPPORTED public base-vertex family");close_context(context);return 77;
        }
    }
    GLuint fbo=0,rb=0,vbo=0,ebo=0;glGenRenderbuffersEXT(1,&rb);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rb);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,32,16);glGenFramebuffersEXT(1,&fbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,rb);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glViewport(0,0,32,16);glDisable(GL_DITHER);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();glClearColor(0,0,0,0);
    glGenBuffers(1,&vbo);glGenBuffers(1,&ebo);const GLenum types[]={GL_UNSIGNED_BYTE,GL_UNSIGNED_SHORT,GL_UNSIGNED_INT};
    for(unsigned layout=0;layout<4;++layout)for(unsigned t=0;t<3;++t)
    for(int negative=0;negative<(family?3:1);++negative)for(int buffered=0;buffered<2;++buffered)for(unsigned mode=0;mode<3;++mode){
        struct Vertex vertices[10];union Indices indices;setup(vertices,&indices,vbo,ebo,layout,types[t],negative,buffered);
        glClear(GL_COLOR_BUFFER_BIT);GLuint list=0;
        if(mode){list=glGenLists(1);CHECK(list!=0);glNewList(list,mode==1?GL_COMPILE:GL_COMPILE_AND_EXECUTE);}
        if(mode && cpu_list_control)control(vertices,&indices,types[t],negative);
        else issue(types[t],&indices,negative,buffered);
        if(mode){
            glEndList();CHECK(glGetError()==GL_NO_ERROR && glIsList(list));
            if(mode==2)pixels(layout,types[t],mode,negative,buffered,"initial");
            for(unsigned i=0;i<10;++i){vertices[i].xy[0]=vertices[i].xy[1]=2;vertices[i].color[0]=vertices[i].color[2]=0;vertices[i].color[1]=255;}
            glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(vertices),vertices);
            memset(&indices,0,sizeof(indices));glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,0,sizeof(indices),&indices);glClear(GL_COLOR_BUFFER_BIT);glCallList(list);
        }
        pixels(layout,types[t],mode,negative,buffered,mode?"playback":"immediate");
        GLint binding=-1;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&binding);CHECK(binding==(GLint)vbo);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&binding);CHECK(binding==(GLint)(mode||buffered?ebo:0));
        CHECK(glGetError()==GL_NO_ERROR);if(list)glDeleteLists(list,1);++cases;
    }
    glDisableClientState(GL_VERTEX_ARRAY);glDisableClientState(GL_COLOR_ARRAY);glDeleteBuffers(1,&vbo);glDeleteBuffers(1,&ebo);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteRenderbuffersEXT(1,&rb);CHECK(glGetError()==GL_NO_ERROR);
    close_context(context);
    printf("compiled-index-families family=%s control=%d safe_empty=%d cases=%u observations=%u failures=%u\n",families[family],cpu_list_control,safe_empty_pointer,cases,observations,failures);return failures?1:0;
}
