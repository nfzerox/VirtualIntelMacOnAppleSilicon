/* Explicit RESEARCH whole-object handoff. Ordinary Begin/End is unchanged.
 * Gate precedes native shared-FBO writes. The source's logical context lock
 * stays held until the real completed event; returned state is GPU_PENDING,
 * NEVER CPU-current. One retained job owns source, pin, map, UPL and ticket.
 * No timeout, polling, callback into user space or guessed completion.
 */
#include <kern/thread_call.h>
#include <sys/ucred.h> // 10.8 ubc.h requires the caller to declare kauth_cred_t.
#include <sys/ubc.h>
#include "../../vminclude/vim_backing_notify_kernel.h"
#include "../../../../../probes/opengl-map-object-profile.h"

class VIMDeferredBacking: public OSObject {
    OSDeclareDefaultStructors(VIMDeferredBacking);
    VMsvga2Accel* provider;
    VMsvga2Surface* source;
    VMsvga2PreparedBacking* owner;
    OSData* pages;
    IOMemoryMap* mapping;
    void* key;
    upl_t upl;
    bool claimed,block_attempted,pending;
    IOLock* lock;
    thread_call_t call;
    uint64_t ticket,namespace_id[2];
    IOReturn status;
    IOReturn restoreCPU() {
        if(upl){upl_t retained=upl;upl=0;IOReturn result=ubc_upl_commit(retained);if(result)return result;}
        else if(block_attempted){
            IOReturn result=mapping->redirect(owner->memory(),kIOMapDefaultCache,0ULL);
            if(result)return result;
        }
        block_attempted=false;return kIOReturnSuccess;
    }
    void releaseKey() {if(claimed){provider->releaseBackingAccess(key);claimed=false;}}
    static void completed(OSObject* target,uint64_t name,IOReturn result) {
        VIMDeferredBacking* self=static_cast<VIMDeferredBacking*>(target);
        IOLockLock(self->lock);
        if(self->pending && self->ticket==name){self->status=result;thread_call_enter(self->call);}
        IOLockUnlock(self->lock);
    }
    static void restore(thread_call_param_t object,thread_call_param_t) {
        VIMDeferredBacking* self=static_cast<VIMDeferredBacking*>(object);
        IOLockLock(self->lock);IOReturn error=self->status;IOLockUnlock(self->lock);
        if(!error)error=self->restoreCPU();
        if(error){
            // Failed notification is NOT a cancellation/unpin license. Keep
            // source lock, real CPU gate and prepared pages until VM reset.
            IOLog("VMsvga2: deferred backing failed; memory quarantined until restart\n");return;
        }
        uint32_t count=self->pages->getLength()/sizeof(uint64_t);
        error=self->provider->directRAMDirty(static_cast<uint64_t const*>(self->pages->getBytesNoCopy()),count);
        if(error){IOLog("VMsvga2: deferred dirty notification failed; pin/source quarantined\n");return;}
        IOReturn unlocked=self->source->context_unlock_memory(0,false,0,self->owner);
        if(unlocked && unlocked!=kIOReturnOffline &&
           self->source->context_unlock_memory(0,true,0,self->owner)!=kIOReturnSuccess){
            IOLog("VMsvga2: deferred source retirement failed; pin/source quarantined\n");return;
        }
        self->releaseKey();
        // CPU faults and source waiters can run before ordinary metadata
        // service cleanup takes its device/transport locks.
        self->provider->retireBackingTicket(self->namespace_id,self->ticket);
        self->release(); // independent worker ref; callback has its own ref
    }
public:
    static VIMDeferredBacking* withBacking(VMsvga2Accel* provider,VMsvga2Surface* source,
        VMsvga2PreparedBacking* owner,OSData* pages,void* borrower,IOReturn* error) {
        *error=kIOReturnNoMemory;VIMDeferredBacking* job=new VIMDeferredBacking;
        if(!job)return 0;
        if(!job->init()){job->release();return 0;}
        job->provider=0;job->source=0;job->owner=0;job->pages=0;job->mapping=0;job->key=0;job->upl=0;
        job->claimed=false;job->block_attempted=false;job->pending=false;job->lock=0;job->call=0;
        job->ticket=0;job->namespace_id[0]=job->namespace_id[1]=0;job->status=kIOReturnNotReady;
        job->provider=provider;provider->retain();job->source=source;source->retain();
        job->owner=owner;owner->retain();job->pages=pages;pages->retain();
        job->lock=IOLockAlloc();job->call=thread_call_allocate(restore,job);
        if(!job->lock || !job->call){job->release();return 0;}
        job->mapping=owner->copyAccessMap(&job->key,error);
        if(!job->mapping){job->release();return 0;}
        if(!provider->promoteBackingAccess(job->key,borrower)){*error=kIOReturnBusy;job->release();return 0;}
        job->claimed=true;
        *error=VIMLegacyMapBlock(job->mapping,owner->memory());
        job->block_attempted=*error!=kIOReturnUnsupported; // rejected BEFORE redirect => nothing to restore
        if(!*error){
            job->upl=static_cast<upl_t>(VIMLegacyMapTakeBlockingUPL(job->mapping,owner->memory()));
            if(!job->upl)*error=kIOReturnCannotLock;
        }
        if(*error){
            // Factory has not returned, so NO host GPU use exists yet.
            IOReturn restored=job->cancelAfterDrained();
            if(restored){*error=restored;return job;} // caller must quarantine its loan
            job->release();return 0;
        }
        return job;
    }
    IOReturn cancelAfterDrained() {
        IOReturn error=restoreCPU();if(!error)releaseKey();return error;
    }
    IOReturn submit(VIMSurfaceBackingPending const* request) {
        if(pending || !upl || !request->ticket || !(request->namespace_id[0]|request->namespace_id[1]))return kIOReturnBadArgument;
        VIMBackingKernelSubscription subscription;bzero(&subscription,sizeof(subscription));
        subscription.version=1;subscription.bytes=sizeof(subscription);
        memcpy(subscription.namespace_id,request->namespace_id,16);
        subscription.ticket=request->ticket;subscription.owner=this;subscription.completed=completed;
        const OSSymbol* symbol=OSSymbol::withCStringNoCopy(VIM_BACKING_NOTIFY_SUBSCRIBE);
        if(!symbol)return kIOReturnNoMemory;
        IOLockLock(lock);pending=true;status=kIOReturnNotReady;ticket=request->ticket;
        memcpy(namespace_id,request->namespace_id,16);retain();IOLockUnlock(lock);
        IOReturn error=provider->callPlatformFunction(symbol,false,&subscription,0,0,0);symbol->release();
        if(error){IOLockLock(lock);pending=false;IOLockUnlock(lock);release();}
        return error;
    }
    void free() {
        if(call)thread_call_free(call);
        if(lock)IOLockFree(lock);
        if(mapping)mapping->release();
        if(pages)pages->release();
        if(owner)owner->release();
        if(source)source->release();
        if(provider)provider->release();
        OSObject::free();
    }
};
OSDefineMetaClassAndStructors(VIMDeferredBacking,OSObject);

