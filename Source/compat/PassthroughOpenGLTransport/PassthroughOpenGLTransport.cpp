/* Headless OpenGL command accelerator for PVG coexistence. IOAccelerator is
 * the graphics-service base accepted by Mojave's system-graphics sandbox
 * rule. IOMatchCategory alone does not satisfy its superclass check.
 * No framebuffer, IOGLBundleName or Metal plug-in is published here: PVG
 * remains the display/Metal provider and supplies CGL's display device. */
#include <IOKit/graphics/IOAccelerator.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOLib.h>
#include <libkern/OSAtomic.h>
#include <sys/systm.h>
#include "../../vendor/source/VMsvga2/vminclude/vim_gl_headless.h"
#include "../../vendor/source/VMsvga2/AC/UCMethods.h"
#include "../../vendor/source/VMsvga2/vmutil/PhysicalSegment.h"

class PassthroughOpenGLTransport;
class PassthroughOpenGLTransportClient : public IOUserClient {
    OSDeclareDefaultStructors(PassthroughOpenGLTransportClient)
    PassthroughOpenGLTransport *provider_ = nullptr;
    task_t owner_ = nullptr;
    IOLock *lock_ = nullptr;
    unsigned slot_ = VIM_GL_HEADLESS_SLOTS;
    uint32_t token_ = 0;
    uint32_t capabilities_ = 0, scatter_token_ = 0, scatter_serial_ = 0;
    uint32_t scatter_count_ = 0, scatter_bytes_ = 0;
    bool scatter_upload_ = false;
    IOMemoryDescriptor *scatter_memory_ = nullptr;
    VIMGLScatterMemorySegment *scatter_segments_ = nullptr;
    void clearScatter();
    void closeEndpoint();
    static IOReturn transfer(OSObject *,void *,IOExternalMethodArguments *);
    static IOReturn exchange(OSObject *,void *,IOExternalMethodArguments *);
    static IOReturn scatter(OSObject *,void *,IOExternalMethodArguments *);
    IOReturn scatterLocked(IOExternalMethodArguments *);
public:
    bool initWithTask(task_t,void *,UInt32,OSDictionary *) override;
    bool start(IOService *) override;
    IOReturn clientClose() override;
    void free() override;
    IOReturn externalMethod(uint32_t,IOExternalMethodArguments *,IOExternalMethodDispatch *,
                           OSObject *,void *) override;
};
class PassthroughOpenGLTransport : public IOAccelerator {
    OSDeclareDefaultStructors(PassthroughOpenGLTransport)
    IOMemoryMap *registers_ = nullptr, *mailbox_ = nullptr;
    IOLock *slots_lock_ = nullptr;
    uint64_t slots_ = 0;
public:
    bool start(IOService *provider) override {
        IOPCIDevice *pci = OSDynamicCast(IOPCIDevice,provider);
        if (!pci || !IOAccelerator::start(provider)) return false;
        pci->setMemoryEnable(true);
        registers_ = pci->mapDeviceMemoryWithIndex(0);
        mailbox_ = pci->mapDeviceMemoryWithIndex(1);
        slots_lock_ = IOLockAlloc();
        if (!registers_ || !mailbox_ || !slots_lock_ || registers_->getLength()<12 ||
            mailbox_->getLength()!=VIM_GL_HEADLESS_MAILBOX_BYTES) return false;
        volatile uint32_t *registers = reinterpret_cast<volatile uint32_t *>(registers_->getVirtualAddress());
        if (registers[0]!=VIM_GL_MAGIC || registers[1]!=VIM_GL_HEADLESS_VERSION) return false;
        registerService();return true;
    }
    void free() override {
        OSSafeReleaseNULL(registers_);OSSafeReleaseNULL(mailbox_);
        if (slots_lock_) IOLockFree(slots_lock_);
        IOAccelerator::free();
    }
    unsigned acquireSlot() {
        IOLockLock(slots_lock_);unsigned index=0;
        for (;index<VIM_GL_HEADLESS_SLOTS;++index) {
            if (!(slots_ & (1ULL<<index))) { slots_|=1ULL<<index;break; }
        }
        IOLockUnlock(slots_lock_);return index;
    }
    void releaseSlot(unsigned slot) {
        IOLockLock(slots_lock_);slots_&=~(1ULL<<slot);IOLockUnlock(slots_lock_);
    }
    VIMGLDevicePacket *packet(unsigned slot) {
        return reinterpret_cast<VIMGLDevicePacket *>(mailbox_->getVirtualAddress()+
            static_cast<uint64_t>(slot)*VIM_GL_HEADLESS_SLOT_BYTES);
    }
    void submit(unsigned slot) {
        OSSynchronizeIO();
        volatile uint32_t *doorbell=reinterpret_cast<volatile uint32_t *>(
            registers_->getVirtualAddress()+VIM_GL_HEADLESS_DOORBELL);
        *doorbell=slot;OSSynchronizeIO();
    }
    uint32_t ramCapabilities() const {
        if (registers_->getLength()<16) return 0;
        volatile uint32_t *value=reinterpret_cast<volatile uint32_t *>(
            registers_->getVirtualAddress()+VIM_GL_HEADLESS_CAPABILITIES_REGISTER);
        return *value & VIM_GL_HEADLESS_RAM_CAPABILITIES;
    }
    IOReturn newUserClient(task_t task,void *security,UInt32 type,OSDictionary *properties,
                          IOUserClient **output) override {
        if (!output || type!=2 || isInactive()) return kIOReturnUnsupported;
        auto client=new PassthroughOpenGLTransportClient;
        if (!client) return kIOReturnNoMemory;
        if (!client->initWithTask(task,security,type,properties) || !client->attach(this)) {
            client->release();return kIOReturnNoResources;
        }
        if (!client->start(this)) {
            client->detach(this);client->release();return kIOReturnNoResources;
        }
        *output=client;return kIOReturnSuccess;
    }
};
OSDefineMetaClassAndStructors(PassthroughOpenGLTransport,IOAccelerator)
OSDefineMetaClassAndStructors(PassthroughOpenGLTransportClient,IOUserClient)

