/* Explicit retained GPU-writer gate, separate from ordinary deferred loans.
 * One source/pin/map/UPL/exclusive alias key spans a bounded ticket chain.
 * Same rendering endpoint only. No CPU pointer, page export, timer or polling
 * is added for continuations. Callback delivery is not GPU completion guessed
 * from a submit ACK: the existing native completion subscription supplies it.
 */
#include "../../vminclude/vim_writer_chain.h"
class VIMWriterBacking: public OSObject {
    OSDeclareDefaultStructors(VIMWriterBacking);
    VMsvga2Accel* provider;
    VMsvga2Surface* source;
    VMsvga2PreparedBacking* owner;
    OSData* pages;
    IOMemoryMap* mapping;
    void* key;
    upl_t upl;
    bool claimed,block_attempted,registered,self_held;
    IOLock* lock;
    thread_call_t call;
    VMsvga22DContext* endpoint; // identity only; source's active loan retains this endpoint
    uint64_t serial,namespace_id[2];
    VIMSurfaceVRAM backing;
    VIMWriterChainState chain;
    IOReturn restoreCPU() {
        if(upl){upl_t held=upl;upl=0;IOReturn error=ubc_upl_commit(held);if(error)return error;}
        else if(block_attempted){IOReturn error=mapping->redirect(owner->memory(),kIOMapDefaultCache,0ULL);if(error)return error;}
        block_attempted=false;return kIOReturnSuccess;
    }
    void releaseKey(){if(claimed){provider->releaseBackingAccess(key);claimed=false;}}
    void snapshot(VIMSurfaceState* state) {
        *state=backing.state;state->content_epoch=chain.epoch;
        state->flags=VIM_SURFACE_STATE_GPU_PENDING|VIM_SURFACE_STATE_GPU_NEWER;
        if(chain.count)state->flags|=VIM_SURFACE_STATE_GPU_FUTURE_SEALED;
    }
    IOReturn retire() {
        IOReturn error=restoreCPU();if(error)return error;
        uint32_t count=pages->getLength()/sizeof(uint64_t);
        error=provider->directRAMDirty(static_cast<uint64_t const*>(pages->getBytesNoCopy()),count);
        if(error)return error;
        // Exact newest logical epoch, not CPU_epoch+1 for an entire chain.
        error=source->context_unlock_memory(0,false,0,owner,chain.epoch,this);
        if(error && error!=kIOReturnOffline)return error;
        registered=false;releaseKey();
        // Source CPU waiters/faults can run before metadata transport cleanup.
        for(unsigned i=0;i<chain.count;++i)provider->retireBackingTicket(namespace_id,chain.items[i].ticket);
        IOLockLock(lock);chain.closed=true;IOLockUnlock(lock);return kIOReturnSuccess;
    }
    static void completed(OSObject* target,uint64_t ticket,IOReturn result) {
        VIMWriterBacking* self=static_cast<VIMWriterBacking*>(target);
        IOLockLock(self->lock);
        bool known=self->chain.complete(ticket,result==kIOReturnSuccess);
        if(known && self->chain.ready())thread_call_enter(self->call);
        IOLockUnlock(self->lock);
    }
    static void restore(thread_call_param_t object,thread_call_param_t) {
        VIMWriterBacking* self=static_cast<VIMWriterBacking*>(object);
        IOLockLock(self->lock);bool ready=self->chain.startRestore();IOLockUnlock(self->lock);
        if(!ready)return; // a newer GPU writer or undelivered older callback still owns the gate
        IOReturn error=self->retire();
        if(error){IOLog("VMsvga2: GPU writer gate restoration failed; restart required\n");return;}
        if(self->self_held){self->self_held=false;self->release();}
    }
public:
    static VIMWriterBacking* withBacking(VMsvga2Accel* provider,VMsvga2Surface* source,
        VMsvga2PreparedBacking* owner,OSData* pages,VMsvga22DContext* endpoint,
        VIMSurfaceVRAM const& backing,uint64_t serial,IOReturn* error) {
        *error=kIOReturnNoMemory;VIMWriterBacking* job=new VIMWriterBacking;if(!job)return 0;
        if(!job->init()){job->release();return 0;}
        job->provider=0;job->source=0;job->owner=0;job->pages=0;job->mapping=0;job->key=0;job->upl=0;
        job->claimed=job->block_attempted=job->registered=job->self_held=false;job->lock=0;job->call=0;
        job->endpoint=endpoint;job->serial=serial;job->backing=backing;
        job->namespace_id[0]=job->namespace_id[1]=0;job->chain.reset(backing.state.content_epoch);
        job->provider=provider;provider->retain();job->source=source;source->retain();
        job->owner=owner;owner->retain();job->pages=pages;pages->retain();
        job->lock=IOLockAlloc();job->call=thread_call_allocate(restore,job);
        if(!job->lock || !job->call){job->release();return 0;}
        job->mapping=owner->copyAccessMap(&job->key,error);if(!job->mapping){job->release();return 0;}
        if(!provider->promoteBackingAccess(job->key,endpoint)){*error=kIOReturnBusy;job->release();return 0;}
        job->claimed=true;*error=VIMLegacyMapBlock(job->mapping,owner->memory());
        job->block_attempted=*error!=kIOReturnUnsupported;
        if(!*error){
            job->upl=static_cast<upl_t>(VIMLegacyMapTakeBlockingUPL(job->mapping,owner->memory()));
            if(!job->upl)*error=kIOReturnCannotLock;
        }
        if(!*error){*error=source->context_register_writer_backing(owner,job);job->registered=!*error;}
        if(*error){
            IOReturn restored=job->restoreCPU();
            if(restored){*error=restored;return job;} // caller must retain/quarantine its actual prepared loan
            job->releaseKey();job->release();return 0;
        }
        return job;
    }
    bool sameSource(VMsvga2Surface* target) const{return source==target;}
    IOReturn claim(VMsvga22DContext* caller,uint64_t next,VIMFramebufferLayout const* layout,VIMSurfaceWriterLoan* out) {
        IOLockLock(lock);IOReturn error=kIOReturnSuccess;
        if(caller!=endpoint)error=kIOReturnUnsupported;
        else if(!next || next<=serial || !layout || layout->version!=VIM_FRAMEBUFFER_LAYOUT_VERSION ||
            layout->width!=backing.width || layout->height!=backing.height ||
            layout->row_bytes!=backing.row_bytes || layout->offset || layout->bits_per_pixel!=32)error=kIOReturnBadArgument;
        else if(!chain.claim())error=kIOReturnNotReady;
        if(!error){
            serial=next;out->loan.backing=backing;snapshot(&out->loan.backing.state);
            out->loan.serial=next;out->previous_ticket=chain.latest();
        }
        IOLockUnlock(lock);
        if(!error){
            error=source->context_set_writer_thread(owner,this,true);
            if(error){
                IOLockLock(lock);chain.releaseWriter();if(chain.ready())thread_call_enter(call);IOLockUnlock(lock);
            }
        }
        return error;
    }
    IOReturn submit(VMsvga22DContext* caller,VIMSurfaceBackingPending const* request,VIMSurfaceState* out) {
        VIM_HANDOFF_SPAN(handoff,"writer-submit");
        IOLockLock(lock);IOReturn error=kIOReturnSuccess;
        VIM_HANDOFF_MARK(handoff,1);
        if(caller!=endpoint || request->serial!=serial || !(request->namespace_id[0]|request->namespace_id[1]) ||
            (chain.count && memcmp(namespace_id,request->namespace_id,16)))error=kIOReturnNotPermitted;
        else if(!chain.append(request->ticket))error=kIOReturnNotReady;
        if(!error){
            memcpy(namespace_id,request->namespace_id,16);
            if(!self_held){retain();self_held=true;} // one root lifetime, not a new per-frame gate
            snapshot(out);
        }
        IOLockUnlock(lock);if(error)return error;
        VIM_HANDOFF_MARK(handoff,2);
        error=source->context_publish_writer_future(owner,this,request->ticket,out->content_epoch);
        VIM_HANDOFF_MARK(handoff,3);
        VIMBackingKernelSubscription subscription;bzero(&subscription,sizeof(subscription));
        subscription.version=1;subscription.bytes=sizeof(subscription);
        memcpy(subscription.namespace_id,namespace_id,16);subscription.ticket=request->ticket;
        subscription.owner=this;subscription.completed=completed;
        const OSSymbol* symbol=error?0:OSSymbol::withCStringNoCopy(VIM_BACKING_NOTIFY_SUBSCRIBE);
        if(!error)error=symbol?provider->callPlatformFunction(symbol,false,&subscription,0,0,0):kIOReturnNoMemory;
        VIM_HANDOFF_MARK(handoff,4);
        if(symbol)symbol->release();
        IOLockLock(lock);
        VIM_HANDOFF_MARK(handoff,5);
        if(error)chain.failed=true; // possibly queued GPU work: never cancel/unpin on a lost subscription
        else {chain.releaseWriter();if(chain.ready())thread_call_enter(call);}
        IOLockUnlock(lock);return error;
    }
    IOReturn cancel(VMsvga22DContext* caller,uint64_t token,bool drained_write,VIMSurfaceState* out) {
        IOLockLock(lock);IOReturn error=kIOReturnSuccess;
        if(caller!=endpoint || token!=serial)error=kIOReturnNotPermitted;
        else if(!chain.releaseWriter(drained_write))error=kIOReturnNotReady;
        bool initial=!error && !chain.count;
        if(!error){snapshot(out);if(initial)chain.startRestore();}
        IOLockUnlock(lock);
        if(!error){
            source->context_set_writer_thread(owner,this,false);
            if(!initial){IOLockLock(lock);if(chain.ready())thread_call_enter(call);IOLockUnlock(lock);}
        }
        if(initial){error=retire();if(!error){out->flags=0;}}
        return error;
    }
    void free() {
        if(call)thread_call_free(call);if(lock)IOLockFree(lock);
        if(mapping)mapping->release();if(pages)pages->release();if(owner)owner->release();
        if(source)source->release();if(provider)provider->release();OSObject::free();
    }
};
OSDefineMetaClassAndStructors(VIMWriterBacking,OSObject);

