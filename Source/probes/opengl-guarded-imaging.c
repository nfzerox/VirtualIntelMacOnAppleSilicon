/* Exercise every CPU imaging transfer with inaccessible skipped storage. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned failures;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);fflush(stdout);failures+=!good;}
static void plain(int pack)
{
    glPixelStorei(pack?GL_PACK_ALIGNMENT:GL_UNPACK_ALIGNMENT,1);
    glPixelStorei(pack?GL_PACK_ROW_LENGTH:GL_UNPACK_ROW_LENGTH,0);
    glPixelStorei(pack?GL_PACK_SKIP_ROWS:GL_UNPACK_SKIP_ROWS,0);
    glPixelStorei(pack?GL_PACK_SKIP_PIXELS:GL_UNPACK_SKIP_PIXELS,0);
    glPixelStorei(pack?GL_PACK_ROW_BYTES_APPLE:GL_UNPACK_ROW_BYTES_APPLE,0);
}
static void sparse(int pack,size_t prefix,unsigned pixel_bytes,int rowbytes)
{
    glPixelStorei(pack?GL_PACK_ALIGNMENT:GL_UNPACK_ALIGNMENT,8);
    glPixelStorei(pack?GL_PACK_ROW_LENGTH:GL_UNPACK_ROW_LENGTH,8);
    glPixelStorei(pack?GL_PACK_SKIP_ROWS:GL_UNPACK_SKIP_ROWS,1);
    glPixelStorei(pack?GL_PACK_SKIP_PIXELS:GL_UNPACK_SKIP_PIXELS,(GLint)(prefix/pixel_bytes));
    glPixelStorei(pack?GL_PACK_ROW_BYTES_APPLE:GL_UNPACK_ROW_BYTES_APPLE,rowbytes);
}
static void state(int pack,size_t prefix,unsigned pixel_bytes,int rowbytes)
{
    GLint alignment=0,row=0,skiprows=0,pixels=0,bytes=0,binding=-1;
    glGetIntegerv(pack?GL_PACK_ALIGNMENT:GL_UNPACK_ALIGNMENT,&alignment);
    glGetIntegerv(pack?GL_PACK_ROW_LENGTH:GL_UNPACK_ROW_LENGTH,&row);
    glGetIntegerv(pack?GL_PACK_SKIP_ROWS:GL_UNPACK_SKIP_ROWS,&skiprows);
    glGetIntegerv(pack?GL_PACK_SKIP_PIXELS:GL_UNPACK_SKIP_PIXELS,&pixels);
    glGetIntegerv(pack?GL_PACK_ROW_BYTES_APPLE:GL_UNPACK_ROW_BYTES_APPLE,&bytes);
    glGetIntegerv(pack?GL_PIXEL_PACK_BUFFER_BINDING:GL_PIXEL_UNPACK_BUFFER_BINDING,&binding);
    check(alignment==8&&row==8&&skiprows==1&&pixels==(GLint)(prefix/pixel_bytes)&&bytes==rowbytes&&
        !binding&&!glGetError(),pack?"imaging pack state preserved":"imaging unpack state preserved");
}
static int same_page(const unsigned char *actual,const unsigned char want[512],size_t page)
{
    if(memcmp(actual,want,512))return 0;
    for(size_t i=512;i<page;++i)if(actual[i]!=0xa5)return 0;
    return 1;
}
int main(int argc,char **argv)
{
    int large=argc==2&&!strcmp(argv[1],"--large"),output_only=argc==2&&!strcmp(argv[1],"--output-only");
    if(argc>1&&!large&&!output_only)return 2;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    size_t page=(size_t)getpagesize(),prefix=large?64u*1024u*1024u:page;
    unsigned char *a=mmap(NULL,prefix+page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    unsigned char *b=mmap(NULL,prefix+page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(a==MAP_FAILED||b==MAP_FAILED||mprotect(a+prefix,page,PROT_READ|PROT_WRITE)||
       mprotect(b+prefix,page,PROT_READ|PROT_WRITE))return 2;
    printf("protected-prefix=%lu\n",(unsigned long)prefix);
    unsigned char want[512],observed[128],reference[128];
    const unsigned char colors[]={255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
    plain(0);plain(1);glColorTable(GL_COLOR_TABLE,GL_RGBA8,4,GL_RGBA,GL_UNSIGNED_BYTE,colors);
    check(!glGetError(),"ordinary imaging table control");
    if(output_only){
        memset(a+prefix,0xa5,page);memset(want,0xa5,sizeof(want));memcpy(want+32,colors,sizeof(colors));
        sparse(1,prefix,4,0);glGetColorTable(GL_COLOR_TABLE,GL_RGBA,GL_UNSIGNED_BYTE,a);
        check(!glGetError()&&same_page(a+prefix,want,page),"guarded table output only");
        goto done;
    }
    memset(a+prefix,0xa5,page);memcpy(a+prefix+32,colors,sizeof(colors));sparse(0,prefix,4,0);
    glColorTable(GL_COLOR_TABLE,GL_RGBA8,4,GL_RGBA,GL_UNSIGNED_BYTE,a);GLenum error=glGetError();
    state(0,prefix,4,0);plain(0);glGetColorTable(GL_COLOR_TABLE,GL_RGBA,GL_UNSIGNED_BYTE,observed);
    check(!error&&!glGetError()&&!memcmp(colors,observed,sizeof(colors)),"guarded color-table input");
    memset(a+prefix,0xa5,page);memset(want,0xa5,sizeof(want));memcpy(want+32,colors,sizeof(colors));sparse(1,prefix,4,0);
    glGetColorTable(GL_COLOR_TABLE,GL_RGBA,GL_UNSIGNED_BYTE,a);
    check(!glGetError()&&same_page(a+prefix,want,page),"guarded color-table output");state(1,prefix,4,0);plain(1);
    const GLenum extra_tables[]={GL_POST_CONVOLUTION_COLOR_TABLE,GL_POST_COLOR_MATRIX_COLOR_TABLE};
    for(unsigned i=0;i<2;++i){
        memcpy(a+prefix+32,colors,sizeof(colors));sparse(0,prefix,4,0);
        glColorTable(extra_tables[i],GL_RGBA8,4,GL_RGBA,GL_UNSIGNED_BYTE,a);error=glGetError();plain(0);
        glGetColorTable(extra_tables[i],GL_RGBA,GL_UNSIGNED_BYTE,observed);
        check(!error&&!glGetError()&&!memcmp(observed,colors,sizeof(colors)),"guarded post-imaging color-table input");
        memset(a+prefix,0xa5,page);memset(want,0xa5,sizeof(want));memcpy(want+32,colors,sizeof(colors));
        sparse(1,prefix,4,0);glGetColorTable(extra_tables[i],GL_RGBA,GL_UNSIGNED_BYTE,a);
        check(!glGetError()&&same_page(a+prefix,want,page),"guarded post-imaging color-table output");plain(1);
    }
    const unsigned char replacement[]={0,0,0,255,255,255,0,255};unsigned char updated[16];
    memcpy(updated,colors,16);memcpy(updated+4,replacement,8);
    memcpy(a+prefix+32,replacement,8);sparse(0,prefix,4,0);
    glColorSubTable(GL_COLOR_TABLE,1,2,GL_RGBA,GL_UNSIGNED_BYTE,a);error=glGetError();plain(0);
    glGetColorTable(GL_COLOR_TABLE,GL_RGBA,GL_UNSIGNED_BYTE,observed);
    check(!error&&!glGetError()&&!memcmp(observed,updated,16),"guarded color-subtable input");
    sparse(0,prefix,4,0);memcpy(a+prefix+32,colors,sizeof(colors));
    glNewList(41,GL_COMPILE);glColorTable(GL_COLOR_TABLE,GL_RGBA8,4,GL_RGBA,GL_UNSIGNED_BYTE,a);glEndList();
    check(!glGetError(),"guarded imaging input compiles into display list");
    memset(a+prefix+32,0,sizeof(colors));plain(0);glCallList(41);
    glGetColorTable(GL_COLOR_TABLE,GL_RGBA,GL_UNSIGNED_BYTE,observed);
    check(!glGetError()&&!memcmp(observed,colors,16),"imaging list retains source bytes before mutation");
    glDeleteLists(41,1);glColorSubTable(GL_COLOR_TABLE,1,2,GL_RGBA,GL_UNSIGNED_BYTE,replacement);
    const GLfloat line[]={0.25f,0.75f},filter[]={0.25f,0.5f,0.75f,1.0f},row[]={0.5f,1.0f},column[]={0.25f,0.5f,0.75f};
    memcpy(a+prefix+32,line,sizeof(line));sparse(0,prefix,4,0);
    glConvolutionFilter1D(GL_CONVOLUTION_1D,GL_LUMINANCE,2,GL_LUMINANCE,GL_FLOAT,a);
    error=glGetError();state(0,prefix,4,0);plain(0);
    glGetConvolutionFilter(GL_CONVOLUTION_1D,GL_LUMINANCE,GL_FLOAT,observed);
    check(!error&&!glGetError()&&!memcmp(observed,line,sizeof(line)),"guarded 1D convolution input");
    memset(a+prefix,0xa5,page);memset(want,0xa5,sizeof(want));memcpy(want+32,line,sizeof(line));
    sparse(1,prefix,4,0);glGetConvolutionFilter(GL_CONVOLUTION_1D,GL_LUMINANCE,GL_FLOAT,a);
    check(!glGetError()&&same_page(a+prefix,want,page),"guarded 1D convolution output");plain(1);
    for(int explicit_stride=0;explicit_stride<2;++explicit_stride){unsigned stride=explicit_stride?64:32;
        memset(a+prefix,0xa5,page);memcpy(a+prefix+stride,filter,8);memcpy(a+prefix+stride*2,filter+2,8);
        sparse(0,prefix,4,explicit_stride?64:0);
        glConvolutionFilter2D(GL_CONVOLUTION_2D,GL_LUMINANCE,2,2,GL_LUMINANCE,GL_FLOAT,a);
        error=glGetError();state(0,prefix,4,explicit_stride?64:0);plain(0);
        glGetConvolutionFilter(GL_CONVOLUTION_2D,GL_LUMINANCE,GL_FLOAT,observed);
        check(!error&&!glGetError()&&!memcmp(observed,filter,sizeof(filter)),"guarded 2D convolution input");
        memset(a+prefix,0xa5,page);memset(want,0xa5,sizeof(want));memcpy(want+stride,filter,8);memcpy(want+stride*2,filter+2,8);
        sparse(1,prefix,4,explicit_stride?64:0);glGetConvolutionFilter(GL_CONVOLUTION_2D,GL_LUMINANCE,GL_FLOAT,a);
        check(!glGetError()&&same_page(a+prefix,want,page),"guarded convolution output and row padding");state(1,prefix,4,explicit_stride?64:0);plain(1);
    }
    size_t gap_stride=large?prefix*2:page*2;
    unsigned char *gap=mmap(NULL,gap_stride+page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(gap==MAP_FAILED||mprotect(gap,page,PROT_READ|PROT_WRITE)||mprotect(gap+gap_stride,page,PROT_READ|PROT_WRITE))return 2;
    memcpy(gap,filter,8);memcpy(gap+gap_stride,filter+2,8);glPixelStorei(GL_UNPACK_ROW_BYTES_APPLE,(GLint)gap_stride);
    glConvolutionFilter2D(GL_CONVOLUTION_2D,GL_LUMINANCE,2,2,GL_LUMINANCE,GL_FLOAT,gap);
    error=glGetError();plain(0);glGetConvolutionFilter(GL_CONVOLUTION_2D,GL_LUMINANCE,GL_FLOAT,observed);
    check(!error&&!glGetError()&&!memcmp(observed,filter,sizeof(filter)),"imaging input skips inaccessible inter-row padding");
    memset(gap,0xa5,page);memset(gap+gap_stride,0xa5,page);glPixelStorei(GL_PACK_ROW_BYTES_APPLE,(GLint)gap_stride);
    glGetConvolutionFilter(GL_CONVOLUTION_2D,GL_LUMINANCE,GL_FLOAT,gap);
    check(!glGetError()&&!memcmp(gap,filter,8)&&!memcmp(gap+gap_stride,filter+2,8),"imaging output skips inaccessible inter-row padding");
    plain(1);munmap(gap,gap_stride+page);
    const GLfloat overlapping[]={0.25f,0.5f,0.75f},expanded[]={0.25f,0.5f,0.5f,0.75f};
    memcpy(a+prefix,overlapping,sizeof(overlapping));glPixelStorei(GL_UNPACK_ROW_BYTES_APPLE,4);
    glConvolutionFilter2D(GL_CONVOLUTION_2D,GL_LUMINANCE,2,2,GL_LUMINANCE,GL_FLOAT,a+prefix);
    error=glGetError();plain(0);glGetConvolutionFilter(GL_CONVOLUTION_2D,GL_LUMINANCE,GL_FLOAT,observed);
    check(!error&&!glGetError()&&!memcmp(observed,expanded,sizeof(expanded)),"imaging overlapping input rows retain logical coefficients");
    glConvolutionFilter2D(GL_CONVOLUTION_2D,GL_LUMINANCE,2,2,GL_LUMINANCE,GL_FLOAT,filter);
    memset(a+prefix,0xa5,page);glPixelStorei(GL_PACK_ROW_BYTES_APPLE,4);
    glGetConvolutionFilter(GL_CONVOLUTION_2D,GL_LUMINANCE,GL_FLOAT,a+prefix);
    const GLfloat overlap_result[]={0.25f,0.75f,1.0f};memset(want,0xa5,sizeof(want));memcpy(want,overlap_result,sizeof(overlap_result));
    check(!glGetError()&&same_page(a+prefix,want,page),"imaging overlapping output rows retain native write ordering");plain(1);
    memcpy(a+prefix+32,row,sizeof(row));memcpy(b+prefix+32,column,sizeof(column));sparse(0,prefix,4,0);
    glSeparableFilter2D(GL_SEPARABLE_2D,GL_LUMINANCE,2,3,GL_LUMINANCE,GL_FLOAT,a,b);
    error=glGetError();state(0,prefix,4,0);plain(0);
    glGetSeparableFilter(GL_SEPARABLE_2D,GL_LUMINANCE,GL_FLOAT,observed,reference,(void *)1);
    check(!error&&!glGetError()&&!memcmp(observed,row,sizeof(row))&&!memcmp(reference,column,sizeof(column)),"guarded separable inputs");
    memset(a+prefix,0xa5,page);memset(b+prefix,0xa5,page);sparse(1,prefix,4,0);
    glGetSeparableFilter(GL_SEPARABLE_2D,GL_LUMINANCE,GL_FLOAT,a,b,(void *)1);
    memset(want,0xa5,sizeof(want));memcpy(want+32,row,sizeof(row));int valid=same_page(a+prefix,want,page);
    memset(want,0xa5,sizeof(want));memcpy(want+32,column,sizeof(column));
    check(valid&&!glGetError()&&same_page(b+prefix,want,page),"guarded separable outputs and ignored span");plain(1);
    for(unsigned alias=0;alias<3;++alias){unsigned r=alias==2?4:0,col=alias==1?4:0;
        memset(reference,0xa5,sizeof(reference));glGetSeparableFilter(GL_SEPARABLE_2D,GL_LUMINANCE,GL_FLOAT,reference+r,reference+col,(void *)1);
        check(!glGetError(),"ordinary aliased separable control");
        memset(a+prefix,0xa5,page);sparse(1,prefix,4,0);
        glGetSeparableFilter(GL_SEPARABLE_2D,GL_LUMINANCE,GL_FLOAT,a+r,a+col,(void *)1);
        memset(want,0xa5,sizeof(want));memcpy(want+32,reference,16);
        check(!glGetError()&&same_page(a+prefix,want,page),"guarded separable preserves actual output aliasing");state(1,prefix,4,0);plain(1);
    }
    sparse(1,prefix,4,0);memset(a+prefix,0xa5,page);memset(want,0xa5,sizeof(want));
    memcpy(want+32,filter,8);memcpy(want+64,filter+2,8);glEnable(0xffffffffu);
    glGetConvolutionFilter(GL_CONVOLUTION_2D,GL_LUMINANCE,GL_FLOAT,a);
    check(glGetError()==GL_INVALID_ENUM&&!glGetError()&&same_page(a+prefix,want,page),"guarded imaging read retains prior error and pixels");plain(1);
    plain(0);glColorTable(GL_COLOR_TABLE,0xffffffffu,4,GL_RGBA,GL_UNSIGNED_BYTE,colors);GLenum expected_error=glGetError();
    sparse(0,prefix,4,0);memcpy(a+prefix+32,colors,sizeof(colors));
    glColorTable(GL_COLOR_TABLE,0xffffffffu,4,GL_RGBA,GL_UNSIGNED_BYTE,a);
    check(expected_error&&glGetError()==expected_error&&!glGetError(),"guarded invalid imaging call preserves native error");state(0,prefix,4,0);plain(0);
    glGetColorTable(GL_COLOR_TABLE,GL_RGBA,GL_UNSIGNED_BYTE,observed);
    check(!glGetError()&&!memcmp(observed,updated,16),"failed compact imaging call preserves native table");
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,1,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"imaging counter framebuffer");
    glHistogram(GL_HISTOGRAM,4,GL_RGBA,GL_FALSE);glMinmax(GL_MINMAX,GL_RGBA,GL_FALSE);
    glEnable(GL_HISTOGRAM);glEnable(GL_MINMAX);glViewport(0,0,4,1);glRasterPos2f(-1,-1);
    glDrawPixels(4,1,GL_RGBA,GL_UNSIGNED_BYTE,colors);glDisable(GL_HISTOGRAM);glDisable(GL_MINMAX);
    check(!glGetError(),"native imaging counters receive real raster data");
    glGetHistogram(GL_HISTOGRAM,GL_FALSE,GL_RGBA,GL_UNSIGNED_INT,reference);
    unsigned sum=0;for(unsigned i=0;i<16;++i){GLuint value;memcpy(&value,reference+i*4,4);sum+=value;}
    check(sum>0&&!glGetError(),"ordinary nonempty histogram control");
    memset(a+prefix,0xa5,page);memset(want,0xa5,sizeof(want));memcpy(want,reference,64);sparse(1,prefix,16,0);
    glGetHistogram(GL_HISTOGRAM,GL_TRUE,GL_RGBA,GL_UNSIGNED_INT,a);
    check(!glGetError()&&same_page(a+prefix,want,page),"guarded histogram ignores skip rows and resets once");state(1,prefix,16,0);plain(1);
    memset(observed,0xa5,sizeof(observed));glGetHistogram(GL_HISTOGRAM,GL_FALSE,GL_RGBA,GL_UNSIGNED_INT,observed);
    unsigned char zero[64]={0};check(!glGetError()&&!memcmp(observed,zero,64),"histogram reset state is native");
    glGetMinmax(GL_MINMAX,GL_FALSE,GL_RGBA,GL_UNSIGNED_BYTE,reference);
    memset(a+prefix,0xa5,page);memset(want,0xa5,sizeof(want));memcpy(want,reference,8);sparse(1,prefix,4,64);
    glGetMinmax(GL_MINMAX,GL_TRUE,GL_RGBA,GL_UNSIGNED_BYTE,a+prefix);
    check(!glGetError()&&same_page(a+prefix,want,page),"minmax ignores pack spacing without extra data transfer");state(1,prefix,4,64);plain(1);
    glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);
done:
    munmap(a,prefix+page);munmap(b,prefix+page);CGLSetCurrentContext(NULL);CGLDestroyContext(c);
    printf("guarded-imaging failures=%u\n",failures);return failures?1:0;
}
