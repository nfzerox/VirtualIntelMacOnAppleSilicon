// Stresses what several vCPUs must get right, and checks itself against what x86 and the OS promise. A failure here is
// a CPU emulation bug, or a bug in this file: run it natively or under Rosetta to tell which. For guest-bench's
// --program, which passes no arguments, so a build sets the seconds per part: -DPART_SECONDS=n. Prints a line per part
// and exits 0 only if every check held. Threads: one per CPU the guest reports, at least 2.
#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PART_SECONDS
#define PART_SECONDS 10
#endif

static int thread_count;
static volatile int running;
static uint64_t failures;

static double now(void) {
    struct timeval t;
    gettimeofday(&t, NULL);
    return (double)t.tv_sec + (double)t.tv_usec / 1e6;
}

static void fail(const char *part, const char *what, uint64_t got, uint64_t want) {
    if (__atomic_add_fetch(&failures, 1, __ATOMIC_RELAXED) > 40)
        return;
    fprintf(stderr, "FAIL %s: %s: got 0x%llx, want 0x%llx\n", part, what, (unsigned long long)got,
            (unsigned long long)want);
}

typedef void *(*part_fn)(void *);

// Runs fn on every thread until the time is up. Each gets its index.
static void run_threads(part_fn fn) {
    pthread_t threads[64];
    running = 1;
    for (intptr_t i = 0; i < thread_count; i++)
        if (pthread_create(&threads[i], NULL, fn, (void *)i)) {
            perror("pthread_create");
            exit(2);
        }
    double end = now() + PART_SECONDS;
    while (now() < end)
        usleep(20000);
    __atomic_store_n(&running, 0, __ATOMIC_SEQ_CST);
    for (int i = 0; i < thread_count; i++)
        pthread_join(threads[i], NULL);
}

static void report(const char *part, uint64_t before, uint64_t rounds) {
    printf("%s %s: %llu rounds, %llu failures\n", failures == before ? "PASS" : "FAIL", part,
           (unsigned long long)rounds, (unsigned long long)(failures - before));
    fflush(stdout);
}

// ---- Every locked form counts exactly. An unlocked add beside them shows the threads really raced. ----
static struct {
    uint64_t add, xadd, cas, bits;
    uint32_t add32;
    char pad[64];
    uint64_t unlocked;
} counters __attribute__((aligned(64)));
static __attribute__((aligned(16))) uint64_t wide[2];
static uint64_t added[64];

static void *atomics_thread(void *arg) {
    intptr_t me = (intptr_t)arg;
    uint64_t n = 0;
    while (__atomic_load_n(&running, __ATOMIC_RELAXED)) {
        for (int i = 0; i < 1000; i++, n++) {
            __asm__ volatile("lock addq $1, %0" : "+m"(counters.add));
            __asm__ volatile("lock addl $1, %0" : "+m"(counters.add32));
            uint64_t one = 1;
            __asm__ volatile("lock xaddq %0, %1" : "+r"(one), "+m"(counters.xadd));
            uint64_t seen = __atomic_load_n(&counters.cas, __ATOMIC_RELAXED);
            while (
                !__atomic_compare_exchange_n(&counters.cas, &seen, seen + 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)) {
            }
            __asm__ volatile("incq %0" : "+m"(counters.unlocked));
            // Both halves move together under cmpxchg16b.
            uint64_t lo = wide[0], hi = wide[1], nlo, nhi;
            char ok;
            do {
                nlo = lo + 1;
                nhi = hi + 1;
                __asm__ volatile("lock cmpxchg16b %1\n sete %0"
                                 : "=q"(ok), "+m"(*(__int128 *)wide), "+a"(lo), "+d"(hi)
                                 : "b"(nlo), "c"(nhi)
                                 : "cc");
            } while (!ok);
            if (lo != hi)
                fail("atomics", "cmpxchg16b halves apart", lo, hi);
            // One bit per thread, set and cleared by only this thread: nobody else's work disturbs it.
            char was;
            __asm__ volatile("lock btsq %2, %1\n setc %0" : "=q"(was), "+m"(counters.bits) : "r"((uint64_t)me) : "cc");
            if (was)
                fail("atomics", "my bit was already set", me, 0);
            __asm__ volatile("lock btrq %2, %1\n setc %0" : "=q"(was), "+m"(counters.bits) : "r"((uint64_t)me) : "cc");
            if (!was)
                fail("atomics", "my bit was already clear", me, 1);
        }
    }
    added[me] = n;
    return NULL;
}

