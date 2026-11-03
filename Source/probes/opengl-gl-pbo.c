/* Public CGL/GL oracle: native Mavericks first, then the isolated PassthroughOpenGLI.
 * All buffers, FBOs and numeric pixel observations belong to this process. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void glGenVertexArraysAPPLE(GLsizei,GLuint *);
extern void glBindVertexArrayAPPLE(GLuint);
extern void glDeleteVertexArraysAPPLE(GLsizei,const GLuint *);
extern void glPushClientAttrib(GLbitfield);
extern void glPopClientAttrib(void);
extern void glBegin(GLenum);
extern void glEnd(void);
#define PIXEL_STORE_BIT 0x00000001
static unsigned failures;
static void check(int good,const char *name)
{ printf("%s %s\n",good?"PASS":"FAIL",name);failures+=!good;fflush(stdout); }
static GLuint shader(GLenum type,const char *source)
{
    GLuint s=glCreateShader(type);GLint good=0;glShaderSource(s,1,&source,NULL);glCompileShader(s);
    glGetShaderiv(s,GL_COMPILE_STATUS,&good);check(good,"PBO sampler shader");return s;
}
static void store(int pack,GLint row,GLint alignment,GLint skiprows,GLint skippixels)
{
    glPixelStorei(pack?GL_PACK_ALIGNMENT:GL_UNPACK_ALIGNMENT,alignment);
    glPixelStorei(pack?GL_PACK_ROW_LENGTH:GL_UNPACK_ROW_LENGTH,row);
    glPixelStorei(pack?GL_PACK_SKIP_ROWS:GL_UNPACK_SKIP_ROWS,skiprows);
    glPixelStorei(pack?GL_PACK_SKIP_PIXELS:GL_UNPACK_SKIP_PIXELS,skippixels);
}
static void sample(GLuint texture,GLuint program,unsigned char result[16])
{
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);store(1,0,1,0,0);
    glBindTexture(GL_TEXTURE_2D,texture);glUseProgram(program);glViewport(0,0,2,2);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);glReadPixels(0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,result);
}
int main(void)
{
    int legacy=getenv("VIM_PBO_LEGACY")!=NULL;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(legacy?kCGLOGLPVersion_Legacy:kCGLOGLPVersion_3_2_Core),0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint count=0;
    CGLError e=CGLChoosePixelFormat(attrs,&pf,&count);if(!e && pf)e=CGLCreateContext(pf,NULL,&c);
    if(pf)CGLDestroyPixelFormat(pf);check(!e && c,"accelerated PBO context");if(!c)return 1;
    CGLSetCurrentContext(c);printf("PBO renderer=%s version=%s pointer-bits=%u\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),(unsigned)(8*sizeof(void *)));
    if(getenv("VIM_PBO_OLD_PEER")) {
        GLuint buffer=0,texture=0,fbo=0;unsigned char source[16]={0},output[16];
        glGenBuffers(1,&buffer);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffer);glBufferData(GL_PIXEL_UNPACK_BUFFER,16,source,GL_STATIC_DRAW);
        glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        check(glGetError()==GL_INVALID_OPERATION,"old peer rejects unpack offsets locally");
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,source);
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
        glBindBuffer(GL_PIXEL_PACK_BUFFER,buffer);glReadPixels(0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        check(glGetError()==GL_INVALID_OPERATION,"old peer rejects pack offsets locally");
        glBindBuffer(GL_PIXEL_PACK_BUFFER,0);memset(output,0xa5,16);glReadPixels(0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,output);
        check(!memcmp(output,source,16) && !glGetError(),"old peer CPU pixels and stream remain valid");
        glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);glDeleteBuffers(1,&buffer);
        CGLSetCurrentContext(NULL);CGLDestroyContext(c);return failures?1:0;
    }
    GLuint buffers[3]={0},textures[2]={0},fbo=0,vao=0;
    unsigned char data[256],actual[256],pixels[16];memset(data,0xa5,sizeof(data));
    const unsigned char expected[16]={255,0,0,255,0,255,0,255,0,0,255,255,255,255,0,255};
    const unsigned positions[4]={44,48,68,72};
    for(int i=0;i<4;++i)memcpy(data+positions[i],expected+i*4,4);
    glGenBuffers(3,buffers);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffers[0]);
    glBufferData(GL_PIXEL_UNPACK_BUFFER,sizeof(data),data,GL_DYNAMIC_DRAW);store(0,6,8,1,1);
    glGenTextures(2,textures);glBindTexture(GL_TEXTURE_2D,textures[0]);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,(void *)16);
    check(!glGetError(),"PBO texture upload with offset, row length and skips");
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);store(0,0,1,0,0);
    glBindTexture(GL_TEXTURE_2D,textures[1]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[1],0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE && !glGetError(),"PBO sampling framebuffer");
    const GLfloat vertices[]={-1,-1,0,0,1,-1,1,0,-1,1,0,1,1,1,1,1};
    GLuint vs=shader(GL_VERTEX_SHADER,legacy?
        "#version 120\nattribute vec2 position;attribute vec2 uv;varying vec2 coord;void main(){coord=uv;gl_Position=vec4(position,0,1);}":
        "#version 150\nin vec2 position;in vec2 uv;out vec2 coord;void main(){coord=uv;gl_Position=vec4(position,0,1);}"),
        fs=shader(GL_FRAGMENT_SHADER,legacy?
        "#version 120\nuniform sampler2D image;varying vec2 coord;void main(){gl_FragColor=texture2D(image,coord);}":
        "#version 150\nuniform sampler2D image;in vec2 coord;out vec4 color;void main(){color=texture(image,coord);}"),program=glCreateProgram();
    glAttachShader(program,vs);glAttachShader(program,fs);glBindAttribLocation(program,0,"position");glBindAttribLocation(program,1,"uv");
    glLinkProgram(program);GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);check(linked,"PBO sampling program");
    glUseProgram(program);glUniform1i(glGetUniformLocation(program,"image"),0);
    if(legacy) { glGenVertexArraysAPPLE(1,&vao);glBindVertexArrayAPPLE(vao); }
    else { glGenVertexArrays(1,&vao);glBindVertexArray(vao); }
    glBindBuffer(GL_ARRAY_BUFFER,buffers[2]);glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,16,NULL);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,16,(void *)8);
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    sample(textures[0],program,pixels);
    printf("PBO sampled %u,%u,%u,%u / %u,%u,%u,%u\n",pixels[0],pixels[1],pixels[2],pixels[3],pixels[12],pixels[13],pixels[14],pixels[15]);
    check(!memcmp(pixels,expected,16) && !glGetError(),"actual GPU shader samples unpacked texels");
    const unsigned char magenta[4]={255,0,255,255};unsigned char updated[16];memcpy(updated,expected,16);memcpy(updated+12,magenta,4);
    const char *sync=getenv("VIM_PBO_SOURCE_SYNC");
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffers[0]);store(0,6,8,1,1);
    if(sync && !strcmp(sync,"finish-before"))glFinish();
    glBufferSubData(GL_PIXEL_UNPACK_BUFFER,44,4,magenta);
    if(sync && !strcmp(sync,"flush"))glFlush();
    if(sync && !strcmp(sync,"finish"))glFinish();
    if(sync && (!strcmp(sync,"gpu-fence") || !strcmp(sync,"cpu-fence"))) {
        GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
        if(!strcmp(sync,"gpu-fence"))glWaitSync(fence,0,GL_TIMEOUT_IGNORED);
        else glClientWaitSync(fence,GL_SYNC_FLUSH_COMMANDS_BIT,GL_TIMEOUT_IGNORED);
        glDeleteSync(fence);
    }
    if(sync && !strcmp(sync,"query")) {
        unsigned char changed[4]={0};glGetBufferSubData(GL_PIXEL_UNPACK_BUFFER,44,4,changed);
        printf("PBO changed source=%u,%u,%u,%u error=%x\n",changed[0],changed[1],changed[2],changed[3],glGetError());
    }
    glTexSubImage2D(GL_TEXTURE_2D,0,1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,(void *)16);
    GLenum suberror=glGetError();sample(textures[0],program,pixels);GLenum drawerror=glGetError();
    printf("PBO subimage errors=%x,%x texels=",suberror,drawerror);for(int i=0;i<16;++i)printf("%u,",pixels[i]);puts("");
    check(!memcmp(pixels,updated,16) && !suberror && !drawerror,"PBO subimage reaches GPU sampler");
    memset(actual,0xa5,sizeof(actual));glBindBuffer(GL_PIXEL_PACK_BUFFER,buffers[1]);
    glBufferData(GL_PIXEL_PACK_BUFFER,sizeof(actual),actual,GL_DYNAMIC_DRAW);store(1,5,8,1,1);
    glReadPixels(0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,(void *)32);
    glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(actual),actual);
    unsigned char packed[256];memset(packed,0xa5,sizeof(packed));const unsigned packedPositions[4]={60,64,84,88};
    /* row_length 5 * RGBA4 aligned to 8 gives a 24-byte stride. */
    for(int i=0;i<4;++i)memcpy(packed+packedPositions[i],updated+i*4,4);
    GLenum packerror=glGetError();printf("PBO pack error=%x texels=",packerror);for(int i=0;i<4;++i)printf("%u,%u,%u,%u / ",actual[packedPositions[i]],actual[packedPositions[i]+1],actual[packedPositions[i]+2],actual[packedPositions[i]+3]);puts("");
    check(!memcmp(actual,packed,sizeof(actual)) && !packerror,"PBO readback preserves all prefix/padding/trailer bytes");
    if(legacy) {
        glBegin(GL_POINTS);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glEnd();
        check(glGetError()==GL_INVALID_OPERATION,"failed bind during Begin/End does not alter PBO interpretation");
        glReadPixels(0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,(void *)32);
        glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(actual),actual);
        check(!memcmp(actual,packed,sizeof(actual)) && !glGetError(),"pack offset still correct after rejected bind");
    }
    void *p=glMapBuffer(GL_PIXEL_PACK_BUFFER,GL_READ_ONLY);check(p && !glGetError(),"pack buffer map");
    if(p) {
        glReadPixels(0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,(void *)32);
        check(glGetError()==GL_INVALID_OPERATION && !memcmp(p,packed,256),"mapped pack buffer rejects readback without mutation");
        check(glUnmapBuffer(GL_PIXEL_PACK_BUFFER) && !glGetError(),"pack buffer unmap");
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffers[0]);p=glMapBuffer(GL_PIXEL_UNPACK_BUFFER,GL_READ_ONLY);
    check(p && !glGetError(),"unpack buffer map");if(p) {
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,(void *)16);
        check(glGetError()==GL_INVALID_OPERATION,"mapped unpack rejects texture update");glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    }
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,(void *)240);
    check(glGetError()==GL_INVALID_OPERATION,"out of bounds unpack rejects before mutation");
    sample(textures[0],program,pixels);check(!memcmp(pixels,updated,16) && !glGetError(),"rejected PBO updates preserve GPU texture");
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffers[0]);store(0,0,1,0,0);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    sample(textures[0],program,pixels);int gray=1;for(int i=0;i<16;++i)gray&=pixels[i]==0xa5;
    check(gray && !glGetError(),"NULL is PBO offset zero, not an empty allocation");
    if(legacy) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER,buffers[1]);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffers[0]);store(0,6,8,1,1);
        glPushClientAttrib(PIXEL_STORE_BIT);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);store(0,0,1,0,0);
        glPopClientAttrib();GLint pack=0,unpack=0,row=0;
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);
        check(pack==(GLint)buffers[1] && unpack==(GLint)buffers[0] && row==6 && !glGetError(),"client pixel-store stack restores native bindings and layout");
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,(void *)16);
        sample(textures[0],program,pixels);check(!memcmp(pixels,magenta,4) && !glGetError(),"restored unpack binding is interpreted as offset");
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffers[0]);glPushClientAttrib(PIXEL_STORE_BIT);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glDeleteBuffers(1,&buffers[0]);glPopClientAttrib();
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);GLboolean exists=glIsBuffer(buffers[0]);GLenum err=glGetError();
        printf("PBO deleted-stack restored=%d original=%u exists=%u error=%x\n",unpack,buffers[0],exists,err);
        check(!err,"native deleted-stack restoration observation");
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER,buffers[1]);glDeleteBuffers(1,&buffers[1]);store(1,0,1,0,0);
    memset(pixels,0,16);glReadPixels(0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    check(pixels[3]==255 || pixels[3]==0xa5,"deleting bound pack buffer restores CPU pointer interpretation");check(!glGetError(),"PBO stream still valid");
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    glDeleteBuffers(3,buffers);glDeleteFramebuffers(1,&fbo);glDeleteTextures(2,textures);
    if(legacy)glDeleteVertexArraysAPPLE(1,&vao);else glDeleteVertexArrays(1,&vao);
    glUseProgram(0);glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);
    check(!glGetError(),"PBO cleanup");CGLSetCurrentContext(NULL);CGLDestroyContext(c);
    printf("PBO failures=%u\n",failures);return failures?1:0;
}
