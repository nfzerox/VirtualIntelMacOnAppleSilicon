/* Actual guest IOUserClient lifetime/admission probe; no renderer replacement.
 * Run each architecture against the experimental non-display PCI service. */
#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../vendor/source/VMsvga2/vminclude/vim_gl_headless.h"
#include "../vendor/source/VMsvga2/AC/UCMethods.h"
static int failed;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);failed=1; } } while(0)
int main(void)
{
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,
        IOServiceMatching("PassthroughOpenGLTransport"));
    CHECK(service);if(!service)return 2;
    for(unsigned round=0;round<2;++round){
        io_connect_t clients[VIM_GL_HEADLESS_SLOTS]={0};
        unsigned opened=0;
        for(unsigned i=0;i<VIM_GL_HEADLESS_SLOTS;++i){
            IOReturn status=IOServiceOpen(service,mach_task_self(),2,&clients[i]);
            // A live desktop may already own endpoint slots. Exercise the
            // available capacity without mistaking that usage for failure.
            if(status){CHECK(status==kIOReturnNoResources);break;}
            opened++;
            struct VIMGLTransfer request={1,0,0};struct VIMGLResult reply={-1,0};size_t bytes=sizeof(reply);
            status=IOConnectCallStructMethod(clients[i],kIOVM2DDirectGLTransfer,&request,sizeof(request),&reply,&bytes);
            CHECK(!status && bytes==sizeof(reply) && !reply.count);
            CHECK((reply.reserved & ~VIM_GL_HEADLESS_RAM_CAPABILITIES)==
                (VIM_GL_RESULT_CAP_EXCHANGE|VIM_GL_RESULT_CAP_DRAINED_CLOSE));
            bytes=sizeof(reply);
            CHECK(IOConnectCallStructMethod(clients[i],kIOVM2DDirectGLTransfer,&request,sizeof(request),&reply,&bytes)!=0);
            // Rejected task pointers must never submit bytes to the stream.
            request=(struct VIMGLTransfer){2,1,1};bytes=sizeof(reply);
            CHECK(IOConnectCallStructMethod(clients[i],kIOVM2DDirectGLTransfer,&request,sizeof(request),&reply,&bytes)!=0);
        }
        CHECK(opened>=2);printf("round=%u opened=%u ambient=%u\n",round,opened,VIM_GL_HEADLESS_SLOTS-opened);
        io_connect_t extra=0;
        CHECK(IOServiceOpen(service,mach_task_self(),2,&extra)!=kIOReturnSuccess);
        if(extra)IOServiceClose(extra);
        for(unsigned i=0;i<VIM_GL_HEADLESS_SLOTS;++i)if(clients[i])CHECK(!IOServiceClose(clients[i]));
    }
    IOObjectRelease(service);
    puts(failed?"FAIL headless endpoint lifetime":"PASS two endpoint exhaustion rounds, duplicate open, invalid task pointer and drained close");
    return failed?1:0;
}
