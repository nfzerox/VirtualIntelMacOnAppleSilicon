#include <dlfcn.h>
#include <stdio.h>

typedef void *(*CreateCompilerFn)(void);
typedef void (*DeleteCompilerFn)(void *);

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s compiler-plugin\n", argv[0]);
        return 64;
    }
    void *image = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!image) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }
    CreateCompilerFn create = (CreateCompilerFn)dlsym(image,
                                                       "MTLCompilerCreate");
    DeleteCompilerFn destroy = (DeleteCompilerFn)dlsym(image,
                                                       "MTLCompilerDelete");
    if (!create || !destroy) {
        fprintf(stderr, "compiler entry points missing\n");
        return 2;
    }
    void *compiler = create();
    printf("compiler=%p\n", compiler);
    if (!compiler) return 3;
    destroy(compiler);
    return 0;
}