IOReturn CLASS::begin_writer_ram_loan(void const* input,void* output,size_t input_size,size_t* output_size)
{
    IOReturn error=begin_ram_backing_loan(input,output,input_size,output_size);if(error)return error;
    VIM2DDirectGuard guard(m_direct_lock);
    if(m_writer_loan || m_deferred_loan || !m_backing_loan || !m_surface_client)return kIOReturnNotOpen;
    const OSSymbol* symbol=OSSymbol::withCStringNoCopy(VIM_BACKING_NOTIFY_AVAILABLE);
    error=symbol?m_provider->callPlatformFunction(symbol,false,0,0,0,0):kIOReturnNoMemory;
    if(symbol)symbol->release();
    if(!error)m_writer_loan=VIMWriterBacking::withBacking(m_provider,m_surface_client,m_backing_loan,m_loan_pages,
        this,m_loan_backing,m_loan_serial,&error);
    if(error && !m_writer_loan){m_surface_client->context_cancel_ram_backing_loan(m_backing_loan);release_backing_loan();}
    else if(error){m_loan_quarantined=true;retain();}
    m_writer_continuation=false;return error;
}
IOReturn CLASS::continue_writer_ram_loan(void const* input,void* output,size_t input_size,size_t* output_size)
{
    if(!input || input_size!=sizeof(VIMFramebufferLayout) || !output || !output_size ||
        *output_size!=sizeof(VIMSurfaceWriterLoan) || !m_direct_lock)return kIOReturnBadArgument;
    VIM2DDirectGuard guard(m_direct_lock);
    if(!m_provider || !m_direct_token || !m_direct_drained_close_supported)return kIOReturnUnsupported;
    if(!bTargetIsCGSSurface || !m_surface_client || m_surface_locked || m_backing_loan || m_writer_loan ||
        m_deferred_loan || m_loan_quarantined)return kIOReturnBusy;
    if(m_loan_serial==UINT64_MAX)return kIOReturnNoResources;
    OSObject* retained=m_surface_client->context_copy_writer_backing();if(!retained)return kIOReturnNotReady;
    VIMWriterBacking* job=OSDynamicCast(VIMWriterBacking,retained);
    IOReturn error=job?job->claim(this,m_loan_serial+1,static_cast<VIMFramebufferLayout const*>(input),
        static_cast<VIMSurfaceWriterLoan*>(output)):kIOReturnUnsupported;
    if(error){retained->release();return error;}
    m_writer_loan=job;m_writer_continuation=true;++m_loan_serial;
    m_loan_backing=static_cast<VIMSurfaceWriterLoan*>(output)->loan.backing;
    return kIOReturnSuccess;
}
IOReturn CLASS::commit_writer_ram_loan(void const* input,void* output,size_t input_size,size_t* output_size)
{
    if(!input || input_size!=sizeof(VIMSurfaceBackingPending) || !output || !output_size ||
        *output_size!=sizeof(VIMSurfaceState) || !m_direct_lock)return kIOReturnBadArgument;
    VIM_HANDOFF_SPAN(handoff,"writer-commit");
    VIM2DDirectGuard guard(m_direct_lock);
    VIM_HANDOFF_MARK(handoff,1);
    VIMSurfaceBackingPending const* request=static_cast<VIMSurfaceBackingPending const*>(input);
    if(!m_writer_loan || request->serial!=m_loan_serial)return kIOReturnNotOpen;
    IOReturn error=m_writer_loan->submit(this,request,static_cast<VIMSurfaceState*>(output));
    VIM_HANDOFF_MARK(handoff,2);
    if(error)return error; // retained gate/claim; failed subscription is quarantined
    m_writer_loan->release();m_writer_loan=0;m_writer_continuation=false;
    VIM_HANDOFF_MARK(handoff,3);
    m_surface_locked=false;release_backing_loan();
    VIM_HANDOFF_MARK(handoff,4);
    return kIOReturnSuccess;
}
IOReturn CLASS::finish_writer_loan(unsigned mode,VIMSurfaceState* output)
{
    if(!m_writer_loan || (mode!=VIM_RAM_LOAN_READ_ONLY && mode!=VIM_RAM_LOAN_ABORT))return kIOReturnNotPermitted;
    if(mode==VIM_RAM_LOAN_ABORT && !close_direct_stream())return kIOReturnIOError;
    IOReturn error=m_writer_loan->cancel(this,m_loan_serial,mode==VIM_RAM_LOAN_ABORT,output);
    if(error)return error;
    m_writer_loan->release();m_writer_loan=0;m_writer_continuation=false;
    m_surface_locked=false;release_backing_loan();return kIOReturnSuccess;
}
IOReturn CLASS::end_writer_ram_loan(uintptr_t serial,uintptr_t mode,void* output,size_t* output_size)
{
    if(!output || !output_size || *output_size!=sizeof(VIMSurfaceState) || !m_direct_lock)return kIOReturnBadArgument;
    VIM2DDirectGuard guard(m_direct_lock);
    if(serial!=m_loan_serial)return kIOReturnNotOpen;
    return finish_writer_loan(static_cast<unsigned>(mode),static_cast<VIMSurfaceState*>(output));
}
