/* Owned native public-CGL fixture, identical on physical Mavericks and guest.
 * Tests per-vertex/per-instance ranges, EBOs, nonzero first and VAO divisors. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void glGenVertexArraysAPPLE(GLsizei,GLuint *);
extern void glBindVertexArrayAPPLE(GLuint);
extern void glDeleteVertexArraysAPPLE(GLsizei,const GLuint *);
extern void glVertexAttribPointerARB(GLuint,GLint,GLenum,GLboolean,GLsizei,const void *);
extern void glGetVertexAttribivARB(GLuint,GLenum,GLint *);
extern void glGetVertexAttribfvARB(GLuint,GLenum,GLfloat *);
extern void glGetVertexAttribdvARB(GLuint,GLenum,GLdouble *);
static unsigned failures;
static void check(int good,const char *name)
{ printf("%s %s\n",good?"PASS":"FAIL",name);failures+=!good;fflush(stdout); }
static GLuint shader(GLenum kind,const char *source)
{
    GLuint s=glCreateShader(kind);GLint good=0;glShaderSource(s,1,&source,NULL);glCompileShader(s);
    glGetShaderiv(s,GL_COMPILE_STATUS,&good);check(good,"instanced GLSL compile");return s;
}
static void pixels(const char *name,int rows)
{
    unsigned char left[4]={0},right[4]={0};int good=1;
    for(int row=0;row<rows;++row) {
        int y=rows==1?64:(row?86:42);
        glReadPixels(32,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,left);
        glReadPixels(96,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,right);
        GLenum error=glGetError();
        printf("INSTANCE %s row=%d left=%u,%u,%u,%u right=%u,%u,%u,%u error=%x\n",name,row,
            left[0],left[1],left[2],left[3],right[0],right[1],right[2],right[3],error);
        good&=left[0]==255 && !left[1] && !left[2] && left[3]==255 &&
            !right[0] && right[1]==255 && !right[2] && right[3]==255 && !error;
    }
    check(good,name);
}
static void attributeQueries(int legacy)
{
    GLuint vao=0,buffer=0;
    if(legacy) { glGenVertexArraysAPPLE(1,&vao);glBindVertexArrayAPPLE(vao); }
    else { glGenVertexArrays(1,&vao);glBindVertexArray(vao); }
    GLint defaultStride=-1,defaultSize=-1,defaultType=-1;
    glGetVertexAttribiv(1,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&defaultStride);
    glGetVertexAttribiv(1,GL_VERTEX_ATTRIB_ARRAY_SIZE,&defaultSize);
    glGetVertexAttribiv(1,GL_VERTEX_ATTRIB_ARRAY_TYPE,&defaultType);
    printf("ATTRIBUTE defaults profile=%s size=%d type=%x stride=%d error=%x\n",
        legacy?"legacy":"core",defaultSize,defaultType,defaultStride,glGetError());
    check(defaultSize==4 && defaultType==GL_FLOAT && !defaultStride,"fresh VAO attribute defaults");
    glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,4096,NULL,GL_STATIC_DRAW);
    const GLenum types[]={GL_BYTE,GL_UNSIGNED_BYTE,GL_SHORT,GL_UNSIGNED_SHORT,GL_INT,GL_UNSIGNED_INT,GL_FLOAT,GL_DOUBLE,GL_HALF_FLOAT};
    const GLint widths[]={1,1,2,2,4,4,4,8,2};
    for(unsigned t=0;t<sizeof(types)/sizeof(types[0]);++t)for(GLint size=2;size<=4;++size)for(int explicitStride=0;explicitStride<2;++explicitStride) {
        GLsizei stride=explicitStride?32:0;GLint integer=-1,normalized=-1;GLfloat single=-1;GLdouble real=-1;
        if(legacy) {
            glVertexAttribPointerARB(1,size,types[t],GL_TRUE,stride,NULL);
            glGetVertexAttribivARB(1,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&integer);
            glGetVertexAttribfvARB(1,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&single);
            glGetVertexAttribdvARB(1,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&real);
            glGetVertexAttribivARB(1,GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,&normalized);
        } else {
            glVertexAttribPointer(1,size,types[t],GL_TRUE,stride,NULL);
            glGetVertexAttribiv(1,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&integer);
            glGetVertexAttribfv(1,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&single);
            glGetVertexAttribdv(1,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&real);
            glGetVertexAttribiv(1,GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,&normalized);
        }
        GLenum error=glGetError();
        printf("ATTRIBUTE profile=%s type=%x size=%d input-stride=%d integer=%d float=%g double=%g normalized=%d error=%x\n",
            legacy?"legacy":"core",types[t],size,stride,integer,(double)single,real,normalized,error);
        GLint expectedStride=stride?stride:size*widths[t];GLint expectedNormalized=t<6;
        check(!error && integer==single && integer==real && integer==expectedStride && normalized==expectedNormalized,
            "typed native stride/normalization query agreement");
    }
    glDeleteBuffers(1,&buffer);
    if(legacy)glDeleteVertexArraysAPPLE(1,&vao);else glDeleteVertexArrays(1,&vao);
}
int main(void)
{
    const char *query=getenv("VIM_ATTRIBUTE_QUERY");int legacy=query && !strcmp(query,"legacy");
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(legacy?kCGLOGLPVersion_Legacy:kCGLOGLPVersion_3_2_Core),kCGLPFAColorSize,24,kCGLPFAAlphaSize,8,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint count=0;
    CGLError e=CGLChoosePixelFormat(attrs,&pf,&count);
    if(!e && pf)e=CGLCreateContext(pf,NULL,&c);if(pf)CGLDestroyPixelFormat(pf);
    check(!e && c,"native accelerated context");if(!c)return 1;CGLSetCurrentContext(c);
    printf("INSTANCE renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    if(query) { attributeQueries(legacy);CGLSetCurrentContext(NULL);CGLDestroyContext(c);return failures?1:0; }
    GLuint texture=0,fbo=0,vao=0,other=0,position=0,offset=0,ebo=0;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,128,128,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"instanced framebuffer");
    if(getenv("VIM_EXPECT_NO_INSTANCING")) {
        // Protocol compatibility test against an older core-capable executor.
        // Rejected new calls must not send an unknown opcode/kill its stream.
        glVertexAttribDivisor(1,1);check(glGetError()==GL_INVALID_OPERATION,"old executor rejects divisor locally");
        glDrawArraysInstanced(GL_TRIANGLES,0,3,1);check(glGetError()==GL_INVALID_OPERATION,"old executor rejects instancing locally");
        glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);unsigned char p[4]={0};
        glReadPixels(64,64,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
        check(p[0]==255 && !p[1] && !p[2] && p[3]==255 && !glGetError(),"old executor connection remains usable");
        glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);
        CGLSetCurrentContext(NULL);CGLDestroyContext(c);return failures?1:0;
    }
    const char *vs="#version 150\nin vec2 position;in vec2 offset;uniform int paired;flat out int side;"
        "void main(){int d=paired!=0?2:1;side=gl_InstanceID/d;"
        "float y=paired!=0?((gl_InstanceID%2)==0?-.35:.35):0;"
        "gl_Position=vec4(position+offset+vec2(0,y),0,1);}";
    const char *fs="#version 150\nflat in int side;out vec4 color;"
        "void main(){color=side==0?vec4(1,0,0,1):vec4(0,1,0,1);}";
    GLuint vertex=shader(GL_VERTEX_SHADER,vs),fragment=shader(GL_FRAGMENT_SHADER,fs),program=glCreateProgram();
    glAttachShader(program,vertex);glAttachShader(program,fragment);
    glBindAttribLocation(program,0,"position");glBindAttribLocation(program,1,"offset");glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);check(linked,"instanced program link");
    glUseProgram(program);GLint paired=glGetUniformLocation(program,"paired");
    const GLfloat points[]={0,0,0,0,0,0, -.30,-.23,.30,-.23,0,.23};
    const GLfloat shifts[]={-.5,0,.5,0};const GLushort indices[]={3,4,5};
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenBuffers(1,&position);glBindBuffer(GL_ARRAY_BUFFER,position);glBufferData(GL_ARRAY_BUFFER,sizeof(points),points,GL_STATIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL);glEnableVertexAttribArray(0);
    glGenBuffers(1,&offset);glBindBuffer(GL_ARRAY_BUFFER,offset);glBufferData(GL_ARRAY_BUFFER,sizeof(shifts),shifts,GL_STATIC_DRAW);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,0,NULL);glEnableVertexAttribArray(1);glVertexAttribDivisor(1,1);
    GLint arraySize=0,enabled=0,binding=0;GLdouble stride=-1;GLfloat normalized=-1;
    glGetVertexAttribiv(1,GL_VERTEX_ATTRIB_ARRAY_SIZE,&arraySize);
    glGetVertexAttribiv(1,GL_VERTEX_ATTRIB_ARRAY_ENABLED,&enabled);
    glGetVertexAttribiv(1,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&binding);
    glGetVertexAttribdv(1,GL_VERTEX_ATTRIB_ARRAY_STRIDE,&stride);
    glGetVertexAttribfv(1,GL_VERTEX_ATTRIB_ARRAY_NORMALIZED,&normalized);
    GLenum stateError=glGetError();
    printf("INSTANCE state size=%d enabled=%d buffer=%u expected=%u stride=%g normalized=%g error=%x\n",
        arraySize,enabled,(GLuint)binding,offset,stride,(double)normalized,stateError);
    check(arraySize==2 && enabled==GL_TRUE && (GLuint)binding==offset && stride==2*sizeof(GLfloat) && normalized==0 && !stateError,
        "VAO queries observe setters before any draw");
    struct { GLfloat value[4];GLuint canary; } current={{0},0x1234abcd};
    glVertexAttrib4f(1,.25,.5,.75,1);glGetVertexAttribfv(1,GL_CURRENT_VERTEX_ATTRIB,current.value);
    check(current.value[0]==.25 && current.value[1]==.5 && current.value[2]==.75 && current.value[3]==1 &&
        current.canary==0x1234abcd && !glGetError(),"typed four-component current attribute query");
    glGenBuffers(1,&ebo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    glViewport(0,0,128,128);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glDrawArraysInstanced(GL_TRIANGLES,3,3,2);pixels("nonzero first, two instances",1);
    glClear(GL_COLOR_BUFFER_BIT);glDrawElementsInstanced(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,NULL,2);
    pixels("EBO, independent instance bounds",1);
    glVertexAttribDivisor(1,2);glUniform1i(paired,1);glClear(GL_COLOR_BUFFER_BIT);
    glDrawElementsInstanced(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,NULL,4);pixels("divisor two, four instances",2);
    GLint divisor=-1;glGetVertexAttribiv(1,GL_VERTEX_ATTRIB_ARRAY_DIVISOR,&divisor);
    check(divisor==2 && !glGetError(),"typed divisor state query");
    glGenVertexArrays(1,&other);glBindVertexArray(other);divisor=-1;
    glGetVertexAttribiv(1,GL_VERTEX_ATTRIB_ARRAY_DIVISOR,&divisor);check(divisor==0 && !glGetError(),"new VAO starts with zero divisor");
    glBindVertexArray(vao);divisor=-1;glGetVertexAttribiv(1,GL_VERTEX_ATTRIB_ARRAY_DIVISOR,&divisor);
    check(divisor==2 && !glGetError(),"VAO switch preserves divisor");
    glClear(GL_COLOR_BUFFER_BIT);glDrawArraysInstanced(GL_TRIANGLES,3,3,4);pixels("VAO restored instancing",2);
    glDrawArraysInstanced(GL_TRIANGLES,0,3,-1);check(glGetError()==GL_INVALID_VALUE,"negative array instance count rejected");
    glDrawElementsInstanced(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,NULL,-1);check(glGetError()==GL_INVALID_VALUE,"negative element instance count rejected");
    GLint limit=0;glGetIntegerv(GL_MAX_VERTEX_ATTRIBS,&limit);glVertexAttribDivisor((GLuint)limit,1);
    check(glGetError()==GL_INVALID_VALUE,"invalid divisor attribute rejected");
    glBindVertexArray(~(GLuint)0);check(glGetError()==GL_INVALID_OPERATION,"ungenerated VAO bind rejected");
    GLint bound=0;glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&bound);
    check((GLuint)bound==vao && !glGetError(),"rejected bind preserves VAO state");
    // A non-instanced draw still uses element zero of a divided attribute.
    glUniform1i(paired,0);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,3,3);
    unsigned char one[4]={0};glReadPixels(32,64,1,1,GL_RGBA,GL_UNSIGNED_BYTE,one);
    check(one[0]==255 && !one[1] && !one[2] && one[3]==255 && !glGetError(),"ordinary draw honors divided attribute element zero");
    glDrawArraysInstanced(GL_TRIANGLES,0,3,0);check(!glGetError(),"zero instances leave no error");
    glDeleteBuffers(1,&ebo);glDeleteBuffers(1,&position);glDeleteBuffers(1,&offset);
    glDeleteVertexArrays(1,&other);glDeleteVertexArrays(1,&vao);glDeleteProgram(program);
    glDeleteShader(vertex);glDeleteShader(fragment);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
    printf("instancing failures=%u\n",failures);return failures?1:0;
}
