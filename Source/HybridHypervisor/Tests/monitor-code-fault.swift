// Standalone test VM only: deliberately replace ONE of our own translated
// instructions after a boundary stop. No Apple binary, installed guest, disk,
// host framework or production VM is patched. Verify the actual EL0 exception,
// monitor stop and Swift error report, including a valid all-zero UDF word.
import CEngine
import Engine
import Foundation

do {
    guard CommandLine.arguments.count == 2,
          let word = UInt32(CommandLine.arguments[1], radix: 16) else { exit(64) }
    var options = EngineOptions()
    options.ramBytes = 64 << 20
    let guest = try FlatGuest(options: options)
    let entry: UInt64 = 0x1000000
    _ = guest.mapRange(va: entry, bytes: 4096, writable: false, executable: true)
    // A MOV emits a real ARM word; a NOP could share its zero-length boundary
    // with the following x86 instruction's map entry.
    guard guest.engine.writeGuest(entry, bytes: [0xb8, 7, 0, 0, 0, 0xeb, 0xf9]) else { exit(2) }
    let context = guest.engine.ctx
    context.pointee.cpl = 3
    context.pointee.seg_sel.1 = 0x2b
    context.pointee.seg_sel.2 = 0x23
    guest.engine.requestBoundary()
    guard try guest.engine.run(entry: entry) == .boundary(rip: entry) else { exit(3) }
    let pc = context.pointee.elr
    let base = UnsafeMutableRawPointer(context) - Int(OFF_CTX)
    guard (pc >= PRIV_VA + OFF_CC && pc < PRIV_VA + OFF_CC + CC_SIZE) ||
          (pc >= PRIV_VA + OFF_CC2 && pc < PRIV_VA + OFF_CC2 + CC_SIZE) else { exit(4) }
    let code = base + Int(pc - PRIV_VA)
    code.storeBytes(of: word, as: UInt32.self)
    sys_icache_invalidate(code, 4)
    do {
        _ = try guest.engine.resume()
        fputs("FAIL invalid ARM word resumed without a fault\n", stderr)
        exit(5)
    } catch {
        let report = String(describing: error)
        let expected = String(format: "arm-word 0x%08x", word)
        guard context.pointee.req_a.0 == UInt64(STOP_MONITOR_FAULT),
              context.pointee.req_a.4 == pc,
              context.pointee.req_a.5 == (UInt64(1) << 32) | UInt64(word),
              report.contains(expected) else {
            fputs("FAIL incorrect monitor fault capture: \(report)\n", stderr)
            exit(6)
        }
        print("PASS actual ARM fault is terminal and captures \(expected)")
    }
} catch {
    fputs("FAIL fault fixture: \(error)\n", stderr)
    exit(1)
}
