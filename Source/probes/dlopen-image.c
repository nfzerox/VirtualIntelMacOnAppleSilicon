#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc < 2) return 64;
    int create = !strcmp(argv[1], "--create-compiler");
    int gli = !strcmp(argv[1], "--gli-version");
    for (int i = create || gli ? 2 : 1; i < argc; ++i) {
        void *image = dlopen(argv[i], RTLD_NOW | RTLD_LOCAL);
        if (!image) { fprintf(stderr, "%s\n", dlerror()); return 1; }
        printf("image loaded: %s\n", argv[i]);
        if (gli) {
            unsigned char (*version)(int *,int *,int *)=dlsym(image,"gliGetVersion");
            if(!version)return 2;
            int major=0,minor=0,engine=0;
            unsigned char ok=version(&major,&minor,&engine);
            printf("gli_ready=%u abi=%d.%d engine=%x\n",ok,major,minor,engine);
            if(!ok||major!=2||minor!=3||engine!=0x30000)return 3;
        }
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
