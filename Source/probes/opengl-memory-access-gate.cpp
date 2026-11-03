/* Standalone Mavericks guest laboratory kext, NOT a compositor policy.
 * The caller donates only its own small scratch range. An independent timed
 * callout restores access after 250ms even if every user thread is faulted.
 * Optional GPU mode uses the calling task's own prepared pages and an actual
 * native completion ticket through an independent kernel transport endpoint.
 * Never enabled on an ordinary surface or by the production renderer.
 * Public IOMemoryMap redirect can return success without a blocking UPL;
 * the exact-method/layout profile checks real admission before returning0.
 * Original/alias tests still separately qualify actual CPU exclusion.
 */
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <kern/clock.h>
#include <kern/thread_call.h>
#include <sys/ubc.h>
#include "opengl-memory-gpu-contract.h"
#include "opengl-map-block-profile.h"
#include "opengl-map-object-profile.h"
#include "opengl-object-admission.h"
#include "../vendor/source/VMsvga2/AC/UCMethods.h"
#include "../vendor/source/VMsvga2/vminclude/vim_passthrough_opengl.h"
#include "../vendor/source/VMsvga2/vminclude/vim_gl_surface_service.h"
#include "../vendor/source/VMsvga2/vminclude/vim_backing_notify_kernel.h"

/* Whole-object GPU jobs are independent; aliases share one key. The older
 * partial-object timer control stays exclusive. Still standalone laboratory
 * code: there is no automatic desktop admission or renderer integration. */
static IOLock *gate_admission;
static VIMObjectAdmission gate_objects;
static uint64_t gate_gpu_events;
static bool reserve_gate(void *object=0) {
    IOLockLock(gate_admission);bool allowed=VIMObjectReserve(&gate_objects,object);
    IOLockUnlock(gate_admission);return allowed;
}
static void release_gate(void *object=0) {
    IOLockLock(gate_admission);VIMObjectRelease(&gate_objects,object);IOLockUnlock(gate_admission);
}

