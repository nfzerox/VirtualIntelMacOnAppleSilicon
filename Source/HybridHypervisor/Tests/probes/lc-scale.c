// How throughput scales with threads, which single-threaded lc-bench can't show. Each test runs for a fixed time at 1, 2
// 4 and 8 threads (pairs of threads for pipe_pingpong), and prints total throughput and its ratio to one. Seconds per run:
// argv[1], LC_SCALE_SECONDS or 2. Each thread's memory fits in cache, except chase's shared 64 MB. Run it under Rosetta
// for a comparison.
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

// guest-bench's --program passes no arguments, so a build can pick one test, for a tally of just it: -DSCALE_ONLY='"exec_per_s"' -DSCALE_SECONDS=10.
#ifndef SCALE_SECONDS
#define SCALE_SECONDS 2
#endif
#define MAX_THREADS 16 // both ends of eight pairs
#define CHASE_NODES (1u << 20) // of 64 bytes each

typedef struct {
    int index;
    uint64_t ops;
    char pad[64];
} Worker;

static volatile int stop;
static uint64_t (*chase_nodes)[8];

static void die(const char *what) {
    perror(what);
    exit(1);
}

static uint32_t xorshift(uint32_t *s) {
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

// Setup, however slow, stays out of the timed part. macOS has no pthread_barrier_t.
static pthread_mutex_t go_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t go_cond = PTHREAD_COND_INITIALIZER;
static int waiting, go_now;

static void go(void) {
    pthread_mutex_lock(&go_lock);
    waiting++;
    pthread_cond_broadcast(&go_cond);
    while (!go_now)
        pthread_cond_wait(&go_cond, &go_lock);
    pthread_mutex_unlock(&go_lock);
}

// ---- Each thread's own memory ----
static void *alu(void *arg) {
    Worker *w = arg;
    enum { WORDS = 4096 };
    uint64_t *a = calloc(WORDS, 8), x = (uint64_t)w->index + 1;
    go();
    while (!stop) {
        for (int i = 0; i < WORDS; i++) {
            x = (x ^ a[i]) + (x << 3) + 0x9E3779B97F4A7C15ULL;
            a[i] = x >> 7;
        }
        w->ops += WORDS;
    }
    free(a);
    return NULL;
}

static void *copy(void *arg) {
    Worker *w = arg;
    enum { BYTES = 256 << 10 };
    char *a = malloc(BYTES), *b = malloc(BYTES);
    memset(a, 1, BYTES);
    go();
    while (!stop) {
        memcpy(b, a, BYTES);
        __asm__ volatile("" : : "r"(a), "r"(b) : "memory"); // or the compiler drops the copies, as nothing reads them
        memcpy(a, b, BYTES);
        __asm__ volatile("" : : "r"(a), "r"(b) : "memory");
        w->ops += 2 * BYTES;
    }
    free(a);
    free(b);
    return NULL;
}

// ---- Shared, read only ----
static void *chase(void *arg) {
    Worker *w = arg;
    uint64_t *p = chase_nodes[(uint32_t)w->index * (CHASE_NODES / MAX_THREADS)];
    go();
    while (!stop) {
        for (int i = 0; i < 4096; i++)
            p = (uint64_t *)*p;
        __asm__ volatile("" : : "r"(p));
        w->ops += 4096;
    }
    return NULL;
}

static void chase_build(void) {
    chase_nodes = mmap(NULL, CHASE_NODES * 64ULL, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if ((void *)chase_nodes == MAP_FAILED)
        die("mmap");
    uint32_t *order = malloc(CHASE_NODES * sizeof *order), s = 2463534242u;
    for (uint32_t i = 0; i < CHASE_NODES; i++)
        order[i] = i;
    for (uint32_t i = CHASE_NODES - 1; i; i--) { // Sattolo: one cycle through every node
        uint32_t j = xorshift(&s) % i, t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    for (uint32_t i = 0; i < CHASE_NODES; i++)
        chase_nodes[order[i]][0] = (uint64_t)chase_nodes[order[(i + 1) % CHASE_NODES]];
    free(order);
}

// ---- The C library's allocator, then the kernel's VM ----
static void *churn(void *arg) {
    Worker *w = arg;
    enum { SLOTS = 256 };
    void *slot[SLOTS] = {0};
    uint32_t s = 88172645u + (uint32_t)w->index;
    go();
    while (!stop) {
        for (int i = 0; i < 1024; i++) {
            uint32_t r = xorshift(&s), at = r % SLOTS;
            free(slot[at]);
            slot[at] = malloc(16 + (r >> 20)); // up to about 4 KB
            if (!slot[at])
                die("malloc");
            *(volatile char *)slot[at] = 1;
        }
        w->ops += 1024;
    }
    for (int i = 0; i < SLOTS; i++)
        free(slot[i]);
    return NULL;
}

static void *touch(void *arg) {
    Worker *w = arg;
    enum { PAGES = 64 };
    size_t page = (size_t)getpagesize();
    go();
    while (!stop) {
        char *m = mmap(NULL, PAGES * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (m == MAP_FAILED)
            die("mmap");
        for (int i = 0; i < PAGES; i++)
            m[i * page] = 1;
        munmap(m, PAGES * page);
        w->ops += PAGES;
    }
    return NULL;
}

// ---- Threads waking each other, across vCPUs when the guest places them so ----
static int pipes[MAX_THREADS][2]; // pipes[i] is what thread i reads

static void *ping(void *arg) {
    Worker *w = arg;
    int me = w->index, peer = me ^ 1;
    char byte = 1;
    go();
    if (!(me & 1)) {
        while (!stop) {
            if (write(pipes[peer][1], &byte, 1) != 1 || read(pipes[me][0], &byte, 1) != 1)
                die("pipe");
            w->ops++;
        }
        close(pipes[peer][1]); // its partner reads the end of the file and leaves
    } else {
        while (read(pipes[me][0], &byte, 1) == 1)
            if (write(pipes[peer][1], &byte, 1) != 1)
                die("pipe");
    }
    return NULL;
}

// ---- Processes ----
static void *exec_true(void *arg) {
    Worker *w = arg;
    go();
    while (!stop) {
        pid_t pid = fork();
        if (pid < 0) {
            if (errno == EAGAIN)
                continue;
            die("fork");
        }
        if (!pid) {
            char *const args[] = {"/usr/bin/true", NULL};
            execv(args[0], args);
            _exit(127);
        }
        int status;
        while (waitpid(pid, &status, 0) < 0)
            if (errno != EINTR)
                die("waitpid");
        if (!WIFEXITED(status) || WEXITSTATUS(status)) {
            fprintf(stderr, "/usr/bin/true: status 0x%x\n", status);
            exit(1);
        }
        w->ops++;
    }
    return NULL;
}

// ---- Running them ----
typedef struct {
    const char *name, *unit;
    double scale;
    void *(*fn)(void *);
    int pairs;
} Test;

static const Test tests[] = {
    {"alu", "Mwords/s", 1e-6, alu, 0},
    {"chase", "Mloads/s", 1e-6, chase, 0},
    {"memcpy", "MB/s", 1e-6, copy, 0},
    {"malloc_free", "Kops/s", 1e-3, churn, 0},
    {"mmap_touch", "Kpages/s", 1e-3, touch, 0},
    {"pipe_pingpong", "Kroundtrips/s", 1e-3, ping, 1},
    {"exec_per_s", "exec/s", 1, exec_true, 0},
};

static double now(void) {
    struct timeval t;
    gettimeofday(&t, NULL);
    return (double)t.tv_sec + (double)t.tv_usec * 1e-6;
}

static double run(const Test *test, int n, double seconds) {
    int threads = test->pairs ? n * 2 : n;
    static Worker workers[MAX_THREADS];
    pthread_t id[MAX_THREADS];
    stop = 0;
    waiting = 0;
    go_now = 0;
    for (int i = 0; test->pairs && i < threads; i++)
        if (pipe(pipes[i]))
            die("pipe");
    for (int i = 0; i < threads; i++) {
        workers[i] = (Worker){.index = i};
        if (pthread_create(&id[i], NULL, test->fn, &workers[i]))
            die("pthread_create");
    }
    pthread_mutex_lock(&go_lock);
    while (waiting < threads)
        pthread_cond_wait(&go_cond, &go_lock);
    go_now = 1;
    pthread_cond_broadcast(&go_cond);
    pthread_mutex_unlock(&go_lock);
    double t0 = now();
    usleep((useconds_t)(seconds * 1e6));
    stop = 1;
    uint64_t ops = 0;
    for (int i = 0; i < threads; i++) {
        pthread_join(id[i], NULL);
        ops += workers[i].ops;
    }
    double elapsed = now() - t0;
    for (int i = 0; test->pairs && i < threads; i++) {
        close(pipes[i][0]);
        if (!(i & 1)) // the even threads closed the odd ones' write ends
            close(pipes[i][1]);
    }
    return (double)ops / elapsed * test->scale;
}

int main(int argc, char **argv) {
    const char *given = argc > 1 ? argv[1] : getenv("LC_SCALE_SECONDS");
    double seconds = given ? atof(given) : SCALE_SECONDS;
    if (seconds <= 0)
        seconds = SCALE_SECONDS;
    int cpus = 0;
    size_t size = sizeof cpus;
    sysctlbyname("hw.ncpu", &cpus, &size, NULL, 0);
    printf("# lc-scale cpus=%d seconds=%.1f\n# test N throughput unit ratio-to-N=1\n", cpus, seconds);
    chase_build();
    static const int counts[] = {1, 2, 4, 8};
    for (size_t t = 0; t < sizeof tests / sizeof *tests; t++) {
#ifdef SCALE_ONLY
        if (strcmp(tests[t].name, SCALE_ONLY))
            continue;
#endif
        double one = 0;
        const char *only_n = getenv("LC_SCALE_ONLY_N");
        for (int c = 0; c < 4; c++) {
            if (only_n && atoi(only_n) != counts[c])
                continue;
            double rate = run(&tests[t], counts[c], seconds);
            if (!c)
                one = rate;
            printf("%s %d %.1f %s %.2f\n", tests[t].name, counts[c], rate, tests[t].unit, one > 0 ? rate / one : 0);
            fflush(stdout);
        }
    }
    return 0;
}
