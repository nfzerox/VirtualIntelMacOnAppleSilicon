/* Finite protocol checks using the production client helper. No VM or real
 * IOKit device: verify safe fallback and pin release after consumed failures. */
#include <IOKit/IOKitLib.h>
#include "../vendor/source/VMsvga2/AC/UCMethods.h"
#include "../vendor/source/VMsvga2/vminclude/vim_passthrough_opengl.h"
#include <assert.h>
#include <stdio.h>
static IOReturn prepare_error,send_error,release_error;
static unsigned calls,partial;
static unsigned char pixels[17];
static IOReturn MockMethod(io_connect_t device,uint32_t method,const void *input,size_t bytes,
                          void *output,size_t *size)
{
    assert(device==123 && method==kIOVM2DDirectGLScatter && *size==sizeof(struct VIMGLScatterResult));
    const struct VIMGLScatterRequest *request=input;
    struct VIMGLScatterResult *result=output;calls++;
    if(calls==1){
        assert(bytes==40 && request->action==4 && request->count==1 && !request->token && request->bytes==sizeof(pixels));
        const struct VIMGLScatterRows *rows=(const void *)(request+1);
        assert(rows->address==(uintptr_t)pixels && rows->stride==sizeof(pixels) &&
            rows->row_bytes==sizeof(pixels) && !rows->reverse);
        *result=(struct VIMGLScatterResult){0,456};return prepare_error;
    }
    assert(bytes==sizeof(*request) && !request->count && request->token==456);
    if(calls==2){
        assert(request->action==5 && request->bytes==sizeof(pixels));
        *result=(struct VIMGLScatterResult){partial?1:sizeof(pixels),456};return send_error;
    }
    assert(calls==3 && request->action==2 && !request->bytes);
    *result=(struct VIMGLScatterResult){0,0};return release_error;
}
#define IOConnectCallStructMethod MockMethod
#include "../PassthroughOpenGL/headless-upload-client.h"
#undef IOConnectCallStructMethod
int main(void)
{
    assert(DGHeadlessWritePrepared(123,pixels,sizeof(pixels))==1 && calls==3);
    const IOReturn fallback[]={kIOReturnUnsupported,kIOReturnNoMemory,kIOReturnNoResources,kIOReturnExclusiveAccess};
    for(unsigned i=0;i<sizeof(fallback)/sizeof(*fallback);i++){
        calls=0;prepare_error=fallback[i];
        assert(DGHeadlessWritePrepared(123,pixels,sizeof(pixels))==-1 && calls==1);
    }
    calls=0;prepare_error=kIOReturnVMError;
    assert(DGHeadlessWritePrepared(123,pixels,sizeof(pixels))==0 && calls==1);
    calls=0;prepare_error=0;send_error=kIOReturnIOError;
    assert(DGHeadlessWritePrepared(123,pixels,sizeof(pixels))==0 && calls==3);
    calls=0;send_error=0;partial=1;
    assert(DGHeadlessWritePrepared(123,pixels,sizeof(pixels))==0 && calls==3);
    calls=0;partial=0;release_error=kIOReturnIOError;
    assert(DGHeadlessWritePrepared(123,pixels,sizeof(pixels))==0 && calls==3);
    calls=0;release_error=0;
    assert(DGHeadlessWritePrepared(123,NULL,sizeof(pixels))==-1 && !calls);
    assert(DGHeadlessWritePrepared(123,pixels,0)==-1 && !calls);
    puts("PASS prepared headless upload, pre-submit fallback, consumed failure without replay and unconditional release");
    return 0;
}
