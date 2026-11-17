// Forks and waits for about a minute, so that a tally of the run is mostly fork. For guest-bench's --program, which
// passes no arguments, so a variant is a build: -DMEGABYTES=n touches that much memory first, and -DHAD_THREAD runs and
// joins a thread first, as lc-bench has by the time it forks. CPU per fork against wall time per fork says whether a
// slow fork is working or waiting.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

static double now(void) {
    struct timeval t;
    gettimeofday(&t, NULL);
    return (double)t.tv_sec + (double)t.tv_usec / 1e6;
}

static double cpu(int who) {
    struct rusage u;
    getrusage(who, &u);
    return (double)(u.ru_utime.tv_sec + u.ru_stime.tv_sec) + (double)(u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e6;
}

#ifdef HAD_THREAD
static void *nothing(void *unused) { return unused; }
#endif

int main(void) {
#ifdef MEGABYTES
    size_t bytes = (size_t)MEGABYTES << 20;
    volatile char *memory = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    for (size_t i = 0; i < bytes; i += 4096)
        memory[i] = 1;
#endif
#ifdef HAD_THREAD
    pthread_t thread;
    pthread_create(&thread, NULL, nothing, NULL);
    pthread_join(thread, NULL);
#endif
    double start = now(), self = cpu(RUSAGE_SELF), children = cpu(RUSAGE_CHILDREN);
    unsigned long forks = 0;
    while (now() - start < 60) {
        pid_t child = fork();
        if (child < 0) {
            perror("fork");
            return 1;
        }
        if (child == 0)
            _exit(0);
        int status;
        waitpid(child, &status, 0);
        forks++;
    }
    double wall = now() - start;
    printf("%lu forks, %.1f us/fork, CPU per fork: parent %.1f us, child %.1f us\n", forks, wall * 1e6 / (double)forks,
           (cpu(RUSAGE_SELF) - self) * 1e6 / (double)forks, (cpu(RUSAGE_CHILDREN) - children) * 1e6 / (double)forks);
    return 0;
}
