#include "ClosureFileSystemPhysical.h"
#include "GuestRoot.hpp"
#include <fcntl.h>
#include <cstdio>

int main()
{
    char directory[]="/tmp/vim-dyld-io.XXXXXX",resolved[PATH_MAX];
    if(!mkdtemp(directory)||!realpath(directory,resolved))
        return 1;
    guestRoot=resolved;
    const std::string file=guestRoot+"/payload",alias=guestRoot+"/alias";
    struct Cleanup {
        const std::string& file;
        const std::string& alias;
        ~Cleanup(){unlink(alias.c_str());unlink(file.c_str());rmdir(guestRoot.c_str());}
    } cleanup{file,alias};
    uint8_t bytes[65536];
    for(size_t i=0;i<sizeof(bytes);i++)
        bytes[i]=(uint8_t)(i/4096+1);
    int fd=open(file.c_str(),O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC,0600);
    if(fd<0)return 2;
    bool written=write(fd,bytes,sizeof(bytes))==(ssize_t)sizeof(bytes);
    close(fd);
    if(!written||symlink("/payload",alias.c_str()))
        return 3;
    if(!guestRealpath(guestRoot.c_str(),resolved)||guestRoot!=resolved)
        return 4;

    dyld3::closure::FileSystemPhysical fs(guestRoot.c_str());
    dyld3::closure::LoadedFileInfo info;
    char canonical[MAXPATHLEN];
    if(!fs.getRealPath("/",canonical)||strcmp(canonical,"/"))
        return 7;
    if(!fs.loadFile("/alias",info,canonical,^(const char*,...){}) ||
       strcmp(canonical,"/payload") || fs.fileExists("/bin/ls"))
        return 5;
    fs.unloadPartialFile(info,4096,4096);
    bool correct=info.fileContentLen==4096&&!memcmp(info.fileContent,bytes+4096,4096);
    fs.unloadFile(info);
    if(!correct)return 6;
    puts("PASS guest-root alias, 4 KiB slice, complete host mapping lifetime, no host fallback");
    return 0;
}
