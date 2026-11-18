import CEngine
import Foundation

/// Forwards `EngineMachine` to the C callback table of `engine_api.h`.
final class CMachine: EngineMachine {
    let ops: EngineMachineOps
    init(ops: EngineMachineOps) { self.ops = ops }

    func mmioRead(address: UInt64, size: Int) -> UInt64 { ops.mmio_read(ops.opaque, address, UInt32(size)) }
    func mmioWrite(address: UInt64, size: Int, value: UInt64) { ops.mmio_write(ops.opaque, address, UInt32(size), value) }
    func portRead(port: UInt16, size: Int) -> UInt32 { ops.port_read(ops.opaque, port, UInt32(size)) }
    func portWrite(port: UInt16, size: Int, value: UInt32) { ops.port_write(ops.opaque, port, UInt32(size), value) }
    func readMSR(_ msr: UInt32) -> UInt64? {
        var v: UInt64 = 0
        return ops.read_msr(ops.opaque, msr, &v) ? v : nil
    }
    func writeMSR(_ msr: UInt32, value: UInt64) -> Bool { ops.write_msr(ops.opaque, msr, value) }
    func cpuid(leaf: UInt32, subleaf: UInt32) -> (eax: UInt32, ebx: UInt32, ecx: UInt32, edx: UInt32) {
        var out: [UInt32] = [0, 0, 0, 0]
        ops.cpuid(ops.opaque, leaf, subleaf, &out)
        return (out[0], out[1], out[2], out[3])
    }
    func acknowledgeInterrupt() -> UInt8? {
        let v = ops.acknowledge_interrupt(ops.opaque)
        return v < 0 ? nil : UInt8(truncatingIfNeeded: v)
    }
    func hasInterruptWaiting() -> Bool { ops.has_interrupt_waiting(ops.opaque) }
    func waitForInterrupt(deadlineNanos: UInt64) -> Bool { ops.wait_for_interrupt(ops.opaque, deadlineNanos) }
    func apicEOI(vector: UInt8) { ops.apic_eoi(ops.opaque, vector) }
    func apicICR(_ icr: UInt64) { ops.apic_icr(ops.opaque, icr) }
    func syscall(_ state: UnsafeMutablePointer<EngineCtx>, engine: Engine) throws {
        throw EngineError.stop("syscall with no shim: the C API is the kernel model")
    }
}

private final class Handle {
    let engine: Engine
    let machine: CMachine
    var error: UnsafeMutablePointer<CChar>?
    var started = false
    var entry: UInt64 = 0
    var awayAt: UInt64 = 0
    var awayKind: UInt32 = 0, awayCode: UInt32 = 0, awayView: UInt32 = 0, awayCPL: UInt32 = 0
    var awayDetail0: UInt64 = 0, awayDetail1: UInt64 = 0
    init(engine: Engine, machine: CMachine) {
        self.engine = engine
        self.machine = machine
    }
}

private func handle(_ h: OpaquePointer) -> Handle { Unmanaged<Handle>.fromOpaque(UnsafeRawPointer(h)).takeUnretainedValue() }

@_cdecl("engine_create")
public func engine_create(_ config: UnsafePointer<EngineConfig>, _ ops: UnsafePointer<EngineMachineOps>) -> OpaquePointer? {
    var options = EngineOptions()
    options.ramBytes = config.pointee.ram_bytes
    options.kernelModel = config.pointee.kernel_model
    options.preciseFlags = config.pointee.precise_flags
    options.useTSO = config.pointee.tso
    options.tzcntLzcnt = config.pointee.tzcnt_lzcnt
    options.hostRAM = config.pointee.ram_host
    options.ramBelow4G = config.pointee.ram_below_4g
    options.hybrid = config.pointee.hybrid
    let engine: Engine
    do {
        engine = try Engine(options: options)
    } catch {
        FileHandle.standardError.write(Data("engine_create: \(error)\n".utf8))
        return nil
    }
    let machine = CMachine(ops: ops.pointee)
    engine.machine = machine
    return OpaquePointer(Unmanaged.passRetained(Handle(engine: engine, machine: machine)).toOpaque())
}

