/* Finite public-API thread/TLS/allocation probe for CPU-backend comparison.
 * This is a developer fixture, never an installed guest service. Build with:
 * clang -O2 -Wall -Wextra -pthread -arch x86_64 -arch arm64 \
 *   -mmacosx-version-min=11.0 pthread-lifecycle.c -o /scratch/pthread-lifecycle
 */
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { LANES = 4, BYTES = 32768 };
static pthread_key_t key;
struct context { unsigned token; int error; };

static void *worker(void *argument)
{
    struct context *context = argument;
    pthread_t self = pthread_self();
    if (pthread_setspecific(key, context)) {
        context->error = 1;
        return argument;
    }
    unsigned char *bytes = malloc(BYTES);
    if (!bytes) {
        context->error = 2;
        return argument;
    }
    for (unsigned turn = 0; turn < 8; ++turn) {
        unsigned char pattern = (unsigned char)(context->token + turn);
        memset(bytes, pattern, BYTES);
        errno = 100 + (int)context->token;
        sched_yield();
        if (!pthread_equal(self, pthread_self()) ||
            pthread_getspecific(key) != context ||
            errno != 100 + (int)context->token) context->error = 3;
        for (unsigned index = 0; index < BYTES; ++index)
            if (bytes[index] != pattern) context->error = 4;
    }
    free(bytes);
    return argument;
}

int main(int argc, char **argv)
{
    char *end = NULL;
    long rounds = argc == 2 ? strtol(argv[1], &end, 10) : 64;
    if (argc > 2 || rounds < 1 || rounds > 4096 ||
        (argc == 2 && (!argv[1][0] || *end))) return 64;
    if (pthread_key_create(&key, NULL)) return 1;
    for (long round = 0; round < rounds; ++round) {
        pthread_t threads[LANES];
        struct context contexts[LANES];
        unsigned started = 0;
        int failed = 0;
        for (unsigned lane = 0; lane < LANES; ++lane) {
            contexts[lane] = (struct context){(unsigned)round * LANES + lane, 0};
            int status = pthread_create(&threads[lane], NULL, worker, &contexts[lane]);
            if (status) { fprintf(stderr, "pthread_create: %d\n", status); failed = 1; break; }
            ++started;
        }
        for (unsigned lane = 0; lane < started; ++lane) {
            void *value = NULL;
            if (pthread_join(threads[lane], &value) || value != &contexts[lane] ||
                contexts[lane].error) failed = 1;
        }
        if (failed) { fprintf(stderr, "FAIL: round %ld\n", round); return 1; }
        if (round % 16 == 15) { printf("Completed %ld threads\n", (round + 1) * LANES); fflush(stdout); }
    }
    pthread_key_delete(key);
    printf("PASS: %ld threads, TLS/errno, allocation and data preservation\n", rounds * LANES);
    return 0;
}
