/* Local research: retain the established GFBOutDirect rectangle/order
 * contract, but execute its copies in native QEMU instead of x86 TCG.
 * Sources are ALREADY prepared by the surface backing's lifetime. */
IOReturn CLASS::directVRAMDirty(uint64_t offset,uint64_t bytes)
{
    if(!m_framebuffer_blit_lock || !m_framebuffer || !m_svga || !bytes ||
       bytes>VIM_GL_SCATTER_MAX_BYTES || offset>getVRAMSize() || bytes>getVRAMSize()-offset)
        return kIOReturnUnsupported;
    IOLockLock(m_framebuffer_blit_lock);IOReturn error=kIOReturnNoMemory;
    if(!m_framebuffer_blit_buffer && setupAllocator()==kIOReturnSuccess)
        m_framebuffer_blit_buffer=VRAMMalloc(sizeof(VIMGLDevicePacket)+VIM_GL_CHUNK);
    if(m_framebuffer_blit_buffer){
        VIMGLDevicePacket* p=static_cast<VIMGLDevicePacket*>(m_framebuffer_blit_buffer);
        bzero(p,sizeof(*p));p->magic=VIM_GL_MAGIC;p->action=12;p->bytes=bytes;p->reserved=offset;p->result=-1;
        // Dirty publication has no mode/index-latch dependency. The packet
        // is already owned by m_framebuffer_blit_lock; use the negotiated
        // independent transport rather than waiting for unrelated GL I/O
        // under the framebuffer's global indexed-register lock. Old hosts
        // retain directGLTransfer's existing conservative indexed fallback.
        error=directGLTransfer(p);
        if(!error)error=p->result==static_cast<int32_t>(bytes)?kIOReturnSuccess:kIOReturnUnsupported;
    }
    IOLockUnlock(m_framebuffer_blit_lock);return error;
}
IOReturn CLASS::directRAMDirty(uint64_t const* pages,uint32_t count)
{
    if(!m_framebuffer_blit_lock || !m_framebuffer || !m_svga || !pages || !count || count>16384)
        return kIOReturnUnsupported;
    IOLockLock(m_framebuffer_blit_lock);IOReturn error=kIOReturnNoMemory;
    if(!m_framebuffer_blit_buffer && setupAllocator()==kIOReturnSuccess)
        m_framebuffer_blit_buffer=VRAMMalloc(sizeof(VIMGLDevicePacket)+VIM_GL_CHUNK);
    if(m_framebuffer_blit_buffer){
        VIMGLDevicePacket* p=static_cast<VIMGLDevicePacket*>(m_framebuffer_blit_buffer);
        bzero(p,sizeof(*p));p->magic=VIM_GL_MAGIC;p->action=12;p->token=1;
        p->bytes=count*4096;p->reserved=count;p->result=-1;
        memcpy(p+1,pages,count*sizeof(uint64_t));
        // This is the actual CPU-gate restoration path, before surface
        // waiters are woken. It must not queue behind another context's
        // synchronous GL service on the shared register-index lock. Keep
        // the same prepared page list, QEMU dirty action and completion ACK.
        error=directGLTransfer(p);
        if(!error)error=p->result==static_cast<int32_t>(p->bytes)?kIOReturnSuccess:kIOReturnUnsupported;
    }
    IOLockUnlock(m_framebuffer_blit_lock);return error;
}

