import CEngine
import Foundation

/// Several vCPUs in one VM running `Tests/x86/kernel/smp.c`, each on its own thread. Every vCPU starts at the image's entry in long mode: no INIT or SIPI, those stay with
/// whatever owns the machine. The machine gives each vCPU a console (0xE9), an exit port (0xF4), the vCPU count (0xF5), whether EnTSO is on (0xF6), and IPIs.
public final class SMPGuest: @unchecked Sendable {
    private var cpus: [SMPCPU] = []
    private let options: EngineOptions
    private let count: UInt32
    private let console = NSLock()

    public init(options: EngineOptions = EngineOptions(), count: UInt32) {
        var o = options
        o.kernelModel = true
        self.options = o
        self.count = count
    }

    fileprivate func deliver(icr: UInt64, from: UInt32) {
        let vector = UInt8(icr & 0xFF), shorthand = (icr >> 18) & 3, target = UInt32(icr >> 56)
        guard (icr >> 8) & 7 == 0 else { fatalError("IPI delivery mode \((icr >> 8) & 7) is not in this test") }
        for cpu in cpus {
            let index = cpu.engine.cpuIndex
            let hit = shorthand == 0 ? index == target : shorthand == 1 ? index == from : shorthand == 2 ? true : index != from
            if hit { cpu.post(vector) }
        }
    }

    fileprivate func write(_ byte: UInt8) {
        console.lock()
        FileHandle.standardOutput.write(Data([byte]))
        console.unlock()
    }

    fileprivate var tsoOn: Bool { options.useTSO }
    fileprivate var vcpuCount: UInt32 { count }

    public func run(image: MachOImage) throws -> Int32 {
        guard (1...8).contains(count) else { throw EngineError.stop("SMP vCPU count must be 1...8") }
        guard let entry = image.entry else { throw EngineError.badImage("no entry") }
        let first = try Engine(options: options)
        first.prepareCount(count)
        let root = KernelGuest.buildBootTables(image: image, ram: first.ram, ramBytes: options.ramBytes)
        func prepare(_ engine: Engine) {
            let ctx = engine.ctx
            ctx.pointee.cr3 = root
            ctx.pointee.cpl = 0
            ctx.pointee.seg_sel.1 = 0x08
            ctx.pointee.seg_sel.2 = 0x10
            ctx.pointee.rflags_sys = 2
            ctx.pointee.x.7 = options.ramBytes
            ctx.pointee.x.6 = UInt64(engine.cpuIndex)
        }
        prepare(first)
        cpus.append(SMPCPU(guest: self, engine: first))

        // All of them exist before any runs, as an IPI may be sent to any at once.
        let created = DispatchSemaphore(value: 0), go = DispatchSemaphore(value: 0)
        final class Box: @unchecked Sendable {
            var engine: Engine?
            var outcome: RunOutcome?
            let finished = DispatchSemaphore(value: 0)
        }
        let boxes = (1..<max(count, 1)).map { _ in Box() }
        for (i, box) in boxes.enumerated() {
            Thread.detachNewThread { [self] in
                defer { box.finished.signal() }
                pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0)
                do {
                    let engine = try Engine(secondaryOf: first, index: UInt32(i + 1))
                    box.engine = engine
                    created.signal()
                    go.wait()
                    box.outcome = try engine.run(entry: entry)
                } catch {
                    FileHandle.standardError.write(Data("engine stop on vCPU \(i + 1): \(error)\n".utf8))
                    exit(2)
                }
                _ = self
            }
        }
        for _ in boxes { created.wait() }
        for box in boxes {
            prepare(box.engine!)
            cpus.append(SMPCPU(guest: self, engine: box.engine!))
        }
        for _ in boxes { go.signal() }
        func finishSecondaries() throws {
            // Give guests which explicitly exit every CPU a brief chance to finish.
            // Other SMP fixtures halt their peers: wake those at an x86 boundary.
            // The semaphore is also the publication boundary for each outcome and
            // context. Never read final statistics while a peer can still write them.
            let grace = DispatchTime.now() + .milliseconds(100)
            var joined = boxes.map { $0.finished.wait(timeout: grace) == .success }
            for (i, cpu) in cpus.dropFirst().enumerated() where !joined[i] {
                cpu.stopAtBoundary()
            }
            let deadline = DispatchTime.now() + .seconds(5)
            for i in boxes.indices where !joined[i] {
                guard boxes[i].finished.wait(timeout: deadline) == .success else {
                    throw EngineError.stop("SMP secondary \(i + 1) did not stop within join deadline")
                }
                joined[i] = true
            }
        }
        let firstOutcome: RunOutcome
        do {
            firstOutcome = try first.run(entry: entry)
        } catch {
            try? finishSecondaries()
            throw error
        }
        try finishSecondaries()
        if ProcessInfo.processInfo.environment["ENGINE_STATS"] != nil {
            FileHandle.standardError.write(Data("SMP_JOIN expected \(count) stopped \(cpus.count)\n".utf8))
            let shared = first.ctx.pointee.shared_dir_count
            for (i, cpu) in cpus.enumerated() {
                let result = i == 0 ? firstOutcome : boxes[i - 1].outcome
                let kind: String
                let code: Int32
                if case .exited(let exitCode) = result {
                    kind = "exited"
                    code = exitCode
                } else if case .halted = result {
                    kind = "halted"
                    code = -1
                } else {
                    kind = "boundary"
                    code = -1
                }
                let c = cpu.engine.ctx.pointee
                let line = "SMP_FINAL cpu \(i) stopped 1 outcome \(kind) exit \(code) guard \(c.keeper_inactive_guard) "
                    + "suppressed \(c.stat_keeper_inactive_suppressed) thresholdFallbacks \(c.stat_keeper_inactive_threshold_fallbacks) sharedDirsGlobal \(shared)\n"
                let sameStore = "PTE_SAME64_FINAL cpu \(i) enabled \(c.pte_same64) exchanges \(c.stat_pte_same64_exchanges) "
                    + "equal \(c.stat_pte_same64_equal) changed \(c.stat_pte_same64_changed) "
                    + "leafSkips \(c.stat_pte_same64_leaf_skips) xmarkSkips \(c.stat_pte_same64_xmark_skips)\n"
                FileHandle.standardError.write(Data((line + sameStore + cpu.engine.stats() + "\n").utf8))
            }
        }
        return first.exitCode ?? -1
    }
}

