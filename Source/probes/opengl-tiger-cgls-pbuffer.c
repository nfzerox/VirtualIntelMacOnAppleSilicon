/* Finite native Tiger server-context pbuffer test, not public CGL/FBO.
 * The actual8R4088 CoreGraphics cglsCreateContext places its GLI renderer
 * before the dispatch prefix. Its first portable dispatch entries match the
 * genuine10.4u header. All private calls below were disassembled before use.
 * Creates only this process's offscreen allocations, never the primary.
 * This constructor admits the virtual provider. The physical8R3032 Intel
 * reference rejects its standalone server context with BadCodeModule10015;
 * that is NOT a native pbuffer rendering result. Use a real server trace for
 * comparisons. --unflushed intentionally omits the visibility boundary and
 * is diagnostic, not an assertion that the private API promises that mode.
 */
#include <ApplicationServices/ApplicationServices.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <OpenGL/gliDispatch.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <signal.h>
#include <unistd.h>
extern int _CGSDefaultConnection(void);
static uint32_t (*unique_id)(int,uint32_t *);
static uint32_t identifier(void)
{
    uint32_t id=0;return unique_id(_CGSDefaultConnection(),&id)?0:id;
}
struct ServerContext {GLIContext renderer;GLIFunctionDispatch dispatch;};
int main(int argc,char **argv)
{
    int render_only=argc==2&&!strcmp(argv[1],"--render-only");
    int unflushed=argc==2&&!strcmp(argv[1],"--unflushed");
    if(argc>2 || (argc==2&&!render_only&&!unflushed))return 64;
    setbuf(stdout,NULL);alarm(20);
    char release[64]={0};size_t length=sizeof(release);
    int mib[]={CTL_KERN,KERN_OSRELEASE};
    int queried=sysctl(mib,2,release,&length,NULL,0);
    printf("kernel query=%d release=%s bytes=%u\n",queried,release,(unsigned)length);
    if(queried||strncmp(release,"8.",2))return 77;
    int (*initialize)(const io_service_t *,const unsigned char *,unsigned,void *)=dlsym(RTLD_DEFAULT,"cglsInitialize");
    int (*choose)(void **,const int *)=dlsym(RTLD_DEFAULT,"cglsChoosePixelFormat");
    int (*create)(void **,void *,void *)=dlsym(RTLD_DEFAULT,"cglsCreateContext");
    int (*pbuffer)(void **,unsigned,unsigned,unsigned,unsigned,unsigned)=dlsym(RTLD_DEFAULT,"cglsCreatePBuffer");
    int (*attach)(void *,void *,unsigned,unsigned,unsigned,void *)=dlsym(RTLD_DEFAULT,"cglsAttachPBuffer");
    int (*texture)(void *,void *,unsigned)=dlsym(RTLD_DEFAULT,"cglsTexImagePBuffer");
    void (*generator)(void *)=dlsym(RTLD_DEFAULT,"cglsSetSurfaceIDGenerator");
    unique_id=dlsym(RTLD_DEFAULT,"CGSUniqueSurfaceID");
    if(!initialize||!choose||!create||!pbuffer||!attach||!texture||!generator||!unique_id){
        printf("exports init=%p choose=%p create=%p pb=%p attach=%p texture=%p generator=%p id=%p\n",
            initialize,choose,create,pbuffer,attach,texture,generator,unique_id);return 77;
    }
    io_service_t accelerator=0;UInt32 slot=0;
    IOReturn found=IOAccelFindAccelerator(CGDisplayIOServicePort(CGMainDisplayID()),&accelerator,&slot);
    if(found||!accelerator||slot>255){printf("accelerator=%x slot=%u status=%x\n",accelerator,(unsigned)slot,found);return 77;}
    unsigned char index=(unsigned char)slot;
    int error=initialize(&accelerator,&index,1,NULL);IOObjectRelease(accelerator);
    printf("native cgls initialize=%d slot=%u\n",error,(unsigned)slot);if(error)return 1;
    generator((void *)identifier);
    int attributes[]={4,80,20,8,21,8,22,8,12,0,13,0,0};void *format=NULL;
    error=choose(&format,attributes);printf("choose=%d\n",error);if(error||!format)return 1;
    struct ServerContext *c=NULL;error=create((void **)&c,format,NULL);
    printf("create=%d\n",error);if(error||!c)return 1;
    GLIFunctionDispatch *gl=&c->dispatch;GLIContext r=c->renderer;
    const char *renderer=(const char *)gl->get_string(r,GL_RENDERER);
    printf("renderer=%s\n",renderer?renderer:"none");if(!renderer||!strstr(renderer,"Intel"))return 77;
    enum {W=32,H=16};
    for(unsigned internal=0;internal<2;++internal){
        void *a=NULL,*b=NULL;
        unsigned fmt=internal?GL_RGBA16:GL_RGBA8;
        error=pbuffer(&a,W,H,GL_TEXTURE_RECTANGLE_ARB,fmt,0);
        if(!error)error=pbuffer(&b,W,H,GL_TEXTURE_RECTANGLE_ARB,fmt,0);
        if(!error)error=attach(c,a,0,0,slot,NULL);
        printf("format=%x attach=%d\n",fmt,error);if(error)return 1;
        gl->viewport(r,0,0,W,H);gl->disable(r,GL_BLEND);gl->disable(r,GL_SCISSOR_TEST);
        gl->clear_color(r,.25,.5,.75,.5);gl->clear(r,GL_COLOR_BUFFER_BIT);
        gl->enable(r,GL_SCISSOR_TEST);gl->scissor(r,0,0,W/2,H/2);
        gl->clear_color(r,1,0,0,1);gl->clear(r,GL_COLOR_BUFFER_BIT);gl->disable(r,GL_SCISSOR_TEST);
        if(render_only){
            /* The late Tiger runtime has the measured759-entry dispatch,
             * beyond the genuine10.4u SDK's686-entry declaration. */
            void (*render_flush)(GLIContext)=((void **)&c->dispatch)[588];
            render_flush(r);
        }else if(!unflushed)gl->flush(r);
        error=attach(c,b,0,0,slot,NULL);if(error)return 1;
        GLuint name=0;gl->gen_textures(r,1,&name);gl->bind_texture(r,GL_TEXTURE_RECTANGLE_ARB,name);
        error=texture(c,a,GL_FRONT);printf("texture=%d\n",error);if(error)return 1;
        unsigned char pixels[W*H*4];memset(pixels,0xcc,sizeof(pixels));
        gl->tex_parameteri(r,GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        gl->tex_parameteri(r,GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        gl->enable(r,GL_TEXTURE_RECTANGLE_ARB);gl->tex_envi(r,GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
        gl->viewport(r,0,0,W,H);gl->matrix_mode(r,GL_PROJECTION);gl->load_identity(r);
        gl->ortho(r,0,W,0,H,-1,1);gl->matrix_mode(r,GL_MODELVIEW);gl->load_identity(r);
        gl->begin(r,GL_QUADS);
        gl->tex_coord2f(r,0,0);gl->vertex2f(r,0,0);gl->tex_coord2f(r,W,0);gl->vertex2f(r,W,0);
        gl->tex_coord2f(r,W,H);gl->vertex2f(r,W,H);gl->tex_coord2f(r,0,H);gl->vertex2f(r,0,H);gl->end(r);
        gl->disable(r,GL_TEXTURE_RECTANGLE_ARB);
        gl->read_pixels(r,0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        GLenum e=gl->get_error(r);unsigned mismatches=0;
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            unsigned char *p=pixels+(y*W+x)*4;int red=x<W/2&&y<H/2;
            const unsigned expected[4]={red?255:64,red?0:128,red?0:191,red?255:128};
            for(unsigned ch=0;ch<4;++ch)if(abs((int)p[ch]-(int)expected[ch])>1)++mismatches;
        }
        printf("format=%x GLerror=%x mismatching-components=%u/%u first=%u,%u,%u,%u last=%u,%u,%u,%u\n",
            fmt,(unsigned)e,mismatches,W*H*4,pixels[0],pixels[1],pixels[2],pixels[3],
            pixels[sizeof(pixels)-4],pixels[sizeof(pixels)-3],pixels[sizeof(pixels)-2],pixels[sizeof(pixels)-1]);
        if(e||mismatches)return 1;
        gl->delete_textures(r,1,&name);
    }
    printf("PASS native CGLS RGBA8/RGBA16 publication/import orientation and alpha (%s)\n",
        render_only?"render-only":unflushed?"unflushed handoff":"ordinary flush");
    return 0; /* Own bounded process teardown releases offscreen allocations. */
}
