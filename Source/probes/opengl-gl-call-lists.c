/* Actual public-CGL display-list oracle for physical Intel and the guest.
 * Protected input tails verify byte/short/int/float and packed 2/3/4-byte
 * indices without exposing a process pointer to the other interpreter. */
#ifdef VIM_CALL_LISTS_DIRECT
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <OpenGL/CGLRenderers.h>
#include "opengl-reference-renderer.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static int failures;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);if(!good)++failures;}
static void pixel(const char *label,unsigned r,unsigned g,unsigned b)
{
    unsigned char p[4]={0};glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    GLenum e=glGetError();printf("%s error=%x pixel=%u,%u,%u,%u\n",label,e,p[0],p[1],p[2],p[3]);
    check(!e && p[0]==r && p[1]==g && p[2]==b && p[3]==255,label);
}
static void fill(void)
{
    glBegin(GL_QUADS);glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();
}
static size_t encode(void *destination,GLenum type)
{
    const GLbyte b[]={1,2,3};const GLubyte ub[]={1,2,3};
    const GLshort s[]={1,2,3};const GLushort us[]={1,2,3};
    const GLint i[]={1,2,3};const GLuint ui[]={1,2,3};const GLfloat f[]={1,2,3};
    const GLubyte two[]={0,1,0,2,0,3},three[]={0,0,1,0,0,2,0,0,3},four[]={0,0,0,1,0,0,0,2,0,0,0,3};
    const void *p=NULL;size_t size=0;
    switch(type){
    case GL_BYTE:p=b;size=sizeof(b);break;case GL_UNSIGNED_BYTE:p=ub;size=sizeof(ub);break;
    case GL_SHORT:p=s;size=sizeof(s);break;case GL_UNSIGNED_SHORT:p=us;size=sizeof(us);break;
    case GL_INT:p=i;size=sizeof(i);break;case GL_UNSIGNED_INT:p=ui;size=sizeof(ui);break;
    case GL_FLOAT:p=f;size=sizeof(f);break;
    case GL_2_BYTES:p=two;size=sizeof(two);break;case GL_3_BYTES:p=three;size=sizeof(three);break;
    case GL_4_BYTES:p=four;size=sizeof(four);break;
    }
    if(destination && p)memcpy(destination,p,size);return size;
}
int main(int argc,char **argv)
{
    if(argc<2||argc>3||strcmp(argv[1],"--require-intel")||
       (argc==3&&strcmp(argv[2],"--intel-hd5000")))return 2;
#ifdef VIM_CALL_LISTS_DIRECT
    if(argc!=2)return 2;
    unsigned char shadow[16*16*4]={0};struct DGContext *c=DGCreateContext(NULL,NULL);
    if(!c||!DGMakeCurrent(c,shadow,GL_UNSIGNED_BYTE,16,16))return 2;
#else
    GLint intel=argc==3?VIMReferenceIntelHD5000():0;if(argc==3&&!intel)return 2;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAAllowOfflineRenderers,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAColorSize,24,kCGLPFAAlphaSize,8,
        argc==3?kCGLPFARendererID:0,(CGLPixelFormatAttribute)intel,0};
    CGLPixelFormatObj format=NULL;CGLContextObj c=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&format,&count)||!format||CGLCreateContext(format,NULL,&c)||!c)return 2;
    CGLSetCurrentContext(c);
#endif
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    printf("renderer=%s\n",renderer?renderer:"none");if(!renderer||!strstr(renderer,"Intel"))return 2;
#ifdef VIM_CALL_LISTS_DIRECT
    if(getenv("VIM_TEST_CALL_LISTS_LEGACY_PEER")){
        c->connection->capabilities&=~DG_CAP_CALL_LISTS;
        GLubyte index=1;glCallLists(1,GL_UNSIGNED_BYTE,&index);
        check(glGetError()==GL_INVALID_OPERATION,"unnegotiated list opcode stays local");
        GLint untouched=0x13572468;glGetIntegerv(GL_LIST_BASE,&untouched);
        check(glGetError()==GL_INVALID_OPERATION && untouched==0x13572468,"unnegotiated list query preserves caller bytes");
        glViewport(0,0,16,16);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        pixel("old peer still executes ordinary rendering after rejections",255,0,0);
        check(!c->connection->dead,"old peer transport remains live");DGDestroyContext(c);
        printf("opengl-peer failures=%d\n",failures);return failures?1:0;
    }
