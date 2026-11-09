/* Keep exact native FP64 storage/readback separate from GPU shader execution. */
#ifdef VIM_UNIFORM_FP64_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define glGetUniformLocation(p,n) glGetUniformLocationARB((GLhandleARB)(uintptr_t)(p),n)
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#endif
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)
static const GLdouble guard=0x1.923456789abcdp+41;

static GLuint compile(GLenum type,const char *source)
{
    GLuint s=glCreateShader(type);GLint ok=0;
    glShaderSource(s,1,&source,NULL);glCompileShader(s);glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[1024]={0};glGetShaderInfoLog(s,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(ok);return s;
}

static void read_values(GLuint program,GLint location,const GLdouble *expected,size_t count)
{
    GLdouble got[18];
    for(size_t i=0;i<18;++i)got[i]=guard;
    glGetUniformdv(program,location,got+1);
    CHECK(glGetError()==GL_NO_ERROR && !memcmp(got+1,expected,count*sizeof(*expected)));
    CHECK(got[0]==guard);
    for(size_t i=count+1;i<18;++i)CHECK(got[i]==guard);
    if(count)printf("FP64 components=%lu first=%a last=%a\n",(unsigned long)count,got[1],got[count]);
}

static void verify(void)
{
    const char *vert="#version 410\n"
        "uniform double gain;uniform dvec3 samples[2];uniform dmat4x3 transform;uniform dmat4 square;\n"
        "void main(){dvec3 v=transform*dvec4(samples[0]+samples[1],gain);"
        "gl_Position=vec4(square*dvec4(v,1));}";
    GLuint v=compile(GL_VERTEX_SHADER,vert);
    GLuint f=compile(GL_FRAGMENT_SHADER,"#version 410\nout vec4 color;void main(){color=vec4(1);}");
    GLuint p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);
    GLint linked=0;glGetProgramiv(p,GL_LINK_STATUS,&linked);
    if(!linked){char log[1024]={0};glGetProgramInfoLog(p,sizeof(log),NULL,log);fputs(log,stderr);}
    CHECK(linked);if(!linked)return;
    glUseProgram(p);
    GLint gain=glGetUniformLocation(p,"gain"),a=glGetUniformLocation(p,"samples[0]");
    GLint b=glGetUniformLocation(p,"samples[1]"),matrix=glGetUniformLocation(p,"transform");
    GLint square=glGetUniformLocation(p,"square");
    CHECK(gain>=0 && a>=0 && b>=0 && matrix>=0 && square>=0);

    const GLdouble precise=0x1.0000000000001p+0;
    const GLdouble samples[]={0x1.0000000001p-3,-0x1.fffffffffffdp+1,4,
                             0x1.123456789abcdp+12,-0x1.23456789abcdep-40,0x1.000000001p-3};
    GLdouble row_major[12],column_major[12],square_values[16];
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c){
        GLdouble value=(double)(4*r+c+1)*precise;
        row_major[r*4+c]=column_major[c*3+r]=value;
    }
    for(unsigned i=0;i<16;++i)square_values[i]=(i+1)*precise;
    glUniform1d(gain,precise);glUniform3dv(a,2,samples);
    glUniformMatrix4x3dv(matrix,1,GL_TRUE,row_major);
    glUniformMatrix4dv(square,1,GL_FALSE,square_values);
    CHECK(glGetError()==GL_NO_ERROR);
    read_values(p,gain,&precise,1);read_values(p,a,samples,3);read_values(p,b,samples+3,3);
    read_values(p,matrix,column_major,12);read_values(p,square,square_values,16);
    glUniform3dv(-1,100,NULL);glUniformMatrix4dv(square,0,GL_FALSE,NULL);
    CHECK(glGetError()==GL_NO_ERROR);
    glUniform3dv(a,-1,NULL);CHECK(glGetError()==GL_INVALID_VALUE);
    GLdouble unchanged=guard;
    glGetUniformdv(p,-1,&unchanged);
    CHECK(glGetError()==GL_INVALID_OPERATION && unchanged==guard);

    glLinkProgram(p);glGetProgramiv(p,GL_LINK_STATUS,&linked);CHECK(linked);
    gain=glGetUniformLocation(p,"gain");
    const GLdouble zero=0;
    read_values(p,gain,&zero,1);
    glUseProgram(0);glDeleteProgram(p);glDeleteShader(f);glDeleteShader(v);
}

int main(void)
{
#ifdef VIM_UNIFORM_FP64_WIRE
    const int attrs[]={0x56494d02,0x3200,0};unsigned char screen[16*16*4]={0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    if(!c || !DGMakeCurrent(c,screen,GL_UNSIGNED_BYTE,16,16))return 2;
    CHECK(c->connection->features&DG_FEATURE_FP64_UNIFORM);
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf||CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    printf("FP64 uniform renderer=%s\n",glGetString(GL_RENDERER));
    verify();
#ifdef VIM_UNIFORM_FP64_WIRE
    CHECK(!c->connection->dead);DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    printf("FP64 uniform failures=%d\n",failures);return failures?1:0;
}
