import CEngine
import Engine
import Foundation

// engine-run <image> [arg0 [arg1]]: runs a flat x86-64 image under the engine's user-mode shim.
// ENGINE_FAST_FLAGS=1 drops flag precision at faulting instructions. ENGINE_STATS=1 prints counters to stderr.

alarm(UInt32(ProcessInfo.processInfo.environment["ENGINE_ALARM"] ?? "") ?? 29)
pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0)
setvbuf(stdout, nil, _IOLBF, 0)

let arguments = CommandLine.arguments
guard arguments.count >= 2 else {
    FileHandle.standardError.write(Data("usage: engine-run image [a0 a1]\n".utf8))
    exit(64)
}
func number(_ s: String) -> UInt64 {
    s.hasPrefix("0x") ? UInt64(s.dropFirst(2), radix: 16) ?? 0 : UInt64(s) ?? 0
}

do {
    if arguments[1] == "--kernel-capi" {
        let image = try MachOImage(bytes: [UInt8](Data(contentsOf: URL(fileURLWithPath: arguments[2]))))
        exit(CAPIHarness().run(image: image))
    }
    if arguments[1] == "--hybrid" {
        let image = try MachOImage(bytes: [UInt8](Data(contentsOf: URL(fileURLWithPath: arguments[2]))))
        exit(HybridHarness().run(image: image))
    }
    if arguments[1] == "--smp" {  // engine-run --smp <vCPUs> <image>
        let image = try MachOImage(bytes: [UInt8](Data(contentsOf: URL(fileURLWithPath: arguments[3]))))
        var smpOptions = EngineOptions()
        smpOptions.useTSO = ProcessInfo.processInfo.environment["ENGINE_NO_TSO"] == nil
        smpOptions.codeCacheBytes = UInt64(ProcessInfo.processInfo.environment["ENGINE_CC_BYTES"] ?? "") ?? 0
        exit(try SMPGuest(options: smpOptions, count: UInt32(arguments[2]) ?? 2).run(image: image))
    }
    if arguments[1] == "--kernel" {
        let image = try MachOImage(bytes: [UInt8](Data(contentsOf: URL(fileURLWithPath: arguments[2]))))
        var kernelOptions = EngineOptions()
        kernelOptions.useTSO = ProcessInfo.processInfo.environment["ENGINE_NO_TSO"] == nil
        kernelOptions.codeCacheBytes = UInt64(ProcessInfo.processInfo.environment["ENGINE_CC_BYTES"] ?? "") ?? 0
        kernelOptions.shadowPoolPages = UInt32(ProcessInfo.processInfo.environment["ENGINE_POOL_PAGES"] ?? "") ?? 0
        kernelOptions.tzcntLzcnt = ProcessInfo.processInfo.environment["ENGINE_TZCNT"] != nil  // as -cpu max under TCG, for the compat kernel's comparison
        let guest = try KernelGuest(options: kernelOptions)
        let code = try guest.run(image: image)
        if ProcessInfo.processInfo.environment["ENGINE_STATS"] != nil {
            FileHandle.standardError.write(Data((guest.engine.stats() + "\n").utf8))
        }
        exit(code)
    }
    let image = try MachOImage(bytes: [UInt8](Data(contentsOf: URL(fileURLWithPath: arguments[1]))))
    var options = EngineOptions()
    options.preciseFlags = ProcessInfo.processInfo.environment["ENGINE_FAST_FLAGS"] == nil
    options.tzcntLzcnt = true  // as Rosetta advertises: flat tests compare against it
    options.codeCacheBytes = UInt64(ProcessInfo.processInfo.environment["ENGINE_CC_BYTES"] ?? "") ?? 0
    options.shadowPoolPages = UInt32(ProcessInfo.processInfo.environment["ENGINE_POOL_PAGES"] ?? "") ?? 0
    let guest = try FlatGuest(options: options)
    let code = try guest.run(
        image: image,
        arguments: (arguments.count > 2 ? number(arguments[2]) : 0, arguments.count > 3 ? number(arguments[3]) : 0))
    if ProcessInfo.processInfo.environment["ENGINE_STATS"] != nil {
        FileHandle.standardError.write(Data((guest.engine.stats() + " syscalls \(guest.syscalls)\n").utf8))
    }
    exit(code)
} catch {
    FileHandle.standardError.write(Data("engine stop: \(error)\n".utf8))
    exit(2)
}
