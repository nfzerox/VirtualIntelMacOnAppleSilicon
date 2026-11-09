/* Exercise the 2.0 hotpatch row-upload capability alongside the 2.1
 * evaluator capability on one connection, without booting another guest. */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"

int main(void)
{
    enum { width=8192,height=2160,stride=width*4 };
    const size_t bytes=(size_t)stride*height;
    unsigned char *pixels=malloc(bytes);
    struct DGContext *ctx=DGCreateContext(NULL,NULL);
    uint64_t key=0;
    arc4random_buf(&key,sizeof(key));
    if(!pixels || !ctx || !DGMakeCurrent(ctx,pixels,GL_UNSIGNED_BYTE,width,height))return 2;
    unsigned features=ctx->connection->features;
    unsigned required=DG_FEATURE_SURFACE_UPLOAD_ROWS|DG_FEATURE_EVALUATOR_QUERIES;
    if((features&required)!=required)return 2;
    uint32_t lease=DGAcquireSurface(key?key:1,1,width,height);
    if(!lease)return 2;
    uint64_t initial=0,observed=0;
    int good=DGSurfaceEpoch(lease,&initial);
    ctx->connection->features=DG_FEATURE_EVALUATOR_QUERIES;
    good=good && !DGUploadSurface(lease,pixels,width,height,stride,initial+1) &&
         DGSurfaceEpoch(lease,&observed) && observed==initial;
    ctx->connection->features=DG_FEATURE_SURFACE_UPLOAD_ROWS;
    for(unsigned y=0;y<height;++y) {
        unsigned char *row=pixels+(size_t)y*stride;
        for(unsigned x=0;x<width;++x) {
            row[x*4]=37;row[x*4+1]=(unsigned char)(y/512);
            row[x*4+2]=(unsigned char)(x/1024);row[x*4+3]=255;
        }
    }
    good=good && DGUploadSurface(lease,pixels,width,height,stride,initial+1) &&
         DGSurfaceEpoch(lease,&observed) && observed==initial+1 && DGAttachSurfaceDrawable(lease);
    for(int y=0;good && y<height;y+=511) {
        unsigned char rgba[4]={0};
        glReadPixels(width-1,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
        good=!glGetError() && rgba[0]==7 && rgba[1]==y/512 && rgba[2]==37 && rgba[3]==255;
    }
    ctx->connection->features=features;
    DGReleaseSurface(lease);
    DGDestroyContext(ctx);
    free(pixels);
    printf("%s independent resize feature and >64 MiB surface upload\n",good?"PASS":"FAIL");
    return !good;
}
