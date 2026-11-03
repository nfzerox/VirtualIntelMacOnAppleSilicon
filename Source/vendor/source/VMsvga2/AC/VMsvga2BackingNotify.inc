/* Native GPU completion has ONE shared PCI interrupt owner: the framebuffer.
 * Its short dispatcher calls here; the slow mode-change handshake is on a
 * separate loop. Per-job callbacks must only enqueue their own restoration.
 * Kernel-only pointers stay here; the device receives integer identities.
 * There is no periodic timer, user-process wakeup, pixel copy or GPU wait.
 */
#include "../vminclude/vim_handoff_profile.h"
struct VIMBackingKernelJob {
    uint64_t cookie,ticket;
    OSObject* owner;
    void (*completed)(OSObject*,uint64_t,IOReturn);
};
IOReturn CLASS::borrowBackingAccess(void* key,void* owner)
{
    if(!owner || !m_backing_access_lock)return kIOReturnBadArgument;
    // Registry admission/retirement must not wait for a notification packet's
    // native/device round trip. No relation between the two tables requires
    // that coupling: callbacks already run outside the notification lock.
    IOLockLock(m_backing_access_lock);int result;
    while((result=m_backing_access.borrow(key,owner))==VIMBackingAccessRegistry::Conflict){
        IOLockSleep(m_backing_access_lock,&m_backing_access,THREAD_UNINT);
    }
    IOLockUnlock(m_backing_access_lock);
    return result==VIMBackingAccessRegistry::Admitted?kIOReturnSuccess:
        result==VIMBackingAccessRegistry::Full?kIOReturnNoResources:kIOReturnBusy;
}
void CLASS::returnBackingAccess(void* owner)
{
    IOLockLock(m_backing_access_lock);
    m_backing_access.returnBorrow(owner);
    IOLockWakeup(m_backing_access_lock,&m_backing_access,false);
    IOLockUnlock(m_backing_access_lock);
}
bool CLASS::promoteBackingAccess(void* key,void* owner)
{
    if(!m_backing_access_lock)return false;
    IOLockLock(m_backing_access_lock);bool promoted=m_backing_access.promote(key,owner);
    IOLockUnlock(m_backing_access_lock);return promoted;
}
void CLASS::releaseBackingAccess(void* key)
{
    IOLockLock(m_backing_access_lock);
    m_backing_access.releaseExclusive(key);
    IOLockWakeup(m_backing_access_lock,&m_backing_access,false);
    IOLockUnlock(m_backing_access_lock);
}
IOReturn CLASS::transferBackingNotifications()
{
    if (!m_framebuffer || !m_svga || !m_backing_notify_buffer)return kIOReturnNoDevice;
    // Both capabilities were negotiated before creating this channel. Its
    // packet and VRAM allocation are serialized by m_backing_notify_lock (or
    // quiesced teardown), not the indexed-register/device lock. Never queue
    // GPU retirement behind another client's synchronous GL wait on that
    // global lock. No geometry or register-index latch is read/changed here.
    m_svga->WriteDirectGLDoorbell(static_cast<uint32_t>(offsetInVRAM(m_backing_notify_buffer)));
    return kIOReturnSuccess;
}
IOReturn CLASS::openBackingNotifications()
{
    if (m_backing_notify_failed || !m_framebuffer || !m_svga)return kIOReturnNotReady;
    if (m_backing_notify_registered)return kIOReturnSuccess;
    m_framebuffer->lockDevice();
    bool supported=m_svga->ReadReg(VIM_GL_NOTIFY_CAPABILITIES_REGISTER)==VIM_GL_NOTIFY_MAGIC &&
        m_svga->ReadReg(VIM_GL_DOORBELL_CAPABILITIES_REGISTER)==VIM_GL_DOORBELL_MAGIC;
    m_framebuffer->unlockDevice();
    if (!supported)return kIOReturnUnsupported;
    if (!m_backing_notify_buffer) {
        IOReturn error=setupAllocator();if (error)return error;
        m_backing_notify_buffer=VRAMMalloc(sizeof(VIMGLDevicePacket)+32);
        if (!m_backing_notify_buffer)return kIOReturnNoMemory;
    }
    VIMGLDevicePacket* p=static_cast<VIMGLDevicePacket*>(m_backing_notify_buffer);
    if (!m_backing_notify_token) {
        bzero(p,sizeof(*p)+32);p->magic=VIM_GL_MAGIC;p->action=1;p->result=-1;
        IOReturn error=transferBackingNotifications();
        if (error || p->result || !p->token)return error?error:kIOReturnIOError;
        m_backing_notify_token=p->token;
    }
    IOReturn error=m_framebuffer->configureBackingInterrupt(this,&CLASS::backingNotifications);
    if (!error)m_backing_notify_registered=true;
    return error;
}
IOReturn CLASS::callPlatformFunction(const OSSymbol* function,bool wait,void* param1,void* param2,void* param3,void* param4)
{
    bool available=function && function->isEqualTo(VIM_BACKING_NOTIFY_AVAILABLE);
    bool subscribe=function && function->isEqualTo(VIM_BACKING_NOTIFY_SUBSCRIBE);
    if (!available && !subscribe)return super::callPlatformFunction(function,wait,param1,param2,param3,param4);
    if (param2 || param3 || param4 || (available && param1) || !m_backing_notify_lock)return kIOReturnBadArgument;
    VIMBackingKernelSubscription request;
    if (subscribe) {
        if (!param1)return kIOReturnBadArgument;
        request=*static_cast<const VIMBackingKernelSubscription*>(param1);
        if (request.version!=1 || request.bytes!=sizeof(request) || !request.owner || !request.completed ||
            !request.ticket || !(request.namespace_id[0]|request.namespace_id[1]))return kIOReturnBadArgument;
    }
    VIM_HANDOFF_SPAN_IF(handoff,"backing-subscribe-v2",subscribe);
    IOLockLock(m_backing_notify_lock);
    VIM_HANDOFF_MARK(handoff,1);
    IOReturn error=openBackingNotifications();
    VIM_HANDOFF_MARK(handoff,2);
    if (!error && subscribe) {
        unsigned slot;
        for (slot=0;slot<256;++slot)if (!m_backing_notify_jobs[slot])break;
        if (slot==256 || m_backing_notify_serial==UINT64_MAX)error=kIOReturnNoResources;
        VIMBackingKernelJob* job=!error?static_cast<VIMBackingKernelJob*>(IOMalloc(sizeof(VIMBackingKernelJob))):0;
        if (!error && !job)error=kIOReturnNoMemory;
        if (!error) {
            job->cookie=++m_backing_notify_serial;job->ticket=request.ticket;
            job->owner=request.owner;job->completed=request.completed;job->owner->retain();
            m_backing_notify_jobs[slot]=job; // publish before native callback can occur
            VIMGLDevicePacket* p=static_cast<VIMGLDevicePacket*>(m_backing_notify_buffer);
            bzero(p,sizeof(*p)+32);p->magic=VIM_GL_MAGIC;p->action=14;
            p->token=m_backing_notify_token;p->bytes=32;p->result=-1;
            uint64_t body[]={request.namespace_id[0],request.namespace_id[1],request.ticket,job->cookie};
            memcpy(p+1,body,sizeof(body));
            VIM_HANDOFF_MARK(handoff,3);
            error=transferBackingNotifications();
            VIM_HANDOFF_MARK(handoff,4);
            if (!error && p->result)error=kIOReturnIOError;
            if (error) {
                m_backing_notify_jobs[slot]=0;job->owner->release();IOFree(job,sizeof(*job));
            }
        }
    }
    IOLockUnlock(m_backing_notify_lock);return error;
}
void CLASS::backingNotifications(OSObject* owner)
{
    CLASS* self=static_cast<CLASS*>(owner);
    for (unsigned delivered=0;delivered<256;++delivered) {
        IOLockLock(self->m_backing_notify_lock);
        VIMGLDevicePacket* p=static_cast<VIMGLDevicePacket*>(self->m_backing_notify_buffer);
        bzero(p,sizeof(*p)+32);p->magic=VIM_GL_MAGIC;p->action=15;p->bytes=24;p->result=-1;
        IOReturn error=self->transferBackingNotifications();
        if (!error && !p->result) { IOLockUnlock(self->m_backing_notify_lock);return; }
        if (!error && p->result!=24)error=kIOReturnIOError;
        uint64_t cookie=0,ticket=0;uint32_t status=0,reserved=0;
        if (!error) {
            memcpy(&cookie,p+1,8);memcpy(&ticket,reinterpret_cast<char*>(p+1)+8,8);
            memcpy(&status,reinterpret_cast<char*>(p+1)+16,4);memcpy(&reserved,reinterpret_cast<char*>(p+1)+20,4);
            if (!cookie || !ticket || reserved)error=kIOReturnIOError;
        }
        VIMBackingKernelJob* job=0;
        for (unsigned slot=0;!error && slot<256;++slot) {
            if (self->m_backing_notify_jobs[slot] && self->m_backing_notify_jobs[slot]->cookie==cookie) {
                job=self->m_backing_notify_jobs[slot];self->m_backing_notify_jobs[slot]=0;
                if (job->ticket!=ticket)error=kIOReturnIOError;
                break;
            }
        }
        if (!job)error=kIOReturnIOError;
        if (error) {
            self->m_backing_notify_failed=true;
            self->m_svga->EnableNativeBackingInterrupt(false);
        }
        IOLockUnlock(self->m_backing_notify_lock);
        if (job) { job->completed(job->owner,job->ticket,error?error:status?kIOReturnIOError:kIOReturnSuccess);
            job->owner->release();IOFree(job,sizeof(*job)); }
        if (error) { IOLog("VMsvga2Accel: backing completion stream failed; pending memory must remain quarantined\n");return; }
    }
}
void CLASS::closeBackingNotifications()
{
    // Unregister without holding the registry lock: an active dispatch may
    // already be waiting for it. The gate waits for that dispatch to finish.
    if (m_backing_notify_registered && m_framebuffer)m_framebuffer->configureBackingInterrupt(0,0);
    m_backing_notify_registered=false;
    if (!m_backing_notify_lock)return;
    for (unsigned slot=0;slot<256;++slot) {
        IOLockLock(m_backing_notify_lock);VIMBackingKernelJob* job=m_backing_notify_jobs[slot];m_backing_notify_jobs[slot]=0;
        IOLockUnlock(m_backing_notify_lock);
        if (job) { job->completed(job->owner,job->ticket,kIOReturnAborted);job->owner->release();IOFree(job,sizeof(*job)); }
    }
    if (m_backing_notify_buffer) {
        if (m_backing_notify_token) {
            VIMGLDevicePacket* p=static_cast<VIMGLDevicePacket*>(m_backing_notify_buffer);
            bzero(p,sizeof(*p));p->magic=VIM_GL_MAGIC;p->action=4;p->token=m_backing_notify_token;p->result=-1;
            transferBackingNotifications();m_backing_notify_token=0;
        }
        VRAMFree(m_backing_notify_buffer);m_backing_notify_buffer=0;
    }
    IOLockFree(m_backing_notify_lock);m_backing_notify_lock=0;
}
