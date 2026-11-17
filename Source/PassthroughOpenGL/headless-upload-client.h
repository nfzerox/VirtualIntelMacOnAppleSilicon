/* The caller serializes this connection. Use the existing prepared-RAM
 * upload contract for one large stream span instead of copyin/mailbox
 * transfers in 256-KB pieces. No GL opcode, driver or host ABI changes.
 * Return -1 only before any payload is submitted (copied transfer is safe),
 * 0 after a failed submission/release (never replay), or 1 after consumption.
 */
#ifndef VIM_HEADLESS_UPLOAD_CLIENT_H
#define VIM_HEADLESS_UPLOAD_CLIENT_H
static int DGHeadlessWritePrepared(io_connect_t device,const void *pixels,size_t bytes)
{
    if(!pixels || !bytes || bytes>VIM_GL_SCATTER_MAX_BYTES)return -1;
    struct {struct VIMGLScatterRequest header;struct VIMGLScatterRows rows;} prepare={
        {4,1,0,(uint32_t)bytes},{(uintptr_t)pixels,bytes,(uint32_t)bytes,0}};
    struct VIMGLScatterResult result={0};size_t size=sizeof(result);
    IOReturn error=IOConnectCallStructMethod(device,kIOVM2DDirectGLScatter,
        &prepare,sizeof(prepare),&result,&size);
    if(error)return error==kIOReturnUnsupported || error==kIOReturnNoMemory ||
        error==kIOReturnNoResources || error==kIOReturnExclusiveAccess?-1:0;
    if(size!=sizeof(result) || result.count || !result.token)return 0;
    uint32_t token=result.token;
    struct VIMGLScatterRequest send={5,0,token,(uint32_t)bytes};size=sizeof(result);
    error=IOConnectCallStructMethod(device,kIOVM2DDirectGLScatter,&send,sizeof(send),&result,&size);
    int okay=!error && size==sizeof(result) && result.count==(int32_t)bytes && result.token==token;
    // Release even if the upload failed or consumed only a prefix. Its task
    // pages cannot be unpinned until the synchronous host write has retired.
    struct VIMGLScatterRequest release={2,0,token,0};size=sizeof(result);
    error=IOConnectCallStructMethod(device,kIOVM2DDirectGLScatter,&release,sizeof(release),&result,&size);
    return okay && !error && size==sizeof(result) && !result.count && !result.token;
}
#endif
