/* Guest x86_64: cold indirect-target faults must reach the normal signal path. */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static void *expected;
static void fault(int signal, siginfo_t *info, void *context)
{
    (void)context;
    _exit((signal == SIGSEGV || signal == SIGBUS) &&
          info->si_addr == expected ? 0 : 91);
}

int main(void)
{
    long page = sysconf(_SC_PAGESIZE);
    void *memory = mmap(NULL, page, PROT_READ | PROT_WRITE,
                        MAP_ANON | MAP_PRIVATE, -1, 0);
    if (memory == MAP_FAILED) return 1;
    *(unsigned char *)memory = 0xc3; /* RET, but the mapping is non-executable. */
    for (int i = 0; i < 12; i++) {
        pid_t child = fork();
        if (child < 0) return 2;
        if (!child) {
            alarm(5);
            struct sigaction action = { .sa_sigaction = fault,
                                        .sa_flags = SA_SIGINFO };
            sigemptyset(&action.sa_mask);
            if (sigaction(SIGSEGV, &action, NULL) ||
                sigaction(SIGBUS, &action, NULL)) _exit(92);
            void (*volatile target)(void) = i & 1 ? memory : (void *)0x1000;
            expected = (void *)target;
            target();
            _exit(90);
        }
        int status;
        if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) {
            fprintf(stderr, "wrong indirect-fault result trial=%d\n", i);
            return 3;
        }
    }
    munmap(memory, page);
    puts("PASS: unmapped and NX indirect targets delivered guest signals");
    return 0;
}
