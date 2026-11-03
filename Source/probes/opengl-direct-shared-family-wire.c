/* Real executor/client contract: explicit full shared drawing, independent
 * partial views, immutable versions, epoch checks and in-place GA copies.
 * The test's readbacks verify pixels; they are not a presentation strategy.
 */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
static int failed;
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);failed=1;}}while(0)
static int pixel(int x,int y,unsigned r,unsigned g,unsigned b)
{
    unsigned char p[4]={0};glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    return !glGetError() && p[0]==r && p[1]==g && p[2]==b && p[3]==255;
}
int main(void)
{
    enum {W=64,H=64};unsigned char seed[W*H*4];
    for(unsigned i=0;i<sizeof(seed);i+=4){seed[i]=0;seed[i+1]=0;seed[i+2]=255;seed[i+3]=255;}
    unsigned char full_key=0,partial_key=0;uint64_t object=0;arc4random_buf(&object,sizeof(object));if(!object)object=1;
    struct DGContext *c=DGCreateContext(NULL,NULL);CHECK(c);if(!c)return 2;
    CHECK(DGMakeCurrent(c,&full_key,GL_UNSIGNED_BYTE,W,H));
    CHECK(DGSupportsSurfaceDrawablePublication());
    uint32_t lease=DGAcquireSurface(object,1,W,H);CHECK(lease);
    CHECK(DGUploadSurface(lease,seed,W,H,W*4,1));
    CHECK(DGAttachSurfaceDrawable(lease));CHECK(pixel(1,1,255,0,0));
    CHECK(DGSealSurfaceVersion(lease,1));
    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
    CHECK(pixel(1,1,0,255,0));
    /* The already sealed version must stay red after producer storage turns
     * green. Mutable producer storage must never leak into presentation. */
    GLuint texture=0,fb=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    CHECK(DGCopySurfaceVersionTexture(lease,1,GL_RGBA8,W,H));
    glGenFramebuffersEXT(1,&fb);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fb);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
    CHECK(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    CHECK(pixel(1,1,255,0,0));glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    glDeleteFramebuffersEXT(1,&fb);glDeleteTextures(1,&texture);
    CHECK(pixel(1,1,0,255,0));
    struct DGSurfaceRegion full={0,0,W,H,0,0};
    CHECK(DGPublishSurfaceRegions(lease,&full,1,0,0,1,2));CHECK(DGSealSurfaceVersion(lease,2));
    uint64_t epoch=0;CHECK(DGSurfaceEpoch(lease,&epoch) && epoch==2);
    struct DGSurfaceRegion shifted={0,0,8,8,1,0};
    CHECK(!DGPublishSurfaceRegions(lease,&shifted,1,0,0,2,3));CHECK(glGetError()==GL_INVALID_VALUE);
    CHECK(!DGPublishSurfaceRegions(lease,&full,1,1,0,2,3));CHECK(glGetError()==GL_INVALID_VALUE);
    CHECK(DGSurfaceEpoch(lease,&epoch) && epoch==2);
    /* A new identity with the SAME extent must still allocate independent
     * drawing storage. A partial publication preserves all outside pixels. */
    CHECK(DGMakeCurrent(c,&partial_key,GL_UNSIGNED_BYTE,W,H));
    CHECK(DGImportSurfaceRegion(lease,2,0,0,W,H,0));CHECK(pixel(32,32,0,255,0));
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    struct DGSurfaceRegion patch={0,0,8,8,0,0};
    CHECK(DGPublishSurfaceRegions(lease,&patch,1,0,0,2,3));CHECK(DGSealSurfaceVersion(lease,3));
    CHECK(pixel(32,32,0,0,255));
    CHECK(DGMakeCurrent(c,&full_key,GL_UNSIGNED_BYTE,W,H));
    CHECK(pixel(1,1,0,0,255));CHECK(pixel(32,32,0,255,0));
    CHECK(!DGPublishSurfaceRegions(lease,&full,1,0,0,2,4));CHECK(glGetError()==GL_INVALID_OPERATION);
    CHECK(!DGImportSurfaceRegion(lease,3,0,0,W,H,0));CHECK(glGetError()==GL_INVALID_VALUE);
    /* Shared drawing sees a canonical in-place copy without a self-import.
     * Preserve disjoint pixels and snapshot the source for overlaps. */
    struct DGSurfaceRegion move={0,0,8,8,20,20};
    CHECK(DGCopySurfaceRegions(lease,&move,1,3,4));
    CHECK(pixel(20,20,0,0,255));CHECK(pixel(32,32,0,255,0));CHECK(DGSealSurfaceVersion(lease,4));
    CHECK(DGMakeCurrent(c,&partial_key,GL_UNSIGNED_BYTE,W,H));
    CHECK(DGImportSurfaceRegion(lease,4,0,0,W,H,0));
    CHECK(pixel(20,20,0,0,255));CHECK(pixel(32,32,0,255,0));
    CHECK(DGSurfaceEpoch(lease,&epoch) && epoch==4);CHECK(glGetError()==GL_NO_ERROR);
    DGReleaseSurface(lease);DGDestroyContext(c);
    puts(failed?"FAIL shared-family ownership":"PASS shared/full and private/partial views, immutable versions, epochs, rejected self-import and in-place copy");
    return failed?1:0;
}