IOReturn CLASS::directFramebufferControl(uintptr_t mode,void* output,size_t* size)
{
    if (mode>14 || !output || !size || *size!=sizeof(VIMGLFramebufferStats))return kIOReturnBadArgument;
    if (!m_framebuffer_blit_lock)return kIOReturnNotReady;
    IOLockLock(m_framebuffer_blit_lock);
    if (mode==1 || mode==2)m_framebuffer_blit_enabled=mode==2;
    if (mode==3 || mode==4)m_direct_ram_upload_enabled=mode==4;
    if (mode==5 || mode==6)m_direct_ram_bulk_upload_enabled=mode==6;
    if (mode==7 || mode==8)m_direct_gl_doorbell_enabled=mode==8;
    // One atomic-width policy value, sampled once per owned stream write.
    // No mapping, pending job or associated lifetime changes with this knob.
    if (mode>=9 && mode<=12)m_direct_command_copy_limit=mode==9?0:mode==10?4096:mode==11?32768:65536;
    if (mode==13 || mode==14)m_direct_gl_parallel_doorbell_enabled=mode==14;
    VIMGLFramebufferStats stats={1,0,m_framebuffer_blits,m_framebuffer_blit_bytes,m_framebuffer_blit_fallbacks};
    stats.flags=(m_framebuffer_blit_enabled?1u:0u)|(m_framebuffer_blit_supported==1?2u:0u)|
                (m_direct_ram_upload_enabled?4u:0u)|(m_direct_ram_bulk_upload_enabled?8u:0u)|
                (directGLBulkUploadAvailable()?16u:0u)|
                (m_direct_gl_doorbell_supported?32u:0u)|
                (m_direct_gl_doorbell_supported && m_direct_gl_doorbell_enabled?64u:0u)|
                256u|(m_direct_gl_parallel_doorbell_enabled && m_direct_gl_doorbell_supported && m_direct_gl_doorbell_enabled?512u:0u)|
                (m_direct_command_copy_limit?128u:0u)|((m_framebuffer_blit_rejection&63u)<<10)|((m_direct_command_copy_limit/1024u)<<16);
    memcpy(output,&stats,sizeof(stats));
    IOLockUnlock(m_framebuffer_blit_lock);return kIOReturnSuccess;
}

