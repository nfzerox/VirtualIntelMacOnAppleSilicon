import CEngine
import Foundation

/// A user-mode x86-64 program with no kernel: a flat image, page tables built here, and Darwin's syscall numbers answered by a
/// small shim on the host. This is what differential tests run on, so its behaviour must match the real thing on what they use.
public final class FlatGuest: EngineMachine {
    public let engine: Engine
    public var output = FileHandle.standardOutput
    private var nextFrame: UInt64 = 1 << 20
    private var heapVA: UInt64 = 0x50_0000_0000
    private let cr3: UInt64
    public private(set) var syscalls: UInt64 = 0
    public var instantClock = false

    public init(options: EngineOptions = EngineOptions()) throws {
        engine = try Engine(options: options)
        cr3 = 1 << 19
        engine.ctx.pointee.cr3 = cr3
        engine.machine = self
    }

    private func frame() -> UInt64 {
        defer { nextFrame += 4096 }
        guard nextFrame + 4096 <= engine.options.ramBytes else { fatalError("guest out of frames") }
        return nextFrame
    }

    private let present: UInt64 = 1
    private let writable: UInt64 = 2
    private let user: UInt64 = 4
    private let noExecute: UInt64 = 1 << 63

    public func map(va: UInt64, frame gpa: UInt64, writable w: Bool, executable x: Bool) {
        var table = cr3
        for level in stride(from: 3, to: 0, by: -1) {
            let index = (va >> UInt64(12 + 9 * level)) & 0x1FF
            let at = Int(table + index * 8)
            var entry = engine.ram.load(fromByteOffset: at, as: UInt64.self)
            if entry & 1 == 0 {
                entry = frame() | present | writable | user
                engine.ram.storeBytes(of: entry, toByteOffset: at, as: UInt64.self)
            }
            table = entry & 0x000F_FFFF_FFFF_F000
        }
        let leaf = gpa | present | user | (w ? writable : 0) | (x ? 0 : noExecute)
        engine.ram.storeBytes(of: leaf, toByteOffset: Int(table + ((va >> 12) & 0x1FF) * 8), as: UInt64.self)
    }

    public func mapRange(va: UInt64, bytes: UInt64, writable w: Bool, executable x: Bool) -> UInt64 {
        let start = va & ~0xFFF
        let end = (va + bytes + 0xFFF) & ~0xFFF
        var firstFrame: UInt64 = 0
        var a = start
        while a < end {
            let f = frame()
            if a == start { firstFrame = f }
            map(va: a, frame: f, writable: w, executable: x)
            a += 4096
        }
        return firstFrame
    }

    public func load(image: MachOImage) throws -> UInt64 {
        for segment in image.segments where segment.size > 0 && segment.name != "__LINKEDIT" {
            let executable = segment.name == "__TEXT"
            let first = mapRange(va: segment.address, bytes: segment.size, writable: !executable, executable: executable)
            // Frames from mapRange are contiguous unless a table page interleaves, so copy by page through the walk.
            _ = first
            image.bytes.withUnsafeBytes { raw in
                var done = 0
                while done < segment.fileSize {
                    let va = segment.address + UInt64(done)
                    let gpa = engine.translate(va)!
                    let chunk = min(4096 - Int(va & 0xFFF), segment.fileSize - done)
                    (engine.ram + Int(gpa)).copyMemory(from: raw.baseAddress! + segment.fileOffset + done, byteCount: chunk)
                    done += chunk
                }
            }
        }
        guard let entry = image.entry else { throw EngineError.badImage("no entry point") }
        return entry
    }

    /// Runs the image with the given arguments and returns its exit code.
    public func run(image: MachOImage, arguments: (UInt64, UInt64) = (0, 0)) throws -> Int32 {
        let entry = try load(image: image)
        let stackTop: UInt64 = 0x7000_0000_0000
        _ = mapRange(va: stackTop - (1 << 20), bytes: 1 << 20, writable: true, executable: false)
        let ctx = engine.ctx
        ctx.pointee.x.4 = stackTop - 64  // aligned as if just called: rsp+8 is 16-byte aligned
        ctx.pointee.x.4 -= 8
        ctx.pointee.x.7 = arguments.0
        ctx.pointee.x.6 = arguments.1
        ctx.pointee.cpl = 3
        ctx.pointee.seg_sel.1 = 0x2B
        ctx.pointee.seg_sel.2 = 0x23
        switch try engine.run(entry: entry) {
        case .exited(let code): return code
        case .halted(let rax): throw EngineError.stop("guest halted, rax \(rax)")
        case .boundary, .fallback: throw EngineError.stop("unexpected boundary stop")
        }
    }

