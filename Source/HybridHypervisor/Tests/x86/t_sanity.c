// First-light program for the engine: compiled C using calls, loops, memory, a jump table, varargs and floating point.
#include "flatlibc.h"

static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

static unsigned long table_op(unsigned long v, int op) {
    switch (op) {
        case 0:
            return v + 1;
        case 1:
            return v * 3;
        case 2:
            return v ^ 0x55;
        case 3:
            return v >> 2;
        case 4:
            return v << 3;
        case 5:
            return ~v;
        default:
            return v - 7;
    }
}

static int compare(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

static void sort(int *v, int n, int (*cmp)(const void *, const void *)) {
    for (int i = 1; i < n; i++) {
        int x = v[i], j = i - 1;
        while (j >= 0 && cmp(&v[j], &x) > 0) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = x;
    }
}

int flat_main(unsigned long a0, unsigned long a1) {
    (void)a0;
    (void)a1;
    printf("hello %d %s %016lx\n", 42, "world", 0x1234567890abcdefUL);
    printf("fib(20) = %d\n", fib(20));
    unsigned long v = 1;
    for (int i = 0; i < 1000; i++)
        v = table_op(v, i % 8);
    printf("table %016lx\n", v);
    int data[64];
    unsigned s = 12345;
    for (int i = 0; i < 64; i++) {
        s = s * 1103515245 + 12345;
        data[i] = (int)(s >> 16);
    }
    sort(data, 64, compare);
    printf("sorted %d %d %d\n", data[0], data[31], data[63]);
    double x = 1.5;
    for (int i = 0; i < 20; i++)
        x = x * 1.25 - 0.125;
    printf("double %.6f %d\n", x, (int)x);
    float f = 2.5f;
    f = f * f / 3.0f;
    printf("float %.4f\n", (double)f);
    char buf[32];
    memset(buf, 'x', sizeof buf);
    buf[31] = 0;
    printf("len %lu\n", strlen(buf));
    return 7;
}