static void part_atomics(void) {
    uint64_t before = failures, total = 0;
    memset(&counters, 0, sizeof counters);
    wide[0] = wide[1] = 0;
    run_threads(atomics_thread);
    for (int i = 0; i < thread_count; i++)
        total += added[i];
    if (counters.add != total)
        fail("atomics", "lock add", counters.add, total);
    if (counters.add32 != (uint32_t)total)
        fail("atomics", "lock add, 32 bits", counters.add32, (uint32_t)total);
    if (counters.xadd != total)
        fail("atomics", "lock xadd", counters.xadd, total);
    if (counters.cas != total)
        fail("atomics", "cmpxchg", counters.cas, total);
    if (wide[0] != total || wide[1] != total)
        fail("atomics", "cmpxchg16b", wide[0], total);
    report("atomics", before, total);
    if (thread_count > 1)
        printf("     unlocked add lost %llu of %llu, which says the threads overlapped. 0 on a loaded or one-CPU guest "
               "is no failure\n",
               (unsigned long long)(total - counters.unlocked), (unsigned long long)total);
}

// ---- x86's memory order: stores seen in order, and with mfence a Dekker lock excludes. ----
static volatile uint64_t mp_data[8], mp_flag;

static void *order_thread(void *arg) {
    intptr_t me = (intptr_t)arg;
    uint64_t round = 0;
    if (me == 0) {
        while (__atomic_load_n(&running, __ATOMIC_RELAXED)) {
            round++;
            // In order: readers that see round in the last must see at least it in the first.
            for (int i = 0; i < 8; i++)
                mp_data[i] = round;
            mp_flag = round;
        }
    } else {
        while (__atomic_load_n(&running, __ATOMIC_RELAXED)) {
            uint64_t flag = mp_flag;
            __asm__ volatile("" : : : "memory");
            for (int i = 0; i < 8; i++) {
                uint64_t data = mp_data[i];
                if (data < flag)
                    fail("memory order", "a store seen before an earlier one", data, flag);
            }
        }
    }
    return NULL;
}

static volatile uint32_t dekker_want[2], dekker_turn;
static volatile uint64_t dekker_count;
static uint64_t dekker_rounds[2];

static void *dekker_thread(void *arg) {
    intptr_t me = (intptr_t)arg;
    if (me > 1)
        return NULL;
    uint64_t n = 0;
    while (__atomic_load_n(&running, __ATOMIC_RELAXED)) {
        dekker_want[me] = 1;
        __asm__ volatile("mfence");
        while (dekker_want[1 - me]) {
            if (dekker_turn != (uint32_t)me) {
                dekker_want[me] = 0;
                while (dekker_turn != (uint32_t)me && __atomic_load_n(&running, __ATOMIC_RELAXED))
                    __asm__ volatile("pause");
                if (!__atomic_load_n(&running, __ATOMIC_RELAXED))
                    goto done;
                dekker_want[me] = 1;
                __asm__ volatile("mfence");
            }
        }
        dekker_count = dekker_count + 1; // loses counts if exclusion fails
        n++;
        dekker_turn = 1 - (uint32_t)me;
        dekker_want[me] = 0;
    }
done:
    dekker_rounds[me] = n;
    return NULL;
}

static void part_order(void) {
    uint64_t before = failures;
    run_threads(order_thread);
    report("memory order, stores", before, mp_flag);
    before = failures;
    dekker_count = 0;
    dekker_rounds[0] = dekker_rounds[1] = 0;
    run_threads(dekker_thread);
    if (dekker_count != dekker_rounds[0] + dekker_rounds[1])
        fail("memory order", "Dekker lock let two in", dekker_count, dekker_rounds[0] + dekker_rounds[1]);
    report("memory order, Dekker", before, dekker_count);
}

