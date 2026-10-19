// Read-only comparison of Mojave's legacy acceleration capability reports.
#include <CoreGraphics/CoreGraphics.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdio.h>
int main(void) {
    void *s = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_NOW);
    int (*connection)(void) = dlsym(s, "SLSMainConnectionID");
    bool (*server)(void) = dlsym(s, "SLSServerMightHaveAcceleration");
    bool (*display)(CGDirectDisplayID) = dlsym(s, "SLDisplayUsesOpenGLAcceleration");
    if (!connection || !server || !display) return 1;
    printf("connection=%d server_might_have_acceleration=%d display_uses_opengl_acceleration=%d\n",
           connection(), server(), display(CGMainDisplayID()));
    return 0;
}
