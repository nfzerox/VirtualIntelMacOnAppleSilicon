/* One public GL call per resource operation, beyond one wire packet.
 * Native physical Intel is the oracle for content, errors and failed writes. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned failures;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);fflush(stdout);failures+=!good;}
static void fill(unsigned char *data,size_t bytes,unsigned seed)
{for(size_t i=0;i<bytes/4;++i)((uint32_t *)data)[i]=(uint32_t)i*2654435761u+seed;}
static int uniform(const unsigned char *p,size_t bytes,unsigned char value)
{for(size_t i=0;i<bytes;++i)if(p[i]!=value)return 0;return 1;}
static void readback(size_t bytes,const unsigned char *source,unsigned char *out,const char *name)
{
    memset(out,0xa5,bytes+32);glGetBufferSubData(GL_ARRAY_BUFFER,0,(GLsizeiptr)bytes,out+16);
    check(!glGetError()&&!memcmp(out+16,source,bytes),name);
    check(uniform(out,16,0xa5)&&uniform(out+16+bytes,16,0xa5),"buffer readback preserves guards");
}
int main(int argc,char **argv)
{
    int legacy=argc==2&&!strcmp(argv[1],"--legacy");if(argc>1&&!legacy)return 2;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(legacy?kCGLOGLPVersion_Legacy:kCGLOGLPVersion_GL4_Core),0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;CGLDestroyPixelFormat(pf);
    printf("large-buffer renderer=%s version=%s legacy=%d\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),legacy);
    size_t bytes=64u*1024u*1024u+4096;
    unsigned char *source=malloc(bytes),*out=malloc(bytes+32);
    check(source&&out,"large buffer fixture allocation");if(!source||!out)return 2;
    fill(source,bytes,17);GLuint buffer=0;glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    glEnable(0xffffffffu);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)bytes,source,GL_DYNAMIC_DRAW);
    check(glGetError()==GL_INVALID_ENUM&&!glGetError(),"large BufferData preserves prior error");
    GLint size=0,usage=0;glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&size);
    glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_USAGE,&usage);
    check(size==(GLint)bytes&&usage==GL_DYNAMIC_DRAW&&!glGetError(),"large BufferData preserves native size and usage");
    readback(bytes,source,out,"large initial buffer bytes");
    fill(source,bytes,79);glBufferSubData(GL_ARRAY_BUFFER,0,(GLsizeiptr)bytes,source);
    check(!glGetError(),"large buffer update accepted");
    readback(bytes,source,out,"large updated buffer bytes");
    fill(source+16,bytes-32,97);
    glBufferSubData(GL_ARRAY_BUFFER,16,(GLsizeiptr)(bytes-32),source+16);
    check(!glGetError(),"large nonzero-offset update accepted");
    readback(bytes,source,out,"large partial update preserves surrounding bytes");
    memset(out,0xa5,bytes+32);glGetBufferSubData(GL_ARRAY_BUFFER,16,(GLsizeiptr)(bytes-32),out+16);
    check(!glGetError()&&!memcmp(out+16,source+16,bytes-32)&&uniform(out,16,0xa5)&&
        uniform(out+bytes-16,48,0xa5),"large nonzero-offset read preserves guards");
    glBufferSubData(GL_ARRAY_BUFFER,4,(GLsizeiptr)bytes,source);
    check(glGetError()==GL_INVALID_VALUE&&!glGetError(),"large out-of-bounds update rejected");
    readback(bytes,source,out,"failed update leaves entire buffer unchanged");
    memset(out,0xa5,bytes+32);glGetBufferSubData(GL_ARRAY_BUFFER,4,(GLsizeiptr)bytes,out+16);
    check(glGetError()==GL_INVALID_VALUE&&!glGetError()&&uniform(out,bytes+32,0xa5),
        "failed large read leaves entire output unchanged");
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)bytes,source,GL_NONE);
    check(glGetError()==GL_INVALID_ENUM&&!glGetError(),"large invalid usage rejected");
    readback(bytes,source,out,"failed definition leaves entire buffer unchanged");
    void *mapped=glMapBuffer(GL_ARRAY_BUFFER,GL_READ_ONLY);
    check(mapped&&!glGetError(),"large read-only map accepted");
    if(mapped){
        check(!memcmp(mapped,source,bytes),"large mapped read bytes");
        void *queried=NULL;glGetBufferPointerv(GL_ARRAY_BUFFER,GL_BUFFER_MAP_POINTER,&queried);
        check(queried==mapped&&!glGetError(),"large map pointer belongs to guest");
        check(glUnmapBuffer(GL_ARRAY_BUFFER)&&!glGetError(),"large read-only unmap");
    }
    mapped=glMapBuffer(GL_ARRAY_BUFFER,GL_WRITE_ONLY);
    check(mapped&&!glGetError(),"large write-only map accepted");
    if(mapped){
        ((unsigned char *)mapped)[bytes-1]=0x37;source[bytes-1]=0x37;
        check(glUnmapBuffer(GL_ARRAY_BUFFER)&&!glGetError(),"large write-only unmap");
        readback(bytes,source,out,"large mapped write preserves untouched bytes");
    }
    if(!legacy){
        mapped=glMapBufferRange(GL_ARRAY_BUFFER,16,(GLsizeiptr)(bytes-32),
            GL_MAP_READ_BIT|GL_MAP_WRITE_BIT|GL_MAP_FLUSH_EXPLICIT_BIT);
        check(mapped&&!glGetError(),"large nonzero-offset ranged map accepted");
        if(mapped){
            check(!memcmp(mapped,source+16,bytes-32),"large ranged map initial bytes");
            ((unsigned char *)mapped)[0]=0x19;source[16]=0x19;
            ((unsigned char *)mapped)[bytes-33]=0x71;source[bytes-17]=0x71;
            glFlushMappedBufferRange(GL_ARRAY_BUFFER,0,(GLsizeiptr)(bytes-32));
            check(!glGetError(),"large mapped range flush");
            check(glUnmapBuffer(GL_ARRAY_BUFFER)&&!glGetError(),"large ranged unmap");
            readback(bytes,source,out,"large ranged mapping preserves surrounding bytes");
        }
    }
    glBufferData(GL_ARRAY_BUFFER,0,(void *)1,GL_STREAM_DRAW);
    glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&size);
    check(!glGetError()&&size==0,"zero-size BufferData ignores inaccessible input");
    glBindBuffer(GL_ARRAY_BUFFER,0);glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)bytes,source,GL_STATIC_DRAW);
    check(glGetError()==GL_INVALID_OPERATION&&!glGetError(),"large definition without binding rejected");
    glDeleteBuffers(1,&buffer);free(source);free(out);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
    printf("large-buffer failures=%u\n",failures);return failures?1:0;
}
