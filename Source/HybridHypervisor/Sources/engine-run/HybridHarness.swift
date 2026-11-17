import CEngine
import Engine
import Foundation

// Plays QEMU and TCG for Tests/x86/kernel/hybrid.c, through the C interface the accelerator uses: hybrid config, RAM from a shared mapping as the app
// gives QEMU, split at 1 GB with the rest above 4 GB, full state read and write-back at each stop, a thread forcing boundaries.
final class HybridHarness {
    nonisolated(unsafe) static var current: HybridHarness?
    let below: UInt64 = 1 << 30
    let total: UInt64 = 2 << 30
    var handle: OpaquePointer!
    var ram: UnsafeMutableRawPointer!
    var exitCode: UInt32 = 255
    var failed: String?
    var direct: UnsafeMutableRawPointer?
    var deadline: UInt64 = 0
    var machineOps: EngineMachineOps?
    var deviceWrites: UInt64 = 0
    var pendingVector = false
    var acks = 0
    var slept: UInt64 = 0
    var portsSeen: [String] = []
    var icrSeen: [UInt64] = []
    nonisolated(unsafe) var timerOn = false

    func host(_ gpa: UInt64) -> UnsafeMutableRawPointer { ram + Int(gpa >= 1 << 32 ? gpa - (1 << 32) + below : gpa) }
    func mailbox(_ i: Int) -> UInt64 { host(0x3000).load(fromByteOffset: i * 8, as: UInt64.self) }

    func command(_ n: UInt32) {
        switch n {
        case 1:
            if host((1 << 32) + 0x2000).load(as: UInt64.self) != 0xAABB_CCDD_EEFF_0011 {
                failed = "a guest write above 4 GB is not where the host looks"
            }
        case 2:
            host(mailbox(1)).storeBytes(of: UInt32(truncatingIfNeeded: mailbox(2)), as: UInt32.self)
            engine_note_write(handle, mailbox(1), 4)
        case 3:
            host(mailbox(1)).storeBytes(of: mailbox(2), as: UInt64.self)
            engine_note_write(handle, mailbox(1), 8)
        case 4:
            pendingVector = true
            engine_set_irq_line(handle, true)
            engine_kick(handle)
        case 5:
            if acks != 0 { failed = "acknowledged an interrupt with IF clear" }
        case 6:
            let t0 = mach_absolute_time()
            usleep(50_000)
            slept = mach_absolute_time() - t0
        case 7:
            // Every frame of low RAM, far more than the ring holds. Few are marked, so write far more notes than that: all of the kernel image.
            for _ in 0..<600 { engine_note_write(handle, 1 << 20, 16 << 20) }
        case 12: failed = "rep movsb under CR0.TS took \(mailbox(2)) ticks, against \(mailbox(1)) without"
        case 13:
            var buffer: UnsafeMutableRawPointer?
            posix_memalign(&buffer, 1 << 14, 1 << 16)
            memset(buffer, 0, 1 << 16)
            buffer!.storeBytes(of: 0x0123_4567_89AB_CDEF as UInt64, as: UInt64.self)
            (buffer! + 8191 * 8).storeBytes(of: 0xFEDC_BA98_7654_3210 as UInt64, as: UInt64.self)
            direct = buffer
            if !engine_map_direct(handle, 0xC000_0000, buffer!, 1 << 16) {
                failed = "could not map device memory directly"
            }
        case 14:
            let at = direct!.assumingMemoryBound(to: UInt64.self)
            if at[1] != 0x1111_2222_3333_4444 || at[64] != 0xA5A5_0000_0000_0000 || at[79] != 0xA5A5_0000_0000_000F
                || at[2] != 7
            {
                failed = "the guest's writes are not in the directly mapped memory"
            }
            engine_unmap_direct(handle, 0xC000_0000, 1 << 16)
        case 15:
            let target = handle!
            DispatchQueue.global().asyncAfter(deadline: .now() + .milliseconds(20)) { [self] in
                pendingVector = true
                engine_request_boundary(target)
                engine_set_irq_line(target, true)
                engine_kick(target)
            }
        case 19: engine_unmap_direct(handle, 0xC000_0000, 1 << 16)
        case 21:
            if !icrSeen.isEmpty { failed = "an ICR access other than the send reached the host" }
        case 22:
            if icrSeen != [0x0300_0000_0000_40DD] { failed = "the host got ICR writes \(icrSeen)" }
        case 20:
            let target = handle!
            Thread.detachNewThread { [self] in
                usleep(20)
                pendingVector = true
                engine_set_irq_line(target, true)
                engine_kick(target)
            }
        case 17:
            if deadline != 0x1234_89AB_CDEF { failed = "the TSC deadline did not reach the APIC: \(deadline)" }
        case 18:
            guard ProcessInfo.processInfo.environment["ENGINE_HYBRID_SMP"] == "1" else { return }
            host(0x3000).storeBytes(of: 1 as UInt64, toByteOffset: 8, as: UInt64.self)
            var state = EngineX86State()
            engine_get_state(handle, &state)
            state.rip = mailbox(2)
            state.gpr.4 = mailbox(3)
            state.rflags &= ~0x200
            let first = handle!
            let machine = machineOps!
            Thread.detachNewThread { [self] in
                var ops = machine
                guard let second = engine_create_secondary(first, 1, &ops) else {
                    failed = "the second vCPU would not start"
                    return
                }
                var s = state
                engine_set_state(second, &s)
                while true {
                    switch engine_run(second) {
                    case ENGINE_RUN_FALLBACK.rawValue:
                        engine_get_state(second, &s)
                        playTCG(&s)
                        engine_set_state(second, &s)
                    case ENGINE_RUN_BOUNDARY.rawValue:
                        engine_get_state(second, &s)
                        engine_set_state(second, &s)
                    default:
                        FileHandle.standardError.write(
                            Data("second vCPU: \(String(cString: engine_error_text(second)))\n".utf8))
                        exit(7)
                    }
                }
            }
        case 10: timerOn = true
        case 11: timerOn = false
        case 9:
            let want = ["1540:2:a5a52000", "1541:1:a5a50020", "112:1:a5a50006", "1544:4:12345678"]
            // Only the low `size` bytes of a value count: address_space_write takes those.
            if portsSeen != want { failed = "port writes were \(portsSeen)" }
        case 8:
            // Between the two rdtsc there is a little more than the sleep, so allow some way above 1.2.
            let ratio = Double(mailbox(1)) / (Double(slept) * 125 / 3)
            if ratio < 1.15 || ratio > 1.5 { failed = "TSC rate \(ratio) GHz, set to 1.2" }
        default:
            failed = "unknown command \(n)"
        }
    }

