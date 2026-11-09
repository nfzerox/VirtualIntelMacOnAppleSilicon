/* Native stipple state, PBO packing, client/server stacks and object lifetime. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <string.h>
static unsigned failures;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);fflush(stdout);failures+=!good;}
static void pack(unsigned shift,int lsb,int explicit_stride)
{
    glPixelStorei(GL_PACK_ALIGNMENT,8);glPixelStorei(GL_PACK_ROW_LENGTH,41);
    glPixelStorei(GL_PACK_SKIP_ROWS,2);glPixelStorei(GL_PACK_SKIP_PIXELS,(GLint)shift);
    glPixelStorei(GL_PACK_ROW_BYTES_APPLE,explicit_stride?7:0);glPixelStorei(GL_PACK_LSB_FIRST,lsb);
}
static void read_shape(GLuint buffer,const unsigned char *pattern,unsigned shift,int lsb,int explicit_stride,
    const char *name,int pending)
{
    unsigned char seed[512],want[512],actual[512];memset(seed,0xa5,sizeof(seed));memcpy(want,seed,sizeof(seed));
    GLint access=0;glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER,GL_BUFFER_ACCESS,&access);
    unsigned stride=explicit_stride?7:8;
    for(unsigned row=0;row<32;++row)for(unsigned x=0;x<32;++x){
        unsigned bit=x+shift,mask=1u<<(lsb?bit%8:7-bit%8),value=(pattern[row*4+x/8]>>(7-x%8))&1u;
        unsigned char *p=want+3+2*stride+row*stride+bit/8;
        *p=(unsigned char)((*p&~mask)|(value?mask:0));
    }
    glBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(seed),seed);pack(shift,lsb,explicit_stride);
    if(pending)glEnable(0xffffffffu);
    glGetPolygonStipple((GLubyte *)3);GLenum error=glGetError();
    glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(actual),actual);
    char label[160];snprintf(label,sizeof(label),"%s shift=%u lsb=%d rowbytes=%d",name,shift,lsb,explicit_stride?7:0);
    check(error==(pending?GL_INVALID_ENUM:GL_NO_ERROR)&&!glGetError()&&!memcmp(actual,want,sizeof(actual)),label);
    GLint binding=0,size=0,actual_access=0,rowbytes=0,skip=0;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&binding);glGetIntegerv(GL_PACK_ROW_BYTES_APPLE,&rowbytes);
    glGetIntegerv(GL_PACK_SKIP_PIXELS,&skip);glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER,GL_BUFFER_SIZE,&size);
    glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER,GL_BUFFER_ACCESS,&actual_access);
    check((GLuint)binding==buffer&&size==512&&access==actual_access&&rowbytes==(explicit_stride?7:0)&&
        skip==(GLint)shift&&!glGetError(),"stipple PBO preserves binding, size, access and pack state");
}
static void shapes(GLuint buffer,const unsigned char *pattern,const char *name)
{
    for(unsigned shift=0;shift<8;++shift)for(int lsb=0;lsb<2;++lsb)for(int stride=0;stride<2;++stride)
        read_shape(buffer,pattern,shift,lsb,stride,name,0);
}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj parent=NULL,child=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&parent)||CGLCreateContext(pf,parent,&child))return 2;
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(parent);
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    unsigned char a[128],b[128],actual[512],seed[512];memset(seed,0xa5,sizeof(seed));
    for(unsigned i=0;i<128;++i){a[i]=(unsigned char)(13+i*37);b[i]=(unsigned char)(241-i*19);}
    glPolygonStipple(a);
    GLuint buffer=0;glGenBuffers(1,&buffer);glBindBuffer(GL_PIXEL_PACK_BUFFER,buffer);
    glBufferData(GL_PIXEL_PACK_BUFFER,128,seed,GL_DYNAMIC_DRAW);glGetPolygonStipple(NULL);
    glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,128,actual);
    check(!glGetError()&&!memcmp(actual,a,128),"exact 128-byte PBO stipple query");
    glBufferData(GL_PIXEL_PACK_BUFFER,127,seed,GL_DYNAMIC_DRAW);glGetPolygonStipple(NULL);
    GLenum error=glGetError();glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,127,actual);
    check(error==GL_INVALID_OPERATION&&!glGetError()&&!memcmp(actual,seed,127),"undersized stipple PBO unchanged");
    glBufferData(GL_PIXEL_PACK_BUFFER,512,seed,GL_DYNAMIC_DRAW);
    void *mapped=glMapBuffer(GL_PIXEL_PACK_BUFFER,GL_READ_WRITE);
    check(mapped&&!glGetError(),"mapped stipple PBO control");
    if(mapped){glGetPolygonStipple(NULL);
        check(glGetError()==GL_INVALID_OPERATION&&!memcmp(mapped,seed,512),"mapped stipple PBO rejected without mutation");
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);}
    mapped=glMapBuffer(GL_PIXEL_PACK_BUFFER,GL_READ_ONLY);
    check(mapped&&!glGetError(),"stipple PBO read-only mapping history");if(mapped)glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    shapes(buffer,a,"named stipple PBO");
    read_shape(buffer,a,7,1,1,"stipple PBO retains prior error",1);
    glPushAttrib(GL_POLYGON_STIPPLE_BIT);glPolygonStipple(b);
    read_shape(buffer,b,3,0,0,"pushed native stipple state",0);glPopAttrib();
    read_shape(buffer,a,3,1,0,"popped native stipple state",0);
    glNewList(37,GL_COMPILE);glPolygonStipple(b);glEndList();glCallList(37);
    read_shape(buffer,b,5,1,1,"display-list native stipple state",0);glDeleteLists(37,1);
    glNewList(39,GL_COMPILE);glPolygonStipple(a);
    read_shape(buffer,b,3,1,1,"stipple read executes during list compilation",0);
    GLint compiling=0;glGetIntegerv(GL_LIST_INDEX,&compiling);
    check(compiling==39&&!glGetError(),"stipple snapshot preserves native list compilation");
    glEndList();glCallList(39);
    read_shape(buffer,a,3,0,1,"compiled list retains its native stipple command",0);
    glDeleteLists(39,1);glPolygonStipple(b);
    CGLSetCurrentContext(child);glPolygonStipple(a);glBindBuffer(GL_PIXEL_PACK_BUFFER,buffer);
    CGLSetCurrentContext(parent);glDeleteBuffers(1,&buffer);CGLSetCurrentContext(child);
    check(!glIsBuffer(buffer)&&!glGetError(),"stipple PBO deleted name remains retained");
    shapes(buffer,a,"deleted-name stipple PBO");
    read_shape(buffer,a,7,0,1,"deleted-name stipple prior error",1);
    GLint client=0,client_max=0,server=0,server_max=0;
    glGetIntegerv(GL_CLIENT_ATTRIB_STACK_DEPTH,&client);glGetIntegerv(GL_MAX_CLIENT_ATTRIB_STACK_DEPTH,&client_max);
    glGetIntegerv(GL_ATTRIB_STACK_DEPTH,&server);glGetIntegerv(GL_MAX_ATTRIB_STACK_DEPTH,&server_max);
    for(GLint i=client;i<client_max;++i)glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    for(GLint i=server;i<server_max;++i)glPushAttrib(GL_CURRENT_BIT);
    check(!glGetError(),"fill client and server stacks without extra PBO retention");
    read_shape(buffer,a,7,1,1,"stipple PBO with full application stacks",0);
    for(GLint i=server;i<server_max;++i)glPopAttrib();
    for(GLint i=client;i<client_max;++i)glPopClientAttrib();
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);CGLSetCurrentContext(parent);
    /* The parent's independent stipple state must not be confused with child. */
    GLuint other=0;glGenBuffers(1,&other);glBindBuffer(GL_PIXEL_PACK_BUFFER,other);
    glBufferData(GL_PIXEL_PACK_BUFFER,512,seed,GL_DYNAMIC_DRAW);
    read_shape(other,b,3,0,1,"parent native state after child queries",0);glDeleteBuffers(1,&other);
    CGLSetCurrentContext(NULL);CGLDestroyContext(child);CGLDestroyContext(parent);
    printf("stipple-pbo failures=%u\n",failures);return failures?1:0;
}
