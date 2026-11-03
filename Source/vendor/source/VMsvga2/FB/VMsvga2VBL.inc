/* Virtual 60-Hz panel clock. Matching IOGraphics-471.92.1 (2.4.1) registers
 * handleVBL here to maintain the PUBLIC StdFBShmem clock and semaphore.
 * Unsupported registration left both vblTime/vblDelta zero: native
 * MPGetNextWakeupTime then returns an expired deadline, so WindowServer
 * paints intermediate window-level transactions instead of deferring them.
 * CVDisplayLink's separate fallback timer does not repair that kernel ABI.
 *
 * This is a virtual display interrupt, not a guessed sleep in glFlush or the
 * host presenter. IOFramebuffer's own enable/disable and fixed-timing VBL
 * throttling stop this timer when it does not need an interrupt. No pixel
 * copy, per-frame allocation, log, CPU polling or extra workloop/thread.
 */
IOReturn CLASS::registerVirtualVBL(IOFBInterruptProc proc,OSObject* target,void* ref,void** result)
{
	if(!proc || !target || !result)return kIOReturnBadArgument;
	*result=0;if(m_vbl_timer)return kIOReturnBusy;
	IOWorkLoop* loop=getWorkLoop();if(!loop)return kIOReturnNotReady;
	m_vbl_loop=loop;loop->retain();
	m_vbl_timer=IOTimerEventSource::timerEventSource(this,&CLASS::virtualVBL);
	if(!m_vbl_timer){cleanupVirtualVBL();return kIOReturnNoMemory;}
	IOReturn rc=loop->addEventSource(m_vbl_timer);
	if(rc){m_vbl_timer->release();m_vbl_timer=0;cleanupVirtualVBL();return rc;}
	uint64_t second=0;clock_interval_to_absolutetime_interval(1,kSecondScale,&second);
	m_vbl_period=second/60;
	if(!m_vbl_period){cleanupVirtualVBL();return kIOReturnUnsupported;}
	m_vbl_intr.target=target;m_vbl_intr.ref=ref;m_vbl_intr.proc=proc;
	rc=loop->runAction(&CLASS::virtualVBLState,this,reinterpret_cast<void*>(1));
	if(rc){cleanupVirtualVBL();return rc;}
	*result=&m_vbl_intr;setProperty("VIMVirtualVBL",kOSBooleanTrue);
	return kIOReturnSuccess;
}
IOReturn CLASS::scheduleVirtualVBL()
{
	if(!m_vbl_enabled || !m_vbl_timer || !m_vbl_period)return kIOReturnNotReady;
	uint64_t now=0;clock_get_uptime(&now);
	// Keep phase through throttling/rescheduling and skip missed periods.
	// Never replay an interrupt storm after a paused/slow emulated CPU.
	m_vbl_deadline=now-now%m_vbl_period+m_vbl_period;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	/* Darwin8/9 kernel AbsoluteTime is UnsignedWide, not uint64_t. Passing
	 * the scalar selects wakeAtTime(UInt32, scale=1), truncating the uptime
	 * to32 bits. After4.29s it repeatedly schedules an expired deadline:
	 * measured guest VBL is ~4000Hz versus the physical panel's60Hz.
	 * Select the actual native absolute-time overload, including both words.
	 * Later kernel SDKs retain their existing scalar call unchanged. */
	AbsoluteTime deadline;
	deadline.lo=static_cast<UInt32>(m_vbl_deadline);
	deadline.hi=static_cast<UInt32>(m_vbl_deadline>>32);
	return m_vbl_timer->wakeAtTime(deadline);
#else
	return m_vbl_timer->wakeAtTime(m_vbl_deadline);
#endif
}
void CLASS::virtualVBL(OSObject* owner,IOTimerEventSource*)
{
	CLASS* self=static_cast<CLASS*>(owner);
	if(!self->m_vbl_enabled || !self->m_vbl_intr.proc)return;
	self->m_vbl_intr.proc(self->m_vbl_intr.target,self->m_vbl_intr.ref);
	// The callback may disable/unregister the source on this same gate.
	if(self->m_vbl_enabled && self->scheduleVirtualVBL())self->m_vbl_enabled=false;
}
void CLASS::cleanupVirtualVBL()
{
	IOWorkLoop* loop=m_vbl_loop;
	if(loop){loop->runAction(&CLASS::virtualVBLState,this,reinterpret_cast<void*>(2));loop->release();}
}
IOReturn CLASS::virtualVBLState(OSObject* owner,void* requested,void*,void*,void*)
{
	CLASS* self=static_cast<CLASS*>(owner);
	uintptr_t state=reinterpret_cast<uintptr_t>(requested);
	self->m_vbl_enabled=false;
	if(state==2){
		if(self->m_vbl_timer){
			self->m_vbl_timer->cancelTimeout();self->m_vbl_timer->disable();
			self->m_vbl_loop->removeEventSource(self->m_vbl_timer);
			self->m_vbl_timer->release();self->m_vbl_timer=0;
		}
		bzero(&self->m_vbl_intr,sizeof self->m_vbl_intr);self->m_vbl_loop=0;
		return kIOReturnSuccess;
	}
	if(!self->m_vbl_timer || !self->m_vbl_intr.proc)return kIOReturnNotReady;
	if(!state){self->m_vbl_timer->cancelTimeout();return kIOReturnSuccess;}
	self->m_vbl_enabled=true;
	IOReturn rc=self->scheduleVirtualVBL();if(rc)self->m_vbl_enabled=false;
	return rc;
}
IOReturn CLASS::getTimingInfoForDisplayMode(IODisplayModeID mode,IOTimingInformation* info)
{
	const DisplayModeEntry* d=GetDisplayMode(mode);
	if(!d || !info || !d->width || !d->height)return kIOReturnBadArgument;
	bzero(info,sizeof(*info));info->flags=kIODetailedTimingValid;
	IODetailedTimingInformationV2& timing=info->detailedInfo.v2;
	// This panel has no physical scanout blanking/link. Describe the same
	// exact virtual 60 Hz as getInformationForDisplayMode, with no invented
	// HDMI/CVT timings. Equal min/max permits IOGraphics' normal VBL throttle
	// to extrapolate the fixed interval while interrupts are disabled.
	timing.horizontalActive=d->width;timing.verticalActive=d->height;
	timing.pixelClock=static_cast<uint64_t>(d->width)*d->height*60U;
	timing.minPixelClock=timing.maxPixelClock=timing.pixelClock;
	return kIOReturnSuccess;
}
