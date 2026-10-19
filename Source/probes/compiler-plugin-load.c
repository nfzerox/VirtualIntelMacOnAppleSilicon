#include <dlfcn.h>
#include <stdio.h>
int main(int argc,char **argv) {
    if(argc!=2) return 64;
    void *image=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!image) { fprintf(stderr,"%s\n",dlerror());return 1; }
    void *(*create)(void)=dlsym(image,"MTLCompilerCreate");
    void (*destroy)(void *)=dlsym(image,"MTLCompilerDelete");
    if(!create||!destroy) return 2;
    void *compiler=create();printf("compiler=%p\n",compiler);
    if(compiler) destroy(compiler);
    return compiler?0:1;
}
