/* The real public CGL/OpenCL contract used by Yosemite Photos. Run on the
 * physical Intel reference first; never dereference private driver objects. */
#include <ApplicationServices/ApplicationServices.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenCL/opencl.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    setbuf(stdout,NULL);
    CGLRendererInfoObj renderers=NULL;GLint renderer_count=0,selected=0;
    GLuint mask=CGDisplayIDToOpenGLDisplayMask(CGMainDisplayID());
    if(CGLQueryRendererInfo(mask,&renderers,&renderer_count))return 2;
    for(GLint i=0;i<renderer_count;++i){
        GLint online=0,accelerated=0,id=0;
        CGLDescribeRenderer(renderers,i,kCGLRPOnline,&online);
        CGLDescribeRenderer(renderers,i,kCGLRPAccelerated,&accelerated);
        CGLDescribeRenderer(renderers,i,kCGLRPRendererID,&id);
        if(online&&accelerated){if(selected&&selected!=id)return 77;selected=id;}
    }
    CGLDestroyRendererInfo(renderers);if(!selected)return 77;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFARendererID,(CGLPixelFormatAttribute)selected,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj gl=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf||CGLCreateContext(pf,NULL,&gl)||!gl)return 2;
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(gl);
    printf("GL vendor=%s renderer=%s\n",glGetString(GL_VENDOR),glGetString(GL_RENDERER));
    if(!strstr((const char *)glGetString(GL_VENDOR),"Intel"))return 77;
    cl_context_properties properties[]={CL_CONTEXT_PROPERTY_USE_CGL_SHAREGROUP_APPLE,
        (cl_context_properties)CGLGetShareGroup(gl),0};
    puts("calling clCreateContextFromType with the actual public CGL share group");
    cl_int error=0;cl_context cl=clCreateContextFromType(properties,CL_DEVICE_TYPE_GPU,NULL,NULL,&error);
    printf("CL error=%d context=%s\n",error,cl?"created":"none");
    if(cl){
        cl_device_id devices[16]={0};size_t bytes=0;
        if(!clGetContextInfo(cl,CL_CONTEXT_DEVICES,sizeof(devices),devices,&bytes))
            for(unsigned i=0;i<bytes/sizeof(devices[0]);++i){
                char name[256]={0};clGetDeviceInfo(devices[i],CL_DEVICE_NAME,sizeof(name),name,NULL);
                printf("CL device=%s\n",name);
            }
        clReleaseContext(cl);
    }
    CGLSetCurrentContext(NULL);CGLDestroyContext(gl);return error?1:0;
}