    // ---- Syscall shim: Darwin numbers ----
    public func syscall(_ c: UnsafeMutablePointer<EngineCtx>, engine: Engine) throws {
        syscalls += 1
        let number = c.pointee.x.0
        let a = c.pointee.x.7
        let b = c.pointee.x.6
        let d = c.pointee.x.2
        func succeed(_ value: UInt64) {
            c.pointee.x.0 = value
            c.pointee.spsr |= 1 << 29  // CF clear
        }
        func fail(_ errno: UInt64) {
            c.pointee.x.0 = errno
            c.pointee.spsr &= ~(1 << 29)
        }
        switch number {
        case 0x200_0001:  // exit
            engine.exitCode = Int32(truncatingIfNeeded: a)
        case 0x200_0004:  // write
            guard let bytes = engine.readGuest(b, count: Int(d)) else {
                fail(14)
                return
            }
            if a == 2 {
                FileHandle.standardError.write(Data(bytes))
            } else {
                output.write(Data(bytes))
            }
            succeed(d)
        case 0x200_00C5:  // mmap: anonymous only, ignores address hints
            let length = (b + 0xFFF) & ~0xFFF
            let va = heapVA
            heapVA += length + 0x1000
            _ = mapRange(va: va, bytes: length, writable: true, executable: c.pointee.x.2 & 4 != 0)
            succeed(va)
        case 0x200_0049:  // munmap: address space is cheap here
            succeed(0)
        case 0x200_0074:  // gettimeofday
            var t = timeval()
            Foundation.gettimeofday(&t, nil)
            var bytes = [UInt8](repeating: 0, count: 12)
            withUnsafeBytes(of: t.tv_sec) { bytes.replaceSubrange(0..<8, with: $0) }
            withUnsafeBytes(of: Int32(t.tv_usec)) { bytes.replaceSubrange(8..<12, with: $0) }
            if !engine.writeGuest(a, bytes: bytes) {
                fail(14)
                return
            }
            succeed(0)
        default:
            throw EngineError.stop("syscall shim: unknown number \(String(number, radix: 16))")
        }
    }

    // ---- Nothing else exists in this machine ----
    public func mmioRead(address: UInt64, size: Int) -> UInt64 { fatalError("no devices: read \(address)") }
    public func mmioWrite(address: UInt64, size: Int, value: UInt64) { fatalError("no devices: write \(address)") }
    public func portRead(port: UInt16, size: Int) -> UInt32 { 0xFFFF_FFFF }
    public func portWrite(port: UInt16, size: Int, value: UInt32) {}
    public func readMSR(_ msr: UInt32) -> UInt64? { nil }
    public func writeMSR(_ msr: UInt32, value: UInt64) -> Bool { false }
    public func acknowledgeInterrupt() -> UInt8? { nil }
    public func waitForInterrupt(deadlineNanos: UInt64) -> Bool { false }
    public func apicEOI(vector: UInt8) {}
    public func apicICR(_ icr: UInt64) {}
    public func hasInterruptWaiting() -> Bool { false }

    public func cpuid(leaf: UInt32, subleaf: UInt32) -> (eax: UInt32, ebx: UInt32, ecx: UInt32, edx: UInt32) {
        CPUIDModel.answer(leaf: leaf, subleaf: subleaf)
    }
}

/// The CPU the engine claims to be: 64-bit, SSE2, nothing the translator cannot run.
public enum CPUIDModel {
    public static func answer(leaf: UInt32, subleaf: UInt32) -> (eax: UInt32, ebx: UInt32, ecx: UInt32, edx: UInt32) {
        switch leaf {
        case 0:
            return (1, 0x756E_6547, 0x6C65_746E, 0x4965_6E69)  // GenuineIntel
        case 1:
            // Family 6, cx8, cmov, fxsr, sse, sse2, tsc, msr, pae, pge, clflush. cx16 and popcnt in ecx.
            let edx: UInt32 =
                1 << 4 | 1 << 5 | 1 << 6 | 1 << 8 | 1 << 13 | 1 << 15 | 1 << 19 | 1 << 24 | 1 << 25 | 1 << 26
            let ecx: UInt32 = 1 | 1 << 9 | 1 << 13 | 1 << 23  // sse3, ssse3, cx16, popcnt
            return (0x0006_06A6, 0x0001_0800, ecx, edx)
        case 0x8000_0000:
            return (0x8000_0001, 0, 0, 0)
        case 0x8000_0001:
            return (0, 0, 1, 1 << 11 | 1 << 20 | 1 << 29)  // lahf; syscall, nx, long mode
        default:
            return (0, 0, 0, 0)
        }
    }
}
