/* Native mode publication, qualified against 13F1911 CoreGraphics and
 * matching IOGraphics-471.92.1. No Apple binary patch. */
void CLASS::configureNativePanel(uint32_t max_width,uint32_t max_height)
{
    if (svga.ReadReg(VIM_PANEL_CAPABILITIES_REGISTER)!=VIM_PANEL_MAGIC)return;
    uint32_t width=svga.ReadReg(VIM_PANEL_WIDTH_REGISTER),height=svga.ReadReg(VIM_PANEL_HEIGHT_REGISTER);
    uint32_t scale=svga.ReadReg(VIM_PANEL_SCALE_REGISTER);
    // The accelerator reserves the negotiated primary aperture. Never
    // advertise geometry that could overlap an allocated backing/transport.
    if ((scale!=1 && scale!=2) || width<640 || height<400 || width>max_width || height>max_height ||
        static_cast<uint64_t>(width)*height*4>svga.getPrimaryScanoutBytes() ||
        (scale==2 && (width<2048 || height<1536)))return;
    for (uint32_t i=1;i<m_num_active_modes;++i) {
        const DisplayModeEntry* mode=GetDisplayMode(m_modes[i]);
        if (mode && mode->width==width && mode->height==height) { m_panel_mode=m_modes[i];break; }
    }
    if (!m_panel_mode) {
        customMode.width=width;customMode.height=height;
        customMode.flags=kDisplayModeValidFlag|kDisplayModeSafeFlag;
        m_panel_mode=CUSTOM_MODE_ID;
    }
    m_panel_width=width;m_panel_height=height;m_panel_scale=scale;
    setProperty("VIMNativePanelWidth",static_cast<uint64_t>(width),32U);
    setProperty("VIMNativePanelHeight",static_cast<uint64_t>(height),32U);
    setProperty("VIMNativePanelScale",static_cast<uint64_t>(scale),32U);
}

void CLASS::setupNativePanelInterrupt(IOService* provider)
{
    if (!m_panel_mode || !m_iolock ||
        svga.ReadReg(VIM_PANEL_IRQ_CAPABILITIES_REGISTER)!=VIM_PANEL_IRQ_MAGIC)return;
    // The hardware dispatcher must NEVER wait for WindowServer. In particular
    // mode-change handshake can need GPU completion on this same PCI vector.
    // A software event on a second loop owns the slow CustomMode handshake.
    m_panel_resize_loop=IOWorkLoop::workLoop();
    m_panel_resize_event=IOInterruptEventSource::interruptEventSource(this,&CLASS::panelResize);
    if (!m_panel_resize_loop || !m_panel_resize_event ||
        m_panel_resize_loop->addEventSource(m_panel_resize_event)!=kIOReturnSuccess) {
        cleanupNativePanelInterrupt();return;
    }
    m_panel_resize_event->enable();
    m_panel_loop=IOWorkLoop::workLoop();
    if (!m_panel_loop) { cleanupNativePanelInterrupt();return; }
    m_panel_dispatch_gate=IOCommandGate::commandGate(this);
    if (!m_panel_dispatch_gate || m_panel_loop->addEventSource(m_panel_dispatch_gate)!=kIOReturnSuccess) {
        cleanupNativePanelInterrupt();return;
    }
    m_backing_interrupt_supported=svga.ReadReg(VIM_GL_NOTIFY_CAPABILITIES_REGISTER)==VIM_GL_NOTIFY_MAGIC;
    m_panel_interrupt=IOFilterInterruptEventSource::filterInterruptEventSource(
        this,&CLASS::panelInterrupt,&CLASS::panelInterruptFilter,provider,0);
    if (!m_panel_interrupt) { cleanupNativePanelInterrupt();return; }
    if (m_panel_loop->addEventSource(m_panel_interrupt)!=kIOReturnSuccess) {
        m_panel_interrupt->release();m_panel_interrupt=0;
        cleanupNativePanelInterrupt();return;
    }
    m_panel_interrupt->enable();
    enableNativePanelInterrupt(m_intr.proc && m_intr_enabled);
    setProperty("VIMNativePanelInterrupt",kOSBooleanTrue);
}

