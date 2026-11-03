/* Same owned public-core-context fixture on physical Mavericks and the guest.
 * No GLRendererFloat oracle, desktop capture, injection or system changes. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdio.h>
#include <stdlib.h>
static int failures;
static void check(int good,const char *name)
{ printf("%s %s\n",good?"PASS":"FAIL",name);failures+=!good;fflush(stdout); }
__attribute__((noinline)) void VIMProfileBeforeCreate(unsigned profile)
{ printf("PROFILE before-create=%x\n",profile);fflush(stdout); }
static CGLContextObj make(CGLOpenGLProfile profile,CGLContextObj share,int required)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)profile,kCGLPFAColorSize,24,kCGLPFAAlphaSize,8,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint count=0,selected=0;
    CGLError e=CGLChoosePixelFormat(attrs,&pf,&count);
    if(!e && (!pf || count<1))e=kCGLBadPixelFormat;
    if(!e)e=CGLDescribePixelFormat(pf,0,kCGLPFAOpenGLProfile,&selected);
    if(!e) { VIMProfileBeforeCreate(profile);e=CGLCreateContext(pf,share,&c); }
    printf("PROFILE request=%x selected=%x count=%d share=%d result=%d\n",profile,selected,count,share!=NULL,e);
    if(pf)CGLDestroyPixelFormat(pf);
    if(required)check(!e && c,"requested accelerated context created");
    return c;
}
static GLuint shader(GLenum type,const char *source)
{
    GLuint name=glCreateShader(type);GLint good=0;
    glShaderSource(name,1,&source,NULL);glCompileShader(name);glGetShaderiv(name,GL_COMPILE_STATUS,&good);
    if(!good) { char log[2048];glGetShaderInfoLog(name,sizeof(log),NULL,log);fprintf(stderr,"shader: %s\n",log); }
    check(good,"GLSL 150 compile");return name;
}
static void draw(CGLContextObj c,CGLContextObj legacy)
{
    if(!c)return;
    check(!CGLSetCurrentContext(c),"core context made current");
    printf("CORE renderer=%s version=%s GLSL=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),glGetString(GL_SHADING_LANGUAGE_VERSION));
    GLint major=0,minor=0,mask=0;glGetIntegerv(GL_MAJOR_VERSION,&major);glGetIntegerv(GL_MINOR_VERSION,&minor);
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK,&mask);
    const char *version=(const char *)glGetString(GL_VERSION);
    check(major>=3 && (mask&GL_CONTEXT_CORE_PROFILE_BIT) && version && version[0]>='3',"actual core profile metadata");
    GLint extensions=-1;glGetIntegerv(GL_NUM_EXTENSIONS,&extensions);
    check(extensions>=0,"core extension count");
    for(GLint i=0;i<extensions && i<8;++i)check(glGetStringi(GL_EXTENSIONS,i)!=NULL,"indexed core extension string");
    check(!glGetError(),"core extension enumeration leaves no error");
    check(!glGetStringi(GL_EXTENSIONS,(GLuint)extensions) && glGetError()==GL_INVALID_VALUE,"indexed extension upper bound rejected");
    check(!glGetString(GL_EXTENSIONS) && glGetError()==GL_INVALID_ENUM,"removed core extension query rejected");
    GLuint tex=0,fbo=0,vao=0,vbo=0,ebo=0;
    glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"core texture FBO");
    const char *vs="#version 150\nin vec2 position;void main(){gl_Position=vec4(position,0,1);}";
    const char *fs="#version 150\nuniform vec4 color;out vec4 outputColor;void main(){outputColor=color;}";
    GLuint vertex=shader(GL_VERTEX_SHADER,vs),fragment=shader(GL_FRAGMENT_SHADER,fs),program=glCreateProgram();
    glAttachShader(program,vertex);glAttachShader(program,fragment);glBindAttribLocation(program,0,"position");
    glBindFragDataLocation(program,0,"outputColor");glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);check(linked,"core program link");
    glUseProgram(program);glUniform4f(glGetUniformLocation(program,"color"),.25,.5,.75,1);
    const GLfloat points[]={-1,-1,1,-1,0,1};const GLushort indices[]={0,1,2};
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(points),points,GL_STATIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,NULL);glEnableVertexAttribArray(0);
    glGenBuffers(1,&ebo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    glViewport(0,0,64,64);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,NULL);
    unsigned char pixel[4]={0};glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    printf("CORE pixel=%u,%u,%u,%u major=%d minor=%d mask=%x\n",pixel[0],pixel[1],pixel[2],pixel[3],major,minor,mask);
    check(abs(pixel[0]-64)<=1 && abs(pixel[1]-128)<=1 && abs(pixel[2]-191)<=1 && pixel[3]==255,"core indexed VBO/VAO shader drawing");
    check(glGetFragDataLocation(program,"outputColor")==0,"fragment output binding query");
    GLenum e=glGetError();printf("CORE draw-error=%x\n",e);check(!e,"core drawing leaves no GL error");
    GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    check(fence && glIsSync(fence),"core GPU fence identity");
    GLint sync_value=-1;GLsizei sync_length=-1;
    glGetSynciv(fence,GL_SYNC_CONDITION,1,&sync_length,&sync_value);
    check(sync_length==1 && sync_value==GL_SYNC_GPU_COMMANDS_COMPLETE,"typed sync condition query");
    GLenum waited=glClientWaitSync(fence,GL_SYNC_FLUSH_COMMANDS_BIT,1000000000);
    check(waited==GL_ALREADY_SIGNALED || waited==GL_CONDITION_SATISFIED,"actual GPU client wait completes");
    glWaitSync(fence,0,GL_TIMEOUT_IGNORED);check(!glGetError(),"server wait preserves exact uint64 timeout");
    GLint64 maximum=0;glGetInteger64v(GL_MAX_SERVER_WAIT_TIMEOUT,&maximum);
    e=glGetError();printf("SYNC maximum-server-wait=%llx error=%x\n",maximum,e);
    check(!e,"64-bit server wait limit query");
    CGLContextObj shared=make(kCGLOGLPVersion_3_2_Core,c,1);
    if(shared) {
        CGLSetCurrentContext(shared);
        check(glIsTexture(tex) && glIsBuffer(vbo) && glIsProgram(program),"shared core texture/buffer/program objects");
        check(glIsSync(fence),"sync visible in shared core context");
        check(!glGetError(),"shared core context starts without GL error");
        CGLSetCurrentContext(c);CGLDestroyContext(shared);
    }
    CGLContextObj parent=make(kCGLOGLPVersion_3_2_Core,NULL,1);
    if(parent) {
        CGLSetCurrentContext(parent);
        GLsync survivor=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);glFlush();
        check(glIsSync(fence) && !glGetError(),"live sync namespace visible from unrelated context");
        CGLContextObj child=make(kCGLOGLPVersion_3_2_Core,parent,1);
        if(child) {
            CGLSetCurrentContext(child);CGLDestroyContext(parent);parent=NULL;
            check(survivor && glIsSync(survivor),"sync survives creating context destruction");
            waited=glClientWaitSync(survivor,GL_SYNC_FLUSH_COMMANDS_BIT,1000000000);
            check(waited==GL_ALREADY_SIGNALED || waited==GL_CONDITION_SATISFIED,"surviving sync waits on actual GPU");
            glDeleteSync(survivor);check(!glGetError(),"surviving sync deletion");
            CGLSetCurrentContext(c);CGLDestroyContext(child);
        } else if(survivor)glDeleteSync(survivor);
        CGLSetCurrentContext(c);if(parent)CGLDestroyContext(parent);
    }
    CGLContextObj incompatible=make(kCGLOGLPVersion_3_2_Core,legacy,0);
    check(!incompatible,"legacy/core share rejected");if(incompatible)CGLDestroyContext(incompatible);
    glDeleteSync(fence);check(!glIsSync(fence) && !glGetError(),"deleted sync becomes invalid");
    glDeleteSync(NULL);printf("SYNC delete-null-error=%x\n",glGetError());
    glDeleteBuffers(1,&ebo);glDeleteBuffers(1,&vbo);glDeleteVertexArrays(1,&vao);
    glDeleteProgram(program);glDeleteShader(vertex);glDeleteShader(fragment);
    glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&tex);
}
int main(void)
{
    CGLContextObj legacy=make(kCGLOGLPVersion_Legacy,NULL,1);
    if(legacy) { CGLSetCurrentContext(legacy);printf("LEGACY version=%s\n",glGetString(GL_VERSION)); }
    CGLContextObj core=make(kCGLOGLPVersion_3_2_Core,NULL,1);draw(core,legacy);
    // Native Mavericks can return success with no candidates for GL4's
    // explicit selector even when the GL3 selector delivers actual GL4.1.
    CGLContextObj fourth=make(kCGLOGLPVersion_GL4_Core,NULL,0);
    if(fourth) { CGLSetCurrentContext(fourth);printf("CORE4 renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION)); }
    CGLSetCurrentContext(NULL);if(fourth)CGLDestroyContext(fourth);if(core)CGLDestroyContext(core);if(legacy)CGLDestroyContext(legacy);
    printf("core profile failures=%d\n",failures);return failures?1:0;
}