@_cdecl("engine_destroy")
public func engine_destroy(_ h: OpaquePointer) { Unmanaged<Handle>.fromOpaque(UnsafeRawPointer(h)).release() }

@_cdecl("engine_prepare_count")
public func engine_prepare_count(_ first: OpaquePointer, _ count: UInt32) { handle(first).engine.prepareCount(count) }

@_cdecl("engine_create_secondary")
public func engine_create_secondary(_ first: OpaquePointer, _ index: UInt32, _ ops: UnsafePointer<EngineMachineOps>) -> OpaquePointer? {
    guard index < UInt32(kMaxVcpus) else {
        FileHandle.standardError.write(Data("engine_create_secondary: vCPU \(index) is past the limit\n".utf8))
        return nil
    }
    let engine: Engine
    do {
        engine = try Engine(secondaryOf: handle(first).engine, index: index)
    } catch {
        FileHandle.standardError.write(Data("engine_create_secondary: \(error)\n".utf8))
        return nil
    }
    let machine = CMachine(ops: ops.pointee)
    engine.machine = machine
    return OpaquePointer(Unmanaged.passRetained(Handle(engine: engine, machine: machine)).toOpaque())
}

@_cdecl("engine_count_exclusive_stop")
public func engine_count_exclusive_stop(_ h: OpaquePointer) { handle(h).engine.tally?.exclusiveStop() }

@_cdecl("engine_ram")
public func engine_ram(_ h: OpaquePointer) -> UnsafeMutableRawPointer { handle(h).engine.ram }

@_cdecl("engine_run")
public func engine_run(_ h: OpaquePointer) -> UInt32 {
    let box = handle(h)
    if box.awayAt != 0 {
        box.engine.recordLatency(kind: box.awayKind, code: box.awayCode,
            start: box.awayAt, end: mach_absolute_time(), wait: 0,
            detail0: box.awayDetail0, detail1: box.awayDetail1, view: box.awayView, cpl: box.awayCPL)
        box.awayAt = 0
    }
    box.engine.tally?.entered(rip: box.started ? box.engine.ctx.pointee.req_r.0 : box.entry)
    do {
        let outcome = box.started ? try box.engine.resume() : try box.engine.run(entry: box.entry)
        box.started = true
        if box.engine.latencyTracing {
            switch outcome {
            case .fallback, .boundary:
                let c = box.engine.ctx.pointee
                if case .fallback = outcome {
                    // The TCG HLT fallback deliberately waits for an interrupt.
                    // Keep those idle intervals out of the slow-execution ring.
                    if c.req_a.0 == UInt64(STOP_FALLBACK) && c.req_a.2 == UInt64(FB_HLT) { break }
                    box.awayKind = UInt32(ENGINE_LATENCY_FALLBACK_AWAY)
                    box.awayCode = UInt32(truncatingIfNeeded: c.req_a.0)
                    box.awayDetail0 = c.req_a.1; box.awayDetail1 = c.req_a.2
                } else {
                    box.awayKind = UInt32(ENGINE_LATENCY_BOUNDARY_AWAY)
                    box.awayCode = 0; box.awayDetail0 = box.engine.boundaryRIP; box.awayDetail1 = 0
                }
                box.awayAt = mach_absolute_time()
                box.awayView = c.view; box.awayCPL = c.cpl
            default: break
            }
        }
        switch outcome {
        case .exited: return ENGINE_RUN_EXIT.rawValue
        case .halted: return ENGINE_RUN_HALTED.rawValue
        case .boundary: return ENGINE_RUN_BOUNDARY.rawValue
        case .fallback: return ENGINE_RUN_FALLBACK.rawValue
        }
    } catch {
        free(box.error)
        box.error = strdup("\(error)")
        box.engine.tally?.stopped("\(error)")
        return ENGINE_RUN_ERROR.rawValue
    }
}

@_cdecl("engine_error_text")
public func engine_error_text(_ h: OpaquePointer) -> UnsafePointer<CChar>? { UnsafePointer(handle(h).error) }

@_cdecl("engine_map_direct")
public func engine_map_direct(_ h: OpaquePointer, _ gpa: UInt64, _ host: UnsafeMutableRawPointer, _ bytes: UInt64) -> Bool {
    handle(h).engine.mapDirect(gpa: gpa, host: host, bytes: bytes)
}

