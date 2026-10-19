/* Cross-address-space code dirty tracking: parent rewrites a shared RWX page,
 * child executes it after each pipe handoff, without mprotect-triggered flushes.
 * A stale saved software-TLB bank must not evade physical code invalidation. */
#include <sys/mman.h>
#include <sys/wait.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
static void transfer(int fd, int writing)
{
    char byte = 1;
    ssize_t n;
    do { n = writing ? write(fd, &byte, 1) : read(fd, &byte, 1); }
    while (n < 0 && errno == EINTR);
    if (n != 1) _exit(2);
}
int main(void)
{
    int ready[2], done[2], status;
    unsigned char *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_ANON | MAP_SHARED, -1, 0);
    if (p == MAP_FAILED || pipe(ready) || pipe(done)) return 2;
    p[0] = 0xb8; p[5] = 0xc3;
    pid_t child = fork();
    if (child < 0) return 2;
    if (!child) {
        close(ready[1]); close(done[0]);
        for (uint32_t i = 0; i < 3000; i++) {
            transfer(ready[0], 0);
            if (((uint32_t (*)(void))p)() != i) _exit(1);
            transfer(done[1], 1);
        }
        _exit(0);
    }
    close(ready[0]); close(done[1]);
    for (uint32_t i = 0; i < 3000; i++) {
        memcpy(p + 1, &i, sizeof(i));
        transfer(ready[1], 1);
        transfer(done[0], 0);
    }
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status)) return 1;
    puts("PASS: 3000 shared executable-page rewrites across address spaces");
    return 0;
}
