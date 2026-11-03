// Tiger's assert.h calls this GCC runtime entry point. Clang does not ship
// GCC 4's static libgcc.a; keep assertions functional without donating a
// compiler runtime or disabling the cryptographic library's checks.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
__attribute__((noreturn))
void __eprintf(const char *format,const char *file,unsigned line,const char *expression) {
    fprintf(stderr,format,file,line,expression);
    fflush(stderr);
    abort();
}

// Clang lowers OpenSSH's checked 64-bit arithmetic to this compiler-rt ABI
// (llvm-project compiler-rt/lib/builtins/mulodi4.c). The period gcc_s runtime
// supplies ordinary 64-bit division, but predates this overflow-reporting
// entry point. Use unsigned magnitudes so INT64_MIN and the returned low
// product bits never invoke signed overflow, retaining the caller's checks.
int64_t __mulodi4(int64_t a,int64_t b,int *overflow) {
    uint64_t ua=a<0?0-(uint64_t)a:(uint64_t)a;
    uint64_t ub=b<0?0-(uint64_t)b:(uint64_t)b;
    uint64_t limit=((a<0)!=(b<0))?((uint64_t)1<<63):INT64_MAX;
    *overflow=ub!=0&&ua>limit/ub;
    return (int64_t)((uint64_t)a*(uint64_t)b);
}