@_cdecl("engine_forget_all")
public func engine_forget_all(_ h: OpaquePointer) {
    handle(h).engine.forgetAll()
}

@_cdecl("engine_unmap_direct")
public func engine_unmap_direct(_ h: OpaquePointer, _ gpa: UInt64, _ bytes: UInt64) {
    handle(h).engine.unmapDirect(gpa: gpa, bytes: bytes)
}

@_cdecl("engine_mark_device")
public func engine_mark_device(_ h: OpaquePointer, _ gpa: UInt64, _ bytes: UInt64) { handle(h).engine.markDevice(gpa: gpa, bytes: bytes) }

@_cdecl("engine_note_write")
public func engine_note_write(_ h: OpaquePointer, _ gpa: UInt64, _ bytes: UInt64) { handle(h).engine.noteWrite(gpa: gpa, bytes: bytes) }

@_cdecl("engine_set_irq_line")
public func engine_set_irq_line(_ h: OpaquePointer, _ level: Bool) { engine_ctx_set_irq_line(handle(h).engine.ctx, level) }

@_cdecl("engine_set_tsc")
public func engine_set_tsc(_ h: OpaquePointer, _ nsOffset: UInt64, _ tscOffset: UInt64, _ num: UInt64, _ den: UInt64) {
    handle(h).engine.setTSC(nsOffset: nsOffset, tscOffset: tscOffset, num: num, den: den)
}

@_cdecl("engine_tally_start")
public func engine_tally_start(_ h: OpaquePointer, _ path: UnsafePointer<CChar>) {
    let tally = Tally(engine: handle(h).engine, path: String(cString: path))
    handle(h).engine.tally = tally
    tally.startWriting()
}

@_cdecl("engine_tally_write")
public func engine_tally_write(_ h: OpaquePointer) { handle(h).engine.tally?.write() }

@_cdecl("engine_tally_refused")
public func engine_tally_refused(_ h: OpaquePointer, _ reason: UnsafePointer<CChar>) {
    handle(h).engine.tally?.refused(String(cString: reason))
}

@_cdecl("engine_tally_note")
public func engine_tally_note(_ h: OpaquePointer, _ text: UnsafePointer<CChar>) { handle(h).engine.tally?.note(String(cString: text)) }

@_cdecl("engine_tally_port_write")
public func engine_tally_port_write(_ h: OpaquePointer, _ port: UInt16, _ size: UInt32, _ value: UInt32, _ slide: UInt64) {
    handle(h).engine.tally?.portWrite(port: port, size: size, value: value, slide: slide)
}

@_cdecl("engine_tally_sample")
public func engine_tally_sample(_ h: OpaquePointer, _ rip: UInt64, _ cpl: UInt32, _ cr3: UInt64) {
    handle(h).engine.tally?.tcgSample(rip: rip, cpl: cpl, cr3: cr3)
}

@_cdecl("engine_progress")
public func engine_progress(_ h: OpaquePointer) -> UInt64 { handle(h).engine.progress() }

@_cdecl("engine_debug_text")
public func engine_debug_text(_ h: OpaquePointer, _ out: UnsafeMutablePointer<CChar>, _ size: UInt) {
    strlcpy(out, handle(h).engine.debugText(), Int(size))
}

@_cdecl("engine_get_state")
public func engine_get_state(_ h: OpaquePointer, _ out: UnsafeMutablePointer<EngineX86State>) {
    out.pointee = handle(h).engine.state()
    if !handle(h).started { out.pointee.rip = handle(h).entry }
}

@_cdecl("engine_set_state")
public func engine_set_state(_ h: OpaquePointer, _ state: UnsafePointer<EngineX86State>) {
    let box = handle(h)
    box.engine.setState(state.pointee)
    if !box.started { box.entry = state.pointee.rip }
}

@_cdecl("engine_request_exit")
public func engine_request_exit(_ h: OpaquePointer) { handle(h).engine.requestStop = true }

@_cdecl("engine_request_boundary")
public func engine_request_boundary(_ h: OpaquePointer) { handle(h).engine.requestBoundary() }

