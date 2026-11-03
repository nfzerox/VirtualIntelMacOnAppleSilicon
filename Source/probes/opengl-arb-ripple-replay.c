/* Replay the captured native Tiger server's ARB programs without CGS,
 * IOSurface, guest transport or the backported driver. Generated opaque grid;
 * captured 200x1 LUT and state only. This is not a replacement renderer. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include "../PassthroughOpenGL/arb-defined-temporaries.h"

static int define_z;
static void load_program(GLenum target,const char *state,const char *marker)
{
    const char *start=strstr(state,marker),*end=start?strstr(start,"\nEND"):NULL;
    if(!end){fprintf(stderr,"Missing program %s\n",marker);exit(2);}
    GLuint name=0;glGenProgramsARB(1,&name);glBindProgramARB(target,name);
    size_t length=(size_t)(end+4-start);char *patched=NULL;
    if(define_z==2 && target==GL_FRAGMENT_PROGRAM_ARB){
        size_t transformed=0;patched=DGARBDefineTemporaries(start,length,&transformed);
        if(!patched)exit(2);start=patched;length=transformed;
    }else if(define_z && target==GL_FRAGMENT_PROGRAM_ARB){
        const char *site=strstr(start,"DPH r1.x, r0, p9;");
        if(!site||site>=end)exit(2);
        const char *instruction="MOV r0.z, 0.0;\n";size_t prefix=(size_t)(site-start),added=strlen(instruction);
        patched=malloc(length+added);if(!patched)exit(2);
        memcpy(patched,start,prefix);memcpy(patched+prefix,instruction,added);
        memcpy(patched+prefix+added,site,length-prefix);length+=added;start=patched;
    }
    glProgramStringARB(target,GL_PROGRAM_FORMAT_ASCII_ARB,(GLsizei)length,start);free(patched);
    GLint position=-1;glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB,&position);
    if(position!=-1||glGetError()){fprintf(stderr,"Program error %d: %s\n",(int)position,glGetString(GL_PROGRAM_ERROR_STRING_ARB));exit(1);}
    const char *tag=target==GL_VERTEX_PROGRAM_ARB?"ARB_VERTEX_LOCAL ":"ARB_LOCAL ";
    for(const char *line=state;(line=strstr(line,tag));++line){
        unsigned index=0;GLfloat v[4];
        if(sscanf(line+strlen(tag),"%u = %f,%f,%f,%f",&index,v,v+1,v+2,v+3)==5 && index<96)
            glProgramLocalParameter4fvARB(target,index,v);
    }
    glEnable(target);
}
int main(int argc,char **argv)
{
    if(argc!=4 && !(argc==5&&(!strcmp(argv[4],"--define-z")||!strcmp(argv[4],"--zero-temporaries")))){
        fprintf(stderr,"usage: %s CAPTURED_STATE LUT_PPM NEW_OUTPUT_PPM [--define-z|--zero-temporaries]\n",argv[0]);return 2;}
    define_z=argc==5?(!strcmp(argv[4],"--define-z")?1:2):0;
    alarm(20);
    FILE *file=fopen(argv[1],"rb");if(!file)return 2;
    char state[131072]={0};size_t n=fread(state,1,sizeof(state)-1,file);fclose(file);
    if(!n||n==sizeof(state)-1)return 2;
    file=fopen(argv[2],"rb");if(!file)return 2;
    int lw=0,lh=0,max=0;
    if(fscanf(file,"P6 %d %d %d",&lw,&lh,&max)!=3||lw!=200||lh!=1||max!=255)return 2;
    fgetc(file);unsigned char lut[200*3];if(fread(lut,1,sizeof(lut),file)!=sizeof(lut))return 2;fclose(file);
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAColorSize,24,kCGLPFAAlphaSize,8,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&format,&count)||!format||CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 77;
    fprintf(stderr,"renderer=%s\n",glGetString(GL_RENDERER));
    int w=0,h=0;
    const char *vp=strstr(state,"viewport=");if(!vp||sscanf(vp,"viewport=0,0,%d,%d",&w,&h)!=2||w<1||h<1||w>4096||h>4096)return 2;
    int tw=0,th=0;const char *texture=strstr(state,"TEXTURE unit=1 target=84f5 ");
    const char *size=texture?strstr(texture,"size="):NULL;
    if(!size||sscanf(size,"size=%d,%d",&tw,&th)!=2||tw<w||th<h||tw>4096||th>4096)return 2;
    unsigned char *grid=calloc((size_t)tw*th,4),*pixels=malloc((size_t)w*h*4);if(!grid||!pixels)return 2;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        unsigned char *p=grid+4*((size_t)y*tw+x);
        p[0]=(unsigned char)((x/20)*255/29);p[1]=(unsigned char)((19-y/20)*255/19);
        p[2]=((x/20+y/20)&1)?242:25;p[3]=255;
    }
    GLuint destination=0,fbo=0,images[2]={0};
    glGenTextures(1,&destination);glBindTexture(GL_TEXTURE_2D,destination);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,destination,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)!=GL_FRAMEBUFFER_COMPLETE_EXT)return 1;
    glGenTextures(2,images);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(unsigned i=0;i<2;++i){
        glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,images[i]);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,i?GL_CLAMP_TO_BORDER:GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_T,i?GL_CLAMP_TO_BORDER:GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,i?tw:200,i?th:1,0,i?GL_RGBA:GL_RGB,GL_UNSIGNED_BYTE,i?(void *)grid:(void *)lut);
    }
    GLfloat projection[16];const char *matrix=strstr(state,"MATRIX ba7");if(!matrix)return 2;
    matrix+=strlen("MATRIX ba7");for(unsigned i=0;i<16;++i){char *end=NULL;projection[i]=strtof(matrix,&end);if(end==matrix)return 2;matrix=end;}
    glViewport(0,0,w,h);glMatrixMode(GL_MODELVIEW);glLoadIdentity();glMatrixMode(GL_PROJECTION);glLoadMatrixf(projection);
    load_program(GL_VERTEX_PROGRAM_ARB,state,"!!ARBvp1.0");load_program(GL_FRAGMENT_PROGRAM_ARB,state,"!!ARBfp1.0");
    /* This fixture is the captured602x402 constant-phase owned server view.
     * Derive its global bounds from the actual orthographic matrix. */
    GLfloat left=(-1-projection[12])/projection[0],right=(1-projection[12])/projection[0];
    GLfloat top=(1-projection[13])/projection[5],bottom=(-1-projection[13])/projection[5];
    glBegin(GL_QUADS);
    glMultiTexCoord4f(GL_TEXTURE0,left,top,0,1);glVertex2f(left,top);
    glMultiTexCoord4f(GL_TEXTURE0,right,top,0,1);glVertex2f(right,top);
    glMultiTexCoord4f(GL_TEXTURE0,right,bottom,0,1);glVertex2f(right,bottom);
    glMultiTexCoord4f(GL_TEXTURE0,left,bottom,0,1);glVertex2f(left,bottom);
    glEnd();glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    GLenum error=glGetError();fprintf(stderr,"GL error=%x\n",(unsigned)error);if(error)return 1;
    int output=open(argv[3],O_WRONLY|O_CREAT|O_EXCL,0600);if(output<0)return 2;
    file=fdopen(output,"wb");if(!file){close(output);return 2;}fprintf(file,"P6\n%d %d\n255\n",w,h);
    for(int y=h-1;y>=0;--y)for(int x=0;x<w;++x)fwrite(pixels+4*((size_t)y*w+x),1,3,file);
    fclose(file);free(grid);free(pixels);CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(format);return 0;
}
