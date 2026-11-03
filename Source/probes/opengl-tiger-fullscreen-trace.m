/* Finite, observation-only Tiger application trace. Load into ONLY the test
 * application using DYLD_FORCE_FLAT_NAMESPACE=1 and DYLD_INSERT_LIBRARIES.
 * Every original result/exception is preserved. No global launchctl state,
 * framework patch, forced renderer, display capture or error consumption.
 */
#import <Foundation/Foundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

static CGLContextObj watched;
static CGLPixelFormatObj fullscreen_format;
static unsigned records;
static int Record(void) { return __sync_fetch_and_add(&records,1)<256; }
static void *Original(const char *name)
{
    void *function=dlsym(RTLD_NEXT,name);
    if(!function){fprintf(stderr,"VIM FULLSCREEN missing original %s\n",name);abort();}
    return function;
}
static void Attributes(const CGLPixelFormatAttribute *attributes)
{
    if(!attributes)return;
    for(unsigned i=0;i<64&&attributes[i];++i){
        unsigned key=attributes[i];fprintf(stderr," %u",key);
        switch(key){
        case kCGLPFARendererID:case kCGLPFADisplayMask:case kCGLPFAColorSize:
        case kCGLPFADepthSize:case kCGLPFAStencilSize:case kCGLPFAAlphaSize:
        case kCGLPFAAccumSize:case kCGLPFASampleBuffers:case kCGLPFASamples:
        case kCGLPFAAuxBuffers:
            fprintf(stderr,"=%#x",(unsigned)attributes[++i]);break;
        }
    }
}
CGLError CGLChoosePixelFormat(const CGLPixelFormatAttribute *attributes,
                             CGLPixelFormatObj *format,GLint *count)
{
    static CGLError (*original)(const CGLPixelFormatAttribute *,CGLPixelFormatObj *,GLint *);
    if(!original)original=Original("CGLChoosePixelFormat");
    CGLError result=original(attributes,format,count);
    int fullscreen=0;
    if(attributes)for(unsigned i=0;i<64&&attributes[i];++i)if(attributes[i]==kCGLPFAFullScreen)fullscreen=1;
    if(fullscreen&&format&&*format)fullscreen_format=*format;
    if((fullscreen||result)&&Record()){
        fprintf(stderr,"VIM FULLSCREEN choose result=%d format=%p count=%d caller=%p attributes:",
            result,format?(void *)*format:NULL,count?(int)*count:-1,__builtin_return_address(0));
        Attributes(attributes);fputc('\n',stderr);
    }
    return result;
}
CGLError CGLCreateContext(CGLPixelFormatObj format,CGLContextObj share,CGLContextObj *context)
{
    static CGLError (*original)(CGLPixelFormatObj,CGLContextObj,CGLContextObj *);
    if(!original)original=Original("CGLCreateContext");
    CGLError result=original(format,share,context);
    if(format==fullscreen_format&&context&&*context)watched=*context;
    if((format==fullscreen_format||result)&&Record())
        fprintf(stderr,"VIM FULLSCREEN create result=%d format=%p context=%p share=%p caller=%p\n",
            result,(void *)format,context?(void *)*context:NULL,(void *)share,__builtin_return_address(0));
    return result;
}
CGLError CGLSetCurrentContext(CGLContextObj context)
{
    static CGLError (*original)(CGLContextObj);
    if(!original)original=Original("CGLSetCurrentContext");
    CGLError result=original(context);
    if((context==watched||result)&&Record())
        fprintf(stderr,"VIM FULLSCREEN current result=%d context=%p caller=%p\n",
            result,(void *)context,__builtin_return_address(0));
    return result;
}
CGLError CGLSetFullScreen(CGLContextObj context)
{
    static CGLError (*original)(CGLContextObj);
    if(!original)original=Original("CGLSetFullScreen");
    CGLError result=original(context);
    if(Record())fprintf(stderr,"VIM FULLSCREEN attach result=%d context=%p caller=%p\n",
        result,(void *)context,__builtin_return_address(0));
    return result;
}
CGLError CGLSetParameter(CGLContextObj context,CGLContextParameter key,const GLint *value)
{
    static CGLError (*original)(CGLContextObj,CGLContextParameter,const GLint *);
    if(!original)original=Original("CGLSetParameter");
    CGLError result=original(context,key,value);
    if((context==watched||result)&&Record())
        fprintf(stderr,"VIM FULLSCREEN parameter result=%d context=%p key=%d value=%d caller=%p\n",
            result,(void *)context,key,value?(int)*value:0,__builtin_return_address(0));
    return result;
}
CGError CGDisplayCapture(CGDirectDisplayID display)
{
    static CGError (*original)(CGDirectDisplayID);
    if(!original)original=Original("CGDisplayCapture");
    CGError result=original(display);
    if(Record())fprintf(stderr,"VIM FULLSCREEN capture result=%d display=%u caller=%p\n",
        result,(unsigned)display,__builtin_return_address(0));
    return result;
}
CGError CGDisplayCaptureWithOptions(CGDirectDisplayID display,CGCaptureOptions options)
{
    static CGError (*original)(CGDirectDisplayID,CGCaptureOptions);
    if(!original)original=Original("CGDisplayCaptureWithOptions");
    CGError result=original(display,options);
    if(Record())fprintf(stderr,"VIM FULLSCREEN capture-options result=%d display=%u options=%u caller=%p\n",
        result,(unsigned)display,(unsigned)options,__builtin_return_address(0));
    return result;
}
void objc_exception_throw(id exception) __attribute__((noreturn));
void objc_exception_throw(id exception)
{
    static void (*original)(id);
    if(!original)original=Original("objc_exception_throw");
    if(Record())fprintf(stderr,"VIM FULLSCREEN exception name=%s reason=%s caller=%p\n",
        [[exception name] UTF8String],[[exception reason] UTF8String],__builtin_return_address(0));
    original(exception);abort();
}
__attribute__((constructor)) static void Loaded(void)
{
    fputs("VIM FULLSCREEN finite trace loaded (original results preserved)\n",stderr);
}
