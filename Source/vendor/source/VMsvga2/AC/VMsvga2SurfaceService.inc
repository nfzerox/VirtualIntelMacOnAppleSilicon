/* Provider-owned, context-free host surface connection. All methods serialize
 * on m_surface_service_lock; no application connection or GL SDK is borrowed.
 * Read failures may have written a prefix: callers MUST keep CPU access locked
 * and the GPU version authoritative until a complete materialization succeeds.
 * No retry/reconnect after transport loss: existing host holds were lost. */
enum { VIMSurfaceServiceChunk=65536 };
IOReturn CLASS::retireBackingTicket(uint64_t const* ns,uint64_t ticket)
{
    if(!ticket || !m_surface_service_lock)return kIOReturnBadArgument;
    IOLockLock(m_surface_service_lock);IOReturn error=openSurfaceService(ns);
    if(!error && !(m_surface_service_capabilities&VIM_GL_SERVICE_CAP_BACKING_TICKETS))error=kIOReturnUnsupported;
    VIMGLServiceMessage message;VIMGLServiceReply reply;bzero(&message,sizeof(message));bzero(&reply,sizeof(reply));
    message.op=VIM_GL_SERVICE_BACKING_RELEASE;message.flags=VIM_GL_SERVICE_REPLY;message.args[0]=ticket;
    if(!error)error=transferSurfaceService(&message,sizeof(message),true);
    if(!error)error=transferSurfaceService(&reply,sizeof(reply),false);
    if(!error && (reply.bytes || reply.error || reply.result!=1))error=kIOReturnIOError;
    IOLockUnlock(m_surface_service_lock);return error;
}
void CLASS::closeSurfaceService()
{
    if(m_surface_service_buffer) {
        VIMGLDevicePacket* packet=static_cast<VIMGLDevicePacket*>(m_surface_service_buffer);
        if(m_surface_service_token && m_framebuffer && m_svga) {
            bzero(packet,sizeof(*packet));packet->magic=VIM_GL_MAGIC;
            packet->action=4;packet->token=m_surface_service_token;directGLTransfer(packet);
        }
        VRAMFree(m_surface_service_buffer);m_surface_service_buffer=0;
    }
    m_surface_service_token=0;
}
IOReturn CLASS::transferSurfaceService(void* bytes,size_t length,bool writing)
{
    if(!m_surface_service_token || !m_surface_service_buffer || m_surface_service_failed)return kIOReturnNotOpen;
    unsigned char* cursor=static_cast<unsigned char*>(bytes);
    while(length) {
        VIMGLDevicePacket* packet=static_cast<VIMGLDevicePacket*>(m_surface_service_buffer);
        uint32_t part=static_cast<uint32_t>(length>VIMSurfaceServiceChunk?VIMSurfaceServiceChunk:length);
        bzero(packet,sizeof(*packet));packet->magic=VIM_GL_MAGIC;packet->token=m_surface_service_token;
        packet->action=writing?2:3;packet->bytes=part;packet->result=-1;
        if(writing)memcpy(packet+1,cursor,part);
        IOReturn error=directGLTransfer(packet);
        if(error || packet->result<=0 || static_cast<uint32_t>(packet->result)>part) {
            m_surface_service_failed=true;closeSurfaceService();return error?error:kIOReturnIOError;
        }
        size_t done=static_cast<size_t>(packet->result);
        if(!writing)memcpy(cursor,packet+1,done);
        cursor+=done;length-=done;
    }
    return kIOReturnSuccess;
}
IOReturn CLASS::openSurfaceService(uint64_t const* namespace_id)
{
    if(!namespace_id || !(namespace_id[0]|namespace_id[1]))return kIOReturnBadArgument;
    if(m_surface_service_failed)return kIOReturnOffline;
    if(m_surface_service_token)return memcmp(namespace_id,m_surface_service_namespace,16)?kIOReturnBadArgument:kIOReturnSuccess;
    if(!directGLAvailable())return kIOReturnUnsupported;
    m_surface_service_buffer=VRAMMalloc(sizeof(VIMGLDevicePacket)+VIMSurfaceServiceChunk);
    if(!m_surface_service_buffer)return kIOReturnNoMemory;
    VIMGLDevicePacket* packet=static_cast<VIMGLDevicePacket*>(m_surface_service_buffer);
    bzero(packet,sizeof(*packet));packet->magic=VIM_GL_MAGIC;packet->action=1;packet->result=-1;
    IOReturn error=directGLTransfer(packet);
    if(!error && (!packet->token || packet->result))error=kIOReturnIOError;
    if(!error) {
        m_surface_service_token=packet->token;
        VIMGLServiceMessage hello;VIMGLServiceReply reply;bzero(&hello,sizeof(hello));bzero(&reply,sizeof(reply));
        hello.op=VIM_GL_SERVICE_HELLO;hello.flags=VIM_GL_SERVICE_REPLY;
        hello.args[0]=VIM_GL_SERVICE_MAGIC;hello.args[1]=VIM_GL_SERVICE_VERSION;
        memcpy(&hello.args[3],namespace_id,16);
        error=transferSurfaceService(&hello,sizeof(hello),true);
        if(!error)error=transferSurfaceService(&reply,sizeof(reply),false);
        if(!error && (reply.bytes || reply.error ||
           (reply.result&(VIM_GL_SERVICE_CAP_HOLDS|VIM_GL_SERVICE_CAP_READ))!=(VIM_GL_SERVICE_CAP_HOLDS|VIM_GL_SERVICE_CAP_READ)))error=kIOReturnUnsupported;
        if(!error)m_surface_service_capabilities=reply.result;
    }
    if(error) { m_surface_service_failed=true;closeSurfaceService(); }
    else memcpy(m_surface_service_namespace,namespace_id,16);
    return error;
}
IOReturn CLASS::holdGPUSurface(uint64_t const* namespace_id,uint64_t object,uint64_t incarnation,uint32_t width,uint32_t height,uint32_t* hold,uint64_t epoch)
{
    if(!m_surface_service_lock || !hold || !object || !incarnation || !width || !height || width>8192 || height>8192)return kIOReturnBadArgument;
    *hold=0;IOLockLock(m_surface_service_lock);
    IOReturn error=openSurfaceService(namespace_id);
    if(!error && epoch && !(m_surface_service_capabilities&VIM_GL_SERVICE_CAP_VERSIONS))error=kIOReturnUnsupported;
    if(!error) {
        VIMGLServiceMessage message;VIMGLServiceReply reply;bzero(&message,sizeof(message));bzero(&reply,sizeof(reply));
        message.op=VIM_GL_SERVICE_HOLD;message.flags=VIM_GL_SERVICE_REPLY;
        message.args[0]=object;message.args[1]=incarnation;message.args[2]=width;message.args[3]=height;
        message.args[4]=epoch;
        error=transferSurfaceService(&message,sizeof(message),true);
        if(!error)error=transferSurfaceService(&reply,sizeof(reply),false);
        if(!error && reply.bytes) { m_surface_service_failed=true;closeSurfaceService();error=kIOReturnIOError; }
        if(!error && (reply.error || !reply.result || reply.result>UINT32_MAX))error=kIOReturnError;
        if(!error)*hold=static_cast<uint32_t>(reply.result);
    }
    IOLockUnlock(m_surface_service_lock);return error;
}
IOReturn CLASS::releaseGPUSurface(uint32_t hold)
{
    if(!hold || !m_surface_service_lock)return kIOReturnBadArgument;
    IOLockLock(m_surface_service_lock);
    VIMGLServiceMessage message;VIMGLServiceReply reply;bzero(&message,sizeof(message));bzero(&reply,sizeof(reply));
    message.op=VIM_GL_SERVICE_UNHOLD;message.flags=VIM_GL_SERVICE_REPLY;message.args[0]=hold;
    IOReturn error=transferSurfaceService(&message,sizeof(message),true);
    if(!error)error=transferSurfaceService(&reply,sizeof(reply),false);
    if(!error && reply.bytes) { m_surface_service_failed=true;closeSurfaceService();error=kIOReturnIOError; }
    if(!error && reply.error)error=kIOReturnError;
    IOLockUnlock(m_surface_service_lock);return error;
}
/* Metadata-only native-display handoff. This does not enable page-flip
 * capabilities by itself: the surface family must own the correct version
 * and the caller must preserve its CPU materialization/locking contract. */
