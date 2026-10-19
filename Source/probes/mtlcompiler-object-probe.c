#include <dlfcn.h>
#include <stdio.h>

typedef void *(*MTLCodeGenServiceCreateFn)(void);

int main(int argc, char **argv) {
    const char *path = argc > 1
        ? argv[1]
        : "/System/Library/PrivateFrameworks/MTLCompiler.framework/Versions/31001/MTLCompiler";
    void *image = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!image) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }

    MTLCodeGenServiceCreateFn create =
        (MTLCodeGenServiceCreateFn)dlsym(image, "MTLCodeGenServiceCreate");
    if (!create) {
        fprintf(stderr, "dlsym failed: %s\n", dlerror());
        return 2;
    }

    printf("MTLCodeGenServiceCreate=%p\n", (void *)create);
    fflush(stdout);
    void *service = create();
    printf("service=%p\n", service);
    return service ? 0 : 3;
}