bool PassthroughOpenGLTransportClient::initWithTask(task_t task,void *security,UInt32 type,
                                                   OSDictionary *properties)
{
    if (!IOUserClient::initWithTask(task,security,type,properties)) return false;
    owner_=task;lock_=IOLockAlloc();return lock_!=nullptr;
}
bool PassthroughOpenGLTransportClient::start(IOService *service)
{
    provider_=OSDynamicCast(PassthroughOpenGLTransport,service);
    if (!provider_ || !IOUserClient::start(service)) { provider_=nullptr;return false; }
    provider_->retain();slot_=provider_->acquireSlot();
    return slot_<VIM_GL_HEADLESS_SLOTS;
}
void PassthroughOpenGLTransportClient::closeEndpoint()
{
    if (lock_) IOLockLock(lock_);
    if (provider_ && slot_<VIM_GL_HEADLESS_SLOTS) {
        if (token_) {
            VIMGLDevicePacket *p=provider_->packet(slot_);
            bzero(p,sizeof(*p));p->magic=VIM_GL_MAGIC;p->action=VIM_GL_HEADLESS_CLOSE;
            p->token=token_;p->result=-1;provider_->submit(slot_);token_=0;
        }
        // The synchronous close has drained accepted I/O before releasing pins.
        clearScatter();capabilities_=0;
        provider_->releaseSlot(slot_);slot_=VIM_GL_HEADLESS_SLOTS;
    }
    if (lock_) IOLockUnlock(lock_);
}
IOReturn PassthroughOpenGLTransportClient::clientClose()
{
    closeEndpoint();terminate();return kIOReturnSuccess;
}
void PassthroughOpenGLTransportClient::free()
{
    if (provider_ && slot_<VIM_GL_HEADLESS_SLOTS) closeEndpoint();
    OSSafeReleaseNULL(provider_);
    if (lock_) IOLockFree(lock_);
    IOUserClient::free();
}
IOReturn PassthroughOpenGLTransportClient::transfer(OSObject *object,void *,IOExternalMethodArguments *args)
{
    auto self=static_cast<PassthroughOpenGLTransportClient *>(object);
    VIMGLTransfer request=*static_cast<const VIMGLTransfer *>(args->structureInput);
    if (request.action<1 || request.action>3 || request.bytes>VIM_GL_CHUNK ||
        (request.action==1 && (request.bytes || request.address)) ||
        (request.action!=1 && (!request.bytes || !request.address ||
                              request.address+request.bytes<request.address))) return kIOReturnBadArgument;
    IOLockLock(self->lock_);IOReturn result=kIOReturnSuccess;
    IOMemoryDescriptor *destination=nullptr;bool prepared=false;
    if (!self->provider_ || self->slot_>=VIM_GL_HEADLESS_SLOTS) result=kIOReturnNotOpen;
    else if (current_task()!=self->owner_) result=kIOReturnNotPermitted;
    else if (request.action==1 ? self->token_!=0 : self->token_==0) result=kIOReturnNotOpen;
    if (!result && request.action==3) {
        destination=IOMemoryDescriptor::withAddressRange(request.address,request.bytes,
            kIODirectionIn,self->owner_);
        if (!destination) result=kIOReturnNoMemory;
        else { result=destination->prepare();prepared=!result; }
    }
    if (!result) {
        VIMGLDevicePacket *p=self->provider_->packet(self->slot_);
        bzero(p,sizeof(*p));p->magic=VIM_GL_MAGIC;p->action=request.action;
        p->token=self->token_;p->bytes=request.bytes;p->result=-1;
        if (request.action==1) p->reserved=self->provider_->ramCapabilities();
        if (request.action==2 && copyin(request.address,p+1,request.bytes)) result=kIOReturnVMError;
        if (!result) {
            self->provider_->submit(self->slot_);
            if (p->result<0 || static_cast<uint32_t>(p->result)>request.bytes) result=kIOReturnIOError;
            else if (request.action==1 && (!p->token || p->result)) result=kIOReturnIOError;
            else if (request.action==3 && destination->writeBytes(0,p+1,p->result)!=
                     static_cast<unsigned>(p->result)) result=kIOReturnVMError;
            if (!result) {
                if (request.action==1) { self->token_=p->token;self->capabilities_=p->reserved; }
                VIMGLResult response={p->result,request.action==1?p->reserved:0};
                *static_cast<VIMGLResult *>(args->structureOutput)=response;
            }
        }
    }
    if (destination) { if (prepared) destination->complete();destination->release(); }
    IOLockUnlock(self->lock_);return result;
}
IOReturn PassthroughOpenGLTransportClient::exchange(OSObject *object,void *,IOExternalMethodArguments *args)
{
    auto self=static_cast<PassthroughOpenGLTransportClient *>(object);
    VIMGLExchange request=*static_cast<const VIMGLExchange *>(args->structureInput);
    if (request.version!=1 || request.reserved || !request.send_bytes || !request.receive_bytes ||
        request.send_bytes>VIM_GL_EXCHANGE_LIMIT || request.receive_bytes>VIM_GL_EXCHANGE_LIMIT ||
        args->structureInputSize!=sizeof(request)+request.send_bytes ||
        args->structureOutputSize!=request.receive_bytes) return kIOReturnBadArgument;
    IOLockLock(self->lock_);IOReturn result=kIOReturnSuccess;
    if (!self->provider_ || self->slot_>=VIM_GL_HEADLESS_SLOTS || !self->token_) result=kIOReturnNotOpen;
    else if (current_task()!=self->owner_) result=kIOReturnNotPermitted;
    else {
        VIMGLDevicePacket *p=self->provider_->packet(self->slot_);
        bzero(p,sizeof(*p));p->magic=VIM_GL_MAGIC;p->action=13;p->token=self->token_;
        p->bytes=request.send_bytes;p->reserved=request.receive_bytes;p->result=-1;
        memcpy(p+1,static_cast<const VIMGLExchange *>(args->structureInput)+1,request.send_bytes);
        self->provider_->submit(self->slot_);
        if (p->result!=static_cast<int32_t>(request.receive_bytes)) result=kIOReturnIOError;
        else memcpy(args->structureOutput,p+1,request.receive_bytes);
    }
    IOLockUnlock(self->lock_);return result;
}
IOReturn PassthroughOpenGLTransportClient::externalMethod(uint32_t selector,IOExternalMethodArguments *args,
    IOExternalMethodDispatch *,OSObject *,void *)
{
    static const IOExternalMethodDispatch transfers={transfer,0,sizeof(VIMGLTransfer),0,sizeof(VIMGLResult)};
    static const IOExternalMethodDispatch exchanges={exchange,0,kIOUCVariableStructureSize,0,kIOUCVariableStructureSize};
    static const IOExternalMethodDispatch scatters={scatter,0,kIOUCVariableStructureSize,0,sizeof(VIMGLScatterResult)};
    // This bounded endpoint uses MIG's copied in-band bytes only. Refuse
    // out-of-line descriptors before touching a possibly absent buffer.
    if (!args || !args->structureInput || !args->structureOutput ||
        args->structureInputDescriptor || args->structureOutputDescriptor) return kIOReturnBadArgument;
    if (selector==kIOVM2DDirectGLTransfer)
        return IOUserClient::externalMethod(selector,args,const_cast<IOExternalMethodDispatch *>(&transfers),this,nullptr);
    if (selector==kIOVM2DDirectGLExchange && args->structureInputSize>=sizeof(VIMGLExchange))
        return IOUserClient::externalMethod(selector,args,const_cast<IOExternalMethodDispatch *>(&exchanges),this,nullptr);
    if (selector==kIOVM2DDirectGLScatter && args->structureInputSize>=sizeof(VIMGLScatterRequest))
        return IOUserClient::externalMethod(selector,args,const_cast<IOExternalMethodDispatch *>(&scatters),this,nullptr);
    return kIOReturnUnsupported;
}

