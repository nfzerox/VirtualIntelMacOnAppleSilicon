/* Real pixel tests for pointer ownership, client arrays, VBOs and EBOs. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
static int failures;
static void pixelWithin(const char *name,unsigned r,unsigned g,unsigned b,int tolerance)
{
    unsigned char p[4]={0};glReadPixels(32,24,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    GLenum error=glGetError();int good=abs((int)p[0]-(int)r)<=tolerance &&
        abs((int)p[1]-(int)g)<=tolerance && abs((int)p[2]-(int)b)<=tolerance && !error;
    printf("%s %s pixel=%u,%u,%u,%u error=%x\n",good?"PASS":"FAIL",name,p[0],p[1],p[2],p[3],error);
    if(!good) ++failures;
}
static void pixel(const char *name,unsigned r,unsigned g,unsigned b)
{ pixelWithin(name,r,g,b,0); }
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n) || CGLCreateContext(pf,NULL,&c) || CGLSetCurrentContext(c)) return 2;
    GLint render_mode[2]={0,0x13572468};
    glGetIntegerv(GL_RENDER_MODE,render_mode);
    GLenum mode_error=glGetError();
    int mode_good=render_mode[0]==GL_RENDER && render_mode[1]==0x13572468 && !mode_error;
    printf("%s scalar render-mode query mode=%x canary=%x error=%x\n",
        mode_good?"PASS":"FAIL",render_mode[0],render_mode[1],mode_error);
    if(!mode_good) ++failures;
    GLuint tex=0,fbo=0;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,tex,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT) return 3;
    glViewport(0,0,64,64);
    GLfloat positions[]={-1,-1,1,-1,0,1},colors[]={1,0,0,1,0,0,1,0,0};
    glVertexPointer(2,GL_FLOAT,0,positions);glColorPointer(3,GL_FLOAT,0,colors);
    glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_COLOR_ARRAY);
    glDrawArrays(GL_TRIANGLES,0,3);pixel("client arrays",255,0,0);
    for(unsigned i=0;i<9;i+=3) { colors[i]=0;colors[i+1]=1; }
    glDrawArrays(GL_TRIANGLES,0,3);pixel("client data modified after Pointer",0,255,0);
    GLushort indices[]={0,1,2};glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,indices);
    pixel("client index array",0,255,0);
    GLfloat interleaved[]={-1,-1,0,0,1,1,-1,0,0,1,0,1,0,0,1};
    GLuint vbo=0,ebo=0;glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(interleaved),interleaved,GL_DYNAMIC_DRAW);
    glVertexPointer(2,GL_FLOAT,5*sizeof(GLfloat),(const void *)0);
    glColorPointer(3,GL_FLOAT,5*sizeof(GLfloat),(const void *)(2*sizeof(GLfloat)));
    glDrawArrays(GL_TRIANGLES,0,3);pixel("interleaved VBO offsets",0,0,255);
    glGenBuffers(1,&ebo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,(const void *)0);
    pixel("EBO offsets",0,0,255);
    for(unsigned i=0;i<15;i+=5) interleaved[i+3]=1;
    glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(interleaved),interleaved);
    glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,(const void *)0);
    pixel("updated VBO with EBO",0,255,255);
    glBufferSubData(GL_ARRAY_BUFFER,(GLintptr)-4,sizeof(GLfloat),interleaved);
    GLenum offset_error=glGetError();
    printf("%s negative pointer-sized VBO offset error=%x\n",
        offset_error==GL_INVALID_VALUE?"PASS":"FAIL",offset_error);
    if(offset_error!=GL_INVALID_VALUE) ++failures;
    if(!getenv("VIM_GL_SKIP_RANGE_REJECT")) {
        glDrawArrays(GL_TRIANGLES,0,1000);
        GLenum error=glGetError();printf("%s out-of-range VBO rejected error=%x\n",error==GL_INVALID_OPERATION?"PASS":"FAIL",error);
        if(error!=GL_INVALID_OPERATION) ++failures;
    }
    glDrawArrays(GL_TRIANGLES,0,3);pixel("connection remains usable",0,255,255);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);glBindBuffer(GL_ARRAY_BUFFER,0);
    glDisableClientState(GL_VERTEX_ARRAY);glDisableClientState(GL_COLOR_ARRAY);
    const char *vs_text="#version 120\nattribute vec2 position;uniform mat3x4 transform;void main(){gl_Position=transform*vec3(position,1.0);}";
    const char *fs_text="#version 120\nuniform vec3 tint;void main(){gl_FragColor=vec4(tint,1.0);}";
    GLuint vs=glCreateShader(GL_VERTEX_SHADER),fs=glCreateShader(GL_FRAGMENT_SHADER),program=glCreateProgram();
    /* Both the physical Mavericks NVIDIA reference and the native M4 legacy
     * renderer require the position/attribute-zero array to trigger draws. */
    GLuint attribute=getenv("VIM_GL_ATTRIB_FIVE") ? 5 : 0;
    glShaderSource(vs,1,&vs_text,NULL);glCompileShader(vs);
    glShaderSource(fs,1,&fs_text,NULL);glCompileShader(fs);
    glAttachShader(program,vs);glAttachShader(program,fs);
    glBindAttribLocationARB((GLhandleARB)(uintptr_t)program,attribute,"position");glLinkProgram(program);
    GLint linked=0;glGetObjectParameterivARB((GLhandleARB)(uintptr_t)program,GL_OBJECT_LINK_STATUS_ARB,&linked);
    if(!linked) { puts("FAIL ARB shader link");++failures; }
    glUseProgram(program);
    GLint tint=glGetUniformLocationARB((GLhandleARB)(uintptr_t)program,"tint");
    GLint transform=glGetUniformLocationARB((GLhandleARB)(uintptr_t)program,"transform");
    printf("program=%u attrib=%d tint=%d transform=%d\n",program,glGetAttribLocationARB((GLhandleARB)(uintptr_t)program,"position"),tint,transform);
    GLfloat tint_value[]={1,0.5,0},matrix[]={1,0,0,0,0,1,0,0,0,0,0,1};
    glUniform3fvARB(tint,1,tint_value);glUniformMatrix3x4fv(transform,1,GL_FALSE,matrix);
    glEnableVertexAttribArrayARB(attribute);glVertexAttribPointerARB(attribute,2,GL_FLOAT,GL_FALSE,0,positions);
    // Native 13F1911 NVIDIA quantizes the half-intensity shader result to
    // 127; the M4 host produces 128. Permit one UNORM8 step, not GL errors.
    glDrawArrays(GL_TRIANGLES,0,3);pixelWithin("generic attributes + ARB vector/non-square matrix uniforms",255,128,0,1);
    glDisableVertexAttribArrayARB(attribute);glUseProgram(0);glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);glBindBuffer(GL_ARRAY_BUFFER,0);
    glDeleteBuffers(1,&ebo);glDeleteBuffers(1,&vbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&tex);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);CGLDestroyPixelFormat(pf);
    return failures ? 1 : 0;
}
