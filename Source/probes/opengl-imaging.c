/* Compare native imaging transfer shapes and buffer semantics. */
#ifdef VIM_IMAGING_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#endif
#include <stdio.h>
#include <string.h>

static void touched(const char *name,const GLubyte *bytes,unsigned length)
{
    unsigned first=length,last=0,count=0;
    for(unsigned i=0;i<length;++i)if(bytes[i]!=0xa5){if(first==length)first=i;last=i;++count;}
    printf("imaging %s first=%u last=%u changed=%u error=%04x\n",name,first,last,count,glGetError());
}

int main(void)
{
#ifdef VIM_IMAGING_WIRE
    const int attrs[]={0x56494d02,0x1000,0};unsigned char screen[16*16*4]={0};
    struct DGContext *ctx=DGCreateContext(attrs,NULL);
    if(!ctx || !DGMakeCurrent(ctx,screen,GL_UNSIGNED_BYTE,16,16))return 2;
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj ctx=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&ctx)||CGLSetCurrentContext(ctx))return 2;
    CGLDestroyPixelFormat(pf);
#endif
    const char *ext=(const char *)glGetString(GL_EXTENSIONS);
    printf("imaging renderer=%s advertised=%d\n",glGetString(GL_RENDERER),ext && strstr(ext,"GL_ARB_imaging")!=NULL);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    const GLubyte table[]={0,0,0,255, 255,0,0,255, 0,255,0,255, 0,0,255,255};
    const GLfloat scale[]={1,0.5f,0.25f,1};
    GLubyte image[32];GLfloat values[5]={0,0,0,0,-999};
    GLint width=-1,height=-1;
    glColorTable(GL_COLOR_TABLE,GL_RGBA8,4,GL_RGBA,GL_UNSIGNED_BYTE,table);
    GLenum set=glGetError();
    glGetColorTableParameteriv(GL_COLOR_TABLE,GL_COLOR_TABLE_WIDTH,&width);
    GLenum query=glGetError();
    memset(image,0xa5,sizeof(image));
    glGetColorTable(GL_COLOR_TABLE,GL_RGBA,GL_UNSIGNED_BYTE,image);
    GLenum read=glGetError();
    glColorTableParameterfv(GL_COLOR_TABLE,GL_COLOR_TABLE_SCALE,scale);
    GLenum param_set=glGetError();
    glGetColorTableParameterfv(GL_COLOR_TABLE,GL_COLOR_TABLE_SCALE,values);
    GLenum param_get=glGetError();
    printf("imaging table errors=%04x/%04x/%04x width=%d data=%d tail=%02x vector=%g,%g,%g,%g tail=%g error=%04x/%04x\n",
        set,query,read,width,!memcmp(table,image,sizeof(table)),image[sizeof(table)],
        (double)values[0],(double)values[1],(double)values[2],(double)values[3],(double)values[4],param_set,param_get);
    const GLfloat filter[]={0.25f,0.5f,0.75f,1.0f},row[]={0.5f,1.0f},column[]={0.25f,0.5f,0.75f};
    GLfloat observed[8]={-999,-999,-999,-999,-999};
    glConvolutionFilter2D(GL_CONVOLUTION_2D,GL_LUMINANCE,2,2,GL_LUMINANCE,GL_FLOAT,filter);
    set=glGetError();
    glGetConvolutionParameteriv(GL_CONVOLUTION_2D,GL_CONVOLUTION_WIDTH,&width);
    glGetConvolutionParameteriv(GL_CONVOLUTION_2D,GL_CONVOLUTION_HEIGHT,&height);
    query=glGetError();
    glGetConvolutionFilter(GL_CONVOLUTION_2D,GL_LUMINANCE,GL_FLOAT,observed);
    read=glGetError();
    printf("imaging convolution errors=%04x/%04x/%04x dimensions=%d,%d data=%d tail=%g\n",
        set,query,read,width,height,!memcmp(filter,observed,sizeof(filter)),(double)observed[4]);
    GLfloat got_row[]={-999,-999,-999},got_col[]={-999,-999,-999,-999},span=-999;
    glSeparableFilter2D(GL_SEPARABLE_2D,GL_LUMINANCE,2,3,GL_LUMINANCE,GL_FLOAT,row,column);
    set=glGetError();
    glGetSeparableFilter(GL_SEPARABLE_2D,GL_LUMINANCE,GL_FLOAT,got_row,got_col,&span);
    read=glGetError();
    printf("imaging separable errors=%04x/%04x row=%d col=%d untouched=%g,%g,%g\n",
        set,read,!memcmp(row,got_row,sizeof(row)),!memcmp(column,got_col,sizeof(column)),
        (double)got_row[2],(double)got_col[3],(double)span);
    glHistogram(GL_HISTOGRAM,4,GL_RGBA,GL_FALSE);set=glGetError();
    width=-1;glGetHistogramParameteriv(GL_HISTOGRAM,GL_HISTOGRAM_WIDTH,&width);query=glGetError();
    GLuint bins[17];for(unsigned i=0;i<17;++i)bins[i]=0xffffffffu;
    glGetHistogram(GL_HISTOGRAM,GL_TRUE,GL_RGBA,GL_UNSIGNED_INT,bins);read=glGetError();
    printf("imaging histogram errors=%04x/%04x/%04x width=%d sample=%u tail=%08x\n",set,query,read,width,bins[0],bins[16]);
    glMinmax(GL_MINMAX,GL_RGBA,GL_FALSE);set=glGetError();
    GLint format=-1;glGetMinmaxParameteriv(GL_MINMAX,GL_MINMAX_FORMAT,&format);query=glGetError();
    memset(image,0xa5,sizeof(image));
    glGetMinmax(GL_MINMAX,GL_TRUE,GL_RGBA,GL_UNSIGNED_BYTE,image);read=glGetError();
    printf("imaging minmax errors=%04x/%04x/%04x format=%x tail=%02x\n",set,query,read,format,image[8]);

    /* Give each potential image separate sentinel storage. Span is not
     * output even if a non-NULL argument is present. */
    GLubyte first[128],second[128];
    glPixelStorei(GL_PACK_ROW_LENGTH,8);
    glPixelStorei(GL_PACK_SKIP_ROWS,1);
    glPixelStorei(GL_PACK_SKIP_PIXELS,2);
    memset(first,0xa5,sizeof(first));
    glGetColorTable(GL_COLOR_TABLE,GL_RGBA,GL_UNSIGNED_BYTE,first);
    touched("table pack skips",first,sizeof(first));
    memset(first,0xa5,sizeof(first));
    glGetConvolutionFilter(GL_CONVOLUTION_2D,GL_RGBA,GL_UNSIGNED_BYTE,first);
    touched("convolution pack skips",first,sizeof(first));
    memset(first,0xa5,sizeof(first));
    glGetHistogram(GL_HISTOGRAM,GL_FALSE,GL_RGBA,GL_UNSIGNED_BYTE,first);
    touched("histogram pack skips",first,sizeof(first));
    memset(first,0xa5,sizeof(first));memset(second,0xa5,sizeof(second));
    glGetSeparableFilter(GL_SEPARABLE_2D,GL_RGBA,GL_UNSIGNED_BYTE,first,second,&span);
    touched("separable row pack skips",first,sizeof(first));
    touched("separable column pack skips",second,sizeof(second));
    memset(first,0xa5,sizeof(first));
    glGetMinmax(GL_MINMAX,GL_FALSE,GL_RGBA,GL_UNSIGNED_BYTE,first);
    touched("minmax pack skips",first,sizeof(first));

    GLubyte custom_stride[256];memset(custom_stride,0xa5,sizeof(custom_stride));
    glPixelStorei(GL_PACK_ROW_BYTES_APPLE,64);
    printf("imaging custom row bytes setup error=%04x\n",glGetError());
    glGetConvolutionFilter(GL_CONVOLUTION_2D,GL_RGBA,GL_UNSIGNED_BYTE,custom_stride);
    touched("convolution custom row bytes",custom_stride,sizeof(custom_stride));
    glPixelStorei(GL_PACK_ROW_BYTES_APPLE,0);
    glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    GLuint pbo=0;glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo);
    memset(first,0xa5,sizeof(first));glBufferData(GL_PIXEL_PACK_BUFFER,sizeof(first),first,GL_STATIC_DRAW);
    glGetColorTable(GL_COLOR_TABLE,GL_RGBA,GL_UNSIGNED_BYTE,(void *)16);
    GLenum pbo_error=glGetError();
    glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(first),first);
    printf("imaging table pack buffer error=%04x\n",pbo_error);
    touched("table pack buffer",first,sizeof(first));
    glBufferData(GL_PIXEL_PACK_BUFFER,sizeof(first),first,GL_STATIC_DRAW);
    glGetColorTable(GL_COLOR_TABLE,GL_RGBA,GL_UNSIGNED_BYTE,(void *)(sizeof(first)-8));
    printf("imaging table short pack buffer error=%04x\n",glGetError());
    glPixelStorei(GL_PACK_ROW_LENGTH,8);
    glPixelStorei(GL_PACK_SKIP_ROWS,1);
    glPixelStorei(GL_PACK_SKIP_PIXELS,2);
    memset(first,0xa5,sizeof(first));
    glBufferData(GL_PIXEL_PACK_BUFFER,sizeof(first),first,GL_STATIC_DRAW);
    glGetHistogram(GL_HISTOGRAM,GL_FALSE,GL_RGBA,GL_UNSIGNED_BYTE,(void *)16);
    pbo_error=glGetError();
    glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(first),first);
    printf("imaging histogram pack buffer error=%04x\n",pbo_error);
    touched("histogram pack buffer",first,sizeof(first));
    memset(first,0xa5,sizeof(first));
    glBufferData(GL_PIXEL_PACK_BUFFER,sizeof(first),first,GL_STATIC_DRAW);
    glGetMinmax(GL_MINMAX,GL_FALSE,GL_RGBA,GL_UNSIGNED_BYTE,(void *)(sizeof(first)-8));
    pbo_error=glGetError();
    glGetBufferSubData(GL_PIXEL_PACK_BUFFER,0,sizeof(first),first);
    printf("imaging tight minmax pack buffer error=%04x\n",pbo_error);
    touched("tight minmax pack buffer",first,sizeof(first));
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);

    glColorTable(GL_PROXY_COLOR_TABLE,GL_RGBA8,4,GL_RGBA,GL_UNSIGNED_BYTE,(const void *)1);
    GLenum proxy=glGetError();
    width=-1;glGetColorTableParameteriv(GL_PROXY_COLOR_TABLE,GL_COLOR_TABLE_WIDTH,&width);
    printf("imaging proxy errors=%04x/%04x width=%d\n",proxy,glGetError(),width);
    glConvolutionFilter1D(GL_CONVOLUTION_1D,GL_LUMINANCE,2,GL_LUMINANCE,GL_FLOAT,row);
    set=glGetError();
    observed[0]=observed[1]=observed[2]=-999;
    glGetConvolutionFilter(GL_CONVOLUTION_1D,GL_LUMINANCE,GL_FLOAT,observed);
    read=glGetError();
    printf("imaging convolution 1D errors=%04x/%04x data=%d tail=%g\n",
        set,read,!memcmp(row,observed,sizeof(row)),(double)observed[2]);
    const GLubyte replace[]={255,255,255,255};
    /* Reset the scale before expecting an unscaled sub-table write. */
    const GLfloat identity[]={1,1,1,1};
    glColorTableParameterfv(GL_COLOR_TABLE,GL_COLOR_TABLE_SCALE,identity);
    glColorSubTable(GL_COLOR_TABLE,1,1,GL_RGBA,GL_UNSIGNED_BYTE,replace);
    set=glGetError();
    glGetColorTable(GL_COLOR_TABLE,GL_RGBA,GL_UNSIGNED_BYTE,image);
    read=glGetError();
    printf("imaging sub-table errors=%04x/%04x data=%d\n",set,read,!memcmp(image+4,replace,sizeof(replace)));

    /* Aliased row/column destinations and poisoned unused span. */
    memset(first,0xa5,sizeof(first));
    glGetSeparableFilter(GL_SEPARABLE_2D,GL_RGBA,GL_UNSIGNED_BYTE,first,first+4,(void *)1);
    printf("imaging aliased separable bytes:");
    for(unsigned i=0;i<18;++i)printf(" %02x",first[i]);
    printf(" error=%04x\n",glGetError());

    /* Nonzero unpack offset, skips, and the same table read back to CPU. */
    glPixelStorei(GL_UNPACK_ROW_LENGTH,8);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,1);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,2);
    memset(first,0xa5,sizeof(first));memcpy(first+16+40,table,sizeof(table));
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER,sizeof(first),first,GL_STATIC_DRAW);
    glColorTable(GL_COLOR_TABLE,GL_RGBA8,4,GL_RGBA,GL_UNSIGNED_BYTE,(const void *)16);
    set=glGetError();
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    glGetColorTable(GL_COLOR_TABLE,GL_RGBA,GL_UNSIGNED_BYTE,image);
    read=glGetError();
    printf("imaging unpack buffer errors=%04x/%04x data=%d\n",set,read,!memcmp(image,table,sizeof(table)));
    glHistogram(GL_HISTOGRAM,0,GL_RGBA,GL_FALSE);
    set=glGetError();memset(first,0xa5,sizeof(first));
    glGetHistogram(GL_HISTOGRAM,GL_TRUE,GL_RGBA,GL_UNSIGNED_BYTE,first);
    printf("imaging empty histogram setup error=%04x\n",set);
    touched("empty histogram",first,sizeof(first));

    /* Exercise imaging state in actual rasterization and framebuffer copies. */
    glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    GLuint fb=0,rb=0;
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glGenRenderbuffersEXT(1,&rb);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,rb);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,16,16);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,rb);
    GLenum fb_status=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
    glViewport(0,0,16,16);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    const GLubyte invert[]={255,255,255,255, 170,170,170,255, 85,85,85,255, 0,0,0,255};
    const GLubyte red[]={255,0,0,255};
    glColorTable(GL_COLOR_TABLE,GL_RGBA8,4,GL_RGBA,GL_UNSIGNED_BYTE,invert);
    glHistogram(GL_HISTOGRAM,4,GL_RGBA,GL_FALSE);
    glMinmax(GL_MINMAX,GL_RGBA,GL_FALSE);
    glEnable(GL_COLOR_TABLE);glEnable(GL_HISTOGRAM);glEnable(GL_MINMAX);
    glRasterPos2f(0,0);glDrawPixels(1,1,GL_RGBA,GL_UNSIGNED_BYTE,red);
    GLenum draw=glGetError();
    glDisable(GL_MINMAX);glDisable(GL_HISTOGRAM);glDisable(GL_COLOR_TABLE);
    memset(image,0xa5,sizeof(image));glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,image);
    read=glGetError();
    printf("imaging rendered fb=%04x errors=%04x/%04x rgba=%02x,%02x,%02x,%02x\n",
        fb_status,draw,read,image[0],image[1],image[2],image[3]);
    glGetHistogram(GL_HISTOGRAM,GL_TRUE,GL_RGBA,GL_UNSIGNED_INT,bins);
    GLenum histogram=glGetError();
    unsigned sum=0;for(unsigned i=0;i<16;++i)sum+=bins[i];
    glGetMinmax(GL_MINMAX,GL_TRUE,GL_RGBA,GL_UNSIGNED_BYTE,image);
    GLenum minmax=glGetError();
    printf("imaging raster counters error=%04x/%04x sum=%u min=%02x,%02x,%02x,%02x max=%02x,%02x,%02x,%02x\n",
        histogram,minmax,sum,image[0],image[1],image[2],image[3],image[4],image[5],image[6],image[7]);
    glCopyColorTable(GL_COLOR_TABLE,GL_RGBA8,8,8,1);
    set=glGetError();memset(image,0xa5,sizeof(image));
    glGetColorTable(GL_COLOR_TABLE,GL_RGBA,GL_UNSIGNED_BYTE,image);read=glGetError();
    printf("imaging framebuffer copy errors=%04x/%04x rgba=%02x,%02x,%02x,%02x\n",
        set,read,image[0],image[1],image[2],image[3]);
#ifdef VIM_IMAGING_WIRE
    DGDestroyContext(ctx);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(ctx);
#endif
    return 0;
}
