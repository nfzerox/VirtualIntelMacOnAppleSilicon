/* Read-only Rosetta1 detection qualification against the actual period OS.
 * The period sysctl CLI has a fixed name table; do not infer the libc API
 * result from that tool rejecting an unfamiliar symbolic top-level name. */
#include <sys/types.h>
#include <sys/sysctl.h>
#include <stdio.h>
#include <errno.h>
int main(void){
    int native=-1;size_t bytes=sizeof(native);
    int result=sysctlbyname("sysctl.proc_native",&native,&bytes,NULL,0);
    printf("pointer-bytes=%lu result=%d errno=%d bytes=%lu native=%d\n",
        (unsigned long)sizeof(void *),result,result?errno:0,(unsigned long)bytes,native);
    return result?1:0;
}
