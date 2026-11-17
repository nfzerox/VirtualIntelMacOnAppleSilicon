import CEngine
import Engine
import Foundation

// Runs the test kernel through the C interface a QEMU accelerator would use, with a thread forcing a stop at an x86 boundary every few
// milliseconds. Each stop reads the whole CPU state and writes it back. The kernel's self-checks then cover the state round trip at
// arbitrary instruction boundaries, including flags and XMM registers.
final class CAPIHarness {
    nonisolated(unsafe) static var current: CAPIHarness?
    var handle: OpaquePointer!
    var exitCode: UInt32 = 255
    var deviceRegisters = [UInt64: UInt64]()
    let lock = NSLock()
    nonisolated(unsafe) var tick = false
    nonisolated(unsafe) var tickGeneration = 0
    var eois: UInt32 = 0
    let wake = DispatchSemaphore(value: 0)

    func portWrite(_ port: UInt16, _ value: UInt32) {
        switch port {
        case 0xE9:
            FileHandle.standardOutput.write(Data([UInt8(truncatingIfNeeded: value)]))
        case 0xF4:
            exitCode = value
            engine_request_exit(handle)
        case 0xF0:
            lock.lock()
            tickGeneration += 1
            let generation = tickGeneration
            lock.unlock()
            guard value > 0 else { return }
            let target = handle!
            Thread.detachNewThread { [self] in
                while true {
                    Thread.sleep(forTimeInterval: Double(value) / 1e6)
                    lock.lock()
                    let stillCurrent = generation == tickGeneration
                    tick = stillCurrent
                    lock.unlock()
                    if !stillCurrent { return }
                    wake.signal()
                    engine_kick(target)
                }
            }
        case 0xF1:
            engine_post_vector(handle, UInt8(truncatingIfNeeded: value), false)
        case 0xF2:
            engine_post_vector(handle, UInt8(truncatingIfNeeded: value), true)
        default:
            break
        }
    }

    func takeTick() -> Bool {
        lock.lock()
        defer { lock.unlock() }
        let t = tick
        tick = false
        return t
    }

    func run(image: MachOImage) -> Int32 {
        CAPIHarness.current = self
        var config = EngineConfig(ram_bytes: 2 << 30, ram_host: nil, kernel_model: true, precise_flags: true, tso: true, tzcnt_lzcnt: false, ram_below_4g: 0, hybrid: false)
        var ops = EngineMachineOps(
            opaque: nil,
            mmio_read: { _, gpa, _ in CAPIHarness.current!.deviceRegisters[gpa] ?? 0 },
            mmio_write: { _, gpa, _, value in CAPIHarness.current!.deviceRegisters[gpa] = value },
            port_read: { _, port, _ in port == 0xF2 ? CAPIHarness.current!.eois : (port == 0xF3 || port == 0x510) ? 0 : 0xFFFF_FFFF },
            port_write: { _, port, _, value in CAPIHarness.current!.portWrite(port, value) },
            read_msr: { _, _, value in
                value?.pointee = 0
                return true
            },
            write_msr: { _, _, _ in true },
            cpuid: { _, leaf, subleaf, out in
                let v = CPUIDModel.answer(leaf: leaf, subleaf: subleaf)
                out?[0] = v.eax
                out?[1] = v.ebx
                out?[2] = v.ecx
                out?[3] = v.edx
            },
            acknowledge_interrupt: { _ in CAPIHarness.current!.takeTick() ? 32 : -1 },
            has_interrupt_waiting: { _ in CAPIHarness.current!.tick },
            wait_for_interrupt: { _, deadline in
                let h = CAPIHarness.current!
                _ = h.wake.wait(timeout: .now() + .nanoseconds(Int(deadline != 0 ? min(deadline, 1_000_000_000) : 1_000_000_000)))
                return true
            },
            apic_eoi: { _, _ in CAPIHarness.current!.eois += 1 },
            apic_icr: { _, _ in })
        handle = engine_create(&config, &ops)
        let ram = engine_ram(handle)!
        let root = KernelGuest.buildBootTables(image: image, ram: ram, ramBytes: config.ram_bytes)
        var state = EngineX86State()
        engine_get_state(handle, &state)
        state.rip = image.entry!
        state.cr3 = root
        state.cr0 = 0x8001_0033
        state.cr4 = 0x620
        state.efer = 0xD01
        state.rflags = 2
        state.cpl = 0
        state.sel.1 = 0x08
        state.sel.2 = 0x10
        state.gpr.7 = config.ram_bytes
        state.mxcsr = 0x1F80
        state.fcw = 0x37F
        engine_set_state(handle, &state)

        let target = handle!
        let stopper = Thread {
            while true {
                Thread.sleep(forTimeInterval: 0.002)
                engine_request_boundary(target)
            }
        }
        stopper.start()
        var stops = 0
        while true {
            let result = engine_run(handle)
            switch result {
            case ENGINE_RUN_BOUNDARY:
                stops += 1
                var s = EngineX86State()
                engine_get_state(handle, &s)
                engine_set_state(handle, &s)
            case ENGINE_RUN_EXIT:
                FileHandle.standardError.write(Data("C API: \(stops) boundary stops with full state round trips\n".utf8))
                return Int32(bitPattern: exitCode)
            case ENGINE_RUN_ERROR:
                FileHandle.standardError.write(Data("engine stop: \(String(cString: engine_error_text(handle)))\n".utf8))
                return 2
            default:
                FileHandle.standardError.write(Data("guest halted for good\n".utf8))
                return 3
            }
        }
    }
}