    // The instruction at rip ran elsewhere. r13: what it did. r14: its length.
    func playTCG(_ s: inout EngineX86State) {
        switch s.gpr.13 {
        case 1: s.gpr.3 = 0x1234
        case 30:  // RAM changes with nothing told, as a snapshot load does it, and the engine is told to forget
            host(mailbox(1)).storeBytes(of: UInt8(truncatingIfNeeded: mailbox(2)), as: UInt8.self)
            engine_forget_all(handle)
        case 2: s.cr0 &= ~8
        case 3:
            s.rip = s.gpr.11
            s.sel.1 = UInt16(truncatingIfNeeded: s.gpr.10)
            s.gpr.4 += 16
            return
        case 4: s.cr3 = s.gpr.12
        case 5: s.gpr.0 = 7
        case 8:  // a store across a page boundary, sent back as TCG would finish it
            let width = Int(mailbox(3))
            let first = 4096 - Int(s.gpr.12 & 0xFFF)
            [s.gpr.11, s.gpr.11].withUnsafeBytes { value in
                for i in 0..<width {
                    let at =
                        i < first ? host(mailbox(1) + UInt64(4096 - first + i)) : host(mailbox(2) + UInt64(i - first))
                    at.storeBytes(of: value[i], as: UInt8.self)
                }
            }
            engine_note_write(handle, mailbox(1) + UInt64(4096 - first), UInt64(first))
            engine_note_write(handle, mailbox(2), UInt64(width - first))
        case 9:
            if s.fp_top != 0 || s.fp_tags != 0xFF || s.mm.3 != 0x1122_3344_5566_7788 || s.fp_exp.3 != 0xFFFF {
                failed =
                    "MMX state at a fallback: top \(s.fp_top), tags \(s.fp_tags), mm3 \(s.mm.3), exponent \(s.fp_exp.3)"
            }
            s.mm.3 = ~s.mm.3
        case 13:  // fld1, fldpi under RC up: TOP 6, two valid
            let fp =
                s.fp_top == 6 && s.fp_tags == 0xC0 && s.fcw == 0xB7F && s.mm.6 == 0xC90F_DAA2_2168_C235
                && s.fp_exp.6 == 0x4000 && s.mm.7 == 1 << 63
            if !fp {
                failed =
                    "x87 state at a fallback: top \(s.fp_top), tags \(s.fp_tags), cw \(s.fcw), st0 \(s.mm.6) \(s.fp_exp.6)"
            }
            s.mm.6 = 0xFEDC_BA98_7654_3210
            s.fp_exp.6 = 0x4005
            s.fsw = 0x20
        case 14:  // the operation that went to TCG hasn't touched anything
            if s.fcw != 0x37E || s.fp_top != 6 || s.fp_tags != 0xC0 {
                failed = "unmasked x87 at a fallback: cw \(s.fcw), top \(s.fp_top)"
            }
        case 15:
            if s.fsw & 0x80 == 0 { failed = "fwait at a fallback with nothing pending" }
        case 10:
            if s.fp_tags != 0 { failed = "emms at a fallback: tags \(s.fp_tags)" }
        case 11:  // daa in 32-bit code: everything the host needs is in the state
            if s.compat != 1 || s.sel.1 != 0x43 || s.sel.2 != 0x23 || s.cpl != 3 || s.gpr.0 != 0x4433_2211
                || s.rip != 0x4000_000F || s.gpr.4 != 0x4000_1000
            {
                failed =
                    "32-bit state at a fallback: compat \(s.compat) cs \(s.sel.1) ss \(s.sel.2) cpl \(s.cpl) eax \(s.gpr.0) rip \(s.rip)"
            }
            s.gpr.2 = 0xABCD_0000_0000_0005
            s.gpr.13 = 12
        case 12:  // das, and the host leaves 32-bit code
            if s.compat != 1 || s.rip != 0x4000_0012 || s.gpr.2 != 5 || s.gpr.7 != 5 {
                failed = "the top half the host left: compat \(s.compat) rip \(s.rip) rdx \(s.gpr.2) rdi \(s.gpr.7)"
            }
            s.gpr.0 = 0x1_0000_0055
            s.compat = 0
            s.sel.1 = 0x2B
        default: break
        }
        s.rip += s.gpr.14
    }

