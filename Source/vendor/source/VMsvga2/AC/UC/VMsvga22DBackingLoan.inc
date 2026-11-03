/* Scoped pin and CPU exclusion belong to the SAME transport user client.
 * No cross-client lock donation, reference cycle, or user/kernel pointer ABI.
 * Normal End is the trusted GL client's completed-host-use boundary. Abort
 * and clientClose instead retire the entire negotiated drained-close stream.
 * This interface alone does NOT enable a host shared render target. */
bool CLASS::close_direct_stream()
{
    if(!m_direct_token)return true;
    if(!m_provider || !m_direct_buffer)return false;
    VIMGLDevicePacket* packet=static_cast<VIMGLDevicePacket*>(m_direct_buffer);
    bzero(packet,sizeof(*packet));packet->magic=VIM_GL_MAGIC;packet->action=4;
    packet->token=m_direct_token;packet->result=-1;
    IOReturn error=m_provider->directGLTransfer(packet);
    if(error || packet->result)return false;
    m_direct_token=0;return true;
}
IOReturn CLASS::reserve_ram_access()
{
    if(m_ram_access_owner)return kIOReturnSuccess;
    if(!m_provider || !m_surface_client || m_surface_future)return kIOReturnNotOpen;
    VMsvga2PreparedBacking* owner=0;
    IOReturn error=m_surface_client->context_copy_ram_owner(&owner);if(error)return error;
    void* key=0;IOReturn profile_error=0;IOMemoryMap* map=owner->copyAccessMap(&key,&profile_error);
    error=m_provider->borrowBackingAccess(map?key:0,this);if(map)map->release();
    if(error){owner->release();return error;}
    m_ram_access_owner=owner;return kIOReturnSuccess;
}
void CLASS::release_ram_access()
{
    if(m_ram_access_owner){
        if(m_provider)m_provider->returnBackingAccess(this);
        m_ram_access_owner->release();m_ram_access_owner=0;
    }
}
void CLASS::release_backing_loan()
{
    release_ram_access();
    if(m_loan_pages){m_loan_pages->release();m_loan_pages=0;}
    if(m_backing_loan){m_backing_loan->release();m_backing_loan=0;}
    m_loan_count=0;bzero(&m_loan_backing,sizeof(m_loan_backing));
}
IOReturn CLASS::begin_ram_backing_loan(void const* input,void* output,size_t input_size,size_t* output_size)
{
    if(!input || input_size!=sizeof(VIMSurfaceVRAM) || !output || !output_size ||
        *output_size!=sizeof(VIMSurfaceBackingLoan) || !m_direct_lock)return kIOReturnBadArgument;
    VIMSurfaceVRAM expected=*static_cast<VIMSurfaceVRAM const*>(input);
    VIM2DDirectGuard guard(m_direct_lock);
    if(!m_provider || !m_direct_token || !m_direct_drained_close_supported)return kIOReturnUnsupported;
    if(m_backing_loan || m_loan_quarantined)return kIOReturnBusy;
    if(!bTargetIsCGSSurface || !m_surface_client || !m_surface_locked)return kIOReturnNotOpen;
    if(m_loan_serial==UINT64_MAX)return kIOReturnNoResources;
    VMsvga2PreparedBacking* owner=0;
    IOReturn error=m_surface_client->context_retain_ram_backing(&expected,&owner,this);
    if(error)return error;
    uint32_t count=static_cast<uint32_t>(expected.length/4096);
    if(!count || count>16384 || expected.length!=uint64_t(count)*4096){
        m_surface_client->context_cancel_ram_backing_loan(owner);owner->release();return kIOReturnBadArgument;
    }
    OSData* pages=owner->copyPages(count,&error);
    if(!pages){m_surface_client->context_cancel_ram_backing_loan(owner);owner->release();return error;}
    // EVERY normal GPU loan reserves its alias before any native use. An
    // unknown VM object conservatively conflicts with every deferred gate.
    // Inspection is cached by the prepared owner; no repeated page walk.
    error=reserve_ram_access();
    if(error){m_surface_client->context_cancel_ram_backing_loan(owner);pages->release();owner->release();return error;}
    m_loan_pages=pages;
    m_backing_loan=owner;m_loan_backing=expected;m_loan_count=count;++m_loan_serial;
    VIMSurfaceBackingLoan reply={expected,m_loan_serial};memcpy(output,&reply,sizeof(reply));
    return kIOReturnSuccess;
}
IOReturn CLASS::get_ram_backing_loan(uintptr_t serial,uintptr_t first,void* output,size_t* output_size)
{
    if(!output || !output_size || *output_size!=sizeof(VIMSurfaceRAM) || first>UINT32_MAX ||
        !m_direct_lock)return kIOReturnBadArgument;
    VIM2DDirectGuard guard(m_direct_lock);
    if(m_writer_continuation)return kIOReturnNotPermitted;
    if(!m_backing_loan || serial!=m_loan_serial)return kIOReturnNotOpen;
    if(first!=UINT32_MAX && first>=m_loan_count)return kIOReturnBadArgument;
    VIMSurfaceRAM* reply=static_cast<VIMSurfaceRAM*>(output);bzero(reply,sizeof(*reply));
    reply->backing=m_loan_backing;reply->first=first;reply->total=m_loan_count;
    if(first!=UINT32_MAX){
        reply->count=m_loan_count-first<VIM_SURFACE_RAM_PAGE_CHUNK?m_loan_count-first:VIM_SURFACE_RAM_PAGE_CHUNK;
        uint64_t const* pages=static_cast<uint64_t const*>(m_loan_pages->getBytesNoCopy());
        memcpy(reply->pages,pages+first,reply->count*sizeof(uint64_t));
    }
    return kIOReturnSuccess;
}
IOReturn CLASS::end_ram_backing_loan(uintptr_t serial,uintptr_t mode,void* output,size_t* output_size)
{
    if(mode>VIM_RAM_LOAN_ABORT || !output || !output_size || *output_size!=sizeof(VIMSurfaceState) ||
        !m_direct_lock)return kIOReturnBadArgument;
    VIM2DDirectGuard guard(m_direct_lock);
    if(m_writer_loan)return kIOReturnBusy;
    if(!m_backing_loan || serial!=m_loan_serial)return kIOReturnNotOpen;
    return finish_backing_loan(static_cast<unsigned>(mode),static_cast<VIMSurfaceState*>(output));
}
IOReturn CLASS::cancel_surface_backing_loan(VMsvga2Surface* source)
{
    if(!m_direct_lock)return kIOReturnNotReady;
    VIM2DDirectGuard guard(m_direct_lock);
    if(m_writer_loan && m_surface_client==source){VIMSurfaceState ignored;return finish_writer_loan(VIM_RAM_LOAN_ABORT,&ignored);}
    if(m_surface_client!=source || !m_backing_loan)return kIOReturnSuccess;
    VIMSurfaceState ignored;
    return finish_backing_loan(VIM_RAM_LOAN_ABORT,&ignored);
}
IOReturn CLASS::finish_backing_loan(unsigned mode,VIMSurfaceState* output)
{
    if(mode==VIM_RAM_LOAN_ABORT && !close_direct_stream())return kIOReturnIOError;
    IOReturn restored=restore_deferred_loan();if(restored)return restored;
    // Normal End follows a validated host End reply. Abort's close returns
    // only after actual GPU retirement. Notify even possibly partial writes.
    IOReturn error=kIOReturnSuccess;
    if(mode!=VIM_RAM_LOAN_READ_ONLY)
        error=m_provider?m_provider->directRAMDirty(static_cast<uint64_t const*>(m_loan_pages->getBytesNoCopy()),m_loan_count):kIOReturnOffline;
    bzero(output,sizeof(VIMSurfaceState));
    IOReturn unlocked=m_surface_client?m_surface_client->context_unlock_memory(0,
        mode==VIM_RAM_LOAN_READ_ONLY,static_cast<VIMSurfaceState*>(output),m_backing_loan):kIOReturnOffline;
    if(unlocked && m_surface_client)m_surface_client->context_unlock_memory(0,true,0,m_backing_loan);
    m_surface_locked=false;release_backing_loan();
    return error?error:unlocked;
}
