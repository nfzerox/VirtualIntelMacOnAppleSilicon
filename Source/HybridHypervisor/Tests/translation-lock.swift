// Native host-only synchronization fixture; no guest, VM or target injection.
import Foundation

enum EngineError: Error { case stop(String) }
private final class Counter: @unchecked Sendable {
    let lock: TranslationLock
    var value = 0
    init() throws { lock = try TranslationLock() }
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
        print("PASS translator mutex: try-lock, 8 workers, 40000 protected updates, lifecycle")
    }
}
