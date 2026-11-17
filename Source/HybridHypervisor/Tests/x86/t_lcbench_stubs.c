// lc-bench references kernel services its flat subset never calls.
extern void exit(int);
extern int puts(const char *);
#define NEVER(name)                                                                                                    \
    void name(void) {                                                                                                  \
        puts(#name " called");                                                                                         \
        exit(3);                                                                                                       \
    }
NEVER(perror)
NEVER(fork)
NEVER(waitpid)
NEVER(pipe)
NEVER(read)
NEVER(close)
NEVER(getpagesize)
NEVER(getppid)
NEVER(pthread_create)
NEVER(pthread_join)
NEVER(strtod)
NEVER(fprintf)
NEVER(setbuf)
int __stderrp, __stdoutp;
int *__error(void) {
    static int e;
    return &e;
}
