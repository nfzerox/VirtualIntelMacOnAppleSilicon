import CEngine
import Foundation

/// A freestanding x86-64 kernel with the engine's full kernel model: the guest owns its page tables, IDT and rings. The machine gives it
/// only a debug console (port 0xE9), an exit port (0xF4) and a periodic tick (port 0xF0, microseconds).
public final class KernelGuest: EngineMachine {
    public let engine: Engine
    private var exitRequested: Int32?
    private let lock = NSLock()
    private nonisolated(unsafe) var tickPeriod: UInt32 = 0
    private nonisolated(unsafe) var tickGeneration = 0
    private nonisolated(unsafe) var pendingTick = false
    private let wake = DispatchSemaphore(value: 0)
    public var tickVector: UInt8 = 32

    public init(options: EngineOptions = EngineOptions()) throws {
        var o = options
        o.kernelModel = true
        engine = try Engine(options: o)
        engine.machine = self
    }

    public func run(image: MachOImage, entry: UInt64? = nil) throws -> Int32 {
        let ctx = engine.ctx
        let root = KernelGuest.buildBootTables(image: image, ram: engine.ram, ramBytes: engine.options.ramBytes)
        ctx.pointee.cr3 = root
        ctx.pointee.cpl = 0
        ctx.pointee.seg_sel.1 = 0x08
        ctx.pointee.seg_sel.2 = 0x10
        ctx.pointee.rflags_sys = 2  // interrupts off
        ctx.pointee.x.4 = 0  // the kernel loads its stack first
        ctx.pointee.x.7 = engine.options.ramBytes
        guard let rip = entry ?? image.entry else { throw EngineError.badImage("no entry") }
        _ = try engine.run(entry: rip)
        return exitRequested ?? -1
    }

    /// Identity physmap with 1 GB pages at 0xFFFF880000000000 and the kernel image mapped at its link address. Returns CR3.
    public static func buildBootTables(image: MachOImage, ram: UnsafeMutableRawPointer, ramBytes: UInt64) -> UInt64 {
        var next: UInt64 = 1 << 20
        func frame() -> UInt64 {
            defer { next += 4096 }
            return next
        }
        let root = frame()
        func put(_ table: UInt64, _ index: UInt64, _ value: UInt64) {
            ram.storeBytes(of: value, toByteOffset: Int(table + index * 8), as: UInt64.self)
        }
        func get(_ table: UInt64, _ index: UInt64) -> UInt64 {
            ram.load(fromByteOffset: Int(table + index * 8), as: UInt64.self)
        }
        let rw: UInt64 = 3
        let physmapL3 = frame()
        put(root, (0xFFFF_8800_0000_0000 >> 39) & 0x1FF, physmapL3 | rw)
        var gb: UInt64 = 0
        while gb < ramBytes {
            put(physmapL3, gb >> 30, gb | rw | 0x80)
            gb += 1 << 30
        }
        func walk(_ va: UInt64) -> UInt64 {
            var table = root
            for level in stride(from: 3, to: 0, by: -1) {
                let index = (va >> UInt64(12 + 9 * level)) & 0x1FF
                var entry = get(table, index)
                if entry & 1 == 0 {
                    entry = frame() | rw
                    put(table, index, entry)
                }
                table = entry & 0x000F_FFFF_FFFF_F000
            }
            return table
        }
        // Image frames from 2 MB up. Mapped writable and executable: the kernel keeps one space.
        var imageFrame: UInt64 = 2 << 20
        for segment in image.segments where segment.size > 0 && segment.name != "__LINKEDIT" {
            var offset: UInt64 = 0
            while offset < segment.size {
                let va = segment.address + offset
                let table = walk(va)
                put(table, (va >> 12) & 0x1FF, imageFrame | rw)
                if Int(offset) < segment.fileSize {
                    let n = min(4096, segment.fileSize - Int(offset))
                    image.bytes.withUnsafeBytes {
                        (ram + Int(imageFrame)).copyMemory(from: $0.baseAddress! + segment.fileOffset + Int(offset), byteCount: n)
                    }
                }
                imageFrame += 4096
                offset += 4096
            }
        }
        return root
    }

