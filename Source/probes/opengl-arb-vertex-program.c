/* Actual accelerated ARB vertex-program qualification on period Intel and
 * PassthroughOpenGL. --exercise-unadvertised deliberately tests existing
 * operations without claiming the extension is already advertised. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned failures;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);fflush(stdout);failures+=!good;}
static const char source[]=
    "!!ARBvp1.0\n"
    "MOV result.position, vertex.position;\n"
    "MAD result.color, vertex.color, program.local[0], program.env[0];\n"
    "MOV result.texcoord[0], vertex.texcoord[0];\nEND\n";
static void pixel(const char *name)
{
    unsigned char rgba[4]={0};glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    GLenum error=glGetError();printf("pixel=%u,%u,%u,%u error=%x\n",rgba[0],rgba[1],rgba[2],rgba[3],error);
    check(!error && abs(rgba[0]-64)<=1 && abs(rgba[1]-32)<=1 && abs(rgba[2]-48)<=1 && rgba[3]==255,name);
}
int main(int argc,char **argv)
{
    int unadvertised=argc==2 && !strcmp(argv[1],"--exercise-unadvertised");
    if(argc!=1 && !unadvertised)return 64;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint count=0;
    CGLError error=CGLChoosePixelFormat(attrs,&pf,&count);
    if(!error)error=CGLCreateContext(pf,NULL,&context);
    if(pf)CGLDestroyPixelFormat(pf);
    if(!error)error=CGLSetCurrentContext(context);
    if(error){printf("context error=%d\n",error);return 2;}
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
    int advertised=extensions && strstr(extensions,"GL_ARB_vertex_program");
    printf("vertex_program_advertised=%d\n",advertised);
    if(!unadvertised)check(advertised,"native vertex-program capability advertised");
    GLuint program=0;glGenProgramsARB(1,&program);glBindProgramARB(GL_VERTEX_PROGRAM_ARB,program);
    glProgramStringARB(GL_VERTEX_PROGRAM_ARB,GL_PROGRAM_FORMAT_ASCII_ARB,sizeof(source)-1,source);
    GLint position=0,length=0,attributes=0,parameters=0;
    glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB,&position);
    glGetProgramivARB(GL_VERTEX_PROGRAM_ARB,GL_PROGRAM_LENGTH_ARB,&length);
    glGetProgramivARB(GL_VERTEX_PROGRAM_ARB,GL_MAX_PROGRAM_NATIVE_ATTRIBS_ARB,&attributes);
    glGetProgramivARB(GL_VERTEX_PROGRAM_ARB,GL_MAX_PROGRAM_NATIVE_PARAMETERS_ARB,&parameters);
    check(!glGetError() && position==-1 && length==sizeof(source)-1 && attributes>=8 && parameters>=24,
        "real vertex compiler and resource queries");
    char roundtrip[sizeof(source)+4];memset(roundtrip,0x5a,sizeof(roundtrip));
    glGetProgramStringARB(GL_VERTEX_PROGRAM_ARB,GL_PROGRAM_STRING_ARB,roundtrip);
    int intact=!memcmp(roundtrip,source,sizeof(source)-1);
    for(unsigned i=sizeof(source)-1;i<sizeof(roundtrip);++i)intact&=roundtrip[i]==0x5a;
    check(!glGetError() && intact,"vertex source round-trip preserves guard bytes");
    GLfloat local[]={.5,.5,.5,1},out[4]={0};GLdouble env[]={.25,0,.125,0},outd[4]={0};
    glProgramLocalParameter4fvARB(GL_VERTEX_PROGRAM_ARB,0,local);
    glProgramEnvParameter4dvARB(GL_VERTEX_PROGRAM_ARB,0,env);
    glGetProgramLocalParameterfvARB(GL_VERTEX_PROGRAM_ARB,0,out);
    glGetProgramEnvParameterdvARB(GL_VERTEX_PROGRAM_ARB,0,outd);
    check(!glGetError() && !memcmp(local,out,sizeof(local)) && !memcmp(env,outd,sizeof(env)),
        "vertex local and environment parameter values");
    GLuint textures[2]={0},fbo=0;glGenTextures(2,textures);
    glBindTexture(GL_TEXTURE_2D,textures[0]);
    const unsigned char texel[]={128,64,192,255};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,texel);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D,textures[1]);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,textures[1],0);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT && !glGetError(),"output framebuffer complete");
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glViewport(0,0,16,16);
    glBindTexture(GL_TEXTURE_2D,textures[0]);glEnable(GL_TEXTURE_2D);glEnable(GL_VERTEX_PROGRAM_ARB);
    glColor4f(.5,1,.25,1);glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(1,0);glVertex2f(1,-1);
    glTexCoord2f(1,1);glVertex2f(1,1);glTexCoord2f(0,1);glVertex2f(-1,1);glEnd();
    pixel("vertex outputs, color MAD and texture coordinates render correctly");
    const GLfloat vertices[]={-1,-1,1,-1,1,1,-1,1},coords[]={0,0,1,0,1,1,0,1};
    glClear(GL_COLOR_BUFFER_BIT);glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2,GL_FLOAT,0,vertices);glTexCoordPointer(2,GL_FLOAT,0,coords);glDrawArrays(GL_QUADS,0,4);
    pixel("same vertex program through client arrays");
    glDisableClientState(GL_VERTEX_ARRAY);glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    const char invalid[]="!!ARBvp1.0\nBOGUS result.position;\nEND\n";
    glProgramStringARB(GL_VERTEX_PROGRAM_ARB,GL_PROGRAM_FORMAT_ASCII_ARB,sizeof(invalid)-1,invalid);
    GLenum compileError=glGetError();glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB,&position);
    const char *diagnostic=(const char *)glGetString(GL_PROGRAM_ERROR_STRING_ARB);
    check(compileError==GL_INVALID_OPERATION && position>=0 && diagnostic && *diagnostic && !glGetError(),
        "invalid vertex program exposes native compiler failure");
    glDisable(GL_VERTEX_PROGRAM_ARB);glDeleteProgramsARB(1,&program);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(2,textures);
    check(!glGetError(),"clean vertex-program teardown");
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);return failures?1:0;
}