    func run(image: MachOImage) -> Int32 {
        HybridHarness.current = self
        ram = engine_test_shared_ram(total)
        var config = EngineConfig(
            ram_bytes: total, ram_host: ram, kernel_model: true, precise_flags: true, tso: true, tzcnt_lzcnt: false,
            ram_below_4g: below, hybrid: true)
        var ops = EngineMachineOps(
            opaque: nil,
            mmio_read: { _, gpa, _ in gpa &* 3 },
            mmio_write: { _, _, _, value in
                let h = HybridHarness.current!
                h.host(0x3000).storeBytes(of: value, as: UInt64.self)
                h.deviceWrites += 1
            },
            port_read: { _, _, _ in 0xFFFF_FFFF },
            port_write: { _, port, size, value in
                let h = HybridHarness.current!
                if port == 0x604 || port == 0x605 || port == 0x70 || port == 0x608 {
                    h.portsSeen.append("\(port):\(size):\(String(value, radix: 16))")
                    if port == 0x604 { engine_tally_port_write(h.handle, port, size, value, 0x1000) }
                } else if port == 0xE9 {
                    FileHandle.standardOutput.write(Data([UInt8(truncatingIfNeeded: value)]))
                } else if port == 0xF4 {
                    h.exitCode = value
                    engine_request_exit(h.handle)
                } else if port == 0xF8 {
                    h.command(value)
                }
            },
            read_msr: { _, _, _ in false },
            write_msr: { _, msr, value in
                guard msr == 0x6E0 else { return false }
                HybridHarness.current!.deadline = value
                return true
            },
            cpuid: { _, _, _, _ in },
            acknowledge_interrupt: { _ in
                let h = HybridHarness.current!
                if !h.pendingVector { return -1 }
                h.pendingVector = false
                h.acks += 1
                return 0x40
            },
            has_interrupt_waiting: { _ in HybridHarness.current!.pendingVector },
            wait_for_interrupt: { _, _ in false },
            apic_eoi: { _, _ in },
            apic_icr: { _, icr in HybridHarness.current!.icrSeen.append(icr) })
        guard let created = engine_create(&config, &ops) else { return 3 }
        machineOps = ops
        if ProcessInfo.processInfo.environment["ENGINE_HYBRID_SMP"] == "1" { engine_prepare_count(created, 2) }
        handle = created
        host((1 << 32) + 0x1000).storeBytes(of: UInt64(0x1122_3344_5566_7788), as: UInt64.self)
        engine_mark_device(handle, 0xA0000, 0x1000)
        let tallyPath =
            ProcessInfo.processInfo.environment["ENGINE_TALLY"] ?? NSTemporaryDirectory() + "hybrid-tally-\(getpid())"
        tallyPath.withCString { engine_tally_start(handle, $0) }
        engine_set_tsc(handle, 0, 1 << 50, 1, 5)

        // The page tables cover the hole and everything above it: 5 GB of physmap.
        let root = KernelGuest.buildBootTables(image: image, ram: ram, ramBytes: (1 << 32) + total - below)
        var s = EngineX86State()
        engine_get_state(handle, &s)
        s.rip = image.entry!
        s.hybrid_apic_page = 0xD000_0000
        s.cr3 = root
        s.cr0 = 0x8001_0033
        s.cr4 = 0x620
        s.efer = 0xD01
        s.rflags = 2
        s.sel.1 = 0x08
        s.sel.2 = 0x10
        s.mxcsr = 0x1F80
        s.fcw = 0x37F
        engine_set_state(handle, &s)

        let target = handle!
        Thread.detachNewThread {
            while true {
                usleep(2000)
                engine_request_boundary(target)
                if HybridHarness.current!.timerOn {
                    HybridHarness.current!.pendingVector = true
                    engine_set_irq_line(target, true)
                    engine_kick(target)
                }
            }
        }
        var fallbacks = 0
        var boundaries = 0
        while true {
            let result = engine_run(handle)
            switch result {
            case ENGINE_RUN_FALLBACK:
                fallbacks += 1
                engine_get_state(handle, &s)
                playTCG(&s)
                engine_set_state(handle, &s)
            case ENGINE_RUN_BOUNDARY:
                boundaries += 1
                engine_get_state(handle, &s)
                engine_set_state(handle, &s)
            case ENGINE_RUN_EXIT:
                if let failed {
                    FileHandle.standardError.write(Data("host check failed: \(failed)\n".utf8))
                    return 4
                }
                if deviceWrites == 0 { return 5 }
                engine_tally_refused(handle, "refused for the test")
                engine_tally_sample(handle, 0x1234, 0, 0)
                var text = [CChar](repeating: 0, count: 4096)
                engine_debug_text(handle, &text, 4096)
                if !String(cString: text).contains("in hv_vcpu_run: false") || engine_progress(handle) == 0 { return 8 }
                engine_tally_write(handle)
                let tally = (try? String(contentsOfFile: tallyPath, encoding: .utf8)) ?? ""
                for reason in [
                    "unknown opcode", "x87: fcos", "hlt", "far return", "mov cr8", "rdmsr 0x1b", "wrmsr 0x277",
                    "popcnt", "mmio form", "verr", "BCD adjust"
                ]
                where !tally.contains(reason) {
                    FileHandle.standardError.write(Data("tally lacks \(reason)\n".utf8))
                    return 6
                }
                if ProcessInfo.processInfo.environment["LC_HVDBT_NO_SHARE"] != "1",
                    tally.contains("under 0 directories") || !tally.contains("shared user blocks")
                {
                    FileHandle.standardError.write(Data("no directory was shared\n".utf8))
                    return 6
                }
                if !tally.contains("wait4 failed, error 10") || tally.contains("SIGNAL 65") {
                    FileHandle.standardError.write(Data("a wait4 that failed was logged as something else\n".utf8))
                    return 6
                }
                // 300 processes ran kernel code and exited, which must not cost the kernel its translations each time.
                let higher =
                    tally.split(separator: "\n").first { $0.hasSuffix("a store higher in the tables") }?.split(
                        separator: " "
                    ).dropFirst(2).first.flatMap { Int($0) } ?? .max
                if ProcessInfo.processInfo.environment["LC_HVDBT_NO_SHARE"] != "1", higher > 300 {
                    FileHandle.standardError.write(
                        Data("kernel blocks died with exiting processes: \\(higher) times\n".utf8))
                    return 6
                }
                // 250,000 stores go to a frame that stopped being a table: only the first may trap.
                let stores = tally.split(separator: "\n").first { $0.hasPrefix("page-table stores") }?.split(
                    separator: " "
                ).dropFirst(2).first
                if let stores, Int(stores) ?? .max > 100_000 {
                    FileHandle.standardError.write(
                        Data("a stale table still traps: \(stores) page-table stores\n".utf8))
                    return 6
                }
                if tally.contains("CR4 write") || !tally.contains("refused for the test")
                    || !tally.contains("tcg 0x1234") || !tally.contains("vmport") || !tally.contains("FAULT vector 3")
                    || !tally.contains("wait4 returned 77 status 0x300 exit 3") || !tally.contains(" exit 7")
                    || !tally.contains("port 0x604 size 2")
                    || !tally.contains("unslid") || !tally.contains("vector 0x7")
                    || !tally.contains("\nC 0x0 ")
                {
                    FileHandle.standardError.write(Data("tally has a wrong row\n".utf8))
                    return 9
                }
                if ProcessInfo.processInfo.environment["ENGINE_TALLY"] == nil { unlink(tallyPath) }
                FileHandle.standardError.write(
                    Data("hybrid: \(fallbacks) fallbacks, \(boundaries) boundary stops, \(tr_stats().reused) dead blocks reused, \(tr_stats().call_tails) call lookups\n".utf8))
                return Int32(bitPattern: exitCode)
            case ENGINE_RUN_ERROR:
                FileHandle.standardError.write(
                    Data("engine stop: \(String(cString: engine_error_text(handle)))\n".utf8))
                return 2
            default:
                FileHandle.standardError.write(Data("guest halted for good\n".utf8))
                return 7
            }
        }
    }
}
