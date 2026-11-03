/* Period WebKit state queries through actual native CGL/GLI, with output
 * guards. Run unchanged on physical Intel and the VM. No browser settings. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static int failures;
static void check(int good,const char *name)
{ printf("%s %s\n",good?"PASS":"FAIL",name);failures+=!good; }
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf||!count||
       CGLCreateContext(pf,NULL,&context)||CGLSetCurrentContext(context))return 2;
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    printf("renderer=%s\n",renderer?renderer:"none");
    if(!renderer||!strstr(renderer,"Intel"))return 2;
    struct { uint32_t before;GLboolean value;unsigned char pad[3];uint32_t after; } boolean;
    memset(&boolean,0xa5,sizeof(boolean));
    glDepthMask(GL_FALSE);glGetBooleanv(GL_DEPTH_WRITEMASK,&boolean.value);
    check(!glGetError()&&!boolean.value&&boolean.before==0xa5a5a5a5&&
        boolean.after==0xa5a5a5a5&&boolean.pad[0]==0xa5&&boolean.pad[1]==0xa5&&boolean.pad[2]==0xa5,
        "one-byte depth write-mask query preserves guards");
    struct { uint64_t before;GLdouble value;uint64_t after; } depth;
    memset(&depth,0xa5,sizeof(depth));
    glClearDepth(.25);glGetDoublev(GL_DEPTH_CLEAR_VALUE,&depth.value);
    check(!glGetError()&&depth.value==.25&&depth.before==UINT64_C(0xa5a5a5a5a5a5a5a5)&&
        depth.after==UINT64_C(0xa5a5a5a5a5a5a5a5),"scalar double depth clear-value preserves guards");
    GLfloat floating=-1;glGetFloatv(GL_DEPTH_CLEAR_VALUE,&floating);
    check(!glGetError()&&floating==.25f,"native floating depth conversion");
    GLuint shader=glCreateShader(GL_VERTEX_SHADER);
    const GLchar *source="void main(){gl_Position=gl_Vertex;}";
    glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
    struct { uint32_t before;GLint value;uint32_t after; } type={0xa5a5a5a5,-1,0xa5a5a5a5};
    glGetShaderiv(shader,GL_SHADER_TYPE,&type.value);
    check(!glGetError()&&type.value==GL_VERTEX_SHADER&&type.before==0xa5a5a5a5&&
        type.after==0xa5a5a5a5,"scalar shader type query preserves guards");
    GLint compiled=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&compiled);
    check(!glGetError()&&compiled,"ordinary shader compilation remains valid");
    GLuint program=glCreateProgram(),fragment=glCreateShader(GL_FRAGMENT_SHADER);
    const GLchar *fragmentSource="void main(){gl_FragColor=vec4(1.0);}";
    glShaderSource(fragment,1,&fragmentSource,NULL);glCompileShader(fragment);
    glAttachShader(program,shader);glAttachShader(program,fragment);glLinkProgram(program);
    GLint attributes=-1,length=-1;glGetProgramiv(program,GL_ACTIVE_ATTRIBUTES,&attributes);
    glGetProgramiv(program,GL_ACTIVE_ATTRIBUTE_MAX_LENGTH,&length);
    check(!glGetError()&&attributes>=0&&length>=0,"scalar active-attribute queries");
    struct { uint32_t before;GLuint names[3];uint32_t after; } attached;
    memset(&attached,0xa5,sizeof(attached));GLsizei returned=-1;
    glGetAttachedShaders(program,3,&returned,attached.names);
    check(!glGetError()&&returned==2&&attached.before==0xa5a5a5a5&&attached.after==0xa5a5a5a5&&
        attached.names[2]==0xa5a5a5a5&&((attached.names[0]==shader&&attached.names[1]==fragment)||
        (attached.names[1]==shader&&attached.names[0]==fragment)),"attached names preserve unused capacity and guards");
    glGetAttachedShaders(program,3,NULL,attached.names);
    check(!glGetError()&&attached.names[2]==0xa5a5a5a5,"optional attached count pointer");
    returned=-1;glGetAttachedShaders(program,0,&returned,NULL);
    check(!glGetError()&&returned==0,"zero attached capacity with null name pointer");
    returned=-1;glGetAttachedShaders(program,-1,&returned,attached.names);
    check(glGetError()==GL_INVALID_VALUE&&returned==-1&&attached.names[2]==0xa5a5a5a5,"negative attached capacity preserves outputs");
    glDeleteProgram(program);glDeleteShader(fragment);
    glDeleteShader(shader);glDepthMask(GL_TRUE);glClearDepth(1);
    check(!glGetError(),"state restoration");
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(pf);
    return !!failures;
}
