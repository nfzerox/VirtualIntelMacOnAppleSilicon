/* Public native CGL buffer contracts on physical accelerated Mavericks and
 * the isolated guest candidate. Never installs drivers or exports pointers. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned failures;
extern void glGenVertexArraysAPPLE(GLsizei,GLuint *);
extern void glBindVertexArrayAPPLE(GLuint);
extern void glDeleteVertexArraysAPPLE(GLsizei,const GLuint *);
extern void glBufferParameteriAPPLE(GLenum,GLenum,GLint);
extern void glFlushMappedBufferRangeAPPLE(GLenum,GLintptr,GLsizeiptr);
static void check(int good,const char *name)
{ printf("%s %s\n",good?"PASS":"FAIL",name);failures+=!good;fflush(stdout); }
static CGLContextObj context(int legacy,CGLContextObj share)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(legacy?kCGLOGLPVersion_Legacy:kCGLOGLPVersion_3_2_Core),0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint count=0;
    CGLError e=CGLChoosePixelFormat(attrs,&pf,&count);
    if(!e && pf)e=CGLCreateContext(pf,share,&c);if(pf)CGLDestroyPixelFormat(pf);
    check(!e && c,"accelerated buffer context");return c;
}
static int all(const unsigned char *p,size_t size,unsigned char value)
{ for(size_t i=0;i<size;++i)if(p[i]!=value)return 0;return 1; }
static GLuint shader(GLenum kind,const char *source)
{
    GLuint name=glCreateShader(kind);GLint good=0;glShaderSource(name,1,&source,NULL);glCompileShader(name);
    glGetShaderiv(name,GL_COMPILE_STATUS,&good);check(good,"buffer consumer shader compile");return name;
}
static void bufferGPUConsumer(int mapped,int legacy)
{
    const GLfloat points[]={-1,-1,1,-1,0,1};const unsigned char zeros[64]={0};
    GLuint source=0,destination=0,vao=0,texture=0,fbo=0;
    if(!mapped) { glGenBuffers(1,&source);glBindBuffer(GL_COPY_READ_BUFFER,source);glBufferData(GL_COPY_READ_BUFFER,sizeof(points),points,GL_STATIC_DRAW); }
    GLenum destinationTarget=legacy?GL_ARRAY_BUFFER:GL_COPY_WRITE_BUFFER;
    glGenBuffers(1,&destination);glBindBuffer(destinationTarget,destination);glBufferData(destinationTarget,sizeof(zeros),zeros,GL_STATIC_DRAW);
    if(mapped) {
        glBindBuffer(GL_ARRAY_BUFFER,destination);
        if(mapped==2) {
            glBufferParameteriAPPLE(GL_ARRAY_BUFFER,0x8a13,GL_FALSE); // BUFFER_FLUSHING_UNMAP_APPLE
            GLenum error=glGetError();printf("BUFFER APPLE unmap policy legacy=%d error=%x\n",legacy,error);
            check(error==(legacy?GL_NO_ERROR:GL_INVALID_OPERATION),"profile-correct APPLE unmap policy");
        }
        void *p=mapped==3?glMapBufferRange(GL_ARRAY_BUFFER,16,sizeof(points),GL_MAP_WRITE_BIT|GL_MAP_FLUSH_EXPLICIT_BIT):
            glMapBuffer(GL_ARRAY_BUFFER,GL_WRITE_ONLY);
        check(p && !glGetError(),"GPU-consumer write-only map");
        if(p) {
            memcpy(p,points,sizeof(points));
            if(mapped>=2) {
                glFlushMappedBufferRangeAPPLE(GL_ARRAY_BUFFER,0,sizeof(points));GLenum error=glGetError();
                printf("BUFFER APPLE flush legacy=%d mode=%d error=%x\n",legacy,mapped,error);
                check(error==(legacy?GL_NO_ERROR:GL_INVALID_OPERATION),"profile-correct APPLE flush");
                if(mapped==3) { glFlushMappedBufferRange(GL_ARRAY_BUFFER,0,sizeof(points));check(!glGetError(),"core explicit flush publishes geometry"); }
            }
            check(glUnmapBuffer(GL_ARRAY_BUFFER)==GL_TRUE && !glGetError(),"GPU-consumer unmap");
        }
    } else glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,0,sizeof(points));
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"buffer GPU-consumer framebuffer");
    GLuint vertex=shader(GL_VERTEX_SHADER,legacy?
        "#version 120\nattribute vec2 position;void main(){gl_Position=vec4(position,0,1);}":
        "#version 150\nin vec2 position;void main(){gl_Position=vec4(position,0,1);}"),
        fragment=shader(GL_FRAGMENT_SHADER,legacy?"#version 120\nvoid main(){gl_FragColor=vec4(1,0,1,1);}":
        "#version 150\nout vec4 color;void main(){color=vec4(1,0,1,1);}"),program=glCreateProgram();
    glAttachShader(program,vertex);glAttachShader(program,fragment);glBindAttribLocation(program,0,"position");glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);check(linked,"buffer consumer program link");glUseProgram(program);
    if(legacy) { glGenVertexArraysAPPLE(1,&vao);glBindVertexArrayAPPLE(vao); }
    else { glGenVertexArrays(1,&vao);glBindVertexArray(vao); }
    glBindBuffer(GL_ARRAY_BUFFER,destination);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,mapped==3?(void *)16:NULL);glEnableVertexAttribArray(0);
    glViewport(0,0,16,16);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,3);
    unsigned char p[4]={0};glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);GLenum error=glGetError();
    printf("BUFFER %s-VBO mode=%d GPU pixel=%u,%u,%u,%u error=%x\n",mapped?"mapped":"copied",mapped,p[0],p[1],p[2],p[3],error);
    check(p[0]==255 && !p[1] && p[2]==255 && p[3]==255 && !error,
        mapped?"actual GPU draw consumes mapped buffer":"actual GPU draw consumes copied buffer");
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);
    glDeleteBuffers(1,&source);glDeleteBuffers(1,&destination);
    if(legacy)glDeleteVertexArraysAPPLE(1,&vao);else glDeleteVertexArrays(1,&vao);
    glUseProgram(0);glDeleteProgram(program);glDeleteShader(vertex);glDeleteShader(fragment);
}
static void queries(int legacy,GLuint buffer)
{
    GLint size=-1,usage=-1,mapped=-1;GLint64 big=-1;
    glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&size);
    glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_USAGE,&usage);
    glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_MAPPED,&mapped);
    check(size==128 && usage==GL_DYNAMIC_DRAW && !mapped && !glGetError(),"typed buffer metadata");
    if(!legacy) { glGetBufferParameteri64v(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&big);check(big==128 && !glGetError(),"64-bit buffer metadata"); }
    else {
        glGetBufferParameteri64v(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&big);
        printf("BUFFER opengl-wide value=%lld error=%x\n",(long long)big,glGetError());
    }
    GLint bad=0x1234abcd;glGetBufferParameteriv(GL_ARRAY_BUFFER,0xdead,&bad);
    check(glGetError()==GL_INVALID_ENUM && bad==0x1234abcd,"failed metadata query preserves output");
    unsigned char output[32];memset(output,0xa5,sizeof(output));
    glGetBufferSubData(GL_ARRAY_BUFFER,7,19,output+4);int correct=all(output,4,0xa5) && all(output+23,9,0xa5);
    for(int i=0;i<19;++i)correct&=output[4+i]==7+i;
    check(correct && !glGetError(),"bounded buffer readback and output canaries");
    memset(output,0xa5,sizeof(output));glGetBufferSubData(GL_ARRAY_BUFFER,-1,10,output);
    check(glGetError()==GL_INVALID_VALUE && all(output,sizeof(output),0xa5),"negative readback offset preserves output");
    glGetBufferSubData(GL_ARRAY_BUFFER,0,-1,output);
    check(glGetError()==GL_INVALID_VALUE && all(output,sizeof(output),0xa5),"negative readback size preserves output");
    glGetBufferSubData(GL_ARRAY_BUFFER,120,16,output);
    check(glGetError()==GL_INVALID_VALUE && all(output,sizeof(output),0xa5),"out-of-range readback preserves output");
    glGetBufferSubData(GL_ARRAY_BUFFER,0,0,output);
    check(!glGetError() && all(output,sizeof(output),0xa5),"zero-byte readback preserves output");
    unsigned char large[8208];memset(large,0xa5,sizeof(large));
    glGetBufferSubData(GL_ARRAY_BUFFER,64,sizeof(large),large);
    check(glGetError()==GL_INVALID_VALUE && all(large,sizeof(large),0xa5),"failed multi-chunk response preserves output");
    size=-1;glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&size);
    check(size==128 && !glGetError(),"error response leaves stream correctly framed");
    if(legacy) {
        // glBegin/End are deliberately absent from gl3.h, but these are
        // actual legacy public entries, used only with a legacy context.
        extern void glBegin(GLenum);extern void glEnd(void);
        GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
        check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE && !glGetError(),"valid legacy Begin/End target");
        bad=0x1234abcd;glBegin(GL_POINTS);glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&bad);glEnd();
        GLenum during=glGetError();printf("BUFFER Begin/End query value=%x error=%x\n",bad,during);
        check(during==GL_INVALID_OPERATION && bad==0x1234abcd,"query during Begin/End preserves output");
        glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);
    }
    if(!legacy) {
        GLuint copy=0;glGenBuffers(1,&copy);glBindBuffer(GL_COPY_READ_BUFFER,buffer);glBindBuffer(GL_COPY_WRITE_BUFFER,copy);
        unsigned char zeros[128]={0},result[128]={0};glBufferData(GL_COPY_WRITE_BUFFER,sizeof(zeros),zeros,GL_DYNAMIC_DRAW);
        glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,5,17,23);
        glGetBufferSubData(GL_COPY_WRITE_BUFFER,0,sizeof(result),result);correct=all(result,17,0) && all(result+40,88,0);
        for(int i=0;i<23;++i)correct&=result[17+i]==5+i;
        check(correct && !glGetError(),"native buffer copy");
        glBindBuffer(GL_COPY_READ_BUFFER,copy);glCopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,1,8);
        check(glGetError()==GL_INVALID_VALUE,"overlapping self-copy rejected");glDeleteBuffers(1,&copy);
        bufferGPUConsumer(0,0);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    }
}
static void mapping(int legacy,CGLContextObj parent,GLuint buffer)
{
    glBindBuffer(GL_ARRAY_BUFFER,buffer);unsigned char *p=glMapBuffer(GL_ARRAY_BUFFER,GL_READ_WRITE);GLenum e=glGetError();
    printf("BUFFER initial-map pointer-null=%d error=%x\n",p==NULL,e);
    check(p && !e,"real read/write buffer map");if(!p)return;
    void *queried=NULL;GLint mapped=0;glGetBufferPointerv(GL_ARRAY_BUFFER,GL_BUFFER_MAP_POINTER,&queried);
    glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_MAPPED,&mapped);
    check(queried==p && mapped && !glGetError(),"mapped pointer identity and state");
    int correct=1;for(int i=0;i<128;++i)correct&=p[i]==i;check(correct,"read/write map contents");
    for(int i=0;i<8;++i)p[i]=0xf0+i;
    unsigned char canary[16];memset(canary,0xa5,sizeof(canary));glGetBufferSubData(GL_ARRAY_BUFFER,0,sizeof(canary),canary);
    check(glGetError()==GL_INVALID_OPERATION && all(canary,sizeof(canary),0xa5),"mapped readback rejects without touching output");
    void *nested=glMapBuffer(GL_ARRAY_BUFFER,GL_READ_ONLY);
    check(!nested && glGetError()==GL_INVALID_OPERATION,"nested buffer map rejected");
    check(glUnmapBuffer(GL_ARRAY_BUFFER)==GL_TRUE && !glGetError(),"unmap publishes real writes");
    memset(canary,0,sizeof(canary));glGetBufferSubData(GL_ARRAY_BUFFER,0,8,canary);
    correct=1;for(int i=0;i<8;++i)correct&=canary[i]==0xf0+i;check(correct && !glGetError(),"unmapped GPU buffer contents");
    p=glMapBuffer(GL_ARRAY_BUFFER,GL_READ_ONLY);check(p && !glGetError(),"read-only buffer map");
    if(p) { check(p[0]==0xf0 && p[7]==0xf7,"read-only map contents");glUnmapBuffer(GL_ARRAY_BUFFER); }
    unsigned char before[128],after[128];glGetBufferSubData(GL_ARRAY_BUFFER,0,sizeof(before),before);
    p=glMapBuffer(GL_ARRAY_BUFFER,GL_WRITE_ONLY);check(p && !glGetError(),"write-only preserve map");
    if(p) {
        GLint access=0;glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_ACCESS,&access);
        check(access==GL_WRITE_ONLY && !glGetError(),"native write-only access metadata");
        p[9]=0xcd;glUnmapBuffer(GL_ARRAY_BUFFER);glGetBufferSubData(GL_ARRAY_BUFFER,0,sizeof(after),after);before[9]=0xcd;
        check(!memcmp(before,after,sizeof(before)) && !glGetError(),"write-only partial update preserves untouched bytes");
    }
    bufferGPUConsumer(1,legacy);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    bufferGPUConsumer(2,legacy);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    if(!legacy) { bufferGPUConsumer(3,0);glBindBuffer(GL_ARRAY_BUFFER,buffer); }
    CGLContextObj child=context(legacy,parent);
    if(child) {
        CGLSetCurrentContext(parent);p=glMapBuffer(GL_ARRAY_BUFFER,GL_READ_WRITE);
        check(p && !glGetError(),"shared object buffer map");
        CGLSetCurrentContext(child);glBindBuffer(GL_ARRAY_BUFFER,buffer);queried=NULL;
        glGetBufferPointerv(GL_ARRAY_BUFFER,GL_BUFFER_MAP_POINTER,&queried);
        check(p && queried==p && !glGetError(),"map pointer visible in shared context");
        check(glUnmapBuffer(GL_ARRAY_BUFFER)==GL_TRUE && !glGetError(),"shared context unmaps buffer");
        CGLSetCurrentContext(parent);queried=(void *)1;glGetBufferPointerv(GL_ARRAY_BUFFER,GL_BUFFER_MAP_POINTER,&queried);
        check(!queried && !glGetError(),"shared unmap invalidates original pointer query");
    }
    if(!legacy) {
        int oldQuirks=getenv("VIM_MAVERICKS_MAP_QUIRKS")!=NULL;
        p=glMapBufferRange(GL_ARRAY_BUFFER,16,32,GL_MAP_READ_BIT|GL_MAP_WRITE_BIT);
        check(p && !glGetError(),"native ranged buffer map");
        if(p) {
            GLint64 start=-1,length=-1;glGetBufferParameteri64v(GL_ARRAY_BUFFER,GL_BUFFER_MAP_OFFSET,&start);
            glGetBufferParameteri64v(GL_ARRAY_BUFFER,GL_BUFFER_MAP_LENGTH,&length);
            check(start==16 && length==32 && !glGetError(),"typed map range metadata");
            p[2]=0xa9;glFlushMappedBufferRange(GL_ARRAY_BUFFER,2,1);
            e=glGetError();printf("BUFFER non-explicit flush error=%x\n",e);
            check(e==(oldQuirks?GL_NO_ERROR:GL_INVALID_OPERATION),"qualified non-explicit flush policy");glUnmapBuffer(GL_ARRAY_BUFFER);
        }
        p=glMapBufferRange(GL_ARRAY_BUFFER,16,32,GL_MAP_WRITE_BIT|GL_MAP_FLUSH_EXPLICIT_BIT);
        check(p && !glGetError(),"explicit-flush buffer map");
        if(p) {
            memset(p+4,0xab,4);glFlushMappedBufferRange(GL_ARRAY_BUFFER,4,4);
            check(!glGetError(),"flush offset relative to mapped window");
            glFlushMappedBufferRange(GL_ARRAY_BUFFER,-1,1);check(glGetError()==GL_INVALID_VALUE,"negative flush offset rejected");
            glFlushMappedBufferRange(GL_ARRAY_BUFFER,31,2);e=glGetError();printf("BUFFER oversized flush error=%x\n",e);
            check(e==(oldQuirks?GL_NO_ERROR:GL_INVALID_VALUE),"qualified oversized flush policy");
            glUnmapBuffer(GL_ARRAY_BUFFER);memset(canary,0,sizeof(canary));glGetBufferSubData(GL_ARRAY_BUFFER,20,4,canary);
            check(all(canary,4,0xab) && !glGetError(),"explicitly flushed bytes reach GPU storage");
        }
        p=glMapBufferRange(GL_ARRAY_BUFFER,0,0,GL_MAP_WRITE_BIT);
        printf("BUFFER zero-map pointer-null=%d error=%x\n",p==NULL,glGetError());if(p)glUnmapBuffer(GL_ARRAY_BUFFER);
    }
    // Record actual orphan/delete lifetime policy before implementing it.
    p=glMapBuffer(GL_ARRAY_BUFFER,GL_READ_WRITE);check(p && !glGetError(),"orphan lifetime map");
    if(p) {
        glBufferData(GL_ARRAY_BUFFER,128,NULL,GL_DYNAMIC_DRAW);e=glGetError();mapped=-1;queried=(void *)1;
        glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_MAPPED,&mapped);glGetBufferPointerv(GL_ARRAY_BUFFER,GL_BUFFER_MAP_POINTER,&queried);
        printf("BUFFER orphan error=%x mapped=%d pointer-null=%d query-error=%x\n",e,mapped,queried==NULL,glGetError());
        if(mapped==GL_TRUE)glUnmapBuffer(GL_ARRAY_BUFFER);
    }
    if(child) {
        GLuint deleted=0;glGenBuffers(1,&deleted);glBindBuffer(GL_ARRAY_BUFFER,deleted);glBufferData(GL_ARRAY_BUFFER,128,NULL,GL_DYNAMIC_DRAW);
        p=glMapBuffer(GL_ARRAY_BUFFER,GL_READ_WRITE);check(p && !glGetError(),"retained-delete lifetime map");
        CGLSetCurrentContext(child);glBindBuffer(GL_ARRAY_BUFFER,deleted);
        CGLSetCurrentContext(parent);glDeleteBuffers(1,&deleted);e=glGetError();
        CGLSetCurrentContext(child);GLint binding=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&binding);
        mapped=-1;queried=NULL;glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_MAPPED,&mapped);
        glGetBufferPointerv(GL_ARRAY_BUFFER,GL_BUFFER_MAP_POINTER,&queried);
        printf("BUFFER retained-delete delete-error=%x binding=%u is-buffer=%d mapped=%d same-pointer=%d query-error=%x\n",
            e,(GLuint)binding,glIsBuffer(deleted),mapped,p && p==queried,glGetError());
        if(mapped==GL_TRUE) { GLboolean result=glUnmapBuffer(GL_ARRAY_BUFFER);printf("BUFFER retained-delete unmap=%d error=%x\n",result,glGetError()); }
        CGLSetCurrentContext(parent);CGLDestroyContext(child);
    }
    CGLContextObj creator=context(legacy,parent);
    if(creator) {
        CGLSetCurrentContext(creator);GLuint survivor=0;glGenBuffers(1,&survivor);glBindBuffer(GL_ARRAY_BUFFER,survivor);
        unsigned char zeros[128]={0};glBufferData(GL_ARRAY_BUFFER,sizeof(zeros),zeros,GL_DYNAMIC_DRAW);
        p=glMapBuffer(GL_ARRAY_BUFFER,GL_READ_WRITE);check(p && !glGetError(),"creator-lifetime map");
        CGLContextObj keeper=context(legacy,creator);
        if(keeper) {
            CGLSetCurrentContext(keeper);glBindBuffer(GL_ARRAY_BUFFER,survivor);CGLDestroyContext(creator);creator=NULL;
            queried=NULL;glGetBufferPointerv(GL_ARRAY_BUFFER,GL_BUFFER_MAP_POINTER,&queried);
            check(p && queried==p && !glGetError(),"mapping survives creating context destruction");
            if(p && queried==p) { p[3]=0xd7;check(glUnmapBuffer(GL_ARRAY_BUFFER)==GL_TRUE && !glGetError(),"surviving mapping unmap"); }
            CGLSetCurrentContext(parent);glBindBuffer(GL_ARRAY_BUFFER,survivor);memset(canary,0,sizeof(canary));
            glGetBufferSubData(GL_ARRAY_BUFFER,0,sizeof(canary),canary);
            check(canary[3]==0xd7 && !glGetError(),"surviving map writes reach shared storage");
            glDeleteBuffers(1,&survivor);CGLDestroyContext(keeper);
        }
        CGLSetCurrentContext(parent);if(creator) { glDeleteBuffers(1,&survivor);CGLDestroyContext(creator); }
    }
    glBindBuffer(GL_ARRAY_BUFFER,buffer);
    const char *stress=getenv("VIM_MAP_STRESS_COUNT");long count=stress?strtol(stress,NULL,10):0;
    if(count>0 && count<=4096) {
        int good=1;for(long i=0;i<count;++i) {
            p=glMapBuffer(GL_ARRAY_BUFFER,GL_READ_WRITE);if(!p) { good=0;break; }
            p[10]=(unsigned char)i;
            if(glUnmapBuffer(GL_ARRAY_BUFFER)!=GL_TRUE || glGetError()) { good=0;break; }
        }printf("BUFFER map/unmap stress count=%ld\n",count);check(good,"repeated map/unmap releases leases");
    }
}
static void largeMapping(int legacy)
{
    const size_t size=2*1024*1024+123;unsigned char *expected=malloc(size),*actual=malloc(size);
    check(expected && actual,"large mapping fixture allocations");if(!expected || !actual) { free(expected);free(actual);return; }
    for(size_t i=0;i<size;++i)expected[i]=(unsigned char)(i%251);
    GLuint buffer=0;glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,size,expected,GL_DYNAMIC_DRAW);
    unsigned char *p=glMapBuffer(GL_ARRAY_BUFFER,GL_WRITE_ONLY);check(p && !glGetError(),"multi-chunk write-only map");
    if(p) {
        const size_t offsets[]={0,1024*1024-1,1024*1024,2*1024*1024,size-1};
        for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) { p[offsets[i]]=0xdc-i;expected[offsets[i]]=0xdc-i; }
        check(glUnmapBuffer(GL_ARRAY_BUFFER)==GL_TRUE && !glGetError(),"multi-chunk unmap");
        glGetBufferSubData(GL_ARRAY_BUFFER,0,size,actual);
        check(!memcmp(expected,actual,size) && !glGetError(),"chunk boundaries and untouched bytes preserved");
    }
    if(!legacy) {
        size_t start=1024*1024-7,length=1024*1024+31;
        p=glMapBufferRange(GL_ARRAY_BUFFER,start,length,GL_MAP_READ_BIT|GL_MAP_WRITE_BIT);
        check(p && !glGetError(),"multi-chunk nonzero-offset map");
        if(p) {
            check(!memcmp(p,expected+start,length),"ranged initial bytes and offset");
            p[0]=expected[start]=0xa1;p[length-1]=expected[start+length-1]=0xb2;
            check(glUnmapBuffer(GL_ARRAY_BUFFER)==GL_TRUE && !glGetError(),"multi-chunk ranged unmap");
            glGetBufferSubData(GL_ARRAY_BUFFER,0,size,actual);
            check(!memcmp(expected,actual,size) && !glGetError(),"ranged writes preserve surrounding buffer");
        }
    }
    glDeleteBuffers(1,&buffer);free(expected);free(actual);
}
int main(void)
{
    int legacy=getenv("VIM_BUFFER_LEGACY")!=NULL;CGLContextObj c=context(legacy,NULL);if(!c)return 1;CGLSetCurrentContext(c);
    printf("BUFFER renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    GLuint buffer=0;glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    unsigned char seed[128];for(int i=0;i<128;++i)seed[i]=i;glBufferData(GL_ARRAY_BUFFER,sizeof(seed),seed,GL_DYNAMIC_DRAW);
    if(getenv("VIM_EXPECT_NO_BUFFER_QUERIES")) {
        GLint value=0x1234abcd;unsigned char bytes[8];memset(bytes,0xa5,sizeof(bytes));
        glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&value);
        check(glGetError()==GL_INVALID_OPERATION && value==0x1234abcd,"old executor rejects metadata locally");
        glGetBufferSubData(GL_ARRAY_BUFFER,0,sizeof(bytes),bytes);
        check(glGetError()==GL_INVALID_OPERATION && all(bytes,sizeof(bytes),0xa5),"old executor rejects readback locally");
        GLint64 wide=0x123456789abcdef;glGetBufferParameteri64v(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&wide);
        check(glGetError()==GL_INVALID_OPERATION && wide==0x123456789abcdef,"old executor rejects wide metadata locally");
        value=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&value);
        check((GLuint)value==buffer && !glGetError(),"old executor stream remains usable");
        glDeleteBuffers(1,&buffer);CGLSetCurrentContext(NULL);CGLDestroyContext(c);return failures?1:0;
    }
    queries(legacy,buffer);
    if(getenv("VIM_EXPECT_NO_MAPPING")) {
        void *p=glMapBuffer(GL_ARRAY_BUFFER,GL_READ_WRITE);
        check(!p && glGetError()==GL_INVALID_OPERATION,"old executor rejects mapping locally");
        p=(void *)1;glGetBufferPointerv(GL_ARRAY_BUFFER,GL_BUFFER_MAP_POINTER,&p);
        check(p==(void *)1 && glGetError()==GL_INVALID_OPERATION,"old executor preserves pointer output");
        check(glUnmapBuffer(GL_ARRAY_BUFFER)==GL_FALSE && glGetError()==GL_INVALID_OPERATION,"old executor rejects unmap locally");
        GLint size=0;glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&size);
        check(size==128 && !glGetError(),"mapping refusal leaves native buffer usable");
    } else if(!getenv("VIM_BUFFER_QUERY_ONLY")) {
        mapping(legacy,c,buffer);if(getenv("VIM_MAP_LARGE"))largeMapping(legacy);
    }
    glDeleteBuffers(1,&buffer);CGLSetCurrentContext(NULL);CGLDestroyContext(c);
    printf("buffer failures=%u\n",failures);return failures?1:0;
}
