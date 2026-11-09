/* Real GL images beyond one wire packet; CPU, PBO, layout and guard pages. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned failures;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);fflush(stdout);failures+=!good;}
static void fill(unsigned char *data,size_t bytes,unsigned seed)
{uint32_t *p=(void *)data;for(size_t i=0;i<bytes/4;++i)p[i]=0xff000000u|(((uint32_t)i*2654435761u+seed)&0xffffffu);}
static void reset(void)
{
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ALIGNMENT,1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,0);glPixelStorei(GL_UNPACK_SKIP_IMAGES,0);
}
static void check_image(GLenum target,int width,int height,int depth,unsigned element,
    const unsigned char *source,size_t offset,size_t row_stride,size_t image_stride,const char *name)
{
    size_t row=(size_t)width*element,bytes=row*height*depth;unsigned char *out=malloc(bytes);
    check(out!=NULL,"readback allocation");if(!out)return;
    memset(out,0,bytes);glGetTexImage(target,0,GL_RGBA,element==16?GL_FLOAT:GL_UNSIGNED_BYTE,out);
    GLenum error=glGetError();int good=!error;
    for(int z=0;good&&z<depth;++z)for(int y=0;good&&y<height;++y)
        good=!memcmp(out+((size_t)z*height+y)*row,source+offset+(size_t)z*image_stride+(size_t)y*row_stride,row);
    printf("readback bytes=%zu error=%x\n",bytes,error);check(good,name);free(out);
}
static void image2d(int pbo)
{
    int width=4096,height=4097;size_t bytes=(size_t)width*height*4;
    unsigned char *data=malloc(bytes);GLuint texture=0,buffer=0;
    check(data!=NULL,"2D input allocation");if(!data)return;fill(data,bytes,17);reset();
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    if(pbo){
        glGenBuffers(1,&buffer);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffer);
        glBufferData(GL_PIXEL_UNPACK_BUFFER,(GLsizeiptr)bytes,NULL,GL_STREAM_DRAW);
        check(!glGetError(),"large allocation-only BufferData");
        for(size_t offset=0;offset<bytes;){size_t count=bytes-offset;if(count>4*1024*1024)count=4*1024*1024;
            glBufferSubData(GL_PIXEL_UNPACK_BUFFER,(GLintptr)offset,(GLsizeiptr)count,data+offset);offset+=count;}
        glFinish();
    }
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,pbo?NULL:data);
    check(!glGetError(),pbo?"large caller-PBO image accepted":"large CPU image accepted");
    GLint bound=-1;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&bound);
    check(bound==(GLint)buffer,"unpack binding preserved");glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    check_image(GL_TEXTURE_2D,width,height,1,4,data,0,width*4,bytes,"large 2D image bytes");
    if(!pbo){fill(data,bytes,79);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,data);
        check(!glGetError(),"large CPU subimage accepted");
        check_image(GL_TEXTURE_2D,width,height,1,4,data,0,width*4,bytes,"large 2D subimage bytes");}
    glDeleteTextures(1,&texture);if(buffer)glDeleteBuffers(1,&buffer);free(data);
}
static void volume(void)
{
    int w=257,h=257,d=65;size_t stride=260*16,image=260*stride,offset=image+stride+16;
    size_t bytes=offset+(d-1)*image+(h-1)*stride+w*16;float *data=malloc(bytes);
    check(data!=NULL,"volume input allocation");if(!data)return;
    for(size_t i=0;i<bytes/4;++i)data[i]=(float)(i%251)*.00390625f;
    reset();GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_3D,texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,260);glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,260);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,1);glPixelStorei(GL_UNPACK_SKIP_PIXELS,1);glPixelStorei(GL_UNPACK_SKIP_IMAGES,1);
    glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA32F_ARB,w,h,d,0,GL_RGBA,GL_FLOAT,data);
    check(!glGetError(),"large padded volume accepted");
    const GLenum keys[]={GL_UNPACK_ROW_LENGTH,GL_UNPACK_IMAGE_HEIGHT,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SKIP_IMAGES};
    const GLint expected[]={260,260,1,1,1};int preserved=1;
    for(unsigned i=0;i<5;++i){GLint state=0;glGetIntegerv(keys[i],&state);if(state!=expected[i])preserved=0;}
    check(preserved&&!glGetError(),"volume layout preserved");
    check_image(GL_TEXTURE_3D,w,h,d,16,(const void *)data,offset,stride,image,"large padded volume bytes");
    reset();glDeleteTextures(1,&texture);free(data);
}
static void guarded(void)
{
    size_t prefix=64*1024*1024,page=(size_t)getpagesize();
    unsigned char *data=mmap(NULL,prefix+page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    check(data!=MAP_FAILED,"guarded input allocation");if(data==MAP_FAILED)return;
    if(mprotect(data+prefix,page,PROT_READ|PROT_WRITE)){check(0,"guarded tail access");munmap(data,prefix+page);return;}
    fill(data+prefix,64,43);reset();GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_1D,texture);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,(GLint)(prefix/4));
    glEnable(0xffffffffu);
    glTexImage1D(GL_TEXTURE_1D,0,GL_RGBA8,16,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    check(glGetError()==GL_INVALID_ENUM&&!glGetError(),"guarded-prefix upload preserves prior error");
    GLint value=-1;glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&value);check(value==(GLint)(prefix/4),"large skip state preserved");
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&value);check(value==0,"guarded upload restores unpack binding");
    check_image(GL_TEXTURE_1D,16,1,1,4,data,prefix,64,64,"guarded-prefix bytes");
    glTexImage1D(GL_TEXTURE_1D,0,GL_NONE,16,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    GLenum error=glGetError();printf("invalid large definition error=%x\n",error);
    check(error!=GL_NO_ERROR&&!glGetError(),"invalid large definition rejected");
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&value);check(value==0,"failed large upload restores unpack binding");
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&value);check(value==(GLint)(prefix/4),"failed large upload restores pixel store");
    check_image(GL_TEXTURE_1D,16,1,1,4,data,prefix,64,64,"failed definition preserves image bytes");
    reset();glDeleteTextures(1,&texture);munmap(data,prefix+page);
}
static void compressed(void)
{
    int w=8192,h=8196;size_t bytes=(size_t)(w/4)*(h/4)*16;
    unsigned char *data=malloc(bytes),*out=malloc(bytes);check(data&&out,"compressed input allocation");
    if(!data||!out){free(data);free(out);return;}
    const unsigned char block[16]={255,128,0,0,0,0,0,0,0,248,224,7,0x44,0x44,0x44,0x44};
    for(size_t i=0;i<bytes;i+=16)memcpy(data+i,block,16);
    reset();GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glCompressedTexImage2D(GL_TEXTURE_2D,0,0x83f3,w,h,0,(GLsizei)bytes,data);
    check(!glGetError(),"large compressed image accepted");
    memset(out,0,bytes);glGetCompressedTexImage(GL_TEXTURE_2D,0,out);
    check(!glGetError()&&!memcmp(data,out,bytes),"large compressed byte-exact readback");
    data[0]=239;glCompressedTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,0x83f3,(GLsizei)bytes,data);
    check(!glGetError(),"large compressed subimage accepted");
    memset(out,0,bytes);glGetCompressedTexImage(GL_TEXTURE_2D,0,out);
    check(!glGetError()&&!memcmp(data,out,bytes),"large compressed subimage bytes");
    glDeleteTextures(1,&texture);free(data);free(out);
}
static void proxies(void)
{
    reset();glTexImage2D(GL_PROXY_TEXTURE_2D,0,GL_RGBA8,4096,4097,0,GL_RGBA,GL_UNSIGNED_BYTE,(void *)1);
    check(!glGetError(),"CPU proxy ignores inaccessible storage");
    GLuint buffer=0;glGenBuffers(1,&buffer);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,buffer);
    glBufferData(GL_PIXEL_UNPACK_BUFFER,4,NULL,GL_STATIC_DRAW);
    glTexImage2D(GL_PROXY_TEXTURE_2D,0,GL_RGBA8,4096,4097,0,GL_RGBA,GL_UNSIGNED_BYTE,(void *)0x10000000);
    check(glGetError()==GL_INVALID_OPERATION,"proxy retains native out-of-range PBO validation");
    glTexImage2D(GL_PROXY_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,(void *)4);
    GLenum error=glGetError();printf("small proxy offset error=%x\n",error);
    check(error==GL_INVALID_OPERATION,"small proxy preserves PBO offset bounds");
    glTexImage2D(GL_PROXY_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    check(!glGetError(),"small proxy accepts in-range PBO offset");
    glTexImage1D(GL_PROXY_TEXTURE_1D,0,GL_RGBA8,1,0,GL_RGBA,GL_UNSIGNED_BYTE,(void *)4);
    check(glGetError()==GL_INVALID_OPERATION,"1D proxy preserves PBO offset bounds");
    glTexImage3D(GL_PROXY_TEXTURE_3D,0,GL_RGBA8,1,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,(void *)4);
    check(glGetError()==GL_INVALID_OPERATION,"3D proxy preserves PBO offset bounds");
    glBufferData(GL_PIXEL_UNPACK_BUFFER,16,NULL,GL_STATIC_DRAW);
    glCompressedTexImage2D(GL_PROXY_TEXTURE_2D,0,0x83f3,4,4,0,16,(void *)16);
    check(glGetError()==GL_INVALID_OPERATION,"compressed proxy preserves PBO offset bounds");
    glCompressedTexImage2D(GL_PROXY_TEXTURE_2D,0,0x83f3,4,4,0,16,NULL);
    check(!glGetError(),"compressed proxy accepts in-range PBO offset");
    glBufferSubData(GL_PIXEL_UNPACK_BUFFER,0,0,(void *)1);
    check(!glGetError(),"zero-size buffer update ignores inaccessible storage");
    reset();glDeleteBuffers(1,&buffer);
}
int main(int argc,char **argv)
{
    int legacy=0;const char *selected=NULL;
    for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--legacy"))legacy=1;
        else if(!strcmp(argv[i],"--case")&&i+1<argc)selected=argv[++i];else return 2;}
    if(selected&&strcmp(selected,"cpu2d")&&strcmp(selected,"pbo2d")&&strcmp(selected,"volume")&&
       strcmp(selected,"guarded")&&strcmp(selected,"compressed")&&strcmp(selected,"proxy"))return 2;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(legacy?kCGLOGLPVersion_Legacy:kCGLOGLPVersion_GL4_Core),0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;CGLDestroyPixelFormat(pf);
    printf("large-input renderer=%s version=%s legacy=%d\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),legacy);fflush(stdout);
    if(!selected||!strcmp(selected,"cpu2d"))image2d(0);
    if(!selected||!strcmp(selected,"pbo2d"))image2d(1);
    if(!selected||!strcmp(selected,"volume"))volume();
    if(!selected||!strcmp(selected,"guarded"))guarded();
    if(!selected||!strcmp(selected,"compressed"))compressed();
    if(!selected||!strcmp(selected,"proxy"))proxies();
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
    printf("large-input failures=%u\n",failures);return failures?1:0;
}
