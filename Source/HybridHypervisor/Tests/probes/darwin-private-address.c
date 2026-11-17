// Parked private-window isolation probe, NOT a current passing qualification.
// Its per-memory-operation guards were reverted for measured performance.
// Finite, read-only integrated Darwin check. Build for x86_64 and run inside
// the real guest after updating QEMU; never writes translator-private state.
#include <inttypes.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ucontext.h>

#if !defined(__APPLE__) || !defined(__x86_64__)
#error This probe exercises x86_64 Darwin guest exceptions.
#endif

static sigjmp_buf resume;
static volatile sig_atomic_t caught_signal, caught_trap;

static void fault(int signo, siginfo_t *info, void *context) {
    (void)info;
    caught_signal = signo;
    caught_trap = ((ucontext_t *)context)->uc_mcontext->__es.__trapno;
    siglongjmp(resume, 1);
}

int main(void) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = fault;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGBUS, &action, NULL) || sigaction(SIGSEGV, &action, NULL)) {
        perror("sigaction");
        return 2;
    }
    static const uintptr_t addresses[] = {
        0, 0xF00000000000ULL, 0xF00000200000ULL, 0xF00000400000ULL,
        0xF00000600000ULL, 0xF00007C00000ULL, 0xF00010000000ULL,
        0xF00040000000ULL, 0xF10000000000ULL
    };
    unsigned failures = 0;
    for (unsigned i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++) {
        caught_signal = caught_trap = 0;
        if (!sigsetjmp(resume, 1)) {
            uint64_t value;
            // RDI explicitly selects DS, not an implicit/default SS operand.
            __asm__ volatile("movq (%%rdi), %%rax" : "=a"(value) : "D"(addresses[i]) : "memory");
            (void)value;
        }
        int expected_trap = addresses[i] ? 13 : 14;
        int pass = (caught_signal == SIGBUS || caught_signal == SIGSEGV) && caught_trap == expected_trap;
        printf("%s address=0x%012" PRIxPTR " signal=%d trap=%d expected=%d\n",
               pass ? "PASS" : "FAIL", addresses[i], caught_signal, caught_trap, expected_trap);
        failures += !pass;
    }
    printf("done, %u failures\n", failures);
    return failures ? 1 : 0;
}