IOReturn CLASS::directFramebufferBlit(IOMemoryDescriptor* memory,uint64_t source_offset,
                                     uint64_t source_length,uint32_t source_pitch,void const* region)
{
    const IOAccelDeviceRegion* rgn=static_cast<const IOAccelDeviceRegion*>(region);
    if (!rgn || !m_framebuffer_blit_lock || !m_framebuffer || !m_svga)return kIOReturnUnsupported;
    IOLockLock(m_framebuffer_blit_lock);
    IOReturn result=kIOReturnUnsupported;
    uint32_t rejection=1;
    if (m_framebuffer_blit_supported<0) {
        m_framebuffer->lockDevice();
        m_framebuffer_blit_supported=m_svga->ReadReg(VIM_GL_FRAMEBUFFER_BLIT_CAPABILITIES_REGISTER)==VIM_GL_FRAMEBUFFER_BLIT_MAGIC;
        m_framebuffer->unlockDevice();
    }
    if (!m_framebuffer_blit_enabled || !m_framebuffer_blit_supported)goto out;
    if (!rgn->num_rects) { result=kIOReturnSuccess;goto out; }
    rejection=2;
    if (!source_pitch || !source_length || source_offset>UINT64_MAX-source_length ||
        (memory && (source_offset>memory->getLength() || source_length>memory->getLength()-source_offset)))goto out;
    if (!m_framebuffer_blit_buffer) {
        rejection=3;
        if (setupAllocator()!=kIOReturnSuccess)goto out;
        m_framebuffer_blit_buffer=VRAMMalloc(sizeof(VIMGLDevicePacket)+VIM_GL_CHUNK);
        if (!m_framebuffer_blit_buffer)goto out;
    }
    {
        VIMGLDevicePacket* packet=static_cast<VIMGLDevicePacket*>(m_framebuffer_blit_buffer);
        VIMGLFramebufferSegment* segments=reinterpret_cast<VIMGLFramebufferSegment*>(packet+1);
        uint32_t count=0,total=0;
        uint64_t vram_base=0,vram_length=0;
        rejection=4;
        if (!directGLVRAMRange(&vram_base,&vram_length))goto out;
        // Hold the same device lock through mode capture, vector generation
        // and execution. Mode changes cannot invalidate framebuffer bounds.
        m_framebuffer->lockDevice();
        uint32_t destination_pitch=m_svga->getCurrentPitch();
        uint64_t destination_base=m_svga->getCurrentFBOffset();
        uint64_t destination_length=m_svga->getCurrentFBSize();
        bool valid=destination_pitch && destination_base<=vram_length && destination_length<=vram_length-destination_base;
        rejection=5;
        for (uint32_t i=0;valid && i<rgn->num_rects;++i) {
            const IOAccelBounds& rect=rgn->rect[i];
            if (rect.w<=0 || rect.h<=0)continue;
            uint32_t row_bytes=static_cast<uint32_t>(rect.w)*4;
            int64_t src=(static_cast<int64_t>(rect.y)-rgn->bounds.y)*source_pitch+
                        (static_cast<int64_t>(rect.x)-rgn->bounds.x)*4;
            int64_t dst=static_cast<int64_t>(rect.y)*destination_pitch+static_cast<int64_t>(rect.x)*4;
            for (int row=0;valid && row<rect.h;++row) {
                // Out-of-bounds legacy rows have unusual skip semantics.
                // Preserve them via the old implementation, BEFORE writes.
                if (src<0 || dst<0 || static_cast<uint64_t>(src)>source_length ||
                    row_bytes>source_length-static_cast<uint64_t>(src) ||
                    static_cast<uint64_t>(dst)>destination_length || row_bytes>destination_length-static_cast<uint64_t>(dst) ||
                    total>VIM_GL_SCATTER_MAX_BYTES-row_bytes) { rejection=6;valid=false;break; }
                uint64_t position=source_offset+static_cast<uint64_t>(src);
                uint64_t destination=destination_base+static_cast<uint64_t>(dst);
                uint32_t remaining=row_bytes;
                while (valid && remaining) {
                    IOByteCount length=remaining;
                    addr64_t address=memory?VIMPhysicalSegment(memory,position,&length):position;
                    if (length>remaining)length=remaining;
                    uint32_t kind=memory?1:0;
                    if (memory && address>=vram_base && address-vram_base<vram_length) {
                        address-=vram_base;kind=0;
                    }
                    if (!length || address>UINT64_MAX-length || destination>UINT32_MAX ||
                        (!kind && (address>vram_length || length>vram_length-address))) { rejection=7;valid=false;break; }
                    if (count && segments[count-1].kind==kind &&
                        segments[count-1].source+segments[count-1].bytes==address &&
                        static_cast<uint64_t>(segments[count-1].destination)+segments[count-1].bytes==destination) {
                        segments[count-1].bytes+=static_cast<uint32_t>(length);
                    } else if (count<VIM_GL_FRAMEBUFFER_BLIT_MAX_SEGMENTS) {
                        VIMGLFramebufferSegment& segment=segments[count++];
                        segment.source=address;segment.destination=static_cast<uint32_t>(destination);
                        segment.bytes=static_cast<uint32_t>(length);segment.kind=kind;segment.reserved=0;
                    } else { rejection=8;valid=false;break; }
                    remaining-=static_cast<uint32_t>(length);position+=length;destination+=length;
                }
                total+=row_bytes;src+=source_pitch;dst+=destination_pitch;
            }
        }
        if (valid && !count)result=kIOReturnSuccess;
        else if (valid) {
            rejection=9;
            bzero(packet,sizeof(*packet));packet->magic=VIM_GL_MAGIC;packet->action=9;
            packet->bytes=total;packet->reserved=count;packet->result=-1;
            // The geometry lock is already held. Use the same negotiated,
            // memory-clobbered packet doorbell as the other shared-memory
            // transfers; do not reacquire that lock via directGLTransfer.
            if(m_direct_gl_doorbell_supported && m_direct_gl_doorbell_enabled)
                m_svga->WriteDirectGLDoorbell(static_cast<uint32_t>(offsetInVRAM(packet)));
            else
                m_svga->WriteReg(VIM_GL_REGISTER,static_cast<uint32_t>(offsetInVRAM(packet)));
            if (packet->result==static_cast<int32_t>(total)) {
                ++m_framebuffer_blits;m_framebuffer_blit_bytes+=total;result=kIOReturnSuccess;
            }
        }
        m_framebuffer->unlockDevice();
    }
out:
    m_framebuffer_blit_rejection=result==kIOReturnSuccess?0:rejection;
    if (result!=kIOReturnSuccess)++m_framebuffer_blit_fallbacks;
    IOLockUnlock(m_framebuffer_blit_lock);return result;
}
