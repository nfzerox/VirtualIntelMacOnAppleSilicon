/* Measure the desktop ARB_ES2_compatibility contract with period libGL.
 * In particular an exported ShaderBinary does not imply a usable format. */
#ifdef VIM_ES2_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define GL_SHADER_BINARY_FORMATS 0x8df8
#define GL_NUM_SHADER_BINARY_FORMATS 0x8df9
#define GL_SHADER_COMPILER 0x8dfa
#define GL_LOW_FLOAT 0x8df0
#define GL_MEDIUM_FLOAT 0x8df1
#define GL_HIGH_FLOAT 0x8df2
#define GL_LOW_INT 0x8df3
#define GL_MEDIUM_INT 0x8df4
#define GL_HIGH_INT 0x8df5
#define GL_GEOMETRY_SHADER 0x8dd9
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#endif
#include <stdio.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)

int main(void)
{
#ifdef VIM_ES2_WIRE
    const int attrs[]={0x56494d02,DG_CGL_PROFILE_GL4,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(c->connection->features2&DG_FEATURE2_ES2_COMPAT);
    uint32_t features=c->connection->features2;
    c->connection->features2&=~DG_FEATURE2_ES2_COMPAT;
    glReleaseShaderCompiler();
    CHECK(glGetError()==GL_INVALID_OPERATION && !c->connection->dead);
    c->connection->features2=features;
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint screens=0;
    if(CGLChoosePixelFormat(attrs,&pf,&screens)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("ES2 compatibility renderer=%s\n",glGetString(GL_RENDERER));

    GLint count[2]={-1,37};GLboolean compiler[2]={GL_FALSE,37};
    glGetIntegerv(GL_NUM_SHADER_BINARY_FORMATS,count);
    glGetBooleanv(GL_SHADER_COMPILER,compiler);
    CHECK(glGetError()==GL_NO_ERROR && count[0]>=0 && count[1]==37 && compiler[0] && compiler[1]==37);
    if(count[0]==0){
        GLint empty=37;glGetIntegerv(GL_SHADER_BINARY_FORMATS,&empty);
        CHECK(glGetError()==GL_NO_ERROR && empty==37);
    }
    printf("shader binary formats=%d\n",count[0]);

    /* Chromium/Skia size every shader from these (issue #25). Each is one
     * scalar; the guard word must survive and every value must be positive.
     * glGetInteger64v uses the same size table, so check that path as well. */
    const GLenum vector_limits[]={0x8dfb,0x8dfc,0x8dfd}; /* MAX_VERTEX_UNIFORM, MAX_VARYING, MAX_FRAGMENT_UNIFORM _VECTORS */
    for(unsigned i=0;i<3;++i){
        GLint value[2]={0,37};GLint64 wide[2]={0,37};
        glGetIntegerv(vector_limits[i],value);
        GLenum error=glGetError();
        glGetInteger64v(vector_limits[i],wide);
        GLenum wide_error=glGetError();
        printf("limit %x=%d/%lld error=%x/%x\n",vector_limits[i],value[0],(long long)wide[0],error,wide_error);
        CHECK(error==GL_NO_ERROR && value[0]>0 && value[1]==37);
        CHECK(wide_error==GL_NO_ERROR && wide[0]==value[0] && wide[1]==37);
    }

    const GLenum stages[]={GL_VERTEX_SHADER,GL_FRAGMENT_SHADER};
    const GLenum precision_types[]={GL_LOW_FLOAT,GL_MEDIUM_FLOAT,GL_HIGH_FLOAT,GL_LOW_INT,GL_MEDIUM_INT,GL_HIGH_INT};
    for(unsigned stage=0;stage<2;++stage)for(unsigned type=0;type<6;++type){
        GLint range[]={-1,-1,37},precision[]={-1,37};
        glGetShaderPrecisionFormat(stages[stage],precision_types[type],range,precision);
        GLenum error=glGetError();
        CHECK(error==GL_NO_ERROR && range[0]>=0 && range[1]>=0 && precision[0]>=0 && range[2]==37 && precision[1]==37);
        printf("precision %x %x=%d,%d,%d error=%x\n",stages[stage],precision_types[type],
            range[0],range[1],precision[0],error);
    }
    GLint range[]={37,37},precision=37;
    glGetShaderPrecisionFormat(GL_GEOMETRY_SHADER,GL_HIGH_FLOAT,range,&precision);
    GLenum invalid_stage=glGetError();
    printf("unsupported precision stage error=%x outputs=%d,%d,%d\n",invalid_stage,range[0],range[1],precision);
    CHECK(invalid_stage==GL_INVALID_ENUM && range[0]==127 && range[1]==127 && precision==23);
    range[0]=range[1]=precision=37;
    glGetShaderPrecisionFormat(GL_VERTEX_SHADER,GL_TEXTURE_2D,range,&precision);
    GLenum invalid_type=glGetError();
    printf("unsupported precision type error=%x outputs=%d,%d,%d\n",invalid_type,range[0],range[1],precision);
    CHECK(invalid_type==GL_INVALID_ENUM && range[0]==37 && range[1]==37 && precision==37);

    glDepthRangef(-.25f,.625f);glClearDepthf(.375f);
    GLdouble limits[3]={-1,-1,37},clear[2]={-1,37};
    glGetDoublev(GL_DEPTH_RANGE,limits);glGetDoublev(GL_DEPTH_CLEAR_VALUE,clear);
    CHECK(glGetError()==GL_NO_ERROR && limits[0]==0 && limits[1]==.625 && limits[2]==37 &&
        clear[0]==.375 && clear[1]==37);
    glReleaseShaderCompiler();
    CHECK(glGetError()==GL_NO_ERROR);
    GLuint shader=glCreateShader(GL_VERTEX_SHADER);
    const GLchar *source="#version 410\nvoid main(){gl_Position=vec4(0,0,0,1);}";
    glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
    GLint compiled=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&compiled);
    CHECK(glGetError()==GL_NO_ERROR && compiled);
    const unsigned char invalid[]={1,2,3,4};
    glShaderBinary(1,&shader,~0u,invalid,sizeof(invalid));
    GLenum invalid_format=glGetError();
    printf("unrecognized shader binary format error=%x\n",invalid_format);
    CHECK(invalid_format==GL_INVALID_ENUM);
    glDeleteShader(shader);
    CHECK(glGetError()==GL_NO_ERROR);
#ifdef VIM_ES2_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("ES2 compatibility failures=%d\n",failures);
    return failures?1:0;
}
