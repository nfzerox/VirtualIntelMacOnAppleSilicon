/* Compare real native texture conversions with sparse client memory. */
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
static int padded_images;
static void check(int good,const char *label)
{printf("%s %s\n",good?"PASS":"FAIL",label);fflush(stdout);failures+=!good;}
static const GLenum unpack_keys[]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,
    GL_UNPACK_SKIP_PIXELS,GL_UNPACK_ROW_BYTES_APPLE,GL_UNPACK_IMAGE_HEIGHT,GL_UNPACK_SKIP_IMAGES,GL_UNPACK_SWAP_BYTES};
static void store(const GLint *values)
{for(unsigned i=0;i<8;++i)glPixelStorei(unpack_keys[i],values[i]);}
static void upload(int dimensions,int sub,GLenum format,GLenum type,const void *data)
{
    GLenum target=dimensions==1?GL_TEXTURE_1D:dimensions==2?GL_TEXTURE_2D:GL_TEXTURE_3D;
    if(dimensions==1){
        if(sub)glTexSubImage1D(target,0,0,3,format,type,data);
        else glTexImage1D(target,0,GL_RGBA8,3,0,format,type,data);
    }else if(dimensions==2){
        if(sub)glTexSubImage2D(target,0,0,0,3,2,format,type,data);
        else glTexImage2D(target,0,GL_RGBA8,3,2,0,format,type,data);
    }else {
        if(sub)glTexSubImage3D(target,0,0,0,0,3,2,2,format,type,data);
        else glTexImage3D(target,0,GL_RGBA8,3,2,2,0,format,type,data);
    }
}
static void pixels(int dimensions,unsigned char *out)
{glGetTexImage(dimensions==1?GL_TEXTURE_1D:dimensions==2?GL_TEXTURE_2D:GL_TEXTURE_3D,0,GL_RGBA,GL_UNSIGNED_BYTE,out);}
static void fixture(int dimensions,GLenum format,GLenum type,unsigned bpp,int swap,size_t prefix,int overlap)
{
    char label[160];
    unsigned rows=dimensions==1?1:2,images=dimensions==3?2:1;
    size_t page=(size_t)getpagesize(),stride=overlap?bpp:page*2,image_stride=stride*(padded_images?3:2);
    size_t skip_pixels=(prefix+bpp-1)/bpp+1;
    size_t offset=skip_pixels*bpp+stride+(dimensions==3?image_stride:0);
    size_t extent=offset+(images-1)*image_stride+(rows-1)*stride+3*bpp;
    size_t length=(extent+page-1)/page*page;
    unsigned char *guard=mmap(NULL,length,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(guard==MAP_FAILED){check(0,"protected texture allocation");return;}
    unsigned char tight[192]={0},expected[64]={0},actual[64]={0};
    size_t row_bytes=3*bpp;
    for(unsigned z=0;z<images;++z)for(unsigned y=0;y<rows;++y){
        size_t start=offset+z*image_stride+y*stride;
        size_t low=start/page*page,high=(start+row_bytes+page-1)/page*page;
        if(mprotect(guard+low,high-low,PROT_READ|PROT_WRITE)){check(0,"protected texture pages");munmap(guard,length);return;}
        for(unsigned x=0;x<row_bytes;++x)guard[start+x]=(unsigned char)(x*17+y*31+z*53+9);
        if(type==GL_FLOAT)for(unsigned x=0;x<12;++x){float f=(float)(x+y+z+1)/16;memcpy(guard+start+x*4,&f,4);}
    }
    /* Overlapping source rows must be sampled after every source write. */
    for(unsigned z=0;z<images;++z)for(unsigned y=0;y<rows;++y)
        memcpy(tight+(z*rows+y)*row_bytes,guard+offset+z*image_stride+y*stride,row_bytes);
    GLint original[]={8,7,1,(GLint)skip_pixels,(GLint)stride,padded_images?3:2,1,swap};
    GLint packed[]={1,0,0,0,0,0,0,swap};
    GLuint texture=0;glGenTextures(1,&texture);
    GLenum target=dimensions==1?GL_TEXTURE_1D:dimensions==2?GL_TEXTURE_2D:GL_TEXTURE_3D;
    glBindTexture(target,texture);store(packed);upload(dimensions,0,format,type,tight);pixels(dimensions,expected);
    snprintf(label,sizeof(label),"%dD native conversion control format=%x type=%x swap=%d overlap=%d",dimensions,format,type,swap,overlap);
    check(!glGetError(),label);
    for(int sub=0;sub<2;++sub){
        if(sub){store(packed);unsigned char zeros[192]={0};upload(dimensions,0,format,type,zeros);}
        store(original);upload(dimensions,sub,format,type,guard);
        GLenum error=glGetError();pixels(dimensions,actual);
        snprintf(label,sizeof(label),"%dD guarded %s format=%x type=%x swap=%d overlap=%d",dimensions,sub?"update":"definition",format,type,swap,overlap);
        check(!error&&!glGetError()&&!memcmp(expected,actual,3*rows*images*4),label);
        int preserved=1;for(unsigned i=0;i<8;++i){GLint value=-1;glGetIntegerv(unpack_keys[i],&value);preserved&=value==original[i];}
        check(preserved&&!glGetError(),"texture input preserves all unpack state");
    }
    if(dimensions==2 && type==GL_UNSIGNED_BYTE && format==GL_RGBA && !overlap){
        store(original);glEnable(0xffffffffu);upload(dimensions,0,format,type,guard);
        check(glGetError()==GL_INVALID_ENUM&&!glGetError(),"guarded texture preserves prior error");
        glTexImage2D(target,0,GL_NONE,3,2,0,format,type,guard);
        GLenum error=glGetError();pixels(dimensions,actual);
        check(error!=GL_NO_ERROR&&!glGetError()&&!memcmp(expected,actual,3*rows*4),
            "invalid guarded definition preserves existing texture");
        int preserved=1;for(unsigned i=0;i<8;++i){GLint value=-1;glGetIntegerv(unpack_keys[i],&value);preserved&=value==original[i];}
        check(preserved&&!glGetError(),"native texture error preserves unpack state");
    }
    if(dimensions==2 && type==GL_UNSIGNED_BYTE && format==GL_RGBA && !overlap &&
       strncmp((const char *)glGetString(GL_VERSION),"4.",2)){
        store(original);GLuint list=glGenLists(1);glNewList(list,GL_COMPILE);upload(dimensions,0,format,type,guard);glEndList();
        GLenum error=glGetError();
        for(unsigned y=0;y<rows;++y)memset(guard+offset+y*stride,0,row_bytes);
        unsigned char zeros[192]={0};store(packed);upload(dimensions,0,format,type,zeros);glCallList(list);pixels(dimensions,actual);
        check(!error&&!glGetError()&&!memcmp(expected,actual,3*rows*4),"texture display list captures guarded source");
        glDeleteLists(list,1);
    }
    store(packed);glDeleteTextures(1,&texture);munmap(guard,length);
}
static void bitmap_fixture(int dimensions,int lsb,size_t prefix)
{
    size_t page=(size_t)getpagesize(),stride=page*2;
    unsigned rows=dimensions==1?1:2,images=dimensions==3?2:1;
    size_t offset=prefix+stride+(dimensions==3?2*stride:0);
    size_t length=offset+(images-1)*2*stride+(rows-1)*stride+page;
    unsigned char *guard=mmap(NULL,length,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(guard==MAP_FAILED){check(0,"bitmap texture allocation");return;}
    unsigned char tight[8]={0},expected[160]={0},actual[160]={0};
    for(unsigned z=0;z<images;++z)for(unsigned y=0;y<rows;++y){
        size_t start=offset+z*2*stride+y*stride;
        if(mprotect(guard+start,page,PROT_READ|PROT_WRITE)){check(0,"bitmap texture page access");munmap(guard,length);return;}
        guard[start]=(unsigned char)(0xd3+y*7+z*13);guard[start+1]=0x6b;
        memcpy(tight+(z*rows+y)*2,guard+start,2);
    }
    GLfloat ramp[]={0,1},ones[]={1,1};
    glPixelMapfv(GL_PIXEL_MAP_I_TO_R,2,ramp);glPixelMapfv(GL_PIXEL_MAP_I_TO_G,2,ramp);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_B,2,ramp);glPixelMapfv(GL_PIXEL_MAP_I_TO_A,2,ones);
    GLint packed[]={1,0,0,3,2,0,0,0};
    GLint original[]={8,17,1,(GLint)(prefix*8+3),(GLint)stride,2,1,0};
    GLenum target=dimensions==1?GL_TEXTURE_1D:dimensions==2?GL_TEXTURE_2D:GL_TEXTURE_3D;
    GLuint texture=0;glGenTextures(1,&texture);glBindTexture(target,texture);glPixelStorei(GL_UNPACK_LSB_FIRST,lsb);
    for(int pass=0;pass<3;++pass){
        if(pass==2){
            const unsigned char zero[8]={0};store(packed);
            if(dimensions==1)glTexImage1D(target,0,GL_RGBA8,10,0,GL_COLOR_INDEX,GL_BITMAP,zero);
            else if(dimensions==2)glTexImage2D(target,0,GL_RGBA8,10,2,0,GL_COLOR_INDEX,GL_BITMAP,zero);
            else glTexImage3D(target,0,GL_RGBA8,10,2,2,0,GL_COLOR_INDEX,GL_BITMAP,zero);
        }
        store(pass?original:packed);const void *input=pass?guard:tight;
        if(dimensions==1){
            if(pass==2)glTexSubImage1D(target,0,0,10,GL_COLOR_INDEX,GL_BITMAP,input);
            else glTexImage1D(target,0,GL_RGBA8,10,0,GL_COLOR_INDEX,GL_BITMAP,input);
        }else if(dimensions==2){
            if(pass==2)glTexSubImage2D(target,0,0,0,10,2,GL_COLOR_INDEX,GL_BITMAP,input);
            else glTexImage2D(target,0,GL_RGBA8,10,2,0,GL_COLOR_INDEX,GL_BITMAP,input);
        }else {
            if(pass==2)glTexSubImage3D(target,0,0,0,0,10,2,2,GL_COLOR_INDEX,GL_BITMAP,input);
            else glTexImage3D(target,0,GL_RGBA8,10,2,2,0,GL_COLOR_INDEX,GL_BITMAP,input);
        }
        GLenum error=glGetError();glGetTexImage(target,0,GL_RGBA,GL_UNSIGNED_BYTE,pass?actual:expected);
        int good=!error&&!glGetError();if(pass)good&=!memcmp(expected,actual,10*rows*images*4);
        else {unsigned white=0,black=0;for(unsigned i=0;i<10*rows*images;++i){white+=expected[i*4]==255;black+=expected[i*4]==0;}good&=white&&black;}
        char label[100];snprintf(label,sizeof(label),"%dD bitmap texture %s lsb=%d",dimensions,pass==0?"control":pass==1?"definition":"update",lsb);check(good,label);
        if(pass){GLint value=0;glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&value);check(value==original[3]&&!glGetError(),"bitmap texture bit addressing restored");}
    }
    store(packed);glPixelStorei(GL_UNPACK_LSB_FIRST,0);glDeleteTextures(1,&texture);munmap(guard,length);
}
int main(int argc,char **argv)
{
    int core=0,large=0,bitmap=0;for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--core"))core=1;else if(!strcmp(argv[i],"--large"))large=1;else if(!strcmp(argv[i],"--padded-images"))padded_images=1;else if(!strcmp(argv[i],"--bitmap-only"))bitmap=1;else return 2;}
    if(core&&bitmap)return 2;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(core?kCGLOGLPVersion_GL4_Core:kCGLOGLPVersion_Legacy),0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;CGLDestroyPixelFormat(pf);
    printf("renderer=%s version=%s prefix=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),large?"64MiB":"page");
    size_t prefix=large?64u*1024u*1024u:(size_t)getpagesize();
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    for(int d=1;d<=3;++d)if(bitmap){for(int lsb=0;lsb<2;++lsb)bitmap_fixture(d,lsb,prefix);}else {
        fixture(d,GL_RGBA,GL_UNSIGNED_BYTE,4,0,prefix,0);
        fixture(d,GL_RGB,GL_UNSIGNED_BYTE,3,0,prefix,0);
        fixture(d,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,4,0,prefix,0);
        fixture(d,GL_RGBA,GL_UNSIGNED_SHORT,8,0,prefix,0);
        fixture(d,GL_RGBA,GL_UNSIGNED_SHORT,8,1,prefix,0);
        fixture(d,GL_RGBA,GL_FLOAT,16,0,prefix,0);
        fixture(d,GL_RGBA,GL_UNSIGNED_BYTE,4,0,prefix,1);
    }
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
    printf("guarded-texture failures=%u\n",failures);return failures?1:0;
}
