/* Bounded cross-process data/TLB integrity check, independent of graphics.
 * Four children inherit the same VA and repeatedly replace its backing.
 * First exercise inherited, populated anonymous COW pages, then replacement.
 * Every word carries process/iteration/page identity; no executable mapping.
 */
#include <sys/mman.h>
#include <sys/wait.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>

int main(void)
{
    const size_t bytes = 16 * (size_t)getpagesize();
    void *base = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    if (base == MAP_FAILED) return 2;
    volatile uint64_t *original = base;
    const uint64_t parent_tag = UINT64_C(0x5041524500000000);
    for (size_t i=0; i<bytes/8; i++) original[i] = parent_tag | i;
    pid_t children[4];
    for (unsigned child = 0; child < 4; child++) {
        children[child] = fork();
        if (children[child] < 0) return 2;
        if (!children[child]) {
            alarm(45);
            /* The old PROT_NONE parent never populated a page before fork,
             * so its replacement loop did not test inherited COW mappings. */
            uint64_t child_tag = (uint64_t)(child+1)<<56;
            for (size_t i=0; i<bytes/8; i++) {
                if (original[i] != (parent_tag|i)) _exit(5);
                original[i] = child_tag | i;
            }
            sched_yield();
            if (mprotect(base, bytes, PROT_READ)) _exit(2);
            for (size_t i=0; i<bytes/8; i++)
                if (original[i] != (child_tag|i)) _exit(6);
            for (unsigned round = 0; round < 500; round++) {
                volatile uint64_t *p = mmap(base, bytes, PROT_READ|PROT_WRITE,
                    MAP_PRIVATE|MAP_ANON|MAP_FIXED, -1, 0);
                if (p == MAP_FAILED) _exit(2);
                uint64_t tag = ((uint64_t)(child+1)<<56) | ((uint64_t)round<<32);
                for (size_t i=0; i<bytes/8; i++) {
                    if (p[i]) _exit(3); /* Fresh anonymous backing is zero. */
                    p[i] = tag | i;
                }
                sched_yield();
                if (mprotect(base, bytes, PROT_READ)) _exit(2);
                for (size_t i=0; i<bytes/8; i++) {
                    uint64_t actual = p[i];
                    if (actual != (tag|i)) {
                        dprintf(2,"mismatch child=%u round=%u word=%zu got=%llx expected=%llx\n",
                            child,round,i,(unsigned long long)actual,
                            (unsigned long long)(tag|i));
                        _exit(4);
                    }
                }
            }
            _exit(0);
        }
    }
    int failed = 0;
    for (unsigned i=0; i<4; i++) {
        int status;
        if (waitpid(children[i], &status, 0) != children[i]) {
            perror("waitpid");failed=1;
        } else if (!WIFEXITED(status) || WEXITSTATUS(status)) {
            fprintf(stderr,"child=%u exit=%d signal=%d (SIGALRM is the test guard, not data-corruption evidence)\n",
                i,WIFEXITED(status)?WEXITSTATUS(status):-1,WIFSIGNALED(status)?WTERMSIG(status):0);
            failed=1;
        }
    }
    for (size_t i=0; i<bytes/8; i++) {
        if (original[i] != (parent_tag|i)) {
            fprintf(stderr,"parent COW backing changed at word=%zu\n",i);
            failed=1;break;
        }
    }
    munmap(base, bytes);
    printf("four-process inherited-COW/remap integrity: %s\n", failed ? "FAIL" : "PASS");
    return failed;
}