void PassthroughOpenGLTransportClient::clearScatter()
{
    if (scatter_memory_) { scatter_memory_->complete();scatter_memory_->release();scatter_memory_=nullptr; }
    if (scatter_segments_) {
        IOFree(scatter_segments_,VIM_GL_RAM_SCATTER_MAX_RANGES*sizeof(*scatter_segments_));
        scatter_segments_=nullptr;
    }
    scatter_count_=scatter_bytes_=scatter_token_=0;scatter_upload_=false;
}
IOReturn PassthroughOpenGLTransportClient::scatter(OSObject *object,void *,IOExternalMethodArguments *args)
{
    auto self=static_cast<PassthroughOpenGLTransportClient *>(object);
    IOLockLock(self->lock_);
    IOReturn error=self->scatterLocked(args);
    IOLockUnlock(self->lock_);return error;
}
IOReturn PassthroughOpenGLTransportClient::scatterLocked(IOExternalMethodArguments *args)
{
    if (!provider_ || slot_>=VIM_GL_HEADLESS_SLOTS || !token_) return kIOReturnNotOpen;
    if (current_task()!=owner_) return kIOReturnNotPermitted;
    if (!(capabilities_&VIM_GL_RESULT_CAP_RAM_SCATTER)) return kIOReturnUnsupported;
    VIMGLScatterRequest request;
    memcpy(&request,args->structureInput,sizeof(request));
    const bool preparing=request.action==0 || request.action==4;
    if (request.action==3 || request.action>5) return kIOReturnUnsupported;
    if (args->structureInputSize!=sizeof(request)+(preparing?sizeof(VIMGLScatterRows):0) ||
        request.count>VIM_GL_RAM_SCATTER_MAX_ROWS || (!preparing && request.count)) return kIOReturnBadArgument;
    VIMGLScatterResult response={0,0};
    if (preparing) {
        if (scatter_memory_) return kIOReturnExclusiveAccess;
        if (request.action==4 && !(capabilities_&VIM_GL_RESULT_CAP_ROW_UPLOAD)) return kIOReturnUnsupported;
        if (!request.count || request.token || !request.bytes ||
            request.bytes>VIM_GL_SCATTER_MAX_BYTES) return kIOReturnBadArgument;
        VIMGLScatterRows rows;
        memcpy(&rows,static_cast<const char *>(args->structureInput)+sizeof(request),sizeof(rows));
        if (!rows.address || !rows.row_bytes || rows.reverse>1 || rows.stride<rows.row_bytes ||
            (uint64_t)rows.row_bytes*request.count!=request.bytes ||
            (request.count>1 && rows.stride>(UINT64_MAX-rows.row_bytes)/(request.count-1))) return kIOReturnBadArgument;
        uint64_t span=(request.count-1)*rows.stride+rows.row_bytes;
        if (span>VIM_GL_SCATTER_MAX_BYTES || rows.address>UINT64_MAX-span) return kIOReturnBadArgument;
        IOMemoryDescriptor *memory=IOMemoryDescriptor::withAddressRange(rows.address,span,
            request.action==4?kIODirectionOut:kIODirectionIn,owner_);
        if (!memory) return kIOReturnNoMemory;
        IOReturn error=memory->prepare();bool prepared=!error;
        VIMGLScatterMemorySegment *segments=!error?static_cast<VIMGLScatterMemorySegment *>(
            IOMalloc(VIM_GL_RAM_SCATTER_MAX_RANGES*sizeof(*segments))):nullptr;
        if (!error && !segments) error=kIOReturnNoMemory;
        uint32_t count=0;
        for (uint32_t row=0;!error && row<request.count;++row) {
            uint64_t position=(rows.reverse?request.count-1-row:row)*rows.stride;
            uint32_t remaining=rows.row_bytes;
            while (remaining) {
                IOByteCount length=0;addr64_t address=VIMPhysicalSegment(memory,position,&length);
                if (length>remaining) length=remaining;
                if (!length || address>UINT64_MAX-length) { error=kIOReturnUnsupported;break; }
                if (count && segments[count-1].address+segments[count-1].bytes==address)
                    segments[count-1].bytes+=static_cast<uint32_t>(length);
                else if (count<VIM_GL_RAM_SCATTER_MAX_RANGES) {
                    segments[count].address=address;segments[count].bytes=static_cast<uint32_t>(length);
                    segments[count++].kind=1;
                } else { error=kIOReturnUnsupported;break; }
                position+=length;remaining-=static_cast<uint32_t>(length);
            }
        }
        if (error) {
            if (prepared) memory->complete();memory->release();
            if (segments) IOFree(segments,VIM_GL_RAM_SCATTER_MAX_RANGES*sizeof(*segments));
            return error;
        }
        scatter_memory_=memory;scatter_segments_=segments;scatter_count_=count;
        scatter_bytes_=request.bytes;scatter_upload_=request.action==4;
        if (!++scatter_serial_) ++scatter_serial_;
        response.token=scatter_token_=scatter_serial_;
    } else {
        if (!scatter_memory_ || !request.token || request.token!=scatter_token_ ||
            request.bytes!=(request.action==2?0:scatter_bytes_) ||
            (request.action==1 && scatter_upload_) || (request.action==5 && !scatter_upload_)) return kIOReturnBadArgument;
        if (request.action==2) clearScatter();
        else {
            VIMGLDevicePacket *p=provider_->packet(slot_);
            bzero(p,sizeof(*p));p->magic=VIM_GL_MAGIC;p->action=scatter_upload_?10:8;
            p->token=token_;p->bytes=scatter_bytes_;p->reserved=scatter_count_;p->result=-1;
            // Only the immutable physical descriptor table uses the mailbox.
            // The complete owning-task allocation stays pinned until release.
            memcpy(p+1,scatter_segments_,scatter_count_*sizeof(*scatter_segments_));
            provider_->submit(slot_);
            if (p->result!=static_cast<int32_t>(scatter_bytes_)) return kIOReturnIOError;
            response.count=p->result;response.token=scatter_token_;
        }
    }
    memcpy(args->structureOutput,&response,sizeof(response));return kIOReturnSuccess;
}
