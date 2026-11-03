/* Real owned drawable/heap reads, not a display capture or FPS benchmark.
 * Same private canonical BGRA read, old chunks versus existing prepared RAM.
 */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
static int failed;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);failed=1;} } while(0)
static int compare_double(const void *a,const void *b)
{ double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y); }
static void shape(int width,int height)
{
    size_t bytes=(size_t)width*height*4;
    unsigned char *shadow=calloc(1,bytes),*out=malloc(bytes+32);
    struct DGContext *c=DGCreateContext(NULL,NULL);
    CHECK(shadow && out && c);if(!shadow || !out || !c)exit(2);
    CHECK(DGMakeCurrent(c,shadow,GL_UNSIGNED_BYTE,width,height));
    int prepared=DGSupportsRAMScatterReads() && bytes>=DG_PREPARED_FULL_READ_MIN_BYTES;
    printf("shape=%dx%d bytes=%lu prepared_eligible=%d transport=%s\n",width,height,
        (unsigned long)bytes,prepared,DGTransportName());
    GLuint pbo=0;glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER,4096,NULL,GL_STREAM_DRAW);
    glPixelStorei(GL_PACK_ROW_LENGTH,37);glPixelStorei(GL_PACK_SKIP_PIXELS,3);
    glPixelStorei(GL_PACK_SKIP_ROWS,2);glPixelStorei(GL_PACK_ALIGNMENT,8);
    glPixelTransferf(GL_RED_SCALE,0);
    mach_timebase_info_data_t timebase;mach_timebase_info(&timebase);
    for(int phase=0;phase<4;++phase){
        dg_prepared_full_read=phase&1;
        uint64_t direct0=0,staged0=0,direct1=0,staged1=0;
        DGReadTransferStats(&direct0,&staged0);double total=0,times[32];
        for(int frame=0;frame<32;++frame){
            int base_red=frame&1;
            glClearColor(base_red,0,!base_red,1);glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_SCISSOR_TEST);glScissor(width/4,height/4,width/2,height/2);
            glClearColor(!base_red,1,base_red,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
            memset(out,0xa5,bytes+32);
            uint64_t began=mach_absolute_time();CHECK(DGReadDrawable(out+16,width,height));
            times[frame]=(mach_absolute_time()-began)*(double)timebase.numer/timebase.denom/1000.0;
            total+=times[frame];
            for(int i=0;i<16;++i)CHECK(out[i]==0xa5 && out[16+bytes+i]==0xa5);
            int matches=1;
            for(int y=0;y<height;++y)for(int x=0;x<width;++x){
                int inside=x>=width/4 && x<width/4+width/2 && y>=height/4 && y<height/4+height/2;
                const unsigned char *p=out+16+((size_t)y*width+x)*4;
                int red=inside?!base_red:base_red;
                if(p[0]!=(red?0:255) || p[1]!=(inside?255:0) || p[2]!=(red?255:0) || p[3]!=255)matches=0;
            }
            CHECK(matches);
        }
        DGReadTransferStats(&direct1,&staged1);
        // The 16-byte wire reply header still uses an ordinary receive. Only
        // pixel bytes must bypass staging; do not claim all traffic is DMA.
        if(prepared && dg_prepared_full_read)CHECK(direct1-direct0==bytes*32 &&
            staged1-staged0==32*sizeof(struct DGReply));
        GLint value=0;GLfloat scale=1;
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&value);CHECK(value==(GLint)pbo);
        glGetIntegerv(GL_PACK_ROW_LENGTH,&value);CHECK(value==37);
        glGetIntegerv(GL_PACK_SKIP_PIXELS,&value);CHECK(value==3);
        glGetIntegerv(GL_PACK_SKIP_ROWS,&value);CHECK(value==2);
        glGetIntegerv(GL_PACK_ALIGNMENT,&value);CHECK(value==8);
        glGetFloatv(GL_RED_SCALE,&scale);CHECK(scale==0);
        CHECK(glGetError()==GL_NO_ERROR && !c->connection->dead);
        qsort(times,32,sizeof(*times),compare_double);
        printf("phase=%d prepared=%d mean_read_us=%.3f median_us=%.3f p95_us=%.3f direct_bytes=%llu staged_bytes=%llu\n",
            phase,dg_prepared_full_read,total/32,times[16],times[30],(unsigned long long)(direct1-direct0),
            (unsigned long long)(staged1-staged0));fflush(stdout);
    }
    glEnable(0xffffffffu);CHECK(DGReadDrawable(out+16,width,height));
    CHECK(glGetError()==GL_INVALID_ENUM && glGetError()==GL_NO_ERROR);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glDeleteBuffers(1,&pbo);DGDestroyContext(c);
    free(shadow);free(out);
}
int main(void)
{
    shape(2560,44);shape(2464,86);shape(1280,800);shape(64,32);
    puts(failed?"FAIL canonical full-read transport":"PASS changing canonical pixels, guards, native errors and pack/transfer state");
    return failed?1:0;
}
