// A tiny libc for flat x86-64 images. The same bytes run under the engine (syscall shim) and under Rosetta (real Darwin syscalls
// through flatrun), so transcripts can be compared byte for byte. Nothing here may depend on addresses or the environment.
#include "flatlibc.h"

#ifdef __i386__ // the compat kernel's: number in eax, three arguments, no second result
long raw_syscall(long n, long a, long b, long c, long d, long e, long f, long *second) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory", "cc");
    return ret;
}
#else
long raw_syscall(long n, long a, long b, long c, long d, long e, long f, long *second) {
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    long ret, rdx_out;
    unsigned char carry;
    __asm__ volatile("syscall\n setc %2"
                     : "=a"(ret), "=d"(rdx_out), "=q"(carry)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory", "cc");
    if (second)
        *second = rdx_out;
    return carry ? -ret : ret;
}
#endif

static char outbuf[65536];
static unsigned long outlen;

void flush_out(void) {
    if (outlen)
        raw_syscall(0x2000004, 1, (long)outbuf, (long)outlen, 0, 0, 0, 0);
    outlen = 0;
}

long write(int fd, const void *p, unsigned long n) {
    if (fd != 1)
        return raw_syscall(0x2000004, fd, (long)p, (long)n, 0, 0, 0, 0);
    const char *s = p;
    for (unsigned long i = 0; i < n; i++) {
        if (outlen == sizeof outbuf)
            flush_out();
        outbuf[outlen++] = s[i];
    }
    return (long)n;
}

void exit(int code) {
    flush_out();
    raw_syscall(0x2000001, code, 0, 0, 0, 0, 0, 0);
    for (;;)
        __asm__ volatile("hlt");
}
void _exit(int code) { exit(code); }

void *mmap(void *addr, unsigned long len, int prot, int flags, int fd, long off) {
    long r = raw_syscall(0x20000C5, (long)addr, (long)len, prot, flags, fd, off, 0);
    return (void *)r;
}
int munmap(void *addr, unsigned long len) { return (int)raw_syscall(0x2000049, (long)addr, (long)len, 0, 0, 0, 0, 0); }

int gettimeofday(struct timeval *t, void *z) {
    (void)z;
    // Darwin's raw syscall takes the mach_absolute_time out-pointer as its third argument and writes t.
    long second = 0;
    unsigned long long mach = 0;
    long r = raw_syscall(0x2000074, (long)t, 0, (long)&mach, 0, 0, 0, &second);
    return r < 0 ? -1 : 0;
}

void *memcpy(void *d, const void *s, unsigned long n) {
    unsigned char *dd = d;
    const unsigned char *ss = s;
    // A plain SSE2 loop, for a comparable memcpy in the same image on every engine.
    while (n >= 16) {
        __asm__ volatile("movdqu (%1), %%xmm0\n movdqu %%xmm0, (%0)" : : "r"(dd), "r"(ss) : "xmm0", "memory");
        dd += 16;
        ss += 16;
        n -= 16;
    }
    while (n--)
        *dd++ = *ss++;
    return d;
}
void *memmove(void *d, const void *s, unsigned long n) {
    unsigned char *dd = d;
    const unsigned char *ss = s;
    if (dd < ss)
        return memcpy(d, s, n);
    while (n--)
        dd[n] = ss[n];
    return d;
}
void *memset(void *d, int v, unsigned long n) {
    unsigned char *dd = d;
    while (n--)
        *dd++ = (unsigned char)v;
    return d;
}
void __bzero(void *d, unsigned long n) { memset(d, 0, n); }
int memcmp(const void *a, const void *b, unsigned long n) {
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y)
            return *x - *y;
    return 0;
}
unsigned long strlen(const char *s) {
    unsigned long n = 0;
    while (s[n])
        n++;
    return n;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}
char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++))
        ;
    return r;
}

// ---- printf: the formats the tests use ----
static void put(char c) { write(1, &c, 1); }
static void pad(const char *s, int len, int width, bool left, char fill) {
    if (!left)
        for (int i = len; i < width; i++)
            put(fill);
    for (int i = 0; i < len; i++)
        put(s[i]);
    if (left)
        for (int i = len; i < width; i++)
            put(' ');
}