@_cdecl("engine_kick")
public func engine_kick(_ h: OpaquePointer) { handle(h).engine.interruptRequested() }

@_cdecl("engine_post_vector")
public func engine_post_vector(_ h: OpaquePointer, _ vector: UInt8, _ level: Bool) {
    handle(h).engine.postVector(vector, level: level)
}

extension Engine {
    /// The x86 CPU state at a boundary, in QEMU's terms.
    public func state() -> EngineX86State {
        var s = EngineX86State()
        let c = ctx
        withUnsafeBytes(of: &c.pointee.x) { src in
            withUnsafeMutableBytes(of: &s.gpr) { $0.copyMemory(from: UnsafeRawBufferPointer(rebasing: src[0..<128])) }
        }
        s.rip = boundaryRIP
        s.rflags = rflags()
        s.cr0 = c.pointee.cr0
        s.cr2 = c.pointee.cr2
        s.cr3 = c.pointee.cr3
        s.cr4 = c.pointee.cr4
        s.cr8 = UInt64(c.pointee.apic_tpr >> 4)
        s.efer = c.pointee.efer
        s.star = c.pointee.star
        s.lstar = c.pointee.lstar
        s.cstar = c.pointee.cstar
        s.sfmask = c.pointee.sfmask
        s.fs_base = c.pointee.x.28
        s.gs_base = c.pointee.x.27
        s.kernel_gs_base = c.pointee.kernel_gs_base
        s.gdt_base = c.pointee.gdtr_base
        s.gdt_limit = c.pointee.gdtr_limit
        s.idt_base = c.pointee.idtr_base
        s.idt_limit = c.pointee.idtr_limit
        s.tss_base = c.pointee.tss_base
        s.tss_limit = c.pointee.tss_limit
        s.sel = c.pointee.seg_sel
        s.tr = c.pointee.tr
        s.ldtr = c.pointee.ldtr
        s.cpl = c.pointee.cpl
        s.mxcsr = mxcsr()
        readXMM(into: &s)
        s.mm = c.pointee.mm
        s.fp_exp = c.pointee.fp_exp
        s.fp_top = c.pointee.fp_top
        s.fp_tags = c.pointee.fp_tags
        s.fcw = c.pointee.fcw
        s.fsw = c.pointee.fsw
        s.fop = c.pointee.fop
        s.fcs = c.pointee.fcs
        s.fds = c.pointee.fds
        s.fip = c.pointee.fip
        s.fdp = c.pointee.fdp
        s.apic_base = c.pointee.apic_base | (c.pointee.apic_enabled != 0 ? 1 << 11 : 0)
        s.apic_tpr = c.pointee.apic_tpr
        s.apic_svr = c.pointee.apic_svr
        s.apic_id = c.pointee.apic_id
        s.apic_ldr = c.pointee.apic_ldr
        s.apic_dfr = c.pointee.apic_dfr
        s.apic_lvt_timer = c.pointee.apic_lvt_timer
        s.apic_lvt = c.pointee.apic_lvt
        s.apic_tmict = c.pointee.apic_tmict
        s.apic_tdcr = c.pointee.apic_tdcr
        s.apic_irr = c.pointee.apic_irr
        s.apic_isr = c.pointee.apic_isr
        s.apic_tmr = c.pointee.apic_tmr
        s.hybrid_apic_page = c.pointee.hybrid_apic_page
        s.hybrid_icr_lo = c.pointee.apic_icr_lo
        s.hybrid_icr_hi = c.pointee.apic_icr_hi
        s.irq_shadow = s.rip == c.pointee.irq_shadow_rip ? 1 : 0
        s.compat = c.pointee.compat
        s.seg_bad = c.pointee.seg_bad
        s.ldt_base = c.pointee.ldt_base
        s.ldt_limit = c.pointee.ldt_limit
        s.sysenter_cs = c.pointee.sysenter_cs
        s.sysenter_esp = c.pointee.sysenter_esp
        s.sysenter_eip = c.pointee.sysenter_eip
        return s
    }

