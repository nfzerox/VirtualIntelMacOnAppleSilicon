import Foundation

/// All vCPUs publish into one code cache. A translation holds the lock for a
/// few microseconds, less than it takes to wake a waiter that sleeps, so a
/// waiter spins first. It blocks after that rather than keep burning a host
/// core while the lock owner has been descheduled.
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
    @inline(__always) func lock() {
        let deadline = mach_absolute_time() &+ Self.spinTicks
        repeat {
            if tryLock() { return }
        } while mach_absolute_time() < deadline
        pthread_mutex_lock(mutex)
    }
    @inline(__always) func unlock() { pthread_mutex_unlock(mutex) }

    // 10 µs: 97% of waits for it end sooner, and waking a waiter that sleeps costs 10 to 20 µs.
    private static let spinTicks: UInt64 = {
        var timebase = mach_timebase_info_data_t()
        mach_timebase_info(&timebase)
        return 10_000 * UInt64(timebase.denom) / UInt64(timebase.numer)
    }()

    deinit {
        pthread_mutex_destroy(mutex)
        mutex.deinitialize(count: 1)
        mutex.deallocate()
    }
}