static void fmt_double(double v, int prec, char *out) {
    // Fixed point with exact integer arithmetic up to 2^63; good enough for benchmark figures and test values.
    char *p = out;
    if (v != v) {
        strcpy(p, "nan");
        return;
    }
    if (v < 0) {
        *p++ = '-';
        v = -v;
    }
    if (v > 9e18) {
        strcpy(p, "inf");
        return;
    }
    double rounding = 0.5;
    for (int i = 0; i < prec; i++)
        rounding /= 10;
    v += rounding;
    unsigned long long ip = (unsigned long long)v;
    double frac = v - (double)ip;
    char tmp[32];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + ip % 10);
        ip /= 10;
    } while (ip);
    while (n)
        *p++ = tmp[--n];
    if (prec) {
        *p++ = '.';
        for (int i = 0; i < prec; i++) {
            frac *= 10;
            int d = (int)frac;
            *p++ = (char)('0' + d);
            frac -= d;
        }
    }
    *p = 0;
}

int vprintf(const char *f, va_list ap) {
    for (; *f; f++) {
        if (*f != '%') {
            put(*f);
            continue;
        }
        f++;
        bool left = false, zero = false, alt = false;
        for (;; f++) {
            if (*f == '-')
                left = true;
            else if (*f == '0')
                zero = true;
            else if (*f == '#')
                alt = true;
            else
                break;
        }
        int width = 0;
        while (*f >= '0' && *f <= '9')
            width = width * 10 + (*f++ - '0');
        int prec = -1;
        if (*f == '.') {
            f++;
            prec = 0;
            while (*f >= '0' && *f <= '9')
                prec = prec * 10 + (*f++ - '0');
        }
        int lng = 0;
        while (*f == 'l' || *f == 'z' || *f == 'j' || *f == 't') {
            lng++;
            f++;
        }
        while (*f == 'h')
            f++;
        char buf[128];
        switch (*f) {
            case 'd':
            case 'i':
            case 'u':
            case 'x':
            case 'X':
            case 'p': {
                unsigned long long v;
                if (*f == 'p') {
                    v = (unsigned long long)va_arg(ap, void *);
                    lng = 1;
                } else if (lng > (sizeof(long) == 4))
                    v = va_arg(ap, unsigned long long);
                else
                    v = va_arg(ap, unsigned);
                bool neg = false;
                if (*f == 'd' || *f == 'i') {
                    long long s = lng > (sizeof(long) == 4) ? (long long)v : (long long)(int)v;
                    neg = s < 0;
                    v = neg ? (unsigned long long)-s : (unsigned long long)s;
                }
                int base = (*f == 'x' || *f == 'X' || *f == 'p') ? 16 : 10, n = 0;
                const char *digits = *f == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
                char tmp[32];
                do {
                    tmp[n++] = digits[v % base];
                    v /= base;
                } while (v);
                int k = 0;
                if (neg)
                    buf[k++] = '-';
                if (alt && base == 16) {
                    buf[k++] = '0';
                    buf[k++] = 'x';
                }
                if (zero && !left)
                    while (k + n < width)
                        buf[k++] = '0';
                while (n)
                    buf[k++] = tmp[--n];
                pad(buf, k, width, left, ' ');
                break;
            }
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s)
                    s = "(null)";
                int n = (int)strlen(s);
                if (prec >= 0 && n > prec)
                    n = prec;
                pad(s, n, width, left, ' ');
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                pad(&c, 1, width, left, ' ');
                break;
            }
            case 'f':
            case 'g':
            case 'e': {
                fmt_double(va_arg(ap, double), prec < 0 ? 6 : prec, buf);
                pad(buf, (int)strlen(buf), width, left, ' ');
                break;
            }
            case '%':
                put('%');
                break;
            default:
                put('?');
        }
    }
    return 0;
}

int printf(const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    vprintf(f, ap);
    va_end(ap);
    return 0;
}
int puts(const char *s) {
    write(1, s, strlen(s));
    put('\n');
    return 0;
}
int fflush(void *f) {
    (void)f;
    return 0;
}

// Image entry: arguments come in registers, from the engine's loader or from flatrun.
extern int flat_main(unsigned long a0, unsigned long a1);
__attribute__((noreturn)) void flat_start(unsigned long a0, unsigned long a1) {
    int code = flat_main(a0, a1);
    exit(code);
    __builtin_unreachable();
}
