/* Public CGL shader/format contracts, unchanged on native Intel and guests.
 * Exercise before enabling extension discovery with --exercise-unadvertised.
 * All results are rendered GPU pixels, native state, and native GL errors.
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
static int extension(const char *wanted)
{
    GLint count=0;glGetIntegerv(GL_NUM_EXTENSIONS,&count);
    for(GLint i=0;i<count;++i){const char *name=(const char *)glGetStringi(GL_EXTENSIONS,(GLuint)i);if(name&&!strcmp(name,wanted))return 1;}
    return 0;
}
static GLuint shader(GLenum type,const char *source)
{
    GLuint s=glCreateShader(type);GLint good=0;glShaderSource(s,1,&source,NULL);glCompileShader(s);
    glGetShaderiv(s,GL_COMPILE_STATUS,&good);
    if(!good){char log[4096];glGetShaderInfoLog(s,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);}
    check(good,"texture-family shader compiles");return s;
}
static GLuint program(const char *fragment)
{
    const char *vertex="#version 150\nin vec2 p;out vec2 uv;void main(){uv=p*.5+.5;gl_Position=vec4(p,0,1);}";
    GLuint v=shader(GL_VERTEX_SHADER,vertex),f=shader(GL_FRAGMENT_SHADER,fragment),p=glCreateProgram();
    glAttachShader(p,v);glAttachShader(p,f);glBindAttribLocation(p,0,"p");glBindFragDataLocation(p,0,"result");
    glLinkProgram(p);GLint good=0;glGetProgramiv(p,GL_LINK_STATUS,&good);check(good,"texture-family program links");
    glDeleteShader(v);glDeleteShader(f);glUseProgram(p);glUniform1i(glGetUniformLocation(p,"image"),0);return p;
}
static void pixel(const GLubyte expected[4],const char *name)
{
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    GLubyte actual[4]={0};glReadPixels(2,2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual);
    GLenum error=glGetError();int good=!error;
    for(unsigned i=0;i<4;++i)if(abs((int)actual[i]-expected[i])>1)good=0;
    printf("pixel=%u,%u,%u,%u expected=%u,%u,%u,%u error=%x\n",actual[0],actual[1],actual[2],actual[3],expected[0],expected[1],expected[2],expected[3],error);
    check(good,name);
}
static void swizzle(void)
{
    GLuint p=program("#version 150\nuniform sampler2D image;in vec2 uv;out vec4 result;void main(){result=texture(image,uv);}");
    GLuint t=0;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);
    const GLubyte data[]={153,102,51,204};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    const GLint requested[]={GL_BLUE,GL_ZERO,GL_RED,GL_ONE};GLint observed[5]={0,0,0,0,0x13579};
    glTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_RGBA,requested);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_RGBA,observed);
    check(!glGetError()&&!memcmp(requested,observed,sizeof(requested))&&observed[4]==0x13579,"four-component swizzle query preserves guard");
    const GLubyte expected[]={51,0,153,255};pixel(expected,"swizzle affects GPU sampling");
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_R,GL_GREEN);
    GLint r=0;glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_R,&r);
    check(!glGetError()&&r==GL_GREEN,"scalar swizzle set/query");
    const GLubyte scalar[]={102,0,153,255};pixel(scalar,"scalar swizzle affects GPU sampling");
    const GLenum keys[]={GL_TEXTURE_SWIZZLE_R,GL_TEXTURE_SWIZZLE_G,GL_TEXTURE_SWIZZLE_B,GL_TEXTURE_SWIZZLE_A,GL_TEXTURE_COMPARE_MODE,GL_TEXTURE_COMPARE_FUNC};
    const GLint expected_state[]={GL_GREEN,GL_ZERO,GL_RED,GL_ONE,GL_NONE,GL_LEQUAL};
    for(unsigned i=0;i<sizeof(keys)/sizeof(*keys);++i){
        GLint state[2]={-1,0x13579};glGetTexParameteriv(GL_TEXTURE_2D,keys[i],state);
        check(!glGetError()&&state[0]==expected_state[i]&&state[1]==0x13579,"scalar texture query and guard");
    }
    GLint invalid_state[2]={0x24680,0x13579};glGetTexParameteriv(GL_TEXTURE_2D,0xffffffffu,invalid_state);
    check(glGetError()==GL_INVALID_ENUM&&invalid_state[0]==0x24680&&invalid_state[1]==0x13579,"invalid texture pname preserves output");
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_R,GL_TEXTURE_2D);
    GLenum invalid=glGetError();printf("invalid swizzle error=%x\n",invalid);
    /* ARB_texture_swizzle specifies INVALID_OPERATION, not INVALID_ENUM. */
    check(invalid==GL_INVALID_OPERATION,"invalid swizzle rejected");
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_R,&r);check(!glGetError()&&r==GL_GREEN,"invalid swizzle preserves state");
    glDeleteTextures(1,&t);glDeleteProgram(p);
}
static void packed_integer(void)
{
    GLuint p=program("#version 150\nuniform usampler2D image;out vec4 result;void main(){result=vec4(texelFetch(image,ivec2(0),0))/vec4(1023,1023,1023,3);}");
    GLuint t=0;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);
    const GLuint packed=1023u|(512u<<10)|(257u<<20)|(2u<<30);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB10_A2UI,1,1,0,GL_RGBA_INTEGER,GL_UNSIGNED_INT_2_10_10_10_REV,&packed);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    GLint format=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&format);
    check(!glGetError()&&format==GL_RGB10_A2UI,"RGB10_A2UI native allocation");
    const GLubyte expected[]={255,128,64,170};pixel(expected,"packed unsigned integer texture GPU sampling");
    GLuint out[5]={0,0,0,0,0x13579};glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA_INTEGER,GL_UNSIGNED_INT,out);
    check(!glGetError()&&out[0]==1023&&out[1]==512&&out[2]==257&&out[3]==2&&out[4]==0x13579,"integer texture readback preserves width and guard");
    glDeleteTextures(1,&t);glDeleteProgram(p);
}
static void buffer_rgb32(void)
{
    GLuint p=program("#version 150\nuniform samplerBuffer image;out vec4 result;void main(){result=texelFetch(image,1);}");
    GLuint t=0,b=0;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_BUFFER,t);glGenBuffers(1,&b);glBindBuffer(GL_TEXTURE_BUFFER,b);
    const GLfloat data[]={1,0,0,.25,.5,1};glBufferData(GL_TEXTURE_BUFFER,sizeof(data),data,GL_DYNAMIC_DRAW);
    glTexBuffer(GL_TEXTURE_BUFFER,GL_RGB32F,b);check(!glGetError(),"RGB32F buffer texture attaches");
    const GLubyte expected[]={64,128,255,255};pixel(expected,"RGB32 buffer texture GPU sampling and implicit alpha");
    const GLfloat update[]={1,.5,.25};glBufferSubData(GL_TEXTURE_BUFFER,3*sizeof(GLfloat),sizeof(update),update);
    const GLubyte modified[]={255,128,64,255};pixel(modified,"buffer storage update visible to texture sampling");
    glDeleteTextures(1,&t);glDeleteBuffers(1,&b);glDeleteProgram(p);
}
static void cube_array(void)
{
    GLuint p=program("#version 150\n#extension GL_ARB_texture_cube_map_array : require\nuniform samplerCubeArray image;uniform vec4 direction;out vec4 result;void main(){result=texture(image,direction);}");
    GLint direction=glGetUniformLocation(p,"direction");GLuint t=0;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY,t);
    GLubyte data[12][4];
    for(unsigned i=0;i<12;++i){data[i][0]=(GLubyte)(20+i*17);data[i][1]=(GLubyte)(32+i*10);data[i][2]=(GLubyte)(200-i*8);data[i][3]=255;}
    glTexImage3D(GL_TEXTURE_CUBE_MAP_ARRAY,0,GL_RGBA8,1,1,12,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    check(!glGetError(),"cube-map array native allocation");
    const GLfloat axes[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for(unsigned i=0;i<12;++i){glUniform4f(direction,axes[i%6][0],axes[i%6][1],axes[i%6][2],(GLfloat)(i/6));pixel(data[i],"cube-map array face/layer sampling");}
    const GLubyte changed[]={17,231,83,255};glTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY,0,0,0,7,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,changed);
    memcpy(data[7],changed,4);glUniform4f(direction,-1,0,0,1);pixel(changed,"cube-map array subimage changes selected face");
    GLubyte out[sizeof(data)+4];memset(out,0xa5,sizeof(out));glGetTexImage(GL_TEXTURE_CUBE_MAP_ARRAY,0,GL_RGBA,GL_UNSIGNED_BYTE,out);
    check(!glGetError()&&!memcmp(out,data,sizeof(data))&&out[sizeof(data)]==0xa5,"cube-map array readback preserves all layers and guard");
    glDeleteTextures(1,&t);glDeleteProgram(p);
}
int main(int argc,char **argv)
{
    int exercise=argc==2&&!strcmp(argv[1],"--exercise-unadvertised");if(argc>1&&!exercise)return 64;
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&format,&n)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;CGLDestroyPixelFormat(format);
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    const char *names[]={"GL_ARB_texture_swizzle","GL_ARB_texture_rgb10_a2ui","GL_ARB_texture_buffer_object_rgb32","GL_ARB_texture_cube_map_array"};
    for(unsigned i=0;i<4;++i){int present=extension(names[i]);printf("discovery %s=%d\n",names[i],present);if(!exercise)check(present,"texture family discoverable");}
    GLuint color=0,fbo=0,vao=0,vbo=0;glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"texture-family output FBO complete");
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    const GLfloat vertices[]={-1,-1,1,-1,-1,1,1,1};glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);glEnableVertexAttribArray(0);glViewport(0,0,4,4);
    swizzle();packed_integer();buffer_rgb32();cube_array();
    check(!glGetError(),"texture-family tests leave no error");
    glDeleteBuffers(1,&vbo);glDeleteVertexArrays(1,&vao);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&color);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);printf("format family failures=%u\n",failures);return failures?1:0;
}