void CLASS::enableNativePanelInterrupt(bool enabled)
{
    if (!m_panel_interrupt || !m_iolock)return;
    IOLockLock(m_iolock);
    // IOFramebuffer's public contract requires suppressing hotplug while
    // captured. Its initial register callback precedes boot configuration;
    // wait for the first real uncapture before delivering an early request.
    svga.WriteReg(VIM_PANEL_IRQ_ENABLE_REGISTER,enabled && m_panel_uncaptured?1:0);
    IOLockUnlock(m_iolock);
}

void CLASS::cleanupNativePanelInterrupt()
{
    if (m_panel_dispatch_gate && m_panel_dispatch_gate->getWorkLoop())
        configureBackingInterrupt(0,0);
    if (m_panel_interrupt) {
        if (m_iolock) {
            IOLockLock(m_iolock);
            svga.WriteReg(VIM_PANEL_IRQ_ENABLE_REGISTER,0);
            IOLockUnlock(m_iolock);
        }
        m_panel_interrupt->disable();
        if (m_panel_loop)m_panel_loop->removeEventSource(m_panel_interrupt);
        m_panel_interrupt->release();m_panel_interrupt=0;
    }
    if (m_panel_dispatch_gate) {
        if (m_panel_dispatch_gate->getWorkLoop())m_panel_loop->removeEventSource(m_panel_dispatch_gate);
        m_panel_dispatch_gate->release();m_panel_dispatch_gate=0;
    }
    if (m_panel_loop) { m_panel_loop->release();m_panel_loop=0; }
    if (m_panel_resize_event) {
        if (m_panel_resize_event->getWorkLoop())m_panel_resize_loop->removeEventSource(m_panel_resize_event);
        m_panel_resize_event->release();m_panel_resize_event=0;
    }
    if (m_panel_resize_loop) { m_panel_resize_loop->release();m_panel_resize_loop=0; }
    m_backing_interrupt_supported=false;
}

struct VIMFramebufferBackingAction { OSObject* owner;void (*action)(OSObject*); };
IOReturn CLASS::configureBackingInterrupt(OSObject* owner,void (*action)(OSObject*))
{
    if (!m_backing_interrupt_supported || !m_panel_dispatch_gate ||
        !m_panel_dispatch_gate->getWorkLoop() || (!!owner!=!!action))return kIOReturnUnsupported;
    VIMFramebufferBackingAction request={owner,action};
    return m_panel_dispatch_gate->runAction(&CLASS::backingInterruptState,&request);
}
IOReturn CLASS::backingInterruptState(OSObject* owner,void* argument,void*,void*,void*)
{
    CLASS* self=static_cast<CLASS*>(owner);
    VIMFramebufferBackingAction* request=static_cast<VIMFramebufferBackingAction*>(argument);
    if (request->owner && self->m_backing_interrupt_owner && request->owner!=self->m_backing_interrupt_owner)
        return kIOReturnExclusiveAccess;
    self->svga.EnableNativeBackingInterrupt(false);
    OSObject* prior=self->m_backing_interrupt_owner;
    if (request->owner)request->owner->retain();
    self->m_backing_interrupt_owner=request->owner;self->m_backing_interrupt_action=request->action;
    self->svga.EnableNativeBackingInterrupt(request->owner!=0);
    if (prior)prior->release();
    return kIOReturnSuccess;
}

bool CLASS::panelInterruptFilter(OSObject* owner,IOFilterInterruptEventSource*)
{
    // Shared PCI vectors can signal for the NIC or another device. This
    // dedicated direct port is safe in a primary interrupt: unlike ReadReg,
    // it does not mutate the indexed register latch or require a sleep lock.
    CLASS* self=static_cast<CLASS*>(owner);
    return self->svga.NativePanelInterruptPending() ||
        (self->m_backing_interrupt_supported && self->svga.NativeBackingInterruptPending());
}

