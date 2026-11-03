/* Own-process, read-only Tiger KERN_PROCARGS2 framing diagnostic. */
#include <sys/types.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    int maximum=0;size_t size=sizeof(maximum);int maximumMIB[]={CTL_KERN,KERN_ARGMAX};
    int status=sysctl(maximumMIB,2,&maximum,&size,NULL,0);
    printf("argmax status=%d errno=%d bytes=%u maximum=%d\n",status,errno,(unsigned)size,maximum);
    if(status||maximum<1||maximum>1024*1024)return 1;
    char *data=calloc(1,(size_t)maximum);if(!data)return 1;
    int mib[]={CTL_KERN,KERN_PROCARGS2,getpid()};size=(size_t)maximum;errno=0;
    status=sysctl(mib,3,data,&size,NULL,0);
    printf("procargs2 status=%d errno=%d bytes=%u\n",status,errno,(unsigned)size);
    if(!status&&size>=sizeof(int)){
        int argc=0;memcpy(&argc,data,sizeof(argc));printf("argc=%d first-bytes=",argc);
        size_t i;for(i=0;i<size&&i<32;i++)printf("%02x",(unsigned char)data[i]);puts("");
    }
    free(data);return status?1:0;
}