    // ---- Ports ----
    public func portWrite(port: UInt16, size: Int, value: UInt32) {
        switch port {
        case 0xE9:
            FileHandle.standardOutput.write(Data([UInt8(truncatingIfNeeded: value)]))
        case 0xF4:
            exitRequested = Int32(bitPattern: value)
            engine.exitCode = Int32(bitPattern: value)
            engine.requestStop = true
        case 0xF0:
            setTick(period: value)
        case 0xF1: // a device raises an edge-triggered vector through the IO-APIC or MSI path
            engine.postVector(UInt8(truncatingIfNeeded: value), level: false)
        case 0xF2: // same, level-triggered: the EOI must reach the host
            engine.postVector(UInt8(truncatingIfNeeded: value), level: true)
        default:
            break
        }
    }
    public func portRead(port: UInt16, size: Int) -> UInt32 {
        port == 0xF2 ? UInt32(eoiCount) : (port == 0xF3 || port == 0x510) ? UInt32(truncatingIfNeeded: tr_stats().blocks) : 0xFFFF_FFFF
    }

    private func setTick(period: UInt32) {
        lock.lock()
        tickGeneration += 1
        tickPeriod = period
        let generation = tickGeneration
        lock.unlock()
        guard period > 0 else { return }
        let target = engine
        Thread.detachNewThread { [self] in
            while true {
                Thread.sleep(forTimeInterval: Double(period) / 1e6)
                lock.lock()
                let stillCurrent = generation == tickGeneration
                pendingTick = stillCurrent
                lock.unlock()
                if !stillCurrent { return }
                wake.signal()
                target.interruptRequested()
            }
        }
    }

    public func hasInterruptWaiting() -> Bool {
        lock.lock()
        defer { lock.unlock() }
        return pendingTick
    }

    public func acknowledgeInterrupt() -> UInt8? {
        lock.lock()
        defer { lock.unlock() }
        if pendingTick {
            pendingTick = false
            return tickVector
        }
        return nil
    }

    public func waitForInterrupt(deadlineNanos: UInt64) -> Bool {
        if deadlineNanos != 0 {
            _ = wake.wait(timeout: .now() + .nanoseconds(Int(min(deadlineNanos, 1_000_000_000))))
            return true
        }
        if tickPeriod == 0 && !hasInterruptWaiting() { return false }
        _ = wake.wait(timeout: .now() + 1)
        return true
    }

    public func apicEOI(vector: UInt8) { eoiCount += 1 }
    public func apicICR(_ icr: UInt64) { icrLog.append(icr) }

    // A scratch device: one register bank, reads return what was written, so the guest can check host-side completion.
    private var deviceRegisters = [UInt64: UInt64]()
    public private(set) var deviceAccesses = 0
    public func mmioRead(address: UInt64, size: Int) -> UInt64 {
        deviceAccesses += 1
        return deviceRegisters[address] ?? 0
    }
    public func mmioWrite(address: UInt64, size: Int, value: UInt64) {
        deviceAccesses += 1
        deviceRegisters[address] = value
    }
    public private(set) var eoiCount = 0
    public private(set) var icrLog: [UInt64] = []
    public func readMSR(_ msr: UInt32) -> UInt64? { 0 }
    public func writeMSR(_ msr: UInt32, value: UInt64) -> Bool { true }
    public func cpuid(leaf: UInt32, subleaf: UInt32) -> (eax: UInt32, ebx: UInt32, ecx: UInt32, edx: UInt32) {
        CPUIDModel.answer(leaf: leaf, subleaf: subleaf)
    }
    public func syscall(_ state: UnsafeMutablePointer<EngineCtx>, engine: Engine) throws {
        throw EngineError.stop("no shim in the kernel model")
    }
}
