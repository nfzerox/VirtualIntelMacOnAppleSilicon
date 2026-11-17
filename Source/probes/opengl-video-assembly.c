/* Replay a captured ARB video-conversion program with deterministic raw
 * packed-video texels. No app injection or decoder: isolate native compiler,
 * texture sampling and current/multiple client texture-coordinate state.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <mach/mach_time.h>
#include "../PassthroughOpenGL/native-arb-revalidation-host.h"

int main(int argc,char **argv)
{
    if(argc!=2&&argc!=5&&argc!=6)return 64;
    int generic=argc==6&&!strcmp(argv[5],"generic");
    int recompile=argc==6&&!strcmp(argv[5],"software-first-recompile");
    int recovery=recompile||(argc==6&&!strcmp(argv[5],"software-first"));if(argc==6&&!generic&&!recovery)return 64;
    unsigned width=8,height=2,frames=1;
    if(argc>=5){
        unsigned *values[]={&width,&height,&frames};const unsigned limits[]={8192,8192,120};
        for(unsigned i=0;i<3;++i){char *end=NULL;unsigned long n=strtoul(argv[i+2],&end,10);
            if(!end||end==argv[i+2]||*end||!n||n>limits[i])return 64;*values[i]=(unsigned)n;}
        if(width<8||height<2)return 64;
    }
    FILE *file=fopen(argv[1],"rb");if(!file||fseek(file,0,SEEK_END))return 2;
    long length=ftell(file);if(length<10||length>65536||fseek(file,0,SEEK_SET))return 2;
    char *source=calloc((size_t)length+1,1);if(!source||fread(source,1,(size_t)length,file)!=(size_t)length)return 2;
    fclose(file);
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    if(recovery)attrs[1]=kCGLPFAAllowOfflineRenderers;
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf||CGLCreateContext(pf,NULL,&context)||!context)return 3;
    CGLSetCurrentContext(context);
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    GLint hardware=0,software=-1;
    if(recovery){
        CGLGetVirtualScreen(context,&hardware);
        for(GLint i=0;i<count;++i){GLint gpu=0;CGLDescribePixelFormat(pf,i,kCGLPFAAccelerated,&gpu);if(!gpu){software=i;break;}}
        if(software<0||CGLSetVirtualScreen(context,software))return 77;
        printf("compiler-renderer=%s\n",glGetString(GL_RENDERER));
    }
    CGLDestroyPixelFormat(pf);
    GLuint program=0,input=0,output=0,fbo=0;
    glGenProgramsARB(1,&program);glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB,program);
    const GLfloat rows[3][4]={{1.16438f,0,1.79274f,-.972945f},
        {1.16438f,-.213249f,-.532909f,.301483f},{1.16438f,2.1124f,0,-1.1334f}};
    for(unsigned i=0;i<3;++i)glProgramLocalParameter4fvARB(GL_FRAGMENT_PROGRAM_ARB,i,rows[i]);
    glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_FORMAT_ASCII_ARB,(GLsizei)length,source);
    if(recovery&&CGLSetVirtualScreen(context,hardware))return 6;
    if(recompile&&DGRevalidateNativeARBPrograms())return 7;
    GLenum error=glGetError();GLint position=-1,native=0;
    glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB,&position);
    glGetProgramivARB(GL_FRAGMENT_PROGRAM_ARB,GL_PROGRAM_UNDER_NATIVE_LIMITS_ARB,&native);
    printf("compile-error=%x position=%d native=%d\n",error,position,native);
    if(error){printf("%s\n",glGetString(GL_PROGRAM_ERROR_STRING_ARB));return 4;}
    glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 5;
    unsigned char packed[32];for(unsigned i=0;i<sizeof(packed);i+=4){packed[i]=128;packed[i+1]=100;packed[i+2]=128;packed[i+3]=100;}
    glGenTextures(1,&input);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,input);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,4,2,0,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,packed);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glViewport(0,0,width,height);glDisable(GL_BLEND);glDisable(GL_DITHER);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,8,0,2,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glEnable(GL_FRAGMENT_PROGRAM_ARB);
    const GLfloat positions[]={0,0,8,0,8,2,0,2};
    GLfloat coordinates[5][8];const GLfloat base[]={0,0,4,0,4,2,0,2};
    if(generic){glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,positions);}
    else {glEnableClientState(GL_VERTEX_ARRAY);glVertexPointer(2,GL_FLOAT,0,positions);}
    for(unsigned unit=0;unit<5;++unit){
        memcpy(coordinates[unit],base,sizeof(base));
        GLfloat offset=unit==1||unit==3?-.5f:unit==2||unit==4?.5f:0;
        for(unsigned vertex=0;vertex<4;++vertex)coordinates[unit][vertex*2]+=offset;
        if(generic){glEnableVertexAttribArray(8+unit);glVertexAttribPointer(8+unit,2,GL_FLOAT,GL_FALSE,0,coordinates[unit]);}
        else {glClientActiveTexture(GL_TEXTURE0+unit);glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            glTexCoordPointer(2,GL_FLOAT,0,coordinates[unit]);}
    }
    if(generic)glVertexAttrib4f(13,.5f,.5f,0,1);else glMultiTexCoord2f(GL_TEXTURE5,.5f,.5f);
    mach_timebase_info_data_t timebase;mach_timebase_info(&timebase);
    glFinish();uint64_t start=mach_absolute_time();
    for(unsigned i=0;i<frames;++i)glDrawArrays(GL_QUADS,0,4);
    glFinish();uint64_t end=mach_absolute_time();
    double elapsed=(double)(end-start)*timebase.numer/timebase.denom/1.e9;
    GLint vertex_gpu=-1,fragment_gpu=-1;
    CGLGetParameter(context,kCGLCPGPUVertexProcessing,&vertex_gpu);
    CGLGetParameter(context,kCGLCPGPUFragmentProcessing,&fragment_gpu);
    printf("draw=%ux%u frames=%u generic=%d elapsed=%.6f fps=%.3f gpu_vertex=%d gpu_fragment=%d\n",width,height,frames,generic,elapsed,elapsed>0?frames/elapsed:0,vertex_gpu,fragment_gpu);
    unsigned char pixels[64]={0};glReadPixels(0,0,8,2,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    error=glGetError();unsigned failed=error!=GL_NO_ERROR;
    for(unsigned i=0;i<16;++i){
        for(unsigned channel=0;channel<3;++channel){
            float expected=rows[channel][0]*100/255.f+(rows[channel][1]+rows[channel][2])*128/255.f+rows[channel][3];
            if(fabsf(pixels[i*4+channel]-255*expected)>2)failed=1;
        }
        if(pixels[i*4+3]!=255)failed=1;
    }
    printf("pixel=%u,%u,%u,%u error=%x %s video assembly conversion\n",pixels[0],pixels[1],pixels[2],pixels[3],error,failed?"FAIL":"PASS");
    glDeleteProgramsARB(1,&program);glDeleteTextures(1,&input);glDeleteTextures(1,&output);glDeleteFramebuffersEXT(1,&fbo);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);free(source);return failed?1:0;
}
