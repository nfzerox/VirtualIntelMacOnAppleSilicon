/* Local full-system TCG diagnostic: scalar SSE operands must not overread. */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static volatile sig_atomic_t offset;
static void fault(int sig, siginfo_t *info, void *context)
{
    (void)context;
    /* Diagnostic only: use async-signal-safe output in the fault handler. */
    static const char message[] = "FAIL: scalar operand crossed a guard page\n";
    (void)info;
    write(STDERR_FILENO, message, sizeof(message) - 1);
    _exit(128 + sig);
}

int main(void)
{
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    unsigned char *bytes = mmap(NULL, page * 3, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANON, -1, 0);
    if (bytes == MAP_FAILED || mprotect(bytes + page * 2, page, PROT_NONE)) {
        perror("guard mapping");
        return 1;
    }
    struct sigaction action = {.sa_sigaction = fault, .sa_flags = SA_SIGINFO};
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, NULL);
    sigaction(SIGBUS, &action, NULL);
    for (size_t i = 0; i < page * 2 / sizeof(float); ++i)
        ((float *)bytes)[i] = 1.0f;

    for (unsigned iteration = 0; iteration < 256; ++iteration) {
        for (offset = 32; offset >= 4; offset -= 4) {
            if (iteration == 0) {
                printf("Checking scalar operand %d bytes before guard\n", offset);
                fflush(stdout);
            }
            float result;
            const float *operand = (const float *)(bytes + page * 2 - offset);
            __asm__ volatile("movss %1, %%xmm2\n\t"
                             "subss %1, %%xmm2\n\t"
                             "movss %%xmm2, %0"
                             : "=m"(result) : "m"(*operand) : "xmm2");
            if (result != 0.0f) {
                fprintf(stderr, "FAIL: wrong scalar result at offset %d\n", offset);
                return 2;
            }
            if (offset >= 8) {
                double wide_result;
                const double *wide_operand = (const double *)operand;
                __asm__ volatile("movsd %1, %%xmm2\n\t"
                                 "subsd %1, %%xmm2\n\t"
                                 "movsd %%xmm2, %0"
                                 : "=m"(wide_result) : "m"(*wide_operand) : "xmm2");
                if (wide_result != 0.0) {
                    fprintf(stderr, "FAIL: wrong double result at offset %d\n", offset);
                    return 3;
                }
            }
        }
    }
    munmap(bytes, page * 3);
    puts("PASS: 32/64-bit scalar SSE loads/subtracts at guarded page endings");
    return 0;
}
