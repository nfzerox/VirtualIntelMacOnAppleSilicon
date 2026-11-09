/* Isolate FP64 fragment execution from integer framebuffer transfer and CTS
 * cleanup. Each process uses one public CGL context and one shader variant. */
#include <OpenGL/gl3.h>
#include <OpenGL/OpenGL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opengl-reference-renderer.h"

static GLuint compile(GLenum type,const char *source)
{
    GLuint shader=glCreateShader(type);GLint ok=0;
    glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
    glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[2048]={0};glGetShaderInfoLog(shader,sizeof(log),NULL,log);
        fprintf(stderr,"compile %x: %s\n",type,log);exit(2);}
    return shader;
}

int main(int argc,char **argv)
{
    int fp64=0,integer=0,limit=0,constant=0,intel=0,recovery=0,software=0,switch_software=0;
    GLenum primitive=GL_TRIANGLE_FAN;
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--double"))fp64=1;
        else if(!strcmp(argv[i],"--integer"))integer=1;
        else if(!strcmp(argv[i],"--limit"))limit=1;
        else if(!strcmp(argv[i],"--constant"))constant=1;
        else if(!strcmp(argv[i],"--triangles"))primitive=GL_TRIANGLES;
        else if(!strcmp(argv[i],"--points"))primitive=GL_POINTS;
        else if(!strcmp(argv[i],"--intel"))intel=1;
        else if(!strcmp(argv[i],"--allow-recovery"))recovery=1;
        else if(!strcmp(argv[i],"--software"))software=1;
        else if(!strcmp(argv[i],"--switch-software"))switch_software=1;
        else return 64;
    }
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)0x4100,0,0,0,0,0};
    if((software&&intel)||(switch_software&&(software||intel)))return 64;
    if(recovery)attrs[1]=kCGLPFAAllowOfflineRenderers;
    if(software){attrs[0]=kCGLPFARendererID;attrs[1]=(CGLPixelFormatAttribute)kCGLRendererGenericFloatID;}
    if(switch_software){attrs[0]=kCGLPFAAllRenderers;attrs[1]=kCGLPFAAllowOfflineRenderers;}
    if(intel){
        GLint renderer=VIMReferenceIntelHD5000();
        if(!renderer){puts("UNSUPPORTED: no accelerated Intel Haswell renderer");return 77;}
        attrs[4]=kCGLPFAAllowOfflineRenderers;attrs[5]=kCGLPFASupportsAutomaticGraphicsSwitching;
        attrs[6]=kCGLPFARendererID;attrs[7]=(CGLPixelFormatAttribute)renderer;
    }
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint available=0;
    if(CGLChoosePixelFormat(attrs,&pf,&available)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&context)||CGLSetCurrentContext(context))return 2;
    GLint hardware=-1,soft=-1;
    if(switch_software){
        for(GLint i=0;i<available;++i){
            GLint accelerated=0,id=0;
            if(CGLDescribePixelFormat(pf,i,kCGLPFAAccelerated,&accelerated)||
               CGLDescribePixelFormat(pf,i,kCGLPFARendererID,&id))return 2;
            printf("screen=%d accelerated=%d renderer-id=%x\n",i,accelerated,id);
            if(accelerated&&hardware<0)hardware=i;
            else if(!accelerated&&soft<0)soft=i;
        }
        if(hardware<0||soft<0){puts("UNSUPPORTED: no mixed GL4 pixel format");return 77;}
        if(CGLSetVirtualScreen(context,hardware))return 2;
    }
    CGLDestroyPixelFormat(pf);
    printf("renderer=%s version=%s double=%d integer=%d limit=%d constant=%d primitive=%x recovery=%d software=%d\n",
        glGetString(GL_RENDERER),glGetString(GL_VERSION),fp64,integer,limit,constant,primitive,recovery,software);
    fflush(stdout);
    GLint components=0,block_size=0;
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS,&components);
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE,&block_size);
    GLint count=limit?components/(fp64?2:1):4;
    if(count>block_size/16)count=block_size/16;
    if(count<1||glGetError())return 2;
    const char *type=fp64?"double":"float";
    char fragment[1024],declaration[128]="",body[512]="bool good=true;";
    if(!constant){
        snprintf(declaration,sizeof(declaration),"layout(std140) uniform Probe { %s value[%d]; };",type,count);
        snprintf(body,sizeof(body),"bool good=true; for(int i=0;i<%d;++i) if(value[i]!=%s(i+1)) good=false;",count,type);
    }
    snprintf(fragment,sizeof(fragment),"#version 400 core\n%s\nlayout(location=0) out %s result;\n"
        "void main(){%s result=%s;}\n",declaration,integer?"int":"vec4",body,
        integer?"(good?1:3)":"(good?vec4(1,0,0,1):vec4(0,1,0,1))");
    const char *vertex="#version 400 core\nvoid main(){const vec2 v[4]=vec2[4](vec2(-1,-1),vec2(-1,1),vec2(1,1),vec2(1,-1));gl_Position=vec4(v[gl_VertexID],0,1);}";
    if(primitive==GL_TRIANGLES)
        vertex="#version 400 core\nvoid main(){const vec2 v[6]=vec2[6](vec2(-1,-1),vec2(-1,1),vec2(1,1),vec2(-1,-1),vec2(1,1),vec2(1,-1));gl_Position=vec4(v[gl_VertexID],0,1);}";
    else if(primitive==GL_POINTS)
        vertex="#version 400 core\nvoid main(){vec2 p=vec2(gl_VertexID%32,gl_VertexID/32)+vec2(0.5);gl_Position=vec4(p/vec2(16,2)-1.0,0,1);}";
    GLuint vs=compile(GL_VERTEX_SHADER,vertex),fs=compile(GL_FRAGMENT_SHADER,fragment);
    GLuint program=glCreateProgram();GLint linked=0;
    glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
    glGetProgramiv(program,GL_LINK_STATUS,&linked);
    if(!linked){char log[2048]={0};glGetProgramInfoLog(program,sizeof(log),NULL,log);
        fprintf(stderr,"link: %s\n",log);return 2;}
    glUseProgram(program);
    GLuint ubo=0;
    if(!constant){
        const char *name="value[0]";GLuint member=GL_INVALID_INDEX;
        GLuint block=glGetUniformBlockIndex(program,"Probe");
        GLint stride=0,offset=0,size=0;
        glGetUniformIndices(program,1,&name,&member);
        if(member==GL_INVALID_INDEX||block==GL_INVALID_INDEX)return 2;
        glGetActiveUniformsiv(program,1,&member,GL_UNIFORM_ARRAY_STRIDE,&stride);
        glGetActiveUniformsiv(program,1,&member,GL_UNIFORM_OFFSET,&offset);
        glGetActiveUniformBlockiv(program,block,GL_UNIFORM_BLOCK_DATA_SIZE,&size);
        printf("count=%d stride=%d offset=%d size=%d\n",count,stride,offset,size);
        if(stride!=16||offset!=0||size<count*16||glGetError())return 2;
        unsigned char *bytes=calloc((size_t)size,1);if(!bytes)return 2;
        for(GLint i=0;i<count;++i){
            if(fp64){GLdouble value=i+1;memcpy(bytes+(size_t)i*16,&value,sizeof(value));}
            else {GLfloat value=i+1;memcpy(bytes+(size_t)i*16,&value,sizeof(value));}
        }
        glGenBuffers(1,&ubo);glBindBuffer(GL_UNIFORM_BUFFER,ubo);
        glBufferData(GL_UNIFORM_BUFFER,size,bytes,GL_STATIC_DRAW);free(bytes);
        glBindBufferRange(GL_UNIFORM_BUFFER,0,ubo,0,size);glUniformBlockBinding(program,block,0);
    }
    GLuint texture=0,fbo=0,vao=0;GLint data[32*4]={0};
    for(unsigned i=0;i<32*4;++i)data[i]=integer?2:(GLint)0x00ff0000;
    GLenum format=integer?GL_RED_INTEGER:GL_RGBA, transfer=integer?GL_INT:GL_UNSIGNED_BYTE;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,integer?GL_R32I:GL_RGBA8,32,4,0,format,transfer,data);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE||glGetError())return 2;
    if(switch_software){
        glFinish();if(CGLSetVirtualScreen(context,soft))return 2;
        printf("draw renderer=%s\n",glGetString(GL_RENDERER));fflush(stdout);
    }
    glViewport(0,0,32,4);glDrawArrays(primitive,0,primitive==GL_POINTS?128:primitive==GL_TRIANGLES?6:4);
    GLenum draw=glGetError();
    memset(data,0,sizeof(data));glGetTexImage(GL_TEXTURE_2D,0,format,transfer,data);
    GLenum tex=glGetError();
    const GLubyte expected[]={255,0,0,255};
    unsigned wrong=0;
    for(unsigned i=0;i<32*4;++i)wrong+=integer?data[i]!=1:memcmp(data+i,expected,4)!=0;
    GLint texel=data[0],pixel=0;glReadPixels(0,0,1,1,format,transfer,&pixel);
    GLenum read=glGetError();
    int good=!draw&&!tex&&!read&&!wrong&&texel==pixel;
    printf("draw=%x tex-error=%x read-error=%x texel=%08x pixel=%08x wrong=%u/128 result=%s\n",
        draw,tex,read,(unsigned)texel,(unsigned)pixel,wrong,good?"pass":"fail");fflush(stdout);
    if(switch_software){
        glFinish();if(CGLSetVirtualScreen(context,hardware))return 2;
        GLint returned=0;glReadPixels(0,0,1,1,format,transfer,&returned);
        GLenum error=glGetError();
        printf("return renderer=%s pixel=%08x error=%x\n",glGetString(GL_RENDERER),(unsigned)returned,error);
        good=good&&!error&&returned==pixel;fflush(stdout);
    }
    glUseProgram(0);glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);
    glDeleteVertexArrays(1,&vao);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);
    if(ubo)glDeleteBuffers(1,&ubo);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    puts("context destroyed");return good?0:1;
}
