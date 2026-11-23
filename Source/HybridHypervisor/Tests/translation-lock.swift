// Native host-only synchronization fixture; no guest, VM or target injection.
import Foundation

enum EngineError: Error { case stop(String) }
private final class Counter: @unchecked Sendable {
    let lock: TranslationLock
    var value = 0
    var waits: UInt64 = 0
    var waitTicks: UInt64 = 0
    init() throws { lock = try TranslationLock() }
}

private func ticks(microseconds: UInt64) -> UInt64 {
    var timebase = mach_timebase_info_data_t()
    mach_timebase_info(&timebase)
    return microseconds * 1000 * UInt64(timebase.denom) / UInt64(timebase.numer)
}

private func busy(for duration: UInt64) {
    let end = mach_absolute_time() &+ duration
    while mach_absolute_time() < end {}
}

private func meanContendedWaitMicroseconds() throws -> Double {
    // Given
    let counter = try Counter()
    let hold = ticks(microseconds: 2)
    let between = ticks(microseconds: 40)
    let group = DispatchGroup()

    // When
    for _ in 0..<4 {
        group.enter()
        DispatchQueue.global().async {
            for _ in 0..<10000 {
                busy(for: UInt64.random(in: 0...between))
                if !counter.lock.tryLock() {
                    let start = mach_absolute_time()
                    counter.lock.lock()
                    counter.waits += 1
                    counter.waitTicks += mach_absolute_time() - start
                }
                busy(for: hold)
                counter.lock.unlock()
            }
            group.leave()
        }
    }
    precondition(group.wait(timeout: .now() + 20) == .success)

    // Then
    precondition(counter.waits > 0)
    return Double(counter.waitTicks) / Double(counter.waits) / Double(ticks(microseconds: 1))
}

@main struct TranslationLockTest {
    static func main() throws {
        let counter = try Counter()
        precondition(counter.lock.tryLock())
        precondition(!counter.lock.tryLock())
        counter.lock.unlock()
        let group = DispatchGroup()
        for _ in 0..<8 {
            group.enter()
            DispatchQueue.global().async {
                for _ in 0..<5000 {
                    counter.lock.lock()
                    counter.value += 1
                    counter.lock.unlock()
                }
                group.leave()
            }
        }
        precondition(group.wait(timeout: .now() + 20) == .success)
        precondition(counter.value == 40000)
        let wait = try meanContendedWaitMicroseconds()
        guard wait < 4 else {
            print(String(format: "FAIL translator mutex: %.1f µs per contended wait for 2 µs holds", wait))
            exit(1)
        }
        print(String(format: "PASS translator mutex: try-lock, 8 workers, 40000 protected updates, lifecycle, %.1f µs per contended wait for 2 µs holds", wait))
    }
}
