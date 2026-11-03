/* Cross-architecture stream/lifetime fixture, no OpenGL or VM dependency.
 * Launch server, then an independently built i386 client on Mojave. */
#include "../PassthroughOpenGL/passthrough-opengl-mach-stream.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int transfer(struct DGMachStream *s,void *p,size_t left,int write)
{
    unsigned char *bytes=p;
    while(left){ssize_t n=write?DGMachWrite(s,bytes,left):DGMachRead(s,bytes,left);
        if(n<=0)return -1;bytes+=n;left-=(size_t)n;}return 0;
}
static void echo(struct DGMachStream *s)
{
    uint32_t bytes;
    while(!transfer(s,&bytes,sizeof(bytes),0)){
        if(bytes>4*1024*1024)break;
        unsigned char *p=malloc(bytes?bytes:1);if(!p)break;
        int bad=transfer(s,p,bytes,0)||transfer(s,p,bytes,1);free(p);if(bad)break;
    }
}
int main(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"--server"))return DGMachServe(echo);
    for(unsigned round=0;round<8;++round){
        struct DGMachStream *s=DGMachOpen();if(!s){perror("open");return 1;}
        uint32_t bytes=1024*1024+37;
        unsigned char *a=malloc(bytes),*b=malloc(bytes);
        if(!a||!b)return 1;
        for(unsigned i=0;i<bytes;++i)a[i]=(unsigned char)(i*37+round);
        int failed=transfer(s,&bytes,sizeof(bytes),1)||transfer(s,a,bytes,1)||transfer(s,b,bytes,0)||memcmp(a,b,bytes);
        free(a);free(b);if(DGMachClose(s))failed=1;
        if(failed){fprintf(stderr,"FAIL shared-memory round%u\n",round);return 1;}
    }
    printf("PASS anonymous shared Mach rings: pointer-bits=%zu, wrapped bytes, drained close, live=%u\n",sizeof(void *)*8,DGMachLiveCount());
    return DGMachLiveCount()?1:0;
}
