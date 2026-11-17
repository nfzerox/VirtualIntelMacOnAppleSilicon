// Read-only qualification: a mapped cache alone does not prove dylibs use it.
#include <dlfcn.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc,char **argv) {
    if(argc<3)return 64;
    int fd=open(argv[1],O_RDONLY|O_NOFOLLOW);
    uint8_t header[32];
    if(fd<0||pread(fd,header,sizeof(header),0)!=sizeof(header)||memcmp(header,"dyld_v1",7))return 1;
    uint32_t offset,count;memcpy(&offset,header+24,4);memcpy(&count,header+28,4);
    const void *(*getRange)(size_t *)=dlsym(RTLD_DEFAULT,"_dyld_get_shared_cache_range");
    size_t size=0;uintptr_t base=getRange?(uintptr_t)getRange(&size):0;
    int outcome=0;
    for(int arg=2;arg<argc;arg++) {
        struct stat actual;
        if(stat(argv[arg],&actual)){perror(argv[arg]);outcome=1;continue;}
        bool found=false;
        for(uint32_t i=0;i<count;i++) {
            struct {uint64_t address,mtime,inode;uint32_t path,padding;} image;
            char path[4096];
            if(pread(fd,&image,sizeof(image),(off_t)offset+(off_t)i*sizeof(image))!=sizeof(image))return 1;
            ssize_t length=pread(fd,path,sizeof(path)-1,image.path);
            if(length<=0)return 1;
            path[length]=0;
            if(strcmp(path,argv[arg]))continue;
            found=true;
            printf("%s: cache inode=%llu mtime=%llu; actual inode=%llu mtime=%lld; identity=%s\n",
                path,(unsigned long long)image.inode,(unsigned long long)image.mtime,
                (unsigned long long)actual.st_ino,(long long)actual.st_mtime,
                image.inode==actual.st_ino&&image.mtime==(uint64_t)actual.st_mtime?"match":"MISMATCH");
            break;
        }
        if(!found)printf("%s: not cached\n",argv[arg]);
        void *handle=dlopen(argv[arg],RTLD_LAZY|RTLD_LOCAL);
        if(!handle){fprintf(stderr,"%s\n",dlerror());outcome=1;continue;}
        for(uint32_t i=0;i<_dyld_image_count();i++) {
            if(strcmp(_dyld_get_image_name(i),argv[arg]))continue;
            uintptr_t address=(uintptr_t)_dyld_get_image_header(i);
            printf("  mapped=%p %s\n",(void *)address,base&&address>=base&&address-base<size?"shared cache":"LOOSE IMAGE");
        }
        dlclose(handle);
    }
    close(fd);return outcome;
}
