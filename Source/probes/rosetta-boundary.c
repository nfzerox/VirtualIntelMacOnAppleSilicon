/* Local, userspace-only Rosetta boundary probe. No host or guest patching.
 * Each case runs in a child; expected instruction faults are caught, avoiding
 * crash reports. Deliberately do not attempt privileged state changes. */
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void fault(int signal_number) { _exit(128 + signal_number); }
static void run(const char *name, const unsigned char *bytes, size_t count,
                int host_pid)
{
    fflush(stdout);
    pid_t child = fork();
    if (child < 0) { perror("fork"); exit(1); }
    if (!child) {
        signal(SIGILL, fault); signal(SIGSEGV, fault); signal(SIGBUS, fault);
        void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED) _exit(2);
        memcpy(p, bytes, count);
        uint64_t value = 41;
        uint64_t result = ((uint64_t (*)(uint64_t *))p)(&value);
        printf("%s: result=%llu host-getpid=%d matches=%d\n", name,
               (unsigned long long)result, getpid(),
               host_pid ? result == (uint64_t)getpid() : result == 42);
        fflush(stdout);
        _exit(0);
    }
    int status;
    if (waitpid(child, &status, 0) != child) exit(1);
    printf("%s: exit=%d signal=%d\n", name,
           WIFEXITED(status) ? WEXITSTATUS(status) : -1,
           WIFSIGNALED(status) ? WTERMSIG(status) : 0);
}
int main(void)
{
    const unsigned char load[] = {0x48,0x8b,0x07,0x48,0x83,0xc0,1,0xc3};
    const unsigned char pid[] = {0xb8,0x14,0,0,2,0x0f,0x05,0xc3};
    const unsigned char cr3[] = {0x0f,0x20,0xd8,0xc3};
    const unsigned char cli[] = {0xfa,0xc3};
    const unsigned char hlt[] = {0xf4,0xc3};
    run("direct-pointer-load", load, sizeof(load), 0);
    run("Darwin-getpid-syscall", pid, sizeof(pid), 1);
    run("read-CR3", cr3, sizeof(cr3), 0);
    run("CLI", cli, sizeof(cli), 0);
    run("HLT", hlt, sizeof(hlt), 0);
    return 0;
}
