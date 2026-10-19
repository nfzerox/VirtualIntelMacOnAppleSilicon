/* Bounded x86 self-modifying-code smoke test for indirect dispatch caches. */
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(void)
{
    size_t size = (size_t)getpagesize();
    unsigned char *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                            MAP_ANON | MAP_PRIVATE, -1, 0);
    if (p == MAP_FAILED) return 2;
    for (uint32_t n = 0; n < 3000; n++) {
        if (mprotect(p, size, PROT_READ | PROT_WRITE)) return 3;
        p[0] = 0xb8; memcpy(p + 1, &n, 4); p[5] = 0xc3;
        if (mprotect(p, size, PROT_READ | PROT_EXEC)) return 4;
        uint32_t value = ((uint32_t (*)(void))p)();
        if (value != n) {
            fprintf(stderr, "stale code: expected %u got %u\n", n, value);
            return 1;
        }
    }
    munmap(p, size);
    puts("PASS: 3000 executable-page rewrites and indirect calls");
    return 0;
}
