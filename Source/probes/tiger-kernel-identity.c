#include "../tools/opengl-tiger-kernel-identity.h"
#include <stdio.h>
int main(int argc,char **argv){
    if(argc!=1&&argc!=2)return 64;
    char value[512]={0};
    if(argc==2){int found=VIMTigerKernelVersionSymbol(argv[1],value,sizeof(value));printf("native-version-symbol=%s\n",found?"true":"false");return found?0:1;}
    int match=VIMTigerRunningKernelMatchesFile();printf("running-kernel-matches-file=%s\n",match?"true":"false");return match?0:1;
}
