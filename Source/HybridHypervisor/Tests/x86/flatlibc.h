#pragma once
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

long raw_syscall(long n, long a, long b, long c, long d, long e, long f, long *second);
void flush_out(void);
long write(int fd, const void *p, unsigned long n);
void exit(int code) __attribute__((noreturn));
void _exit(int code) __attribute__((noreturn));
void *mmap(void *addr, unsigned long len, int prot, int flags, int fd, long off);
int munmap(void *addr, unsigned long len);
struct timeval {
    long tv_sec;
    int tv_usec;
};
int gettimeofday(struct timeval *t, void *z);
void *memcpy(void *d, const void *s, unsigned long n);
void *memmove(void *d, const void *s, unsigned long n);
void *memset(void *d, int v, unsigned long n);
int memcmp(const void *a, const void *b, unsigned long n);
unsigned long strlen(const char *s);
int strcmp(const char *a, const char *b);
char *strcpy(char *d, const char *s);
int vprintf(const char *f, va_list ap);
int printf(const char *f, ...) __attribute__((format(printf, 1, 2)));
int puts(const char *s);
int fflush(void *f);
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_PRIVATE 2
#define MAP_ANON 0x1000
#define MAP_FAILED ((void *)-1L)
