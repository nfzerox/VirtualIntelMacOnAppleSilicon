/* Native GL owns invalid-input timing, including deferred list playback.
 * The deliberately inaccessible CPU input (or unused PBO offset) must never
 * be read for these calls. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static unsigned checks,failures;
static int pbo_input;
static void check(int good,const char *what)
{++checks;failures+=!good;printf("%s %s\n",good?"PASS":"FAIL",what);fflush(stdout);}
static void upload(unsigned dimensions,int subimage,GLenum format,GLenum type,unsigned bad_axis,const void *pixels)
{
    GLsizei width=bad_axis==1?-1:4,height=bad_axis==2?-1:4,depth=bad_axis==3?-1:4;
    if(dimensions==1){
        if(subimage)glTexSubImage1D(GL_TEXTURE_1D,0,0,width,format,type,pixels);
        else glTexImage1D(GL_TEXTURE_1D,0,GL_RGBA8,width,0,format,type,pixels);
    }else if(dimensions==2){
        if(subimage)glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,format,type,pixels);
        else glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,format,type,pixels);
    }else{
        if(subimage)glTexSubImage3D(GL_TEXTURE_3D,0,0,0,0,width,height,depth,format,type,pixels);
        else glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA8,width,height,depth,0,format,type,pixels);
    }
}
static void run(unsigned dimensions,int subimage,unsigned kind,GLenum mode)
{
    const GLenum targets[]={GL_TEXTURE_1D,GL_TEXTURE_2D,GL_TEXTURE_3D};
    GLenum target=targets[dimensions-1];GLuint texture=0,list=0,pbo=0;
    unsigned char input[256],output[256];memset(input,113,sizeof(input));
    glGenTextures(1,&texture);glBindTexture(target,texture);
    upload(dimensions,0,GL_RGBA,GL_UNSIGNED_BYTE,0,input);
    check(!glGetError(),"initial texture accepted");
    if(pbo_input){glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);
        glBufferData(GL_PIXEL_UNPACK_BUFFER,sizeof(input),input,GL_STATIC_DRAW);}
    if(mode){list=glGenLists(1);glNewList(list,mode);}
    upload(dimensions,subimage,kind==0?0xdead:GL_RGBA,kind==1?0xdead:GL_UNSIGNED_BYTE,
        kind>=2?kind-1:0,(const void *)(uintptr_t)1);
    if(mode)glEndList();
    GLenum error=glGetError();
    GLenum expected=kind>=2?GL_INVALID_VALUE:GL_INVALID_ENUM;
    printf("dimension=%u subimage=%d kind=%u mode=%x initial-error=%x\n",dimensions,subimage,kind,mode,error);
    check(error==(mode==GL_COMPILE?GL_NO_ERROR:expected),"native initial/compile error timing");
    if(mode){glCallList(list);error=glGetError();
        check(error==expected,"native list playback reports the original error");}
    if(pbo_input)glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    memset(output,0,sizeof(output));glGetTexImage(target,0,GL_RGBA,GL_UNSIGNED_BYTE,output);
    unsigned bytes=dimensions==1?16:dimensions==2?64:256;
    check(!glGetError()&&!memcmp(output,input,bytes),"rejected upload preserves texture pixels");
    if(list)glDeleteLists(list,1);if(pbo)glDeleteBuffers(1,&pbo);glDeleteTextures(1,&texture);
    check(!glGetError(),"rejected command leaves connection and cleanup usable");
}
int main(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"--pbo"))pbo_input=1;else if(argc!=1)return 2;
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);
    printf("renderer=%s version=%s input=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),pbo_input?"pbo":"cpu");
    if(pbo_input){
        unsigned major=0,minor=0;sscanf((const char *)glGetString(GL_VERSION),"%u.%u",&major,&minor);
        const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
        if((major<2||(major==2&&minor<1))&&(!extensions||!strstr(extensions,"GL_ARB_pixel_buffer_object"))){
            puts("UNSUPPORTED pixel buffer objects");CGLSetCurrentContext(NULL);CGLDestroyContext(context);return 77;}
    }
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    const GLenum modes[]={0,GL_COMPILE,GL_COMPILE_AND_EXECUTE};
    for(unsigned dimensions=1;dimensions<=3;++dimensions)for(int subimage=0;subimage<2;++subimage)
        for(unsigned kind=0;kind<2+dimensions;++kind)for(unsigned mode=0;mode<3;++mode)
            run(dimensions,subimage,kind,modes[mode]);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("texture-list-errors checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
