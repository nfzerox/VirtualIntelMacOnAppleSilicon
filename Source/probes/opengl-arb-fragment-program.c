/* Same public GL workload on physical Intel Mavericks and our typed wire.
 * The physical implementation is the oracle, not GLRendererFloat. */
#ifdef VIM_WIRE_PROBE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static unsigned failures;
static void check(int good,const char *what)
{printf("%s %s\n",good?"PASS":"FAIL",what);fflush(stdout);failures+=!good;}
static const char program[]=
    "!!ARBfp1.0\nTEMP t;\n"
    "TEX t, fragment.texcoord[0], texture[0], RECT;\n"
    "MUL t, t, program.local[0];\n"
    "ADD result.color, t, program.env[0];\nEND\n";
int main(int argc,char **argv)
{
    int requireIntel=argc==2 && !strcmp(argv[1],"--require-intel");
    if(argc>1 && !requireIntel)return 2;
#ifdef VIM_WIRE_PROBE
    unsigned char shadow[64*64*4]={0};void *context=DGCreateContext(NULL,NULL);
    if(!context || !DGMakeCurrent(context,shadow,GL_UNSIGNED_BYTE,64,64))return 2;
#else
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count) || !pf || CGLCreateContext(pf,NULL,&context))return 2;
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(context);
#endif
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    if(requireIntel && !strstr((const char *)glGetString(GL_RENDERER),"Intel"))return 2;
    const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
    check(extensions && strstr(extensions,"GL_ARB_fragment_program"),"real fragment-program extension available");
    if(failures)return 1;
    GLuint names[2]={0};glGenProgramsARB(2,names);
    check(names[0] && names[1] && names[0]!=names[1] && !glIsProgramARB(names[0]) && !glGetError(),
        "generated names are distinct and not objects before bind");
    glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB,names[0]);
    check(glIsProgramARB(names[0]) && !glGetError(),"bound ARB program becomes a real object");
    glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_FORMAT_ASCII_ARB,sizeof(program)-1,program);
    GLint position=99,length=0,format=0,instructions=0;
    glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB,&position);
    glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_LENGTH_ARB,&length);
    glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_FORMAT_ARB,&format);
    glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_INSTRUCTIONS_ARB,&instructions);
    check(!glGetError() && position==-1 && length==sizeof(program)-1 && format==GL_PROGRAM_FORMAT_ASCII_ARB && instructions>=3,
        "native compiler accepts program and exposes real source/resource metadata");
    GLint limit=0,coordinates=0,underNative=0;
    glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB,GL_MAX_PROGRAM_ENV_PARAMETERS_ARB,&limit);
    glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB,&underNative);
    glGetIntegerv(GL_MAX_TEXTURE_COORDS_ARB,&coordinates);
    check(!glGetError() && limit>=24 && coordinates>=4 && underNative,
        "resource limits and native execution status come from the driver");
    unsigned char source[sizeof(program)+8];memset(source,0xa5,sizeof(source));
    glGetProgramStringARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_STRING_ARB,source);
    int good=!glGetError() && !memcmp(source,program,sizeof(program)-1);
    for(unsigned i=sizeof(program)-1;i<sizeof(source);++i)good&=source[i]==0xa5;
    check(good,"program source round-trip writes exact bytes without NUL or padding overwrite");
    GLfloat local[]={1,.5f,.25f,1},outf[4]={0};GLdouble env[]={.125,0,0,0},outd[4]={0};
    glProgramLocalParameter4fvARB(GL_FRAGMENT_PROGRAM_ARB,0,local);
    glProgramEnvParameter4dARB(GL_FRAGMENT_PROGRAM_ARB,0,env[0],0,0,0);
    glGetProgramLocalParameterfvARB(GL_FRAGMENT_PROGRAM_ARB,0,outf);
    glGetProgramEnvParameterdvARB(GL_FRAGMENT_PROGRAM_ARB,0,outd);
    check(!glGetError() && !memcmp(local,outf,sizeof(local)) && !memcmp(env,outd,sizeof(env)),
        "float local and double environment parameters retain four components");
    glProgramLocalParameter4dARB(GL_FRAGMENT_PROGRAM_ARB,1,.25,.5,.75,1);
    glProgramLocalParameter4dvARB(GL_FRAGMENT_PROGRAM_ARB,2,env);
    glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB,3,1,.5f,.25f,1);
    glProgramEnvParameter4dvARB(GL_FRAGMENT_PROGRAM_ARB,1,env);
    glProgramEnvParameter4fARB(GL_FRAGMENT_PROGRAM_ARB,2,1,.5f,.25f,1);
    glProgramEnvParameter4fvARB(GL_FRAGMENT_PROGRAM_ARB,3,local);
    glGetProgramLocalParameterdvARB(GL_FRAGMENT_PROGRAM_ARB,1,outd);
    glGetProgramEnvParameterfvARB(GL_FRAGMENT_PROGRAM_ARB,3,outf);
    check(!glGetError() && outd[0]==.25 && outd[1]==.5 && outd[2]==.75 && outd[3]==1 && !memcmp(local,outf,sizeof(local)),
        "all scalar/vector float/double parameter entry points execute");
    GLfloat matrix[17]={0},expected[16]={1,0,0,0,0,1,0,0,0,0,1,0,.25f,.5f,.75f,1};
    GLdouble transpose[17]={0};matrix[16]=123;transpose[16]=456;
    glMatrixMode(GL_MATRIX0_ARB);glLoadMatrixf(expected);glPushMatrix();
    glGetFloatv(GL_CURRENT_MATRIX_ARB,matrix);glGetDoublev(GL_TRANSPOSE_CURRENT_MATRIX_ARB,transpose);
    GLint depth=0;glGetIntegerv(GL_CURRENT_MATRIX_STACK_DEPTH_ARB,&depth);glPopMatrix();glMatrixMode(GL_MODELVIEW);
    good=!glGetError() && depth>=2 && matrix[16]==123 && transpose[16]==456 && !memcmp(matrix,expected,sizeof(expected));
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)good&=transpose[r*4+c]==expected[c*4+r];
    check(good,"ARB program matrix/transpose and stack queries preserve vector canaries");
    GLuint input=0,output=0,fbo=0;glGenTextures(1,&input);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,input);
    unsigned char pixels[64*64*4];for(unsigned i=0;i<sizeof(pixels);i+=4){pixels[i]=64;pixels[i+1]=128;pixels[i+2]=192;pixels[i+3]=128;}
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT && !glGetError(),"GPU output FBO complete");
    glViewport(0,0,64,64);glEnable(GL_FRAGMENT_PROGRAM_ARB);GLboolean enabled=0;glGetBooleanv(GL_FRAGMENT_PROGRAM_ARB,&enabled);
    glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(64,0);glVertex2f(1,-1);
    glTexCoord2f(64,64);glVertex2f(1,1);glTexCoord2f(0,64);glVertex2f(-1,1);glEnd();
    unsigned char pixel[4]={0};glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    printf("ARB GPU pixel=%u,%u,%u,%u\n",pixel[0],pixel[1],pixel[2],pixel[3]);
    check(!glGetError() && enabled && abs(pixel[0]-96)<=1 && abs(pixel[1]-64)<=1 && abs(pixel[2]-48)<=1 && pixel[3]==128,
        "native TEX/MUL/ADD fragment program produces parameterized GPU pixels");
    const GLfloat vertices[]={-1,-1,1,-1,1,1,-1,1},coords[]={0,0,64,0,64,64,0,64};
    glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2,GL_FLOAT,0,vertices);glTexCoordPointer(2,GL_FLOAT,0,coords);glDrawArrays(GL_QUADS,0,4);
    glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    check(!glGetError() && abs(pixel[0]-96)<=1 && abs(pixel[1]-64)<=1 && abs(pixel[2]-48)<=1 && pixel[3]==128,
        "same fragment program renders through real client-array drawing");
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);glDisableClientState(GL_VERTEX_ARRAY);
    const char invalid[]="!!ARBfp1.0\nBOGUS result.color;\nEND\n";
    glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_FORMAT_ASCII_ARB,sizeof(invalid)-1,invalid);
    GLenum error=glGetError();glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB,&position);
    const char *diagnostic=(const char *)glGetString(GL_PROGRAM_ERROR_STRING_ARB);
    check(error==GL_INVALID_OPERATION && position>=0 && (size_t)position<sizeof(invalid)-1 && diagnostic && *diagnostic && !glGetError(),
        "bad program returns real compiler error position and diagnostic string");
    glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_FORMAT_ASCII_ARB,sizeof(program)-1,program);
    glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB,&position);diagnostic=(const char *)glGetString(GL_PROGRAM_ERROR_STRING_ARB);
    check(position==-1 && diagnostic && !glGetError(),"successful recompile refreshes mutable compiler diagnostics");
    GLuint untouched=0xabcdef;glGenProgramsARB(0,&untouched);glDeleteProgramsARB(0,NULL);
    check(!glGetError() && untouched==0xabcdef,"zero-count program lifetime calls preserve caller memory");
    glGenProgramsARB(-1,&untouched);check(glGetError()==GL_INVALID_VALUE && untouched==0xabcdef,"negative program count returns INVALID_VALUE without writes");
    glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_FORMAT_ASCII_ARB,-1,program);
    check(glGetError()==GL_INVALID_VALUE,"negative program source length returns INVALID_VALUE");
    glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB,names[1]);
    glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_FORMAT_ASCII_ARB,0,program);
    check(glGetError()==GL_INVALID_OPERATION,"empty program reaches native compiler without pointer-shape failure");
    glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB,names[0]);
    glDisable(GL_FRAGMENT_PROGRAM_ARB);glDeleteProgramsARB(2,names);
    GLint binding=-1;glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_BINDING_ARB,&binding);
    unsigned char empty=0xa5;glGetProgramStringARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_STRING_ARB,&empty);
    check(!glIsProgramARB(names[0]) && binding==0 && empty==0xa5 && !glGetError(),"delete unbinds object; empty default source leaves output untouched");
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&input);glDeleteTextures(1,&output);
#ifdef VIM_WIRE_PROBE
    DGDestroyContext(context);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
#endif
    return failures?1:0;
}
