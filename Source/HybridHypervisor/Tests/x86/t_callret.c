// One callee called from N direct call sites: real ret versus pop + indirect jump. The experiment from research-rosetta.md.
#include "flatlibc.h"

__asm__(".text\n_leaf_ret:\n ret\n_leaf_jmp:\n pop %rcx\n jmp *%rcx\n");
#define SITES(n, leaf)                                                                                                 \
    __asm__(".text\n_sites_" #n "_" #leaf ":\n 1:\n .rept " #n "\n call _leaf_" #leaf "\n .endr\n dec %rdi\n jnz 1b\n ret\n");    \
    extern void sites_##n##_##leaf(uint64_t);
SITES(1, ret)
SITES(16, ret)
SITES(256, ret)
SITES(4096, ret)
SITES(16384, ret)
SITES(1, jmp)
SITES(16, jmp)
SITES(256, jmp)
SITES(4096, jmp)
SITES(16384, jmp)
__asm__(".text\n_rec:\n test %rdi,%rdi\n jz 1f\n dec %rdi\n call _rec\n 1:\n ret\n");
extern void rec(uint64_t);

static double now(void) {
    struct timeval t;
    gettimeofday(&t, 0);
    return (double)t.tv_sec + (double)t.tv_usec * 1e-6;
}

static void run(const char *name, void (*f)(uint64_t), uint64_t sites) {
    uint64_t rounds = 20000000 / sites + 1;
    f(rounds / 10 + 1);
    double best = 1e30;
    for (int r = 0; r < 3; r++) {
        double t = now();
        f(rounds);
        t = now() - t;
        if (t < best)
            best = t;
    }
    printf("%-6s %6llu sites %.2f ns\n", name, (unsigned long long)sites, best * 1e9 / (double)(rounds * sites));
}

int flat_main(unsigned long a0, unsigned long a1) {
    (void)a0, (void)a1;
    run("ret", sites_1_ret, 1);
    run("ret", sites_16_ret, 16);
    run("ret", sites_256_ret, 256);
    run("ret", sites_4096_ret, 4096);
    run("ret", sites_16384_ret, 16384);
    run("jmp*", sites_1_jmp, 1);
    run("jmp*", sites_16_jmp, 16);
    run("jmp*", sites_256_jmp, 256);
    run("jmp*", sites_4096_jmp, 4096);
    run("jmp*", sites_16384_jmp, 16384);
    static const uint64_t depths[] = {8, 32, 48, 56, 64, 256, 1000, 4000, 100000};
    for (unsigned i = 0; i < sizeof depths / sizeof depths[0]; i++) {
        uint64_t d = depths[i], n = 8000000 / d + 1;
        double best = 1e30;
        for (int r = 0; r < 3; r++) {
            double t = now();
            for (uint64_t k = 0; k < n; k++)
                rec(d);
            t = now() - t;
            if (t < best)
                best = t;
        }
        printf("depth %6llu %.2f ns per call+ret\n", (unsigned long long)d, best * 1e9 / (double)(n * d));
    }
    return 0;
}
