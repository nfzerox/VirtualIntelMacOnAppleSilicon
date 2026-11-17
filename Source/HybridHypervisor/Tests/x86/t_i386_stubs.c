// Library entry points test-i386.c references from tests that do not run in 64-bit mode. They must never be reached.
extern void exit(int);
extern int puts(const char *);
#define NEVER(name)                                                                                                    \
    void name(void) {                                                                                                  \
        puts(#name " called");                                                                                         \
        exit(3);                                                                                                       \
    }
NEVER(sigaction)
NEVER(setjmp)
NEVER(longjmp)

// test_fops prints libm's answers. There is no libm here, and fsin and friends stay on TCG, so these are short series in long double: x87 arithmetic of no accuracy to
// speak of, the same bytes under both engines. fmod, sqrt and the like are the real instructions.
double fabs(double x) {
    __asm__ volatile("fldl %0\n fabs\n fstpl %0" : "+m"(x));
    return x;
}
double rint(double x) {
    __asm__ volatile("fldl %0\n frndint\n fstpl %0" : "+m"(x));
    return x;
}
double sqrt(double x) {
    __asm__ volatile("fldl %0\n fsqrt\n fstpl %0" : "+m"(x));
    return x;
}
double fmod(double a, double b) {
    __asm__ volatile("fldl %1\n fldl %0\n 1: fprem\n fnstsw %%ax\n testb $4, %%ah\n jnz 1b\n fstp %%st(1)\n fstpl %0" : "+m"(a) : "m"(b) : "ax");
    return a;
}
static long double series(long double x, int start, int sign) {
    long double term = start ? x : 1, sum = term, x2 = x * x;
    for (int i = start + 1; i < 24; i += 2) {
        term *= sign * x2 / (i * (i + 1));
        sum += term;
    }
    return sum;
}
double sin(double x) { return series(x, 1, -1); }
double cos(double x) { return series(x, 0, -1); }
double tan(double x) { return series(x, 1, -1) / series(x, 0, -1); }
double exp(double x) { return series(x, 1, 1) + series(x, 0, 1); }
double log(double x) { return (x - 1) / (x + 1) * 2; }
double atan(double x) { return x / (1 + 0.28L * x * x); }
double atan2(double a, double b) { return a / (b + 0.28L * a * a / b); }
double asin(double x) { return x + x * x * x / 6; }
double acos(double x) { return 1.5707963267948966L - x - x * x * x / 6; }
