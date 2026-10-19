// Mojave 18G103: native WindowServer IPC compositor selection experiment.
// SLSSetDebugOptions(0x800000ac) reaches activate_compositor_metal only after
// WSSystemCanCompositeWithMetal succeeds. No private text/global writes.
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
int main(void) {
    void *image = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_NOW);
    int (*selectCompositor)(uint32_t) = image ? dlsym(image, "SLSSetDebugOptions") : NULL;
    if (!selectCompositor) { fprintf(stderr, "SkyLight API unavailable\n"); return 1; }
    int (*connection)(void) = dlsym(image, "SLSMainConnectionID");
    if (connection) printf("Connection: %d\n", connection());
    int result = selectCompositor(0x800000acU);
    printf("Native Metal compositor request: %d\n", result);
    return result != 0;
}