class VIMMemoryGateClient : public IOUserClient {
    OSDeclareDefaultStructors(VIMMemoryGateClient);
    task_t owner;
    IOLock *lock;
    IOMemoryDescriptor *memory;
    IOMemoryMap *mapping;
    upl_t blocked_upl; // independently owned, NOT mapping's fRedirUPL anymore
    thread_call_t call;
    bool pending;
    bool gpu_prepared,gpu_mode,quarantined,owns_slot;
    void *object_key; // held alive by memory/mapping through release_gate
    bool gpu_notified;
    IOReturn gpu_notification_status;
    uint64_t gpu_serial,gpu_ticket;
    IOUserClient *gpu_service;
    IOService *gpu_accelerator;
    IOReturn result;
#include "opengl-memory-gpu-service.inc"
    IOReturn restoreAccess() {
        if(!blocked_upl)return kIOReturnSuccess;
        upl_t upl=blocked_upl;blocked_upl=0;
        // Published KPI commits AND deallocates exactly once. Independent of
        // IOMemory's global lock and any faulting alias's client/device locks.
        return ubc_upl_commit(upl);
    }
    IOReturn blockAccess() {
        IOReturn error=VIMLegacyMapBlock(mapping,memory);
        if(!error){
            blocked_upl=static_cast<upl_t>(VIMLegacyMapTakeBlockingUPL(mapping,memory));
            if(!blocked_upl)error=kIOReturnCannotLock;
        }
        return error;
    }
    static void restore(thread_call_param_t object,thread_call_param_t) {
        VIMMemoryGateClient *self=(VIMMemoryGateClient *)object;
        IOReturn completion=self->gpu_mode?self->gpuCompletionStatus():kIOReturnSuccess;
        if(completion){
            // Ambiguous/failed completion is NOT a release event. Keep the
            // one prepared range/gate quarantined until VM reset, not stale
            // CPU access or writes into an allocator's reused pages.
            IOLockLock(self->lock);self->result=completion;self->pending=false;self->quarantined=true;
            IOLockWakeup(self->lock,&self->pending,false);IOLockUnlock(self->lock);
            IOLog("VIMMemoryGate: GPU completion failed; scratch range quarantined until guest restart\n");return;
        }
        IOReturn restored=self->restoreAccess();
        if(restored){
            IOLockLock(self->lock);self->result=restored;self->pending=false;self->quarantined=true;
            IOLockWakeup(self->lock,&self->pending,false);IOLockUnlock(self->lock);
            IOLog("VIMMemoryGate: UPL retirement failed; scratch pin quarantined until guest restart\n");return;
        }
        // Original/alias CPU faults can resolve BEFORE map release needs the
        // global IOMemory lock. Never invert that order.
        if(self->gpu_mode)completion=self->gpuRetireTicket();
        self->mapping->release();self->mapping=0;
        release_gate(self->object_key);self->owns_slot=false;self->object_key=0;
        self->memory->complete();self->memory->release();self->memory=0;
        IOLockLock(self->lock);self->gpu_prepared=false;self->gpu_mode=false;
        self->result=completion?completion:restored;self->pending=false;
        IOLockWakeup(self->lock,&self->pending,false);IOLockUnlock(self->lock);
        self->release(); // free may run here; thread_call_free supports this
    }
public:
    bool initWithTask(task_t task,void *security,UInt32 type) {
        owner=TASK_NULL;lock=0;memory=0;mapping=0;blocked_upl=0;call=0;pending=false;result=kIOReturnSuccess;
        gpu_prepared=gpu_mode=quarantined=owns_slot=gpu_notified=false;object_key=0;
        gpu_notification_status=kIOReturnNotReady;gpu_serial=gpu_ticket=0;gpu_service=0;gpu_accelerator=0;
        if(!task || type || !IOUserClient::initWithTask(task,security,type))return false;
        owner=task;lock=IOLockAlloc();call=thread_call_allocate(restore,this);
        return lock && call;
    }
    IOReturn externalMethod(uint32_t selector,IOExternalMethodArguments *a,
        IOExternalMethodDispatch *,OSObject *,void *) {
        if(!a || a->structureInputSize || a->asyncWakePort)
            return kIOReturnBadArgument;
        if(selector==VIM_GATE_PREPARE){
            if(a->scalarInputCount!=2 || a->scalarOutputCount || a->structureOutputSize!=sizeof(VIMGatePages) ||
               !a->structureOutput || !a->scalarInput[0] || (a->scalarInput[0]&4095) ||
               !a->scalarInput[1] || a->scalarInput[1]>65536 || (a->scalarInput[1]&4095) ||
               a->scalarInput[0]>UINT64_MAX-a->scalarInput[1])return kIOReturnBadArgument;
            IOLockLock(lock);
            if(memory || pending || quarantined || gpu_serial==UINT64_MAX){IOLockUnlock(lock);return kIOReturnBusy;}
            memory=IOMemoryDescriptor::withAddressRange(a->scalarInput[0],a->scalarInput[1],kIODirectionInOut|kIOMemoryPersistent,owner);
            result=memory?memory->prepare():kIOReturnNoMemory;
            bool prepared=!result;
            VIMGatePages out;bzero(&out,sizeof(out));out.length=a->scalarInput[1];
            for(uint64_t i=0;!result && i<out.length/4096;++i){
                IOByteCount span=0;addr64_t physical=memory->getPhysicalSegment(i*4096,&span,kIOMemoryMapperNone);
                if(!physical || (physical&4095) || span<4096)result=kIOReturnUnsupported;else out.pages[i]=physical;
            }
            if(!result){mapping=memory->createMappingInTask(kernel_task,0,kIOMapAnywhere|kIOMapUnique);if(!mapping)result=kIOReturnNoMemory;}
            if(!result){
                object_key=VIMLegacyWholeObject(mapping,memory);
                if(!object_key)result=kIOReturnUnsupported;
                else if(!reserve_gate(object_key)){object_key=0;result=kIOReturnBusy;}
                else owns_slot=true;
            }
            if(result){
                if(mapping){mapping->release();mapping=0;}
                if(owns_slot){release_gate(object_key);owns_slot=false;}object_key=0;
                if(memory){if(prepared)memory->complete();memory->release();memory=0;}
            }else {out.serial=++gpu_serial;memcpy(a->structureOutput,&out,sizeof(out));gpu_prepared=true;retain();}
            IOLockUnlock(lock);return result;
        }
        if(a->structureOutputSize)return kIOReturnBadArgument;
        if(selector==VIM_GATE_MAP_ALIAS){
            if(a->scalarInputCount || a->scalarOutputCount!=1)return kIOReturnBadArgument;
            IOLockLock(lock);
            if(!pending || !memory){IOLockUnlock(lock);return kIOReturnNotReady;}
            // Deliberate own-scratch lock-order control: createMapping takes
            // IOMemory's global recursive lock and may fault on blocked_access.
            // Restoration MUST NOT need this lock or this client's lock.
            IOMemoryMap *alias=memory->createMappingInTask(kernel_task,0,kIOMapAnywhere|kIOMapUnique);
            a->scalarOutput[0]=alias!=0;if(alias)alias->release();
            IOLockUnlock(lock);return alias?kIOReturnSuccess:kIOReturnNoMemory;
        }
        if(selector==VIM_GATE_EVENT_COUNTS){
            if(a->scalarInputCount || a->scalarOutputCount!=1)return kIOReturnBadArgument;
            IOLockLock(gate_admission);a->scalarOutput[0]=gate_gpu_events;IOLockUnlock(gate_admission);
            return kIOReturnSuccess;
        }
        if(selector==VIM_GATE_ABORT_DRAINED){
            if(a->scalarInputCount!=1 || a->scalarOutputCount)return kIOReturnBadArgument;
            IOLockLock(lock);
            if(!gpu_prepared || pending || quarantined || a->scalarInput[0]!=gpu_serial){IOLockUnlock(lock);return kIOReturnNotOpen;}
            // Trusted lab caller must FIRST drain/destroy its real producer.
            IOReturn restored=restoreAccess();
            if(restored){quarantined=true;IOLockUnlock(lock);return restored;}
            mapping->release();mapping=0;release_gate(object_key);owns_slot=false;object_key=0;
            memory->complete();memory->release();memory=0;gpu_prepared=false;
            IOLockUnlock(lock);release();return kIOReturnSuccess;
        }
        if(selector==VIM_GATE_COMMIT){
            if(a->scalarInputCount!=4 || a->scalarOutputCount || !a->scalarInput[1] ||
               !(a->scalarInput[2]|a->scalarInput[3]))return kIOReturnBadArgument;
            IOLockLock(lock);
            if(!gpu_prepared || !owns_slot || pending || blocked_upl || quarantined || a->scalarInput[0]!=gpu_serial){IOLockUnlock(lock);return kIOReturnNotOpen;}
            uint64_t namespace_id[]={a->scalarInput[2],a->scalarInput[3]};
            result=VIMLegacyWholeObject(mapping,memory)==object_key?kIOReturnSuccess:kIOReturnUnsupported;
            if(!result)result=gpuOpen(namespace_id,a->scalarInput[1]);
            if(!result){
                result=blockAccess();
            }
            if(!result){
                gpu_ticket=a->scalarInput[1];gpu_mode=pending=true;gpu_notified=false;
                result=gpuSubscribe(namespace_id,gpu_ticket);
                // Refused admission has NO callback. Keep the prepared/gated
                // range until the trusted caller drains then ABORT_DRAINED;
                // do not fall back to a waiting MMIO worker or unpin early.
                if(result){gpu_mode=pending=false;gpuClose();}
            }else gpuClose();
            IOLockUnlock(lock);return result;
        }
        if(selector==1) {
            if(a->scalarInputCount || a->scalarOutputCount!=2)return kIOReturnBadArgument;
            IOLockLock(lock);while(pending)IOLockSleep(lock,&pending,THREAD_UNINT);
            a->scalarOutput[0]=pending;a->scalarOutput[1]=result;
            IOLockUnlock(lock);return kIOReturnSuccess;
        }
        if(selector || a->scalarInputCount!=2 || a->scalarOutputCount ||
           !a->scalarInput[0] || (a->scalarInput[0]&4095) ||
           !a->scalarInput[1] || a->scalarInput[1]>65536 || (a->scalarInput[1]&4095) ||
           a->scalarInput[0]>UINT64_MAX-a->scalarInput[1])
            return kIOReturnBadArgument;
        IOLockLock(lock);
        if(pending || memory || quarantined || !reserve_gate()){IOLockUnlock(lock);return kIOReturnBusy;}
        owns_slot=true;
        memory=IOMemoryDescriptor::withAddressRange(a->scalarInput[0],a->scalarInput[1],
            kIODirectionInOut|kIOMemoryPersistent,owner);
        result=memory?memory->prepare():kIOReturnNoMemory;
        if(!result) {
            mapping=memory->createMappingInTask(kernel_task,0,kIOMapAnywhere|kIOMapUnique);
            if(mapping)result=blockAccess();
            else result=kIOReturnNoMemory;
            if(result) {
                if(mapping){mapping->release();mapping=0;}
                memory->complete();
            }
        }
        if(result) {
            if(memory){memory->release();memory=0;}
            release_gate();owns_slot=false;
            IOLockUnlock(lock);return result;
        }
        pending=true;retain();uint64_t deadline=0;
        clock_interval_to_deadline(250,kMillisecondScale,&deadline);
        thread_call_enter_delayed(call,deadline);
        IOLockUnlock(lock);return kIOReturnSuccess;
    }
    IOReturn clientClose() {
        // An uncommitted prepared range may already be used by its producer.
        // Keep its self-retained pin, rather than guessing task death drained
        // another endpoint. Normal committed closure is completed by callout.
        terminate();return kIOReturnSuccess;
    }
    void free() {
        /* The callout retains the instance through completed restoration.
         * Matching XNU explicitly defers freeing a running thread_call. */
        if(call)thread_call_free(call);
        if(lock)IOLockFree(lock);
        IOUserClient::free();
    }
};
OSDefineMetaClassAndStructors(VIMMemoryGateClient,IOUserClient);

class VIMMemoryGate : public IOService {
    OSDeclareDefaultStructors(VIMMemoryGate);
    bool owns_admission;
public:
    bool init(OSDictionary *properties=0) {owns_admission=false;return IOService::init(properties);}
    bool start(IOService *provider) {
        if(!IOService::start(provider))return false;
        if(gate_admission)return false;
        gate_admission=IOLockAlloc();if(!gate_admission)return false;owns_admission=true;
        bzero(&gate_objects,sizeof(gate_objects));
        registerService();return true;
    }
    void free() {if(owns_admission && gate_admission){IOLockFree(gate_admission);gate_admission=0;}IOService::free();}
    IOReturn newUserClient(task_t task,void *security,UInt32 type,
        OSDictionary *,IOUserClient **out) {
        VIMMemoryGateClient *client=new VIMMemoryGateClient;
        if(!client)return kIOReturnNoMemory;
        if(!client->initWithTask(task,security,type) || !client->attach(this) || !client->start(this)) {
            if(client->getProvider())client->detach(this);
            client->release();return kIOReturnError;
        }
        *out=client;return kIOReturnSuccess;
    }
};
OSDefineMetaClassAndStructors(VIMMemoryGate,IOService);
