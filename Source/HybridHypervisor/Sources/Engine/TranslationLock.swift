import Foundation

/// All vCPUs publish into one code cache. Block on contention rather than
/// burning a host core spinning while the lock owner has been descheduled.
final class TranslationLock: @unchecked Sendable {
    private let mutex: UnsafeMutablePointer<pthread_mutex_t>

    init() throws { mutex = try Self.makeMutex() }

    private static func makeMutex() throws -> UnsafeMutablePointer<pthread_mutex_t> {
        var attributes = pthread_mutexattr_t()
        var status = pthread_mutexattr_init(&attributes)
        guard status == 0 else { throw EngineError.stop("translator mutex attributes: \(status)") }
        defer { pthread_mutexattr_destroy(&attributes) }
        // First-fit avoids reserving every short critical section for a
        // sleeping waiter. A launch can make all vCPUs request translations;
        // strict FIFO hand-off then pays a scheduler round trip at each one.
        // Mutual exclusion and the cache-reset gate are unchanged.
        status = pthread_mutexattr_setpolicy_np(&attributes, PTHREAD_MUTEX_POLICY_FIRSTFIT_NP)
        guard status == 0 else { throw EngineError.stop("translator mutex policy: \(status)") }
        let result = UnsafeMutablePointer<pthread_mutex_t>.allocate(capacity: 1)
        result.initialize(to: pthread_mutex_t())
        status = pthread_mutex_init(result, &attributes)
        guard status == 0 else {
            result.deinitialize(count: 1)
            result.deallocate()
            throw EngineError.stop("translator mutex: \(status)")
        }
        return result
    }

    @inline(__always) func tryLock() -> Bool { pthread_mutex_trylock(mutex) == 0 }
    @inline(__always) func lock() { pthread_mutex_lock(mutex) }
    @inline(__always) func unlock() { pthread_mutex_unlock(mutex) }

    deinit {
        pthread_mutex_destroy(mutex)
        mutex.deinitialize(count: 1)
        mutex.deallocate()
    }
}
