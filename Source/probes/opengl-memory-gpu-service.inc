/* A separate KERNEL-owned normal 2D transport endpoint. Only the context-free
 * completion protocol is sent. No borrowed application connection/context,
 * private VMsvga class linkage or guest-thread dependency. Native completion
 * arrives through the framebuffer's one interrupt owner, not a waiting MMIO.
 * Compatibility ticket cleanup follows actual CPU-access restoration, so its
 * ordinary transport/device locks cannot strand a faulting CPU alias. */
    IOReturn gpuTransfer(void *bytes,size_t length,bool writing) {
        while(length){
            VIMGLTransfer request={writing?2U:3U,static_cast<uint32_t>(length),reinterpret_cast<uintptr_t>(bytes)};
            VIMGLResult reply={0,0};IOExternalMethodArguments a;bzero(&a,sizeof(a));
            a.structureInput=&request;a.structureInputSize=sizeof(request);
            a.structureOutput=&reply;a.structureOutputSize=sizeof(reply);
            IOReturn error=gpu_service->externalMethod(kIOVM2DDirectGLTransfer,&a);
            if(error || a.structureOutputSize!=sizeof(reply) || reply.count<=0 || static_cast<uint32_t>(reply.count)>length)
                return error?error:kIOReturnIOError;
            length-=reply.count;bytes=static_cast<char*>(bytes)+reply.count;
        }
        return kIOReturnSuccess;
    }
    IOReturn gpuExchange(VIMGLServiceMessage *message,VIMGLServiceReply *reply) {
        bzero(reply,sizeof(*reply));IOReturn error=gpuTransfer(message,sizeof(*message),true);
        if(!error)error=gpuTransfer(reply,sizeof(*reply),false);
        if(!error && (reply->bytes || reply->error))error=kIOReturnIOError;
        return error;
    }
    void gpuClose() {
        if(gpu_service){gpu_service->clientClose();gpu_service->release();gpu_service=0;}
        if(gpu_accelerator){gpu_accelerator->release();gpu_accelerator=0;}
    }
    IOReturn gpuPlatform(const char *name,void *request) {
        const OSSymbol *symbol=OSSymbol::withCStringNoCopy(name);
        if(!symbol)return kIOReturnNoMemory;
        IOReturn error=gpu_accelerator->callPlatformFunction(symbol,false,request,0,0,0);
        symbol->release();return error;
    }
    static void gpuNotified(OSObject *owner,uint64_t ticket,IOReturn status) {
        VIMMemoryGateClient *self=static_cast<VIMMemoryGateClient*>(owner);
        IOLockLock(self->lock);
        if(self->gpu_mode && self->pending && ticket==self->gpu_ticket && !self->gpu_notified){
            self->gpu_notified=true;self->gpu_notification_status=status;
            IOLockLock(gate_admission);++gate_gpu_events;IOLockUnlock(gate_admission);
            thread_call_enter(self->call);
        }
        IOLockUnlock(self->lock);
    }
    IOReturn gpuSubscribe(const uint64_t namespace_id[2],uint64_t ticket) {
        VIMBackingKernelSubscription request; bzero(&request,sizeof(request));
        request.version=1;request.bytes=sizeof(request);memcpy(request.namespace_id,namespace_id,16);
        request.ticket=ticket;request.owner=this;request.completed=gpuNotified;
        return gpuPlatform(VIM_BACKING_NOTIFY_SUBSCRIBE,&request);
    }
    IOReturn gpuOpen(const uint64_t namespace_id[2],uint64_t ticket) {
        OSDictionary *matching=IOService::serviceMatching("VMsvga2Accel");
        IOService *accelerator=matching?IOService::copyMatchingService(matching):0;
        if(matching)matching->release();
        if(!accelerator)return kIOReturnNotFound;
        gpu_accelerator=accelerator;
        IOReturn error=gpuPlatform(VIM_BACKING_NOTIFY_AVAILABLE,0);
        if(!error)error=accelerator->newUserClient(kernel_task,0,2,&gpu_service);
        if(error){IOLog("VIMMemoryGate: kernel event transport unavailable=%x\n",error);gpuClose();return error;}
        VIMGLTransfer request={1,0,0};VIMGLResult opened={0,0};IOExternalMethodArguments a;bzero(&a,sizeof(a));
        a.structureInput=&request;a.structureInputSize=sizeof(request);
        a.structureOutput=&opened;a.structureOutputSize=sizeof(opened);
        error=gpu_service->externalMethod(kIOVM2DDirectGLTransfer,&a);
        if(!error && (a.structureOutputSize!=sizeof(opened) || opened.count))error=kIOReturnIOError;
        VIMGLServiceMessage message;VIMGLServiceReply reply;bzero(&message,sizeof(message));
        message.op=VIM_GL_SERVICE_HELLO;message.flags=VIM_GL_SERVICE_REPLY;
        message.args[0]=VIM_GL_SERVICE_MAGIC;message.args[1]=VIM_GL_SERVICE_VERSION;
        memcpy(&message.args[3],namespace_id,16);
        if(error)IOLog("VIMMemoryGate: kernel stream open failed=%x\n",error);
        if(!error)error=gpuExchange(&message,&reply);
        if(!error && !(reply.result&VIM_GL_SERVICE_CAP_BACKING_TICKETS))error=kIOReturnUnsupported;
        bzero(&message,sizeof(message));message.op=VIM_GL_SERVICE_BACKING_CHECK;
        message.flags=VIM_GL_SERVICE_REPLY;message.args[0]=ticket;
        if(!error)error=gpuExchange(&message,&reply);
        if(!error && reply.result!=1)error=kIOReturnNotReady;
        if(error){IOLog("VIMMemoryGate: completion service admission failed=%x\n",error);gpuClose();}return error;
    }
    IOReturn gpuCompletionStatus() {
        // Only the matched retained kernel subscription calls gpuNotified.
        // A validated positive IRQ is completion proof, not a cancellation or
        // user-controlled ACK. No native WAIT/control lock is needed here.
        IOLockLock(lock);IOReturn status=gpu_notified?gpu_notification_status:kIOReturnNotReady;
        IOLockUnlock(lock);return status;
    }
    IOReturn gpuRetireTicket() {
        // Only AFTER restoreAccess. This may take ordinary metadata locks;
        // those can no longer hold up CPU page-fault resolution.
        VIMGLServiceMessage message;VIMGLServiceReply reply;bzero(&message,sizeof(message));
        message.op=VIM_GL_SERVICE_BACKING_WAIT;message.flags=VIM_GL_SERVICE_REPLY;message.args[0]=gpu_ticket;
        IOReturn error=gpuExchange(&message,&reply);
        if(!error && reply.result!=1)error=kIOReturnNotReady;
        if(!error){
            message.op=VIM_GL_SERVICE_BACKING_RELEASE;error=gpuExchange(&message,&reply);
            if(!error && reply.result!=1)error=kIOReturnIOError;
        }
        gpuClose();return error;
    }