/// One vCPU's view of the machine. Runs on that vCPU's thread, except `post`.
final class SMPCPU: EngineMachine, @unchecked Sendable {
    private unowned let guest: SMPGuest
    let engine: Engine
    private let wake = DispatchSemaphore(value: 0)
    private let stopLock = NSLock()
    private var stopRun = false

    var shouldStopRun: Bool {
        stopLock.lock()
        defer { stopLock.unlock() }
        return stopRun
    }

    init(guest: SMPGuest, engine: Engine) {
        self.guest = guest
        self.engine = engine
        engine.machine = self
    }

    /// Any thread.
    func post(_ vector: UInt8) {
        engine.postVector(vector, level: false)
        wake.signal()
    }

    /// Any thread. A boundary request is atomic; wake a peer sleeping in HLT too.
    func stopAtBoundary() {
        stopLock.lock()
        stopRun = true
        stopLock.unlock()
        engine.requestBoundary()
        wake.signal()
    }

    func portWrite(port: UInt16, size: Int, value: UInt32) {
        switch port {
        case 0xE9:
            guest.write(UInt8(truncatingIfNeeded: value))
        case 0xF4:
            engine.exitCode = Int32(bitPattern: value)
            engine.requestStop = true
        default:
            break
        }
    }
    func portRead(port: UInt16, size: Int) -> UInt32 {
        switch port {
        case 0xF5: return guest.vcpuCount
        case 0xF6: return guest.tsoOn ? 1 : 0
        default: return 0xFFFF_FFFF
        }
    }
    func waitForInterrupt(deadlineNanos: UInt64) -> Bool {
        // Signals pile up while this vCPU is awake. Anything posted before this drain shows in the host IRR, and anything after leaves a signal.
        while wake.wait(timeout: .now()) == .success {}
        let posted = withUnsafeBytes(of: &engine.ctx.pointee.apic_host_irr) { $0.bindMemory(to: UInt32.self).contains { $0 != 0 } }
        if posted { return true }
        _ = wake.wait(timeout: .now() + .nanoseconds(Int(deadlineNanos != 0 ? min(deadlineNanos, 100_000_000) : 100_000_000)))
        return true
    }
    func apicICR(_ icr: UInt64) { guest.deliver(icr: icr, from: engine.cpuIndex) }
    func apicEOI(vector: UInt8) {}
    func acknowledgeInterrupt() -> UInt8? { nil }
    func hasInterruptWaiting() -> Bool { false }
    func mmioRead(address: UInt64, size: Int) -> UInt64 { 0 }
    func mmioWrite(address: UInt64, size: Int, value: UInt64) {}
    func readMSR(_ msr: UInt32) -> UInt64? { 0 }
    func writeMSR(_ msr: UInt32, value: UInt64) -> Bool { true }
    func cpuid(leaf: UInt32, subleaf: UInt32) -> (eax: UInt32, ebx: UInt32, ecx: UInt32, edx: UInt32) {
        CPUIDModel.answer(leaf: leaf, subleaf: subleaf)
    }
    func syscall(_ state: UnsafeMutablePointer<EngineCtx>, engine: Engine) throws {
        throw EngineError.stop("no shim in the kernel model")
    }
}
