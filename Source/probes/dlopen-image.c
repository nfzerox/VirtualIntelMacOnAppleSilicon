#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc < 2) return 64;
    int create = !strcmp(argv[1], "--create-compiler");
    for (int i = create ? 2 : 1; i < argc; ++i) {
        void *image = dlopen(argv[i], RTLD_NOW | RTLD_LOCAL);
        if (!image) { fprintf(stderr, "%s\n", dlerror()); return 1; }
        printf("image loaded: %s\n", argv[i]);
        if (create) {
            void *(*make)(void) = dlsym(image,"MTLCompilerCreate");
            void (*destroy)(void *) = dlsym(image,"MTLCompilerDelete");
            if(!make||!destroy)return 2;
            void *compiler=make();printf("compiler=%p\n",compiler);
            if(!compiler)return 3;
            destroy(compiler);
        }
    }
    return 0;
}
