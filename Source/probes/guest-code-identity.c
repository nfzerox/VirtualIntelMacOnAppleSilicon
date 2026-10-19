// Read only our own kernel code-signing flags; no TCC permission/capture calls.
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
extern int csops(pid_t pid,unsigned int operations,void *useraddr,size_t usersize);
int main(void) {
    uint32_t flags=0;
    if(csops(getpid(),0,&flags,sizeof(flags))){perror("csops status");return 1;}
    printf("{\"Flags\":%u,\"PlatformBinary\":%s,\"RequireLibraryValidation\":%s}\n",flags,
        flags&0x04000000?"true":"false",flags&0x2000?"true":"false");
    return 0;
}
