/* EXT_texture_sRGB_decode: actual native texture/sampler pixels and mipmaps.
 * Same public CGL binary on physical Intel and the guest; no CPU substitute.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define DECODE_PARAMETER 0x8a48
#define DECODE 0x8a49
#define SKIP_DECODE 0x8a4a
static unsigned failures;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);fflush(stdout);failures+=!good;}
static int extension(const char *name,int legacy)
{
    if(legacy){const char *all=(const char *)glGetString(GL_EXTENSIONS);size_t n=strlen(name);
        for(const char *p=all;p&&(p=strstr(p,name));p+=n)if((p==all||p[-1]==' ')&&(!p[n]||p[n]==' '))return 1;
    }else{GLint count=0;glGetIntegerv(GL_NUM_EXTENSIONS,&count);for(GLint i=0;i<count;++i){const char *s=(const char *)glGetStringi(GL_EXTENSIONS,(GLuint)i);if(s&&!strcmp(s,name))return 1;}}
    return 0;
}
static GLuint shader(GLenum stage,const char *source)
{
    GLuint s=glCreateShader(stage);glShaderSource(s,1,&source,NULL);glCompileShader(s);GLint good=0;glGetShaderiv(s,GL_COMPILE_STATUS,&good);
    if(!good){char log[2048];glGetShaderInfoLog(s,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);}check(good,"sRGB sampling shader compiles");return s;
}
static GLuint program(int legacy)
{
    const char *vs=legacy?"#version 120\nattribute vec2 p;varying vec2 uv;void main(){uv=p*.5+.5;gl_Position=vec4(p,0,1);}":
        "#version 150\nin vec2 p;out vec2 uv;void main(){uv=p*.5+.5;gl_Position=vec4(p,0,1);}";
    const char *fs=legacy?"#version 120\nuniform sampler2D image;varying vec2 uv;void main(){gl_FragColor=texture2D(image,uv);}":
        "#version 150\nuniform sampler2D image;in vec2 uv;out vec4 result;void main(){result=texture(image,uv);}";
    GLuint v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,fs),p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);
    glBindAttribLocation(p,0,"p");glLinkProgram(p);GLint good=0;glGetProgramiv(p,GL_LINK_STATUS,&good);check(good,"sRGB sampling program links");
    glDeleteShader(v);glDeleteShader(f);glUseProgram(p);glUniform1i(glGetUniformLocation(p,"image"),0);return p;
}
static unsigned decoded(unsigned byte)
{double s=byte/255.0;return (unsigned)lround(255*(s<=.04045?s/12.92:pow((s+.055)/1.055,2.4)));}
static void pixel(const GLubyte expected[4],const char *name)
{
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLE_STRIP,0,4);GLubyte p[4]={0};
    glReadPixels(2,2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);GLenum error=glGetError();int good=!error;
    for(unsigned i=0;i<4;++i)if(abs((int)p[i]-expected[i])>1)good=0;
    printf("pixel=%u,%u,%u,%u expected=%u,%u,%u,%u error=%x\n",p[0],p[1],p[2],p[3],expected[0],expected[1],expected[2],expected[3],error);check(good,name);
}
static void verify(unsigned format,int legacy,int samplers)
{
    const GLenum formats[]={GL_SRGB8_ALPHA8,GL_RGBA8,0x8c4c,0x8c4d,0x8c4e,0x8c4f};
    GLubyte encoded[4]={128,128,128,128};GLuint t=0;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);
    if(format<2)glTexImage2D(GL_TEXTURE_2D,0,formats[format],1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,encoded);
    else{
        GLubyte block[16]={0};unsigned bytes=format<4?8:16,colors=bytes-8;
        if(format==4)memset(block,0x88,8);if(format==5)block[0]=128;
        block[colors]=0x10;block[colors+1]=0x84;block[colors+2]=0x10;block[colors+3]=0x84;
        glCompressedTexImage2D(GL_TEXTURE_2D,0,formats[format],4,4,0,bytes,block);
        encoded[0]=132;encoded[1]=130;encoded[2]=132;encoded[3]=format==4?136:format==5?128:255;
    }
    printf("sRGB format=%x legacy=%d\n",formats[format],legacy);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    GLubyte linear[4];memcpy(linear,encoded,4);if(format!=1)for(unsigned i=0;i<3;++i)linear[i]=(GLubyte)decoded(encoded[i]);
    GLint mode[2]={0,0x13579};glGetTexParameteriv(GL_TEXTURE_2D,DECODE_PARAMETER,mode);
    check(!glGetError()&&mode[0]==DECODE&&mode[1]==0x13579,"default texture decode and query guard");pixel(linear,"default texture sRGB decode");
    glTexParameteri(GL_TEXTURE_2D,DECODE_PARAMETER,SKIP_DECODE);pixel(encoded,"texture skip decode");
    glTexParameterf(GL_TEXTURE_2D,DECODE_PARAMETER,DECODE);pixel(linear,"float texture decode setter");
    GLint skip=SKIP_DECODE;GLfloat decode=DECODE;
    glTexParameteriv(GL_TEXTURE_2D,DECODE_PARAMETER,&skip);pixel(encoded,"vector texture skip setter");
    glTexParameterfv(GL_TEXTURE_2D,DECODE_PARAMETER,&decode);GLfloat query[2]={0,123};glGetTexParameterfv(GL_TEXTURE_2D,DECODE_PARAMETER,query);
    check(!glGetError()&&query[0]==DECODE&&query[1]==123,"float texture decode query and guard");
    if(!legacy){
        glTexParameterIiv(GL_TEXTURE_2D,DECODE_PARAMETER,&skip);GLint got[2]={0,0x13579};glGetTexParameterIiv(GL_TEXTURE_2D,DECODE_PARAMETER,got);
        check(!glGetError()&&got[0]==SKIP_DECODE&&got[1]==0x13579,"typed integer texture decode state");pixel(encoded,"typed texture skip decode");
        GLuint decode_u=DECODE,got_u[2]={0,0x13579};glTexParameterIuiv(GL_TEXTURE_2D,DECODE_PARAMETER,&decode_u);glGetTexParameterIuiv(GL_TEXTURE_2D,DECODE_PARAMETER,got_u);
        check(!glGetError()&&got_u[0]==DECODE&&got_u[1]==0x13579,"typed unsigned texture decode state");
    }
    glTexParameteri(GL_TEXTURE_2D,DECODE_PARAMETER,GL_TEXTURE_2D);check(glGetError()==GL_INVALID_ENUM,"invalid texture decode rejected");
    glGetTexParameteriv(GL_TEXTURE_2D,DECODE_PARAMETER,mode);check(!glGetError()&&mode[0]==DECODE,"invalid decode setter preserves state");
    if(samplers){
        GLuint s=0;glGenSamplers(1,&s);glSamplerParameteri(s,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glSamplerParameteri(s,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,DECODE_PARAMETER,SKIP_DECODE);glBindSampler(0,s);
        glGetSamplerParameteriv(s,DECODE_PARAMETER,mode);check(!glGetError()&&mode[0]==DECODE&&mode[1]==0x13579,"default sampler decode and query guard");pixel(linear,"sampler overrides texture skip state");
        glSamplerParameteri(s,DECODE_PARAMETER,SKIP_DECODE);pixel(encoded,"sampler skip decode");
        glSamplerParameterf(s,DECODE_PARAMETER,DECODE);glGetSamplerParameterfv(s,DECODE_PARAMETER,query);
        check(!glGetError()&&query[0]==DECODE&&query[1]==123,"float sampler decode query and guard");pixel(linear,"sampler float decode setter");
        glSamplerParameteriv(s,DECODE_PARAMETER,&skip);glGetSamplerParameteriv(s,DECODE_PARAMETER,mode);check(!glGetError()&&mode[0]==SKIP_DECODE,"sampler vector integer state");
        glSamplerParameterfv(s,DECODE_PARAMETER,&decode);pixel(linear,"sampler vector float decode setter");
        glSamplerParameterIiv(s,DECODE_PARAMETER,&skip);glGetSamplerParameterIiv(s,DECODE_PARAMETER,mode);check(!glGetError()&&mode[0]==SKIP_DECODE,"sampler typed integer state");
        GLuint decode_u=DECODE,got[2]={0,0x13579};glSamplerParameterIuiv(s,DECODE_PARAMETER,&decode_u);glGetSamplerParameterIuiv(s,DECODE_PARAMETER,got);
        check(!glGetError()&&got[0]==DECODE&&got[1]==0x13579,"sampler typed unsigned state and guard");
        glSamplerParameteri(s,DECODE_PARAMETER,GL_TEXTURE_2D);check(glGetError()==GL_INVALID_ENUM,"invalid sampler decode rejected");
        glGetSamplerParameteriv(s,DECODE_PARAMETER,mode);check(!glGetError()&&mode[0]==DECODE,"invalid sampler setter preserves state");
        glDeleteSamplers(1,&s);pixel(encoded,"deleting sampler restores texture skip state");
    }
    glDeleteTextures(1,&t);
}
static void mipmaps(void)
{
    const GLubyte checker[]={0,0,0,255,255,255,255,255,255,255,255,255,0,0,0,255};
    for(unsigned skip=0;skip<2;++skip){
        GLuint t=0;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,checker);
        glTexParameteri(GL_TEXTURE_2D,DECODE_PARAMETER,skip?SKIP_DECODE:DECODE);glGenerateMipmap(GL_TEXTURE_2D);
        GLubyte value[4]={0};glGetTexImage(GL_TEXTURE_2D,1,GL_RGBA,GL_UNSIGNED_BYTE,value);unsigned expected=skip?128:188;
        printf("mipmap skip=%u rgb=%u,%u,%u expected=%u\n",skip,value[0],value[1],value[2],expected);
        check(!glGetError()&&abs((int)value[0]-(int)expected)<=1&&value[0]==value[1]&&value[1]==value[2]&&value[3]==255,"mipmap generation respects decode policy");glDeleteTextures(1,&t);
    }
}
int main(int argc,char **argv)
{
    int legacy=0,exercise=0;for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--legacy"))legacy=1;else if(!strcmp(argv[i],"--exercise-unadvertised"))exercise=1;else return 64;}
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)(legacy?kCGLOGLPVersion_Legacy:kCGLOGLPVersion_3_2_Core),0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;CGLDestroyPixelFormat(pf);
    printf("renderer=%s version=%s legacy=%d\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),legacy);
    int present=extension("GL_EXT_texture_sRGB_decode",legacy);printf("discovery sRGB_decode=%d exercise=%d\n",present,exercise);if(!exercise)check(present,"sRGB decode discoverable");
    int samplers=!legacy&&extension("GL_ARB_sampler_objects",legacy);
    if(!samplers)puts("NOTSUPPORTED optional sampler-object interaction in this profile");
    GLuint color=0,fbo=0,vao=0,vbo=0;glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"sRGB output FBO complete");
    GLuint p=program(legacy);if(!legacy){glGenVertexArrays(1,&vao);glBindVertexArray(vao);}glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    const GLfloat positions[]={-1,-1,1,-1,-1,1,1,1};glBufferData(GL_ARRAY_BUFFER,sizeof(positions),positions,GL_STATIC_DRAW);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);glEnableVertexAttribArray(0);glViewport(0,0,4,4);
    for(unsigned i=0;i<6;++i)verify(i,legacy,samplers);mipmaps();check(!glGetError(),"sRGB fixture leaves no error");
    glDeleteProgram(p);glDeleteBuffers(1,&vbo);if(vao)glDeleteVertexArrays(1,&vao);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&color);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);printf("sRGB decode failures=%u\n",failures);return failures?1:0;
}