IOReturn CLASS::presentGPUSurface(uint32_t hold,uint64_t epoch,uint64_t* ticket)
{
    if(!hold || !epoch || !ticket || !m_surface_service_lock)return kIOReturnBadArgument;
    *ticket=0;IOLockLock(m_surface_service_lock);
    IOReturn error=(m_surface_service_capabilities&VIM_GL_SERVICE_CAP_PRESENT)?kIOReturnSuccess:kIOReturnUnsupported;
    VIMGLServiceMessage message;VIMGLServiceReply reply;bzero(&message,sizeof(message));bzero(&reply,sizeof(reply));
    message.op=VIM_GL_SERVICE_PRESENT;message.flags=VIM_GL_SERVICE_REPLY;
    message.args[0]=hold;message.args[1]=epoch;
    if(!error)error=transferSurfaceService(&message,sizeof(message),true);
    if(!error)error=transferSurfaceService(&reply,sizeof(reply),false);
    if(!error && reply.bytes){m_surface_service_failed=true;closeSurfaceService();error=kIOReturnIOError;}
    if(!error && (reply.error || !reply.result))error=kIOReturnNotReady;
    if(!error)*ticket=reply.result;
    IOLockUnlock(m_surface_service_lock);return error;
}
IOReturn CLASS::gpuPresentationReady(bool* ready)
{
    if(!ready || !m_surface_service_lock)return kIOReturnBadArgument;
    *ready=false;IOLockLock(m_surface_service_lock);
    IOReturn error=(m_surface_service_capabilities&VIM_GL_SERVICE_CAP_PRESENT)?kIOReturnSuccess:kIOReturnUnsupported;
    VIMGLServiceMessage message;VIMGLServiceReply reply;bzero(&message,sizeof(message));bzero(&reply,sizeof(reply));
    message.op=VIM_GL_SERVICE_READY;message.flags=VIM_GL_SERVICE_REPLY;
    if(!error)error=transferSurfaceService(&message,sizeof(message),true);
    if(!error)error=transferSurfaceService(&reply,sizeof(reply),false);
    if(!error && reply.bytes){m_surface_service_failed=true;closeSurfaceService();error=kIOReturnIOError;}
    if(!error && (reply.error || reply.result>1))error=kIOReturnIOError;
    if(!error)*ready=reply.result!=0;
    IOLockUnlock(m_surface_service_lock);return error;
}
IOReturn CLASS::readGPUSurface(uint32_t hold,uint64_t epoch,uint32_t x,uint32_t y,uint32_t width,uint32_t height,
    IOMemoryDescriptor* destination,uint64_t offset,uint64_t stride)
{
    uint64_t row=uint64_t(width)*4;
    if(!m_surface_service_lock || !hold || !epoch || !destination || !width || !height || width>8192 || height>8192 ||
       x>8192-width || y>8192-height || stride<row ||
       offset>destination->getLength() || row>destination->getLength()-offset ||
       uint64_t(height-1)>(destination->getLength()-offset-row)/stride)return kIOReturnBadArgument;
    /* A small, bounded CPU staging buffer is used only on genuine CPU reads.
     * Never allocate a full image or put 64 KiB on the kernel stack. */
    void* scratch=IOMalloc(VIMSurfaceServiceChunk);if(!scratch)return kIOReturnNoMemory;
    IOLockLock(m_surface_service_lock);
    IOReturn error=kIOReturnSuccess,write_error=kIOReturnSuccess;
    uint32_t limit=(64U*1024U*1024U)/(width*4);
    // Keep each reply within the service's existing wire limit; every strip
    // reads the same retained epoch and the caller owns CPU materialization.
    for(uint32_t first=0;!error && !write_error && first<height;) {
        uint32_t rows=height-first<limit?height-first:limit;
        uint64_t total=row*rows;
        VIMGLServiceMessage message;VIMGLServiceReply reply;bzero(&message,sizeof(message));bzero(&reply,sizeof(reply));
        message.op=VIM_GL_SERVICE_READ;message.flags=VIM_GL_SERVICE_REPLY|VIM_GL_SERVICE_OUTPUT_ONLY;
        message.bytes=static_cast<uint32_t>(total);message.args[0]=hold;message.args[1]=epoch;
        message.args[2]=x;message.args[3]=y+first;message.args[4]=width;message.args[5]=rows;
        error=transferSurfaceService(&message,sizeof(message),true);
        if(!error)error=transferSurfaceService(&reply,sizeof(reply),false);
        if(!error && reply.bytes!=total) { m_surface_service_failed=true;closeSurfaceService();error=kIOReturnIOError; }
        if(reply.error)write_error=kIOReturnNotReady;
        for(uint64_t position=0;!error && position<total;) {
            size_t part=static_cast<size_t>(total-position>VIMSurfaceServiceChunk?VIMSurfaceServiceChunk:total-position);
            error=transferSurfaceService(scratch,part,false);
            if(error)break;
            for(size_t consumed=0;!write_error && consumed<part;) {
                uint64_t cursor=position+consumed,column=cursor%row;
                size_t bytes=static_cast<size_t>(row-column<part-consumed?row-column:part-consumed);
                if(destination->writeBytes(offset+(first+cursor/row)*stride+column,static_cast<char*>(scratch)+consumed,bytes)!=bytes)
                    { write_error=kIOReturnVMError;break; }
                consumed+=bytes;
            }
            position+=part; // Drain failed output to preserve framing.
        }
        first+=rows;
    }
    IOLockUnlock(m_surface_service_lock);IOFree(scratch,VIMSurfaceServiceChunk);
    return error?error:write_error;
}