// ---- A page unmapped by one thread faults at once in all. A stale TLB entry would let a read through. ----
static volatile uint64_t shoot_seq; // odd: mapped or about to be. Even: unmapped, for as long as it stays that value
static volatile uint64_t *shoot_page;
static __thread sigjmp_buf *fault_jump;
static uint64_t shoot_reads;

static void on_fault(int sig) {
    if (fault_jump)
        siglongjmp(*fault_jump, 1);
    signal(sig, SIG_DFL);
    raise(sig);
}

static void *shoot_thread(void *arg) {
    intptr_t me = (intptr_t)arg;
    if (me == 0) {
        uint64_t seq = 0;
        while (__atomic_load_n(&running, __ATOMIC_RELAXED)) {
            __atomic_store_n(&shoot_seq, ++seq, __ATOMIC_SEQ_CST);
            void *p = mmap((void *)shoot_page, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
            if (p != (void *)shoot_page) {
                fail("shootdown", "mmap", (uint64_t)p, (uint64_t)shoot_page);
                break;
            }
            *shoot_page = seq;
            sched_yield();
            // Replaced, not removed: a hole would be anybody's next allocation, and a read of that would be no stale
            // TLB.
            mmap((void *)shoot_page, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
            __atomic_store_n(&shoot_seq, ++seq, __ATOMIC_SEQ_CST);
            sched_yield();
        }
    } else {
        sigjmp_buf jump;
        fault_jump = &jump;
        uint64_t reads = 0;
        while (__atomic_load_n(&running, __ATOMIC_RELAXED)) {
            uint64_t s1 = __atomic_load_n(&shoot_seq, __ATOMIC_SEQ_CST), value = 0;
            int faulted = sigsetjmp(jump, 1);
            if (!faulted)
                value = *shoot_page;
            uint64_t s2 = __atomic_load_n(&shoot_seq, __ATOMIC_SEQ_CST);
            reads++;
            if (s1 == s2 && !(s1 & 1) && !faulted)
                fail("shootdown", "a read succeeded of a page unmapped throughout", value, s1);
            if (s1 == s2 && (s1 & 1) && !faulted && value != s1 && value != 0)
                fail("shootdown", "a page held another generation's value", value, s1);
        }
        __atomic_add_fetch(&shoot_reads, reads, __ATOMIC_RELAXED);
        fault_jump = NULL;
    }
    return NULL;
}

static void part_shootdown(void) {
    uint64_t before = failures;
    shoot_page = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    shoot_seq = 0;
    signal(SIGSEGV, on_fault);
    signal(SIGBUS, on_fault);
    run_threads(shoot_thread);
    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    munmap((void *)shoot_page, 4096);
    report("shootdown", before, shoot_reads);
}

// ---- Fresh memory is zero, private to its owner, and whole across a storm of map, touch and unmap. ----
static uint64_t storm_pages;

static void *storm_thread(void *arg) {
    intptr_t me = (intptr_t)arg;
    uint64_t pages = 0, n = 0;
    while (__atomic_load_n(&running, __ATOMIC_RELAXED)) {
        size_t count = 16 + (size_t)((n++ * 2654435761u) % 1024);
        volatile uint64_t *m = mmap(NULL, count * 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (m == MAP_FAILED) {
            fail("map storm", "mmap", errno, 0);
            break;
        }
        for (size_t i = 0; i < count; i++) {
            if (m[i * 512])
                fail("map storm", "a fresh page was not zero", m[i * 512], 0);
            m[i * 512] = (uint64_t)me << 48 | i;
        }
        if (n & 1) // a downgrade under the others' feet, which must take the shadow with it
            mprotect((void *)m, count * 4096, PROT_READ);
        for (size_t i = 0; i < count; i++)
            if (m[i * 512] != ((uint64_t)me << 48 | i))
                fail("map storm", "a page held the wrong value", m[i * 512], (uint64_t)me << 48 | i);
        munmap((void *)m, count * 4096);
        pages += count;
    }
    __atomic_add_fetch(&storm_pages, pages, __ATOMIC_RELAXED);
    return NULL;
}

// ---- Fork copies, with threads about and memory changing: a child sees what was there, and its writes stay its own.
// ----
#define COW_WORDS 4096
static uint64_t cow_data[64][COW_WORDS];
static uint64_t forks_done;

static void *fork_thread(void *arg) {
    intptr_t me = (intptr_t)arg;
    uint64_t *mine = cow_data[me], n = 0;
    static char *const argv[] = {"/usr/bin/true", NULL};
    while (__atomic_load_n(&running, __ATOMIC_RELAXED)) {
        uint64_t mark = ((uint64_t)me << 48) | ++n;
        for (int i = 0; i < COW_WORDS; i++)
            mine[i] = mark + (uint64_t)i;
        int use_exec = n % 4 == 0;
        pid_t child = fork();
        if (child < 0) {
            if (errno == EAGAIN) {
                usleep(1000);
                continue;
            }
            fail("fork", "fork", errno, 0);
            break;
        }
        if (child == 0) {
            // Async-signal-safe only from here: other threads held locks at the fork.
            if (use_exec) {
                execv(argv[0], argv);
                _exit(99);
            }
            for (int i = 0; i < COW_WORDS; i++) {
                if (mine[i] != mark + (uint64_t)i)
                    _exit(1);
                mine[i] = ~(uint64_t)0;
            }
            _exit(0);
        }
        int status = 0;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            fail("fork", use_exec ? "an exec'd child's status" : "a child saw wrong memory", (uint64_t)status, 0);
        for (int i = 0; i < COW_WORDS; i++)
            if (mine[i] != mark + (uint64_t)i) {
                fail("fork", "a child's write reached its parent", mine[i], mark + (uint64_t)i);
                break;
            }
        __atomic_add_fetch(&forks_done, 1, __ATOMIC_RELAXED);
    }
    return NULL;
}

// ---- Code written by one thread runs on the others, once they serialise, as x86 says they must. ----
static volatile uint8_t *smc_code;
static volatile uint64_t smc_generation;
static uint64_t smc_calls;

// mov $value, %eax; ret. Written with a locked store on the four bytes, whose address is aligned, so a reader sees old
// or new.
static void smc_write(uint32_t value) {
    __atomic_store_n((volatile uint32_t *)(smc_code + 4), value, __ATOMIC_SEQ_CST);
}

static inline void serialise(void) {
    uint32_t a = 0, b, c = 0, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
}

static void *smc_thread(void *arg) {
    intptr_t me = (intptr_t)arg;
    uint32_t (*call)(void) = (uint32_t (*)(void))(uintptr_t)(smc_code + 3);
    if (me == 0) {
        uint32_t g = 0;
        while (__atomic_load_n(&running, __ATOMIC_RELAXED)) {
            smc_write(++g);
            __atomic_store_n(&smc_generation, g, __ATOMIC_SEQ_CST);
            if (!(g & 63))
                sched_yield();
        }
    } else {
        uint64_t calls = 0;
        while (__atomic_load_n(&running, __ATOMIC_RELAXED)) {
            uint32_t g = (uint32_t)__atomic_load_n(&smc_generation, __ATOMIC_SEQ_CST);
            serialise();
            uint32_t got = call();
            calls++;
            if (got < g)
                fail("self-modifying code", "ran code older than its store", got, g);
        }
        __atomic_add_fetch(&smc_calls, calls, __ATOMIC_RELAXED);
    }
    return NULL;
}

static void part_smc(void) {
    uint64_t before = failures;
    uint8_t *page = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (page == MAP_FAILED) {
        printf("SKIP self-modifying code: no writable, executable memory here\n");
        return;
    }
    smc_code = page;
    // 3 nops, so that the immediate at offset 4 is aligned; mov $0, %eax at 3; ret at 8.
    static const uint8_t code[] = {0x90, 0x90, 0x90, 0xB8, 0, 0, 0, 0, 0xC3};
    memcpy(page, code, sizeof code);
    smc_generation = 0;
    run_threads(smc_thread);
    report("self-modifying code", before, smc_calls);
    munmap(page, 4096);
}

// ---- Registers belong to their thread across every switch: SSE, x87 and MMX, with others using all three. ----
static uint64_t fp_rounds;

static void *fp_thread(void *arg) {
    intptr_t me = (intptr_t)arg;
    uint64_t rounds = 0;
    while (__atomic_load_n(&running, __ATOMIC_RELAXED)) {
        uint64_t seed = ((uint64_t)me + 1) * 0x9E3779B97F4A7C15ULL + rounds;
        uint64_t in[2] __attribute__((aligned(16))) = {seed, ~seed}, out[2] __attribute__((aligned(16))), mm_out = 0;
        double x = (double)((me + 1) * 3) + (double)(rounds & 255), x87 = 0;
        // x87 and MMX by turns, with emms between. An MMX instruction sets TOP to 0 and marks every register in use, so
        // one between fld and fstp leaves fstp reading another register, and one before fld overflows the stack. A CPU
        // and TCG do that. Rosetta, which keeps them apart, does not, so it can't check this rule.
        int use_x87 = rounds & 1;
        __asm__ volatile("movdqa %1, %%xmm7\n movdqa %%xmm7, %%xmm13\n" : "=m"(out) : "m"(in) : "xmm7", "xmm13");
        if (use_x87)
            __asm__ volatile("fldl %0\n fldl %0\n faddp\n" : : "m"(x) : "st");
        else
            __asm__ volatile("movq %0, %%mm3\n paddq %%mm3, %%mm3\n" : : "m"(in[0]) : "mm3");
        sched_yield(); // and every timer tick. A switch that loses any of it shows below
        for (volatile int spin = 0; spin < 200; spin++) {
        }
        __asm__ volatile("movdqa %%xmm13, %0\n" : "=m"(out));
        if (out[0] != in[0] || out[1] != in[1])
            fail("registers", "xmm across a switch", out[0], in[0]);
        if (use_x87) {
            __asm__ volatile("fstpl %0\n" : "=m"(x87));
            if (x87 != x + x)
                fail("registers", "x87 across a switch", (uint64_t)x87, (uint64_t)(x + x));
        } else {
            __asm__ volatile("movq %%mm3, %0\n emms\n" : "=m"(mm_out));
            if (mm_out != in[0] + in[0])
                fail("registers", "mm across a switch", mm_out, in[0] + in[0]);
        }
        rounds++;
    }
    __atomic_add_fetch(&fp_rounds, rounds, __ATOMIC_RELAXED);
    return NULL;
}

// ---- Wakeups arrive: a lost one here hangs, which guest-bench's timeout shows. Each hand-off wakes the next thread.
// ----
static pthread_mutex_t ring_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ring_wake = PTHREAD_COND_INITIALIZER;
static uint64_t ring_token, ring_passes;

static void *ring_thread(void *arg) {
    intptr_t me = (intptr_t)arg;
    pthread_mutex_lock(&ring_lock);
    while (__atomic_load_n(&running, __ATOMIC_RELAXED)) {
        if (ring_token % (uint64_t)thread_count == (uint64_t)me) {
            ring_token++;
            ring_passes++;
            pthread_cond_broadcast(&ring_wake);
        } else {
            struct timespec until;
            struct timeval t;
            gettimeofday(&t, NULL);
            until.tv_sec = t.tv_sec + 1;
            until.tv_nsec = t.tv_usec * 1000;
            pthread_cond_timedwait(&ring_wake, &ring_lock, &until); // so the end of the part is seen
        }
    }
    pthread_cond_broadcast(&ring_wake);
    pthread_mutex_unlock(&ring_lock);
    return NULL;
}

int main(void) {
    int cpus = 0;
    size_t size = sizeof cpus;
    sysctlbyname("hw.ncpu", &cpus, &size, NULL, 0);
    thread_count = cpus < 2 ? 2 : cpus > 64 ? 64 : cpus;
    printf("smp-stress: %d CPUs, %d threads, %d s a part\n", cpus, thread_count, PART_SECONDS);
    fflush(stdout);
    uint64_t before;
    part_atomics();
    part_order();
    part_shootdown();
    before = failures;
    run_threads(storm_thread);
    report("map storm", before, storm_pages);
    before = failures;
    run_threads(fork_thread);
    report("fork and exec", before, forks_done);
    part_smc();
    before = failures;
    run_threads(fp_thread);
    report("registers", before, fp_rounds);
    before = failures;
    run_threads(ring_thread);
    report("wakeups", before, ring_passes);
    printf("%s: %llu failures\n", failures ? "FAILED" : "PASSED", (unsigned long long)failures);
    return failures ? 1 : 0;
}
