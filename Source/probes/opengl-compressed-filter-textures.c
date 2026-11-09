/* Public CGL texture-family contract. Run unchanged on native and guest.
 * --exercise-unadvertised qualifies the API before enabling discovery; the
 * ordinary mode requires both extension names. No injected client or CPU
 * substitute: GLSL samples real compressed textures into an FBO.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned failures;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);fflush(stdout);failures+=!good;}
static int extension(const char *wanted,int legacy)
{
    if(legacy){
        const char *all=(const char *)glGetString(GL_EXTENSIONS);size_t n=strlen(wanted);
        for(const char *p=all;p&&(p=strstr(p,wanted));p+=n)
            if((p==all||p[-1]==' ')&&(!p[n]||p[n]==' '))return 1;
    }else{
        GLint count=0;glGetIntegerv(GL_NUM_EXTENSIONS,&count);
        for(GLint i=0;i<count;++i){const char *s=(const char *)glGetStringi(GL_EXTENSIONS,(GLuint)i);if(s&&!strcmp(s,wanted))return 1;}
    }
    return 0;
}
static GLuint shader(GLenum type,const char *source)
{
    GLuint name=glCreateShader(type);GLint good=0;
    glShaderSource(name,1,&source,NULL);glCompileShader(name);glGetShaderiv(name,GL_COMPILE_STATUS,&good);
    if(!good){char log[2048];glGetShaderInfoLog(name,sizeof(log),NULL,log);fprintf(stderr,"shader: %s\n",log);}
    check(good,"texture sampling shader compiles");return name;
}
static GLuint program(int legacy)
{
    const char *vs=legacy?"#version 120\nattribute vec2 p;varying vec2 uv;void main(){uv=p*.5+.5;gl_Position=vec4(p,0,1);}":
        "#version 150\nin vec2 p;out vec2 uv;void main(){uv=p*.5+.5;gl_Position=vec4(p,0,1);}";
    const char *fs=legacy?"#version 120\nuniform sampler2D image;varying vec2 uv;void main(){gl_FragColor=texture2D(image,uv);}":
        "#version 150\nuniform sampler2D image;in vec2 uv;out vec4 result;void main(){result=texture(image,uv);}";
    GLuint v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,fs),p=glCreateProgram();
    glAttachShader(p,v);glAttachShader(p,f);glBindAttribLocation(p,0,"p");glLinkProgram(p);
    GLint linked=0;glGetProgramiv(p,GL_LINK_STATUS,&linked);check(linked,"texture sampling program links");
    glDeleteShader(v);glDeleteShader(f);glUseProgram(p);glUniform1i(glGetUniformLocation(p,"image"),0);return p;
}
static void pixel(unsigned red,unsigned blue,unsigned alpha)
{
    glClearColor(.25,.5,.75,1);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    GLubyte p[4]={0};glReadPixels(2,2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);GLenum error=glGetError();
    printf("sample=%u,%u,%u,%u error=%x\n",p[0],p[1],p[2],p[3],error);
    check(!error&&p[0]==red&&!p[1]&&p[2]==blue&&p[3]==alpha,"compressed texture GPU sampling");
}
static void compression(void)
{
    const GLenum formats[]={0x83f0,0x83f1,0x83f2,0x83f3};
    for(unsigned format=0;format<4;++format){
        GLubyte data[16]={0};size_t bytes=format<2?8:16,colors=bytes-8;
        unsigned alpha=255;
        if(format==2){memset(data,0x88,8);alpha=136;}
        if(format==3){data[0]=128;alpha=128;}
        data[colors]=0;data[colors+1]=0xf8;data[colors+2]=0xe0;data[colors+3]=7;
        GLuint texture=0,buffer=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glCompressedTexImage2D(GL_TEXTURE_2D,0,formats[format],4,4,0,(GLsizei)bytes,data);
        GLint compressed=0,size=0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_COMPRESSED,&compressed);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_COMPRESSED_IMAGE_SIZE,&size);
        check(!glGetError()&&compressed&&size==(GLint)bytes,"S3TC format accepted as native compressed storage");
        pixel(255,0,alpha);
        GLubyte out[20];memset(out,0xa5,sizeof(out));glGetCompressedTexImage(GL_TEXTURE_2D,0,out);
        check(!glGetError()&&!memcmp(out,data,bytes)&&out[bytes]==0xa5,"compressed byte readback and guard");
        /* A real PBO input offset, not a guest pointer sent to the host. */
        GLubyte upload[20]={0};data[colors]=0x1f;data[colors+1]=0;memcpy(upload+4,data,bytes);
        glGenBuffers(1,&buffer);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffer);
        glBufferData(GL_PIXEL_UNPACK_BUFFER,4+bytes,upload,GL_STATIC_DRAW);
        glCompressedTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,formats[format],(GLsizei)bytes,(void *)4);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glDeleteBuffers(1,&buffer);
        check(!glGetError(),"compressed subimage upload from PBO offset");pixel(0,255,alpha);
        glGenBuffers(1,&buffer);glBindBuffer(GL_PIXEL_PACK_BUFFER,buffer);memset(upload,0xa5,sizeof(upload));
        glBufferData(GL_PIXEL_PACK_BUFFER,sizeof(upload),upload,GL_STATIC_READ);
        glGetCompressedTexImage(GL_TEXTURE_2D,0,(void *)4);
        glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(out),out);
        check(!glGetError()&&out[0]==0xa5&&out[3]==0xa5&&!memcmp(out+4,data,bytes),"compressed PBO readback preserves offset guard");
        glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glDeleteBuffers(1,&buffer);glDeleteTextures(1,&texture);
    }
    /* DXT1's transparent selector, not just the opaque RGB interpretation. */
    const GLubyte transparent[]={0,0,0,0xf8,255,255,255,255};GLuint texture=0;
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glCompressedTexImage2D(GL_TEXTURE_2D,0,0x83f1,4,4,0,sizeof(transparent),transparent);
    pixel(0,0,0);glDeleteTextures(1,&texture);
}
static void anisotropy(int legacy)
{
    GLfloat maximum=0,value=0;glGetFloatv(0x84ff,&maximum);
    printf("maximum anisotropy=%g\n",maximum);check(!glGetError()&&maximum>=2,"native anisotropy limit");
    GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexParameterf(GL_TEXTURE_2D,0x84fe,maximum);glGetTexParameterfv(GL_TEXTURE_2D,0x84fe,&value);
    check(!glGetError()&&value==maximum,"texture anisotropy set/query");
    glTexParameterf(GL_TEXTURE_2D,0x84fe,0);
    check(glGetError()==GL_INVALID_VALUE,"invalid texture anisotropy rejected");
    glGetTexParameterfv(GL_TEXTURE_2D,0x84fe,&value);check(!glGetError()&&value==maximum,"invalid setter preserves texture anisotropy");
    GLint major=0,minor=0;
    if(!legacy){glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);}
    if((major>3||(major==3&&minor>=3))||extension("GL_ARB_sampler_objects",legacy)){
        GLuint sampler=0;glGenSamplers(1,&sampler);glSamplerParameterf(sampler,0x84fe,maximum);
        glGetSamplerParameterfv(sampler,0x84fe,&value);check(!glGetError()&&value==maximum,"sampler anisotropy set/query");
        glSamplerParameterf(sampler,0x84fe,0);check(glGetError()==GL_INVALID_VALUE,"invalid sampler anisotropy rejected");
        glGetSamplerParameterfv(sampler,0x84fe,&value);check(!glGetError()&&value==maximum,"invalid setter preserves sampler anisotropy");
        glDeleteSamplers(1,&sampler);
    }
    glDeleteTextures(1,&texture);
}
int main(int argc,char **argv)
{
    int legacy=0,exercise=0;
    for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--legacy"))legacy=1;else if(!strcmp(argv[i],"--exercise-unadvertised"))exercise=1;else return 64;}
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(legacy?kCGLOGLPVersion_Legacy:kCGLOGLPVersion_3_2_Core),0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&format,&n)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);
    printf("renderer=%s version=%s legacy=%d\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),legacy);
    int s3tc=extension("GL_EXT_texture_compression_s3tc",legacy),aniso=extension("GL_EXT_texture_filter_anisotropic",legacy);
    printf("discovery S3TC=%d anisotropy=%d exercise=%d\n",s3tc,aniso,exercise);
    if(!exercise)check(s3tc&&aniso,"qualified texture extensions discoverable");
    GLuint color=0,fbo=0,vao=0,vbo=0;
    glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"sampling framebuffer complete");
    GLuint p=program(legacy);
    if(!legacy){glGenVertexArrays(1,&vao);glBindVertexArray(vao);}
    const GLfloat vertices[]={-1,-1,1,-1,-1,1,1,1};
    glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);glEnableVertexAttribArray(0);glViewport(0,0,4,4);
    if(exercise||s3tc)compression();if(exercise||aniso)anisotropy(legacy);
    check(!glGetError(),"texture family leaves no GL error");
    glDeleteProgram(p);glDeleteBuffers(1,&vbo);if(vao)glDeleteVertexArrays(1,&vao);
    glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&color);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("texture family failures=%u\n",failures);return failures?1:0;
}
