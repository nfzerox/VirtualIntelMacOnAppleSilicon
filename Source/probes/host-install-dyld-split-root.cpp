#include "ClosureFileSystemPhysical.h"
#include "GuestRoot.hpp"
#include <cstdio>

int main(int argc,char **argv) {
    if(argc!=3)return 64;
    guestRoot=argv[1];
    if(!guestConfigureDataRoot(argv[2]))return 1;
    dyld3::closure::FileSystemPhysical fs(guestRoot.c_str());
    char physical[PATH_MAX],logical[PATH_MAX],canonical[MAXPATHLEN];
    if(!guestRealpath("/Library/payload",physical,logical)||strcmp(logical,"/Library/payload")||
        std::string(physical)!=guestDataRoot+"/Library/payload")return 2;
    struct stat actual;
    if(stat(physical,&actual))return 3;
    uint64_t inode=0,mtime=0;bool matches=false;
    if(!fs.fileExists("/Library/payload",&inode,&mtime,nullptr,&matches)||!matches||
        inode!=actual.st_ino||mtime!=(uint64_t)actual.st_mtime)return 4;
    dyld3::closure::LoadedFileInfo info;
    if(!fs.loadFile("/Library/payload",info,canonical,^(const char*,...){})||
        info.inode!=actual.st_ino||info.mtime!=(uint64_t)actual.st_mtime||
        *(const uint8_t *)info.fileContent!=0x42)return 5;
    fs.unloadFile(info);
    if(!fs.getRealPath("/Library/alias",canonical)||strcmp(canonical,"/payload")||
        !guestRealpath("/Library/alias",physical)||std::string(physical)!=guestRoot+"/payload")return 6;
    if(!guestRealpath((guestDataRoot+"/Library/payload").c_str(),physical,logical)||
        strcmp(logical,"/Library/payload")||fs.fileExists("/Library/escape"))return 7;
    if(!fs.fileExists("/System/Volumes/Data/Library/payload"))return 8;
    puts("PASS final System/Data inodes, original firmlink namespace, root-confined absolute symlinks");
    return 0;
}
