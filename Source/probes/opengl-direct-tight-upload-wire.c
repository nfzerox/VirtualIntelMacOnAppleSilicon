/* Real native GL wire, with exact texel and unpack-state assertions. Includes
 * the real client solely for bounded A/B counters and device fallback checks.
 */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
static int failed;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);failed=1;}}while(0)
// Larger than a legacy 256-KB device chunk, including pitched/skipped source.
enum { W=512,H=256,PITCH=640,SKIPX=13,SKIPY=7,TW=520,TH=264 };
static unsigned char source[(SKIPY+H)*PITCH*4],result[TW*TH*4],zeros[TW*TH*4];
static void verify(int uploaded)
{
    memset(result,0x65,sizeof(result));
    glReadPixels(0,0,TW,TH,GL_RGBA,GL_UNSIGNED_BYTE,result);
    for(int y=0;y<TH;++y)for(int x=0;x<TW;++x)for(int c=0;c<4;++c) {
        unsigned char expected=uploaded && x>=4 && x<W+4 && y>=3 && y<H+3 ?
            source[((y-3+SKIPY)*PITCH+x-4+SKIPX)*4+c]:0;
        if(result[(y*TW+x)*4+c]!=expected){
            fprintf(stderr,"texel mismatch at %d,%d channel %d: %u != %u\n",x,y,c,result[(y*TW+x)*4+c],expected);
            failed=1;return;
        }
    }
    const GLenum keys[]={GL_UNPACK_ROW_LENGTH,GL_UNPACK_ALIGNMENT,GL_UNPACK_SKIP_ROWS,
        GL_UNPACK_SKIP_PIXELS,GL_UNPACK_ROW_BYTES_APPLE,GL_UNPACK_SWAP_BYTES,GL_UNPACK_LSB_FIRST};
    const GLint expected[]={PITCH,8,SKIPY,SKIPX,0,1,1};
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);++i){GLint value=-1;glGetIntegerv(keys[i],&value);CHECK(value==expected[i]);}
}
static void upload(void)
{ glTexSubImage2D(GL_TEXTURE_2D,0,4,3,W,H,GL_RGBA,GL_UNSIGNED_BYTE,source); }
static double milliseconds(uint64_t ticks)
{
    mach_timebase_info_data_t clock;mach_timebase_info(&clock);
    return (double)ticks*clock.numer/clock.denom/1e6;
}
static void transaction_bounds(struct DGContext *c)
{
    if(!c->connection->row_upload_transaction_supported)return;
    struct { struct VIMGLScatterRequest header;struct VIMGLScatterUpload upload;struct DGMessage command; } request={
        {6,1,0,4},{{(uintptr_t)source,4,4,0},sizeof(struct DGMessage),0},{0}};
    struct VIMGLScatterResult result={0};size_t size;
    request.upload.reserved=1;size=sizeof(result);
    CHECK(IOConnectCallStructMethod(c->connection->device,kIOVM2DDirectGLScatter,
        &request,sizeof(request),&result,&size)==kIOReturnBadArgument);
    request.upload.reserved=0;request.upload.prefix_bytes=0;size=sizeof(result);
    CHECK(IOConnectCallStructMethod(c->connection->device,kIOVM2DDirectGLScatter,
        &request,sizeof(request),&result,&size)==kIOReturnBadArgument);
    request.upload.prefix_bytes=sizeof(struct DGMessage);request.header.token=1;size=sizeof(result);
    CHECK(IOConnectCallStructMethod(c->connection->device,kIOVM2DDirectGLScatter,
        &request,sizeof(request),&result,&size)==kIOReturnBadArgument);
    request.header.token=0;request.upload.rows.address=UINT64_MAX-1;size=sizeof(result);
    CHECK(IOConnectCallStructMethod(c->connection->device,kIOVM2DDirectGLScatter,
        &request,sizeof(request),&result,&size)==kIOReturnBadArgument);
    // These rejected requests must not emit even a prefix into the GL stream.
    CHECK(glGetError()==GL_NO_ERROR);
}
static void benchmark(struct DGContext *c)
{
    unsigned supported=c->connection->row_upload_transaction_supported;
    if(!supported)return;
    // Interleave both paths on one context/texture. Finish at boundaries,
    // keeping identical work and source lifetime; no global runtime toggles.
    for(unsigned round=0;round<4;++round)for(unsigned mode=0;mode<2;++mode) {
        c->connection->row_upload_transaction_supported=(round&1)?!mode:mode;
        upload();glFinish();uint64_t began=mach_absolute_time();
        for(unsigned i=0;i<48;++i)upload();
        glFinish();double elapsed=milliseconds(mach_absolute_time()-began);
        printf("upload-round=%u transaction=%u ms=%.3f uploads=48 bytes-per-upload=%u\n",
            round,c->connection->row_upload_transaction_supported,elapsed,W*H*4);
        verify(1);CHECK(glGetError()==GL_NO_ERROR);
    }
    c->connection->row_upload_transaction_supported=supported;
}
static void run(const int *attributes,int fallback)
{
    struct DGContext *c=DGCreateContext(attributes,NULL);CHECK(c);if(!c)return;
    unsigned char drawable[16*16*4]={0};CHECK(DGMakeCurrent(c,drawable,GL_UNSIGNED_BYTE,16,16));
    CHECK(!!(c->connection->capabilities&DG_CAP_TIGHT_TEX_SUB_IMAGE_2D)!=fallback);
    printf("renderer=%s row-gather=%u profile=%u\n",glGetString(GL_RENDERER),c->connection->row_upload_supported,c->profile);
    CHECK(strstr((const char *)glGetString(GL_RENDERER),"Intel")!=NULL);
    GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,TW,TH,0,GL_RGBA,GL_UNSIGNED_BYTE,zeros);
    GLuint fbo=0;glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,PITCH);glPixelStorei(GL_UNPACK_ALIGNMENT,8);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,SKIPY);glPixelStorei(GL_UNPACK_SKIP_PIXELS,SKIPX);
    glPixelStorei(GL_UNPACK_SWAP_BYTES,1);glPixelStorei(GL_UNPACK_LSB_FIRST,1);
    transaction_bounds(c);
    uint64_t calls=dg_tight_upload_calls,bytes=dg_tight_upload_payload_bytes;
    glEnable(0xffffffffu);upload();verify(1);CHECK(glGetError()==GL_INVALID_ENUM);
    CHECK(dg_tight_upload_calls==calls+!fallback);
    CHECK(dg_tight_upload_payload_bytes==bytes+(!fallback?W*H*4:0));
    unsigned gather=c->connection->row_upload_supported;c->connection->row_upload_supported=0;
    upload();verify(1);CHECK(glGetError()==GL_NO_ERROR);c->connection->row_upload_supported=gather;
    if(c->profile==0x1000) {
        glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);glPixelStorei(GL_UNPACK_SKIP_ROWS,SKIPY+1);
        glPixelStorei(GL_UNPACK_ROW_BYTES_APPLE,PITCH*4);glPopClientAttrib();
        upload();verify(1);CHECK(glGetError()==GL_NO_ERROR);
    }
    calls=dg_tight_upload_calls;
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,-1,H,GL_RGBA,GL_UNSIGNED_BYTE,source);
    CHECK(glGetError()==GL_INVALID_VALUE && dg_tight_upload_calls==calls);
    if(c->profile==0x1000) {
        // The prior desktop codec supports legacy lists. Capture input now,
        // execute later, without compiling temporary unpack state into a list.
        glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
        GLuint list=glGenLists(1);CHECK(list);glNewList(list,GL_COMPILE);upload();glEndList();
        verify(0);glCallList(list);verify(1);CHECK(glGetError()==GL_NO_ERROR);glDeleteLists(list,1);
        glBegin(GL_POINTS);upload();glEnd();CHECK(glGetError()==GL_INVALID_OPERATION);
    }
    if(!fallback)benchmark(c);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
    glDeleteTextures(1,&texture);DGDestroyContext(c);
}
int main(int argc,char **argv)
{
    int fallback=argc==2 && !strcmp(argv[1],"--fallback");if(argc>2 || (argc==2 && !fallback))return 2;
    memset(source,0x7d,sizeof(source));
    for(int y=0;y<H;++y)for(int x=0;x<W;++x)for(int c=0;c<4;++c)
        source[((y+SKIPY)*PITCH+x+SKIPX)*4+c]=(unsigned char)(x*3+y*5+c*29);
    run(NULL,fallback);const int core[]={0x56494d02,0x3200,0};run(core,fallback);
    printf("%s tight uploads: exact pixels, skipped source, unpack restore, prior errors, client stacks, lists, Begin/core/device/old-host paths; calls=%llu gather=%llu copies=%llu source=%llu payload=%llu\n",
        failed?"FAIL":"PASS",(unsigned long long)dg_tight_upload_calls,(unsigned long long)dg_tight_upload_gathers,
        (unsigned long long)dg_tight_upload_copies,(unsigned long long)dg_tight_upload_source_bytes,
        (unsigned long long)dg_tight_upload_payload_bytes);
    return failed;
}