#endif
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"real color framebuffer");
    glViewport(0,0,16,16);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    GLuint base=glGenLists(9);check(base!=0,"native list allocation");if(!base)return 2;
    for(unsigned n=0;n<3;++n){
        glNewList(base+n,GL_COMPILE);glColor3ub(n==0?255:0,n==1?255:0,n==2?255:0);fill();glEndList();
    }
    check(!glGetError(),"compile native drawing lists");
    long page=sysconf(_SC_PAGESIZE);if(page<4096)return 2;
    unsigned char *memory=mmap(NULL,(size_t)page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(memory==MAP_FAILED||mprotect(memory+page,(size_t)page,PROT_NONE))return 2;
    const GLenum types[]={GL_BYTE,GL_UNSIGNED_BYTE,GL_SHORT,GL_UNSIGNED_SHORT,GL_INT,GL_UNSIGNED_INT,
        GL_FLOAT,GL_2_BYTES,GL_3_BYTES,GL_4_BYTES};
    for(unsigned n=0;n<sizeof(types)/sizeof(types[0]);++n){
        size_t bytes=encode(NULL,types[n]);void *indices=memory+page-bytes;encode(indices,types[n]);
        glListBase(base-1);glClear(GL_COLOR_BUFFER_BIT);glCallLists(3,types[n],indices);
        char label[96];snprintf(label,sizeof(label),"guarded ordered list type=%x bytes=%lu",types[n],(unsigned long)bytes);
        pixel(label,0,0,255);
        // Neither zero-count input nor invalid count/type may read a byte.
        glCallLists(0,types[n],memory+page);check(!glGetError(),"zero count ignores protected input");
    }
    const GLbyte negative[]={-2,-1,0};glListBase(base+2);glCallLists(3,GL_BYTE,negative);
    pixel("signed negative offsets use native list base",0,0,255);
    GLubyte nested[]={0,1};glListBase(base);
    glNewList(base+3,GL_COMPILE);
    GLint list_index=-1,list_mode=-1;
    glGetIntegerv(GL_LIST_INDEX,&list_index);glGetIntegerv(GL_LIST_MODE,&list_mode);
    check(!glGetError() && (GLuint)list_index==base+3 && list_mode==GL_COMPILE,"native compiling-list state queries");
    glCallLists(2,GL_UNSIGNED_BYTE,nested);glEndList();
    nested[0]=nested[1]=2;glClear(GL_COLOR_BUFFER_BIT);glCallList(base+3);
    pixel("compiled list owns copied indices",0,255,0);
    glListBase(base+1);glCallList(base+3);pixel("compiled list uses execution-time list base",0,0,255);
    GLint saved=-1;glGetIntegerv(GL_LIST_BASE,&saved);check(!glGetError() && (GLuint)saved==base+1,"native list-base state query");
    glListBase(0);GLuint outer=base+3;glCallLists(1,GL_UNSIGNED_INT,&outer);
    // The nested list sees list base zero now; those names may be undefined.
    check(!glGetError(),"nested list invocation with undefined names remains valid");
    glListBase(base-1);glNewList(base+4,GL_COMPILE_AND_EXECUTE);
    const GLushort forward[]={1,2,3};glCallLists(3,GL_UNSIGNED_SHORT,forward);glEndList();
    pixel("compile-and-execute list sequence",0,0,255);
    glNewList(base+5,GL_COMPILE);glVertex2f(-1,-1);glEndList();
    glNewList(base+6,GL_COMPILE);glVertex2f(1,-1);glEndList();
    glNewList(base+7,GL_COMPILE);glVertex2f(0,1);glEndList();
    glClear(GL_COLOR_BUFFER_BIT);glColor3ub(255,0,0);glListBase(base+5);
    const GLubyte triangle[]={0,1,2};glBegin(GL_TRIANGLES);glCallLists(3,GL_UNSIGNED_BYTE,triangle);glEnd();
    pixel("list sequence inside Begin/End",255,0,0);
    glNewList(base+8,GL_COMPILE);glColor3ub(0,255,0);glEndList();
    const GLsizei many=70001;GLubyte *large=calloc((size_t)many,1);if(!large)return 2;
    glListBase(base+8);glColor3ub(255,0,0);glCallLists(many,GL_UNSIGNED_BYTE,large);
    // The returned API must not retain the caller's mutable array, including
    // across the transport's large-input borrowing/batching boundary.
    memset(large,1,(size_t)many);free(large);fill();
    pixel("large list call consumes input before caller reuse",0,255,0);
    glCallLists(-1,GL_UNSIGNED_BYTE,memory+page);GLenum e=glGetError();
    /* Both actual Mavericks Intel and the current Intel host ignore a
     * negative count here, before reading input. Preserve native behavior. */
    printf("negative count error=%x\n",e);check(!e,"native negative-count no-op without reading input");
    glCallLists(0,0xdead,memory+page);e=glGetError();printf("invalid type zero error=%x\n",e);
    check(e==GL_INVALID_ENUM,"invalid type is not hidden by zero count");
    glCallLists(1,0xdead,memory+page);e=glGetError();check(e==GL_INVALID_ENUM,"invalid type rejects without reading input");
    check(!glGetError(),"error consumed once");
    glDeleteLists(base,9);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    check(!glGetError(),"native list teardown");munmap(memory,(size_t)page*2);
#ifdef VIM_CALL_LISTS_DIRECT
    check(!c->connection->dead,"list validation preserves the live transport");DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);CGLDestroyPixelFormat(format);
#endif
    printf("call-list failures=%d\n",failures);return failures?1:0;
}
