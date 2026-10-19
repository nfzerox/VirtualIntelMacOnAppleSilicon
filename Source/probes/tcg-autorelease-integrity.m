/* Exercise the failing runtime facility without AppKit, Metal or PVG. */
#import <Foundation/Foundation.h>
#include <pthread.h>
#include <unistd.h>
static void *worker(void *unused)
{
    (void)unused;
    for (unsigned round=0; round<500; round++) {
        @autoreleasepool {
            for (unsigned i=0; i<1024; i++) {
                NSMutableData *data = [NSMutableData dataWithLength:64];
                memset(data.mutableBytes, (int)(i&255), data.length);
            }
        }
    }
    return NULL;
}
int main(void)
{
    alarm(45);
    pthread_t threads[4];
    for (unsigned i=0; i<4; i++)
        if (pthread_create(&threads[i], NULL, worker, NULL)) return 2;
    for (unsigned i=0; i<4; i++)
        if (pthread_join(threads[i], NULL)) return 2;
    puts("four-thread autorelease integrity: PASS");
    return 0;
}
