#import <Foundation/Foundation.h>
#include <time.h>
extern dispatch_data_t PVGHostRepairLibrary(dispatch_data_t);
static double now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static NSData *repair(dispatch_data_t input) {
    dispatch_data_t result = PVGHostRepairLibrary(input);
    if (!result) return nil;
    const void *bytes; size_t count;
    dispatch_data_t map = dispatch_data_create_map(result, &bytes, &count);
    NSData *copy = map ? [NSData dataWithBytes:bytes length:count] : nil;
    if (map) dispatch_release(map);
    dispatch_release(result);
    return copy;
}
int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc < 2) return 64;
        for (int f = 1; f < argc; f++) {
            NSData *input = [NSData dataWithContentsOfFile:@(argv[f])];
            if (!input) return 1;
            dispatch_data_t data = dispatch_data_create(input.bytes, input.length,
                dispatch_get_global_queue(0,0), DISPATCH_DATA_DESTRUCTOR_DEFAULT);
            setenv("PVG_HOST_AIR_CACHE", "0", 1);
            NSData *expected = [repair(data) retain];
            for (int mode=0; mode<2; mode++) {
                setenv("PVG_HOST_AIR_CACHE", mode ? "1" : "0", 1);
                double start = now();
                for (int n=0; n<100; n++) {
                    @autoreleasepool {
                        NSData *got = repair(data);
                        if ((got || expected) && ![got isEqual:expected]) return 2;
                    }
                }
                printf("%s cache=%d repaired=%d runs=100 sec=%.6f identical=1\n",
                       argv[f], mode, expected != nil, now()-start);
            }
            [expected release]; dispatch_release(data);
        }
    }
    return 0;
}
