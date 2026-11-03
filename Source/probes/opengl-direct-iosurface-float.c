/* Native IOSurface float import/render/publication. Hidden host-client build
 * uses the production interpreter; no quantization or software substitute. */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include <assert.h>
#include <signal.h>
static void run(unsigned width,unsigned height,unsigned element)
{
    size_t stride=(size_t)width*element+16,bytes=(height-1)*stride+width*element;
    unsigned char *input=calloc(1,bytes),*output=malloc(bytes);assert(input&&output);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        if(element==8){const uint16_t p[]={0x3000,0x3400,0x3800,0x3c00};memcpy(input+y*stride+x*element,p,sizeof(p));}
        else{const float p[]={.125f,.25f,.5f,1.f};memcpy(input+y*stride+x*element,p,sizeof(p));}
    }
    memset(output,0xa5,bytes);
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    assert(DGImportIOSurfaceTexture(element==8?0x881a:0x8814,width,height,input,stride)==1);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
    assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    for(unsigned corner=0;corner<4;++corner){
        float pixel[4]={0};glReadPixels(corner&1?width-1:0,corner&2?height-1:0,1,1,GL_RGBA,GL_FLOAT,pixel);
        assert(pixel[0]==.125f&&pixel[1]==.25f&&pixel[2]==.5f&&pixel[3]==1.f);
    }
    glClearColor(.25,.5,.75,1);glClear(GL_COLOR_BUFFER_BIT);
    unsigned count=0;uint32_t draw=0;struct DGIosurfaceWrite writes[2]={{0}};
    assert(DGFlushIOSurfaceState(0,&draw,writes,2,&count)==1&&count==1);
    assert(DGReadIOSurfaceTextureElements(texture,writes[0].serial,width,height,output,stride,element));
    for(unsigned y=0;y<height;++y){
        for(unsigned x=0;x<width;++x){
            if(element==8){
                uint16_t pixel[4];memcpy(pixel,output+y*stride+x*element,sizeof(pixel));
                assert(pixel[0]==0x3400&&pixel[1]==0x3800&&pixel[2]==0x3a00&&pixel[3]==0x3c00);
            }else{
                float pixel[4];memcpy(pixel,output+y*stride+x*element,sizeof(pixel));
                assert(pixel[0]==.25f&&pixel[1]==.5f&&pixel[2]==.75f&&pixel[3]==1.f);
            }
        }
        if(y+1<height)for(size_t x=width*element;x<stride;++x)assert(output[y*stride+x]==0xa5);
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    assert(!glGetError());free(input);free(output);
    printf("PASS %ux%u RGBA%u float import, GPU clear, exact writeback and padding\n",width,height,element*2);
}
int main(void)
{
    alarm(30);unsigned char shadow[16*16*4]={0};
    struct DGContext *c=DGCreateContext(NULL,NULL);assert(c&&DGMakeCurrent(c,shadow,GL_UNSIGNED_BYTE,16,16));
    run(17,19,8);run(17,19,16);run(3062,3062,8);
    DGDestroyContext(c);return 0;
}