void CLASS::panelInterrupt(OSObject* owner,IOInterruptEventSource*,int)
{
    CLASS* self=static_cast<CLASS*>(owner);
    if (self->m_backing_interrupt_owner && self->svga.NativeBackingInterruptPending())
        self->m_backing_interrupt_action(self->m_backing_interrupt_owner);
    IOLockLock(self->m_iolock);
    uint32_t generation=self->svga.ReadReg(VIM_PANEL_GENERATION_REGISTER);
    if (generation==self->m_panel_generation) {
        IOLockUnlock(self->m_iolock);return;
    }
    uint32_t width=self->svga.ReadReg(VIM_PANEL_REQUEST_WIDTH_REGISTER);
    uint32_t height=self->svga.ReadReg(VIM_PANEL_REQUEST_HEIGHT_REGISTER);
    uint32_t scale=self->svga.ReadReg(VIM_PANEL_REQUEST_SCALE_REGISTER);
    bool stable=generation==self->svga.ReadReg(VIM_PANEL_GENERATION_REGISTER);
    if (!self->m_intr.proc || !self->m_intr_enabled) {
        // Preserve an early request without a level-IRQ storm. Registration
        // or re-enabling of the real IOFramebuffer callback re-arms delivery.
        self->svga.WriteReg(VIM_PANEL_IRQ_ENABLE_REGISTER,0);
        IOLockUnlock(self->m_iolock);return;
    }
    if (stable) {
        self->svga.WriteReg(VIM_PANEL_ACK_REGISTER,generation);
        self->m_panel_generation=generation;
        self->m_panel_pending_width=width;self->m_panel_pending_height=height;
        self->m_panel_pending_scale=scale==1 || scale==2 ? scale : self->m_panel_scale;
    }
    IOLockUnlock(self->m_iolock);
    // A concurrent request leaves the level interrupt asserted for retry.
    if (!stable ||
        width<800 || height<600 || width>self->svga.getMaxWidth() || height>self->svga.getMaxHeight() ||
        static_cast<uint64_t>(width)*height*4>self->svga.getPrimaryScanoutBytes())return;
    self->m_panel_resize_event->interruptOccurred(0,0,0);
}

void CLASS::panelResize(OSObject* owner,IOInterruptEventSource*,int)
{
    CLASS* self=static_cast<CLASS*>(owner);
    IOLockLock(self->m_iolock);
    uint32_t width=self->m_panel_pending_width,height=self->m_panel_pending_height,generation=self->m_panel_generation;
    uint32_t scale=self->m_panel_pending_scale;
    IOLockUnlock(self->m_iolock);
    if (width<800 || height<600)return;
    CustomModeData input={1,width,height},output={0,0,0};size_t size=sizeof(output);
    self->setProperty("VIMNativePanelRequestWidth",static_cast<uint64_t>(width),32U);
    self->setProperty("VIMNativePanelRequestHeight",static_cast<uint64_t>(height),32U);
    // The EDID keeps its boot density. This IORegistry property is the
    // effective density for the existing guest agent, including agents
    // already installed before the hotpatch.
    self->setProperty("VIMNativePanelScale",static_cast<uint64_t>(scale),32U);
    self->setProperty("VIMNativePanelRequestGeneration",static_cast<uint64_t>(generation),32U);
    IOReturn result=self->CustomMode(&input,&output,sizeof(input),&size);
    if (result==kIOReturnSuccess && output.width==width && output.height==height) {
        self->setProperty("VIMNativePanelAppliedWidth",static_cast<uint64_t>(width),32U);
        self->setProperty("VIMNativePanelAppliedHeight",static_cast<uint64_t>(height),32U);
    }
}
