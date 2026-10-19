// Read-only loader-path diagnostic. ABI verified at gfxInitializeLibrary's
// gpu_bundle_find_trusted call in Catalina libGFXShared: name, output, capacity.
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    int (*lookup)(const char *,char *,size_t)=dlsym(RTLD_DEFAULT,"gpu_bundle_find_trusted");
    if(!lookup){fprintf(stderr,"GPU bundle lookup is unavailable\n");return 69;}
    if(argc<2)return 64;
    for(int i=1;i<argc;i++) {
        char path[1024]={0};errno=0;
        int result=lookup(argv[i],path,sizeof(path));
        printf("%s: status=%d errno=%d path=%s\n",argv[i],result,errno,path);
    }
    return 0;
}
