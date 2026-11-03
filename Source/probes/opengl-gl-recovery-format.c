/* Chess's final non-MSAA request, with and without native recovery. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLContext.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
static void records(CGLContextObj context)
{
#if defined(__i386__)
    if(!getenv("VIM_GL_RECOVERY_RECORDS")) return;
    /* Read-only metadata of this owned context, measured in matching
     * 13F1911 CGLCreateContext / virtual-screen-selection disassembly. */
    const unsigned char *p=(const void *)context->priv;
    uint16_t screens=0,engines=0;const uint32_t *table=NULL;
    memcpy(&screens,p+0x50,2);memcpy(&engines,p+0x52,2);memcpy(&table,p+0x48,4);
    printf(" records screens=%u engines=%u selected_record=%x selected_engine=%x\n",
        screens,engines,*(const uint32_t *)(p+0x40),*(const uint32_t *)(p+0x44));
    if(screens>16 || engines>16 || !table) return;
    for(unsigned i=0;i<screens;++i) {
        const uint32_t *row=table+4*i,*engine=(const void *)(uintptr_t)row[0];
        printf(" record=%u engine=%x local_screen=%u mask=%x flags=%x rend=%x methods=%x\n",
            i,row[0],row[1]&0xffff,row[2],row[3],engine?engine[0]:0,engine?engine[2]:0);
    }
#else
    (void)context;
#endif
}
int main(int argc,char **argv)
{
    int failures=0;
    for(int recovery=0;recovery<2;++recovery) {
        CGLPixelFormatAttribute attributes[]={kCGLPFADoubleBuffer,kCGLPFAWindow,
            kCGLPFAColorSize,24,kCGLPFAAlphaSize,8,kCGLPFADepthSize,16,
            kCGLPFAStencilSize,1,kCGLPFANoRecovery,0};
        if(recovery) attributes[10]=0;
        CGLPixelFormatObj format=NULL;GLint count=0;
        CGLError error=CGLChoosePixelFormat(attributes,&format,&count);
        printf("recovery=%d error=%d count=%d\n",recovery,error,count);
        for(int i=0;i<count;++i) {
            GLint renderer=0;CGLDescribePixelFormat(format,i,kCGLPFARendererID,&renderer);
            printf(" screen=%d renderer=%x\n",i,renderer);
        }
        fflush(stdout);
        if(argc>1 && count) {
            CGLContextObj context=NULL;
            error=CGLCreateContext(format,NULL,&context);
            printf(" context error=%d created=%d\n",error,context!=NULL);fflush(stdout);
            if(error || !context) ++failures;
            if(context) {
                CGLSetCurrentContext(context);
                records(context);
                int screens=!strcmp(argv[1],"--switch-screens") ? count : 1;
                for(int i=0;i<screens;++i) {
                    error=CGLSetVirtualScreen(context,i);
                    GLint selected=-1,renderer=0,expected_renderer=0;
                    CGLDescribePixelFormat(format,i,kCGLPFARendererID,&expected_renderer);
                    CGLGetVirtualScreen(context,&selected);
                    CGLGetParameter(context,kCGLCPCurrentRendererID,&renderer);
                    printf(" requested=%d selected=%d renderer_id=%x error=%d renderer=%s rend=%p clear=%p\n",
                        i,selected,renderer,error,glGetString(GL_RENDERER),context->rend,context->disp.clear);fflush(stdout);
                    records(context);
                    if(error) ++failures;
                    if(selected!=i || renderer!=expected_renderer) {
                        printf(" FAIL actual renderer did not switch: expected=%x\n",expected_renderer);
                        ++failures;
                    }
                    GLuint texture=0,fbo=0;
                    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
                    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
                    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
                    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,texture,0);
                    GLenum status=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);
                    glClearColor(i&1,!(i&1),0,1);glClear(GL_COLOR_BUFFER_BIT);
                    unsigned char pixel[4]={0};glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                    GLenum gl_error=glGetError();
                    int good=status==GL_FRAMEBUFFER_COMPLETE_EXT && !gl_error &&
                        pixel[0]==((i&1)?255:0) && pixel[1]==((i&1)?0:255) && !pixel[2] && pixel[3]==255;
                    printf(" %s screen draw=%d fbo=%x pixel=%u,%u,%u,%u error=%x\n",good?"PASS":"FAIL",
                        i,status,pixel[0],pixel[1],pixel[2],pixel[3],gl_error);fflush(stdout);
                    if(!good) ++failures;
                    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
                    glDeleteTextures(1,&texture);
                }
                CGLSetCurrentContext(NULL);CGLDestroyContext(context);
            }
        }
        if(format) CGLDestroyPixelFormat(format);
    }
    return !!failures;
}
