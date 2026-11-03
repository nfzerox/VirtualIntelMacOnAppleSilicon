/* Bounded, headless transport measurements using the actual direct backend.
 * Run the same source natively on the host and under TCG in the guest.
 * No system files, visible windows, or current desktop contexts are changed. */
#include <OpenGL/gl.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
extern void *DGCreateContext(const int *,void *);
extern GLboolean DGMakeCurrent(void *,void *,GLenum,GLsizei,GLsizei);
extern void DGDestroyContext(void *);
extern int DGReadDrawable(void *,int,int);
extern int DGImportDrawable(const void *,int,int,size_t,int);
extern const char *DGTransportName(void);
static double seconds(void)
{
    static mach_timebase_info_data_t timebase;
    if(!timebase.denom) mach_timebase_info(&timebase);
    return mach_absolute_time()*(double)timebase.numer/timebase.denom/1e9;
}
int main(void)
{
    void *context=DGCreateContext(NULL,NULL);
    unsigned char *pixels=calloc(1280*800,4);
    if(!context || !pixels || !DGMakeCurrent(context,pixels,GL_UNSIGNED_BYTE,1280,800)) return 2;
    printf("renderer=%s transport=%s\n",glGetString(GL_RENDERER),DGTransportName());fflush(stdout);
    GLint value=0;double start=seconds();
    for(int i=0;i<100;++i) glGetIntegerv(GL_PACK_ALIGNMENT,&value);
    printf("query 100 calls: %.3f ms/call value=%d\n",(seconds()-start)*10,value);fflush(stdout);
    const int sizes[][2]={{1,1},{64,64},{640,480},{1280,800}};
    for(unsigned s=0;s<sizeof(sizes)/sizeof(*sizes);++s) {
        int w=sizes[s][0],h=sizes[s][1],frames=10;
        glClearColor(0.25f,0.5f,0.75f,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();
        start=seconds();
        for(int i=0;i<frames;++i) {
            glClear(GL_COLOR_BUFFER_BIT);
            glReadPixels(0,0,w,h,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,pixels);
        }
        double elapsed=seconds()-start;
        printf("clear+readback %dx%d: %.3f ms/frame %.2f MiB/s first=%u,%u,%u,%u error=%x\n",
            w,h,elapsed*1000/frames,(double)w*h*4*frames/elapsed/1048576,
            pixels[0],pixels[1],pixels[2],pixels[3],glGetError());fflush(stdout);
    }
    start=seconds();
    for(int i=0;i<10;++i) { glClear(GL_COLOR_BUFFER_BIT);if(!DGReadDrawable(pixels,1280,800)) return 3; }
    printf("private clear+readback 1280x800: %.3f ms/frame first=%u,%u,%u,%u error=%x\n",
        (seconds()-start)*100,pixels[0],pixels[1],pixels[2],pixels[3],glGetError());fflush(stdout);
    /* Explicitly verify import/readback and preservation of awkward pixel
     * store state. These private transactions must not mutate public state. */
    for(int i=0;i<1280*800;++i) { pixels[i*4]=20;pixels[i*4+1]=40;pixels[i*4+2]=60;pixels[i*4+3]=255; }
    glPixelStorei(GL_PACK_ROW_LENGTH,7);glPixelStorei(GL_UNPACK_ROW_LENGTH,9);
    glPixelStorei(GL_PACK_SKIP_PIXELS,1);glPixelStorei(GL_UNPACK_SKIP_ROWS,2);
    int good=DGImportDrawable(pixels,1280,800,1280*4,0) && DGReadDrawable(pixels,1280,800);
    GLint pack=0,unpack=0,skip_pixels=0,skip_rows=0;
    glGetIntegerv(GL_PACK_ROW_LENGTH,&pack);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&unpack);
    glGetIntegerv(GL_PACK_SKIP_PIXELS,&skip_pixels);glGetIntegerv(GL_UNPACK_SKIP_ROWS,&skip_rows);
    good=good && pack==7 && unpack==9 && skip_pixels==1 && skip_rows==2 &&
        pixels[0]==20 && pixels[1]==40 && pixels[2]==60 && pixels[3]==255 && !glGetError();
    printf("private pixels/state %s\n",good ? "PASS" : "FAIL");
    pixels[0]=91;pixels[799*1280*4]=173;
    int flipped=DGImportDrawable(pixels,1280,800,1280*4,1) && DGReadDrawable(pixels,1280,800);
    flipped=flipped && pixels[0]==173 && pixels[799*1280*4]==91;
    printf("top-down import inverse %s\n",flipped ? "PASS" : "FAIL");
    glPixelStorei(GL_PACK_SWAP_BYTES,GL_TRUE);glPixelStorei(GL_PACK_LSB_FIRST,GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT);
    int canonical=DGReadDrawable(pixels,1280,800) && pixels[0]==191 && pixels[1]==128 && pixels[2]==64 && pixels[3]==255;
    GLint swap=0,lsb=0;glGetIntegerv(GL_PACK_SWAP_BYTES,&swap);glGetIntegerv(GL_PACK_LSB_FIRST,&lsb);
    canonical=canonical && swap && lsb;
    glPixelStorei(GL_PACK_SWAP_BYTES,GL_FALSE);
    for(int i=0;i<1280*800;++i) { pixels[i*4]=20;pixels[i*4+1]=40;pixels[i*4+2]=60;pixels[i*4+3]=255; }
    glPixelStorei(GL_UNPACK_SWAP_BYTES,GL_TRUE);glPixelStorei(GL_UNPACK_LSB_FIRST,GL_TRUE);
    canonical=DGImportDrawable(pixels,1280,800,1280*4,0) && DGReadDrawable(pixels,1280,800) && canonical;
    glGetIntegerv(GL_UNPACK_SWAP_BYTES,&swap);glGetIntegerv(GL_UNPACK_LSB_FIRST,&lsb);
    canonical=canonical && swap && lsb && pixels[0]==20 && pixels[1]==40 && pixels[2]==60 && pixels[3]==255;
    printf("private canonical bytes with caller swap/LSB state %s\n",canonical ? "PASS" : "FAIL");
    DGDestroyContext(context);free(pixels);return !(good && flipped && canonical);
}