    /// Writes CPU state back at a boundary. Control registers that select a view take effect through the monitor's next path.
    public func setState(_ s: EngineX86State) {
        let c = ctx
        var input = s
        withUnsafeBytes(of: &input.gpr) { src in
            withUnsafeMutableBytes(of: &c.pointee.x) { dst in
                dst.baseAddress!.copyMemory(from: src.baseAddress!, byteCount: 128)
            }
        }
        setRFlags(s.rflags)
        c.pointee.cr0 = s.cr0
        c.pointee.cr2 = s.cr2
        c.pointee.cr3 = s.cr3
        c.pointee.cr4 = s.cr4
        c.pointee.efer = s.efer
        c.pointee.star = s.star
        c.pointee.lstar = s.lstar
        c.pointee.cstar = s.cstar
        c.pointee.sfmask = s.sfmask
        c.pointee.x.28 = s.fs_base
        c.pointee.x.27 = s.gs_base
        c.pointee.kernel_gs_base = s.kernel_gs_base
        c.pointee.gdtr_base = s.gdt_base
        c.pointee.gdtr_limit = s.gdt_limit
        c.pointee.idtr_base = s.idt_base
        c.pointee.idtr_limit = s.idt_limit
        c.pointee.tss_base = s.tss_base
        c.pointee.tss_limit = s.tss_limit
        c.pointee.seg_sel = s.sel
        c.pointee.tr = s.tr
        c.pointee.ldtr = s.ldtr
        c.pointee.cpl = s.cpl
        c.pointee.mxcsr = s.mxcsr & 0xFFC0
        c.pointee.hybrid_apic_page = s.hybrid_apic_page
        c.pointee.apic_icr_lo = s.hybrid_icr_lo
        c.pointee.apic_icr_hi = s.hybrid_icr_hi
        c.pointee.resume_mxcsr = s.mxcsr
        writeXMM(from: s)
        c.pointee.mm = s.mm
        c.pointee.compat = s.compat
        c.pointee.seg_bad = s.seg_bad
        c.pointee.ldt_base = s.ldt_base
        c.pointee.ldt_limit = s.ldt_limit
        c.pointee.sysenter_cs = s.sysenter_cs
        c.pointee.sysenter_esp = s.sysenter_esp
        c.pointee.sysenter_eip = s.sysenter_eip
        c.pointee.fp_exp = s.fp_exp
        c.pointee.fp_top = s.fp_top & 7
        c.pointee.fp_tags = s.fp_tags
        c.pointee.fcw = s.fcw
        c.pointee.fsw = s.fsw & ~0x3800
        c.pointee.fop = s.fop
        c.pointee.fcs = s.fcs
        c.pointee.fds = s.fds
        c.pointee.fip = s.fip
        c.pointee.fdp = s.fdp
        c.pointee.req_r.0 = s.rip
    }

    func rflags() -> UInt64 {
        let c = ctx
        let nz = c.pointee.spsr
        var r = c.pointee.rflags_sys
        if nz & (1 << 29) == 0 { r |= 1 }
        if nz & (1 << 30) != 0 { r |= 0x40 }
        if nz & (1 << 31) != 0 { r |= 0x80 }
        if nz & (1 << 28) != 0 { r |= 0x800 }
        if (c.pointee.x.21 & 0xFF).nonzeroBitCount & 1 == 0 { r |= 4 }
        if (c.pointee.x.25 ^ c.pointee.x.21) & 0x10 != 0 { r |= 0x10 }
        if c.pointee.df < 0 { r |= 0x400 }
        return r
    }

    func setRFlags(_ r: UInt64) {
        let c = ctx
        var s = c.pointee.spsr & ~(0xF << 28)
        if r & 1 == 0 { s |= 1 << 29 }
        if r & 0x40 != 0 { s |= 1 << 30 }
        if r & 0x80 != 0 { s |= 1 << 31 }
        if r & 0x800 != 0 { s |= 1 << 28 }
        c.pointee.spsr = s
        c.pointee.x.21 = r & 4 != 0 ? 0 : 1
        c.pointee.x.25 = c.pointee.x.21 ^ (r & 0x10)
        c.pointee.df = r & 0x400 != 0 ? -1 : 1
        c.pointee.rflags_sys = (r & ~0xCD5) | 2
    }
}
