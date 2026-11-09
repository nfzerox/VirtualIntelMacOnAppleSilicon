/* Public CGL object metadata: real compiled programs and native FBO storage.
 * Run unchanged on physical Intel, native host GL and the installed guest.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdio.h>
#include <string.h>
static unsigned failures;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);fflush(stdout);failures+=!good;}
static GLuint shader(GLenum type,const char *source)
{
    GLuint s=glCreateShader(type);glShaderSource(s,1,&source,NULL);glCompileShader(s);
    GLint good=0;glGetShaderiv(s,GL_COMPILE_STATUS,&good);check(good,"object-query shader compiles");
    if(!good){char log[2048];glGetShaderInfoLog(s,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);}
    GLint kind[2]={0,0x13579};glGetShaderiv(s,GL_SHADER_TYPE,kind);
    check(!glGetError()&&kind[0]==(GLint)type&&kind[1]==0x13579,"shader scalar query and guard");return s;
}
static void programs(void)
{
    const char *vs="#version 150\nvoid main(){gl_Position=vec4(0,0,0,1);}";
    const char *gs="#version 150\nlayout(lines) in;layout(triangle_strip,max_vertices=3) out;void main(){for(int i=0;i<3;++i){gl_Position=gl_in[0].gl_Position;EmitVertex();}EndPrimitive();}";
    const char *fs="#version 150\nout vec4 result;void main(){result=vec4(1,0,0,1);}";
    GLuint v=shader(GL_VERTEX_SHADER,vs),g=shader(GL_GEOMETRY_SHADER,gs),f=shader(GL_FRAGMENT_SHADER,fs),p=glCreateProgram();
    glAttachShader(p,v);glAttachShader(p,g);glAttachShader(p,f);glLinkProgram(p);
    GLint linked=0;glGetProgramiv(p,GL_LINK_STATUS,&linked);check(linked,"geometry metadata program links");
    const GLenum keys[]={GL_GEOMETRY_VERTICES_OUT,GL_GEOMETRY_INPUT_TYPE,GL_GEOMETRY_OUTPUT_TYPE};
    const GLint expected[]={3,GL_LINES,GL_TRIANGLE_STRIP};
    for(unsigned i=0;i<3;++i){
        GLint value[2]={-1,0x13579};glGetProgramiv(p,keys[i],value);GLenum error=glGetError();
        printf("program pname=%x value=%d expected=%d error=%x\n",keys[i],value[0],expected[i],error);
        check(!error&&value[0]==expected[i]&&value[1]==0x13579,"program scalar query and guard");
    }
    GLint invalid[2]={0x24680,0x13579};glGetProgramiv(p,0xffffffffu,invalid);
    check(glGetError()==GL_INVALID_ENUM&&invalid[0]==0x24680&&invalid[1]==0x13579,"invalid program pname preserves output");
    glDeleteProgram(p);glDeleteShader(v);glDeleteShader(g);glDeleteShader(f);
}
static void framebuffers(void)
{
    GLuint t=0,f=0;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D_ARRAY,t);
    glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_RGBA8,4,4,3,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&f);glBindFramebuffer(GL_FRAMEBUFFER,f);
    glFramebufferTextureLayer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,t,0,2);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"layered texture framebuffer complete");
    const GLenum keys[]={GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
        GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER,GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE,GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE,
        GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE,GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE,GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE,
        GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING};
    const GLint expected[]={GL_TEXTURE,(GLint)t,2,8,8,8,8,GL_UNSIGNED_NORMALIZED,GL_LINEAR};
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i){
        GLint value[2]={-1,0x13579};glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,keys[i],value);
        GLenum error=glGetError();printf("framebuffer pname=%x value=%d expected=%d error=%x\n",keys[i],value[0],expected[i],error);
        check(!error&&value[0]==expected[i]&&value[1]==0x13579,"framebuffer scalar query and guard");
    }
    GLint invalid[2]={0x24680,0x13579};glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,0xffffffffu,invalid);
    check(glGetError()==GL_INVALID_ENUM&&invalid[0]==0x24680&&invalid[1]==0x13579,"invalid framebuffer pname preserves output");
    glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&f);glDeleteTextures(1,&t);
}
static void renderbuffers(void)
{
    GLuint rb=0;glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,4,4);check(!glGetError(),"depth/stencil renderbuffer allocated");
    GLint depth[2]={-1,0x13579},stencil[2]={-1,0x13579};
    glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_DEPTH_SIZE,depth);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_STENCIL_SIZE,stencil);
    printf("renderbuffer depth=%d stencil=%d\n",depth[0],stencil[0]);
    check(!glGetError()&&depth[0]>=24&&depth[1]==0x13579&&stencil[0]>=8&&stencil[1]==0x13579,"renderbuffer component sizes and guards");
    GLint maximum=0;glGetIntegerv(GL_MAX_SAMPLES,&maximum);GLsizei samples=maximum>=4?4:maximum;
    check(!glGetError()&&samples>1,"multisample storage available");
    glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_RGBA8,4,4);
    GLint actual[2]={-1,0x13579};glGetRenderbufferParameteriv(GL_RENDERBUFFER,GL_RENDERBUFFER_SAMPLES,actual);
    printf("renderbuffer samples=%d requested=%d\n",actual[0],samples);
    check(!glGetError()&&actual[0]>=samples&&actual[1]==0x13579,"renderbuffer sample count and guard");
    GLint invalid[2]={0x24680,0x13579};glGetRenderbufferParameteriv(GL_RENDERBUFFER,0xffffffffu,invalid);
    check(glGetError()==GL_INVALID_ENUM&&invalid[0]==0x24680&&invalid[1]==0x13579,"invalid renderbuffer pname preserves output");
    glDeleteRenderbuffers(1,&rb);
}
int main(void)
{
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&format,&n)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;CGLDestroyPixelFormat(format);
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    programs();framebuffers();renderbuffers();check(!glGetError(),"object queries leave no error");
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);printf("object query failures=%u\n",failures);return failures?1:0;
}
