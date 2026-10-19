#include <dlfcn.h>
#include <mach/mach.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>
#include <sched.h>

static atomic_uint data[8192], stop, violations;
static int enabled;
static kern_return_t (*set_compat)(uint32_t);
static void enable_thread(void) {
    if (set_compat(enabled) != KERN_SUCCESS) abort();
}
static void *writer(void *unused) {
    (void)unused;
    enable_thread();
    unsigned epoch = 0;
    while (!atomic_load(&stop)) {
        ++epoch;
        for (unsigned i=0; i<8192; ++i) {
            atomic_store_explicit(&data[i], epoch, memory_order_relaxed);
            atomic_signal_fence(memory_order_seq_cst);
        }
        if (!(epoch & 255)) sched_yield();
    }
    return NULL;
}
static void *reader(void *unused) {
    (void)unused;
    enable_thread();
    while (!atomic_load(&stop)) {
        for (unsigned i=0; i<8191; ++i) {
            unsigned later=atomic_load_explicit(&data[i+1], memory_order_relaxed);
            atomic_signal_fence(memory_order_seq_cst);
            unsigned earlier=atomic_load_explicit(&data[i], memory_order_relaxed);
            if (earlier < later) atomic_fetch_add(&violations, 1);
        }
        sched_yield();
    }
    return NULL;
}

int main(int argc, char **argv)
{
    set_compat = dlsym(RTLD_DEFAULT, "thread_set_x86_64_compat");
    if (!set_compat) {
        puts("thread_set_x86_64_compat is absent from the running system");
        return 2;
    }
    kern_return_t on = set_compat(1);
    kern_return_t off = on == KERN_SUCCESS ? set_compat(0) : KERN_FAILURE;
    printf("enable=%d disable=%d\n", on, off);
    if (argc == 2 && on == 0 && off == 0) {
        enabled=atoi(argv[1]) != 0;
        pthread_t a,b;
        pthread_create(&a,NULL,writer,NULL);
        pthread_create(&b,NULL,reader,NULL);
        struct timespec duration={3,0};
        nanosleep(&duration,NULL);
        atomic_store(&stop,1);
        pthread_join(a,NULL); pthread_join(b,NULL);
        printf("tso=%d ordering_violations=%u\n", enabled, atomic_load(&violations));
        if (enabled && atomic_load(&violations)) return 3;
    }
    return on == KERN_SUCCESS && off == KERN_SUCCESS ? 0 : 1;
}
