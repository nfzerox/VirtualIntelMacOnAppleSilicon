/* Native Intel / guest volume-texture contract. Each slice is sampled by
 * the GPU, not inferred from allocation metadata. Pixel-store prefixes end
 * at a guard page; PBO storage ends at the last actually addressed texel. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
/* Exact core declarations from the target SDK's gl3.h; that header excludes
 * gl.h, while this fixture intentionally tests both profiles in one binary. */
extern void glGenVertexArrays(GLsizei,GLuint *);
extern void glBindVertexArray(GLuint);
extern void glDeleteVertexArrays(GLsizei,const GLuint *);
#ifndef VIM_TEST_IMAGE_HEIGHT
#define VIM_TEST_IMAGE_HEIGHT 3
#endif
enum {W=5,H=3,D=3,ROW=32,IMAGE=ROW*VIM_TEST_IMAGE_HEIGHT,PREFIX=2*IMAGE+ROW+8,
      SPAN=PREFIX+(D-1)*IMAGE+(H-1)*ROW+W*4};
static int failures;
static GLuint program;
static void check(int good,const char *name)
{fprintf(stderr,"%s %s\n",good?"PASS":"FAIL",name);failures+=!good;}
static void verify(const unsigned char *expected,const char *name)
{
    unsigned different=0;unsigned char result[W*H*4];
    glViewport(0,0,W,H);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);
    if(!program) {
        glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
        glEnable(GL_TEXTURE_3D);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    }
    for(unsigned z=0;z<D;++z) {
        GLfloat r=(z+.5f)/D;
        if(program) { glUniform1f(glGetUniformLocation(program,"volumeZ"),r);glDrawArrays(GL_TRIANGLES,0,3); }
        else {
            glBegin(GL_QUADS);glTexCoord3f(0,0,r);glVertex2f(-1,-1);glTexCoord3f(1,0,r);glVertex2f(1,-1);
            glTexCoord3f(1,1,r);glVertex2f(1,1);glTexCoord3f(0,1,r);glVertex2f(-1,1);glEnd();
        }
        glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,result);
        if(getenv("VIM_3D_DETAILS"))for(unsigned y=0;y<H;++y) {
            fprintf(stderr,"slice %u row %u:",z,y);
            for(unsigned x=0;x<W;++x)fprintf(stderr," %u/%u",result[(y*W+x)*4],expected[((z*H+y)*W+x)*4]);
            fputc('\n',stderr);
        }
        for(unsigned i=0;i<sizeof(result);++i)different+=result[i]!=expected[z*sizeof(result)+i];
    }
    GLenum error=glGetError();fprintf(stderr,"%s mismatched=%u/%u error=%x\n",name,different,W*H*D*4,error);
    check(!different&&!error,name);if(!program)glDisable(GL_TEXTURE_3D);
}
static void pattern(unsigned char *p,unsigned char *expected,unsigned seed)
{
    memset(p,0xa5,SPAN);
    for(unsigned z=0;z<D;++z)for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)for(unsigned c=0;c<4;++c)
        p[PREFIX+z*IMAGE+y*ROW+x*4+c]=
            c==3 ? 255 : (seed+z*59+y*31+x*17+c*71)%251;
    for(unsigned z=0;z<D;++z)for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)for(unsigned c=0;c<4;++c)
        expected[((z*H+y)*W+x)*4+c]=p[PREFIX+z*IMAGE+y*ROW+x*4+c];
}
static GLuint shader(GLenum type,const char *source)
{
    GLuint name=glCreateShader(type);glShaderSource(name,1,&source,NULL);glCompileShader(name);
    GLint good=0;glGetShaderiv(name,GL_COMPILE_STATUS,&good);check(good,"volume shader compiled");return name;
}
int main(void)
{
    int core=getenv("VIM_3D_CORE")!=NULL;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)(core?kCGLOGLPVersion_3_2_Core:kCGLOGLPVersion_Legacy),0};
    CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf||CGLCreateContext(pf,NULL,&ctx)||CGLSetCurrentContext(ctx))return 2;
    const char *renderer=(const char *)glGetString(GL_RENDERER);fprintf(stderr,"renderer=%s\n",renderer?renderer:"none");
    if(!renderer||!strstr(renderer,"Intel"))return 2;
    if(getenv("VIM_3D_EXPECT_UNSUPPORTED")) {
        glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA8,W,H,D,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        check(glGetError()==GL_INVALID_OPERATION,"old peer declines new volume operation locally");
        glFinish();check(glGetError()==GL_NO_ERROR,"old peer connection remains usable");
        CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);CGLDestroyPixelFormat(pf);return !!failures;
    }
    GLuint vao=0;
    if(core) {
        GLuint vs=shader(GL_VERTEX_SHADER,"#version 150\nout vec2 uv;void main(){vec2 p=gl_VertexID==0?vec2(-1,-1):gl_VertexID==1?vec2(3,-1):vec2(-1,3);uv=(p+1.)*.5;gl_Position=vec4(p,0,1);}");
        GLuint fs=shader(GL_FRAGMENT_SHADER,"#version 150\nin vec2 uv;out vec4 color;uniform sampler3D volume;uniform float volumeZ;void main(){color=texture(volume,vec3(uv,volumeZ));}");
        program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
        GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);check(linked,"volume shader linked");
        glDeleteShader(vs);glDeleteShader(fs);glUseProgram(program);
        glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    }
    GLuint destination=0,fbo=0,texture=0,pbo=0;
    glGenTextures(1,&destination);glBindTexture(GL_TEXTURE_2D,destination);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,destination,0);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"slice sampling FBO");
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_3D,texture);
    glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_WRAP_R,GL_CLAMP_TO_EDGE);
    size_t page=getpagesize();if(SPAN>page)return 2;
    unsigned char *allocation=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(allocation==MAP_FAILED||mprotect(allocation+page,page,PROT_NONE))return 2;
    unsigned char *pixels=allocation+page-SPAN,expected[W*H*D*4];pattern(pixels,expected,7);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,8);glPixelStorei(GL_UNPACK_ALIGNMENT,8);
    glPixelStorei(GL_UNPACK_ROW_BYTES_APPLE,getenv("VIM_3D_APPLE_PITCH")?ROW:0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,1);glPixelStorei(GL_UNPACK_SKIP_PIXELS,2);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,VIM_TEST_IMAGE_HEIGHT);glPixelStorei(GL_UNPACK_SKIP_IMAGES,2);
    glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA8,W,H,D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    check(glGetError()==GL_NO_ERROR,"CPU volume allocation with native image/row/pixel skips");verify(expected,"CPU volume texels");
    if(getenv("VIM_3D_NATIVE_GET_IMAGE")) {
        unsigned char readback[W*H*D*4];memset(readback,0x7d,sizeof(readback));
        glGetTexImage(GL_TEXTURE_3D,0,GL_RGBA,GL_UNSIGNED_BYTE,readback);
        for(unsigned z=0;z<D;++z)for(unsigned y=0;y<H;++y) {
            fprintf(stderr,"native get slice %u row %u:",z,y);
            for(unsigned x=0;x<W;++x)fprintf(stderr," %u",readback[((z*H+y)*W+x)*4]);
            fputc('\n',stderr);
        }
    }
    /* Pixel-store state is client state and must survive push/pop. */
    if(!core) {
        glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,7);glPixelStorei(GL_UNPACK_SKIP_IMAGES,4);glPopClientAttrib();
    }
    GLint value=-1;glGetIntegerv(GL_UNPACK_IMAGE_HEIGHT,&value);check(value==VIM_TEST_IMAGE_HEIGHT,"image height restored");
    glGetIntegerv(GL_UNPACK_SKIP_IMAGES,&value);check(value==2,"image skips restored");
    pattern(pixels,expected,29);
    glTexSubImage3D(GL_TEXTURE_3D,0,0,0,0,W,H,D,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    check(glGetError()==GL_NO_ERROR,"CPU volume subimage after client-state pop");verify(expected,"CPU subimage texels");
    unsigned char partial[W*H*D*4];memcpy(partial,expected,sizeof(partial));
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)for(unsigned c=0;c<4;++c)
        partial[(((1*H+1+y)*W+1+x)*4)+c]=pixels[PREFIX+y*ROW+x*4+c];
    glTexSubImage3D(GL_TEXTURE_3D,0,1,1,1,2,2,1,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    check(glGetError()==GL_NO_ERROR,"offset partial volume subimage");verify(partial,"partial subimage preserves other texels");
    glTexSubImage3D(GL_TEXTURE_3D,0,0,0,-1,W,H,D,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    check(glGetError()==GL_INVALID_VALUE,"negative volume destination rejected atomically");verify(partial,"negative volume update preserves all slices");
    glTexSubImage3D(GL_TEXTURE_3D,0,0,0,D-1,W,H,D,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    check(glGetError()==GL_INVALID_VALUE,"overflowing volume destination rejected atomically");verify(partial,"overflowing volume update preserves all slices");
    if(!getenv("VIM_3D_CPU_ONLY")) {
    unsigned char buffer[16+SPAN];pattern(buffer+16,expected,53);memset(buffer,0x7d,16);
    glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER,sizeof(buffer),buffer,GL_STREAM_DRAW);
    glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA8,W,H,D,0,GL_RGBA,GL_UNSIGNED_BYTE,(void *)16);
    check(glGetError()==GL_NO_ERROR,"PBO volume allocation uses offset");verify(expected,"PBO volume texels");
    pattern(buffer+16,expected,71);glBufferSubData(GL_PIXEL_UNPACK_BUFFER,16,SPAN,buffer+16);
    glTexSubImage3D(GL_TEXTURE_3D,0,0,0,0,W,H,D,GL_RGBA,GL_UNSIGNED_BYTE,(void *)16);
    check(glGetError()==GL_NO_ERROR,"PBO volume subimage after mutation");verify(expected,"PBO subimage texels");
    glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA8,W,H,D,0,GL_RGBA,GL_UNSIGNED_BYTE,(void *)17);
    check(glGetError()==GL_INVALID_OPERATION,"one-byte-short PBO volume rejected");
    glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA8,W,H,-1,0,GL_RGBA,GL_UNSIGNED_BYTE,(void *)16);
    check(glGetError()==GL_INVALID_VALUE,"negative PBO depth rejected");
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    }
    glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA8,W,H,-1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    check(glGetError()==GL_INVALID_VALUE,"negative CPU depth rejected");
    glTexSubImage3D(GL_TEXTURE_3D,0,0,0,0,W,0,D,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    check(glGetError()==GL_NO_ERROR,"zero-height null subimage accepted");
    glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA8,W,H,D,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    check(glGetError()==GL_NO_ERROR,"null CPU pointer allocates volume storage");
    /* A proxy request must not read a deliberately inaccessible source. */
    glTexImage3D(GL_PROXY_TEXTURE_3D,0,GL_RGBA8,W,H,D,0,GL_RGBA,GL_UNSIGNED_BYTE,allocation+page);
    check(glGetError()==GL_NO_ERROR,"proxy request ignores inaccessible client memory");
    if(core) { glUseProgram(0);glDeleteProgram(program);glBindVertexArray(0);glDeleteVertexArrays(1,&vao); }
    glDeleteBuffers(1,&pbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
    glDeleteTextures(1,&destination);glDeleteTextures(1,&texture);munmap(allocation,page*2);
    CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);CGLDestroyPixelFormat(pf);
    fprintf(stderr,"texture3d failures=%d\n",failures);return !!failures;
}
