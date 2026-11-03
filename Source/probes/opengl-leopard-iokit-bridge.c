/* Compare the period bridge's real service/right ownership with native IOKit.
 * Finite owned connection controls, runnable in either width on reference/VM. */
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
static kern_return_t NativeRetain(io_object_t object){return IOObjectRetain(object);}
static kern_return_t NativeStruct(io_connect_t c,uint32_t s,const void *i,size_t n,void *o,size_t *z)
{return IOConnectCallStructMethod(c,s,i,n,o,z);}
static kern_return_t NativeScalar(io_connect_t c,uint32_t s,const uint64_t *i,uint32_t n,uint64_t *o,uint32_t *z)
{return IOConnectCallScalarMethod(c,s,i,n,o,z);}
#include "../PassthroughOpenGL/iokit-bridge-compat.h"
#include "../vendor/source/VMsvga2/AC/UCMethods.h"
#include "../vendor/source/VMsvga2/vminclude/vim_passthrough_opengl.h"
#include "../vendor/source/VMsvga2/vminclude/vim_gl_surface_service.h"
int main(int argc,char **argv)
{
    if(argc>2)return 64;const char *name=argc==2?argv[1]:"IOPCIDevice";
    io_service_t native=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching(name));
    io_service_t bridge=VIMIOServiceForClass(name);
    printf("class=%s native=%x bridge=%x bits=%u\n",name,native,bridge,(unsigned)(8*sizeof(void*)));
    assert(native&&bridge&&native==bridge&&VIMIOObjectConformsTo(bridge,name));
    mach_port_urefs_t before=0,after=0;assert(!mach_port_get_refs(mach_task_self(),bridge,MACH_PORT_RIGHT_SEND,&before));
    assert(!IOObjectRetain(bridge));assert(!mach_port_get_refs(mach_task_self(),bridge,MACH_PORT_RIGHT_SEND,&after));
    assert(after==before+1);assert(!IOObjectRelease(bridge));
    assert(!NativeRetain(bridge));assert(!mach_port_get_refs(mach_task_self(),bridge,MACH_PORT_RIGHT_SEND,&after));
    assert(after==before+1);assert(!IOObjectRelease(bridge));
    assert(!VIMIOServiceForClass("VIM_No_Such_Class_20261029"));
    const char *property=!strcmp(name,"VMsvga2Accel")?VIM_GL_BOOT_NAMESPACE_PROPERTY:"vendor-id";
    CFStringRef key=CFStringCreateWithCString(NULL,property,kCFStringEncodingASCII);
    CFDataRef data=(CFDataRef)IORegistryEntryCreateCFProperty(native,key,kCFAllocatorDefault,0);
    assert(data&&CFGetTypeID(data)==CFDataGetTypeID());CFIndex length=CFDataGetLength(data);unsigned char bytes[256]={0};
    assert(length>0&&length<(CFIndex)sizeof(bytes)&&VIMIOCopyDataProperty(bridge,property,bytes,length));
    assert(!memcmp(bytes,CFDataGetBytePtr(data),length));assert(!VIMIOCopyDataProperty(bridge,property,bytes,length-1));
    CFRelease(data);CFRelease(key);puts("PASS fixed-size data bytes and short-buffer rejection match native CFData");
    if(!strcmp(name,"VMsvga2Accel")){
        io_connect_t connection=0;assert(!IOServiceOpen(bridge,mach_task_self(),2,&connection)&&connection);
        struct VIMGLTransfer request={1,0,0};struct VIMGLResult a={0},b={0};size_t az=sizeof(a),bz=sizeof(b);
        kern_return_t ae=NativeStruct(connection,kIOVM2DDirectGLTransfer,&request,sizeof(request),&a,&az);
        // Action1 claims its own stream; it is not repeatable on a connection.
        assert(!IOServiceClose(connection));connection=0;
        assert(!IOServiceOpen(bridge,mach_task_self(),2,&connection)&&connection);
        kern_return_t be=IOConnectCallStructMethod(connection,kIOVM2DDirectGLTransfer,&request,sizeof(request),&b,&bz);
        printf("struct native=%x bridge=%x bytes=%lu,%lu count=%d,%d caps=%x,%x\n",ae,be,(unsigned long)az,(unsigned long)bz,a.count,b.count,a.reserved,b.reserved);fflush(stdout);
        assert(!ae&&!be&&az==bz&&az==sizeof(a)&&!memcmp(&a,&b,sizeof(a)));
        uint32_t ac=0,bc=0;
        ae=NativeScalar(connection,0x7fffffff,NULL,0,NULL,&ac);
        be=IOConnectCallScalarMethod(connection,0x7fffffff,NULL,0,NULL,&bc);
        assert(ae==be&&ae!=0&&ac==bc);assert(!IOServiceClose(connection));
        puts("PASS actual struct query and scalar failure equivalence");
    }
    assert(!IOObjectRelease(native));assert(!IOObjectRelease(bridge));
    puts("PASS native matching identity, send-right retain/release and absent class");return 0;
}
