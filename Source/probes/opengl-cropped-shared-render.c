/* Native Intel feasibility control, NOT a runtime implementation. Can a
 * cropped, pitched client-storage texture be a genuinely CPU-visible render
 * target without translating GL viewport/fragment coordinates? A successful
 * texture/FBO or returned range pointer alone is NOT acceptance: independently
 * inspect the exported allocation after genuine GPU completion, including
 * every pixel outside the crop. No guest/OS files or driver policy is touched.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    /* Snow rejects the later AutomaticGraphicsSwitching attribute. Force
     * integrated mode externally on the physical Mac, and reject a renderer
     * which is not actually Intel rather than silently accepting its dGPU. */
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=0;CGLContextObj context=0;GLint formats=0;
    if(CGLChoosePixelFormat(attrs,&pf,&formats)||!pf||CGLCreateContext(pf,0,&context))return 2;
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(context);
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    printf("renderer=%s\n",renderer?renderer:"missing");
    if(!renderer||!strstr(renderer,"Intel")){CGLDestroyContext(context);return 77;}
    enum{W=320,H=240,CW=251,CH=197};
    mach_vm_size_t bytes=(W*H*4+16383)&~(mach_vm_size_t)16383;
    mach_vm_address_t allocation=0,alias=0;
    if(mach_vm_allocate(mach_task_self(),&allocation,bytes,VM_FLAGS_ANYWHERE))return 2;
    memory_object_size_t exported=bytes;mach_port_t entry=MACH_PORT_NULL;
    if(mach_make_memory_entry_64(mach_task_self(),&exported,allocation,VM_PROT_READ|VM_PROT_WRITE,&entry,MACH_PORT_NULL)||
       exported<bytes||mach_vm_map(mach_task_self(),&alias,bytes,0,VM_FLAGS_ANYWHERE,entry,0,FALSE,
       VM_PROT_READ|VM_PROT_WRITE,VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_NONE))return 2;
    volatile uint32_t *cpu=(volatile uint32_t *)(uintptr_t)allocation;
    const unsigned origins[][2]={{0,0},{1,7},{3,7},{8,17},{13,17}};
    unsigned failures=0;
    for(unsigned test=0;test<sizeof(origins)/sizeof(origins[0]);++test){
        unsigned x=origins[test][0],y=origins[test][1];
        for(unsigned i=0;i<bytes/4;++i)cpu[i]=0xffc02010u;
        GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
        glTextureRangeAPPLE(GL_TEXTURE_RECTANGLE_ARB,(GLsizei)bytes,(void *)(uintptr_t)alias);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_STORAGE_HINT_APPLE,GL_STORAGE_SHARED_APPLE);
        glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE,GL_TRUE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,W);glPixelStorei(GL_UNPACK_ROW_BYTES_APPLE,W*4);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,CW,CH,0,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,
            (void *)(uintptr_t)(alias+(y*W+x)*4));
        glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE,GL_FALSE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_ROW_BYTES_APPLE,0);
        glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        GLenum status=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
        if(status==GL_FRAMEBUFFER_COMPLETE_EXT){
            glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();
        }
        unsigned inside=0,outside=0;
        for(unsigned row=0;row<H;++row)for(unsigned col=0;col<W;++col){
            int crop=col>=x&&col<x+CW&&row>=y&&row<y+CH;
            if(cpu[row*W+col]!=(crop?0xff00ff00u:0xffc02010u)){if(crop)inside++;else outside++;}
        }
        for(unsigned i=W*H;i<bytes/4;++i)outside+=cpu[i]!=0xffc02010u;
        unsigned char sample[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,sample);
        GLenum error=glGetError();int good=status==GL_FRAMEBUFFER_COMPLETE_EXT&&!error&&!inside&&!outside;
        printf("%s crop=%u,%u %ux%u pitch=%u framebuffer=%x error=%x cpu_bad_inside=%u outside=%u gpu=%u,%u,%u,%u\n",
            good?"PASS":"UNSUPPORTED",x,y,CW,CH,W*4,status,error,inside,outside,sample[0],sample[1],sample[2],sample[3]);
        failures+=!good;
        glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    }
    CGLSetCurrentContext(0);CGLDestroyContext(context);
    mach_vm_deallocate(mach_task_self(),alias,bytes);mach_port_deallocate(mach_task_self(),entry);
    mach_vm_deallocate(mach_task_self(),allocation,bytes);
    return failures?1:0;
}
