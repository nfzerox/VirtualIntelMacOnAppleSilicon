// Exercise the production cold-media wrapper without mounting or changing input.
#include "../VirtualIntelMac/VIMHFSInstallerDisk.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv){
    if(argc==4&&!strcmp(argv[1],"--copy-volume")){
        if(VIMCopyHFSInstallerVolume(argv[2],argv[3],NULL,NULL)){perror("Copy HFS installer volume");return 1;}
        return 0;
    }
    if(argc!=3)return 64;
    if(VIMWrapHFSInstallerDisk(argv[1],argv[2],NULL,NULL)){perror("Prepare HFS installer disk");return 1;}
    return 0;
}