IOReturn CLASS::restore_deferred_loan()
{
    if(!m_deferred_loan)return kIOReturnSuccess;
    IOReturn error=m_deferred_loan->cancelAfterDrained();
    if(!error){m_deferred_loan->release();m_deferred_loan=0;}
    return error;
}
IOReturn CLASS::begin_deferred_ram_loan(void const* input,void* output,size_t input_size,size_t* output_size)
{
    IOReturn error=begin_ram_backing_loan(input,output,input_size,output_size);
    if(error)return error;
    VIM2DDirectGuard guard(m_direct_lock);
    if(!m_backing_loan || !m_provider || !m_surface_client || m_deferred_loan)return kIOReturnNotOpen;
    const OSSymbol* name=OSSymbol::withCStringNoCopy(VIM_BACKING_NOTIFY_AVAILABLE);
    error=name?m_provider->callPlatformFunction(name,false,0,0,0,0):kIOReturnNoMemory;
    if(name)name->release();
    if(!error)m_deferred_loan=VIMDeferredBacking::withBacking(m_provider,m_surface_client,m_backing_loan,m_loan_pages,this,&error);
    if(error && !m_deferred_loan){
        m_surface_client->context_cancel_ram_backing_loan(m_backing_loan);release_backing_loan();
        // Keep the caller's ORIGINAL CPU context lock so it can retry the
        // old synchronous Begin on a positively unsupported allocation.
    }else if(error){m_loan_quarantined=true;retain();}
    return error;
}
IOReturn CLASS::commit_deferred_ram_loan(void const* input,void* output,size_t input_size,size_t* output_size)
{return commit_pending_ram_loan(input,output,input_size,output_size,false);}
IOReturn CLASS::commit_future_ram_loan(void const* input,void* output,size_t input_size,size_t* output_size)
{return commit_pending_ram_loan(input,output,input_size,output_size,true);}
IOReturn CLASS::commit_pending_ram_loan(void const* input,void* output,size_t input_size,size_t* output_size,bool gpu_future)
{
    if(!input || input_size!=sizeof(VIMSurfaceBackingPending) || !output || !output_size ||
       *output_size!=sizeof(VIMSurfaceState) || !m_direct_lock)return kIOReturnBadArgument;
    VIM2DDirectGuard guard(m_direct_lock);
    VIMSurfaceBackingPending const* request=static_cast<VIMSurfaceBackingPending const*>(input);
    if(!m_deferred_loan || !m_backing_loan || request->serial!=m_loan_serial ||
       m_loan_backing.state.content_epoch==UINT64_MAX)return kIOReturnNotOpen;
    VIMSurfaceState future=m_loan_backing.state;future.flags=VIM_SURFACE_STATE_GPU_PENDING;++future.content_epoch;
    if(gpu_future)future.flags|=VIM_SURFACE_STATE_GPU_FUTURE_SEALED;
    IOReturn error=gpu_future?m_surface_client->context_mark_backing_future(m_backing_loan,request->ticket):
        m_surface_client->context_mark_backing_pending(m_backing_loan);
    if(!error)error=m_deferred_loan->submit(request);
    if(error)return error; // same loan/gate retained for trusted drain+normal End
    m_deferred_loan->release();m_deferred_loan=0;
    // The independent job owns the ACTUAL source lock and prepared owner.
    // This endpoint may target another surface while old-source CPU callers
    // remain blocked until completed restoration; it must not unlock twice.
    m_surface_locked=false;release_backing_loan();memcpy(output,&future,sizeof(future));
    return kIOReturnSuccess;
}
