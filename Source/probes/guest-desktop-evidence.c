// Read-only diagnosis of desktop process enumeration in the actual guest.
#include <libproc.h>
#include <sys/proc_info.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    pid_t pids[8192];errno=0;
    int bytes=proc_listpids(PROC_ALL_PIDS,0,pids,sizeof(pids));
    printf("proc_listpids result=%d errno=%d capacity=%zu\n",bytes,errno,sizeof(pids));
    for(int i=0;i<bytes/(int)sizeof(pid_t);i++) {
        char path[PROC_PIDPATHINFO_MAXSIZE]={0},name[128]={0};
        errno=0;int result=proc_pidpath(pids[i],path,sizeof(path)),failure=errno;
        proc_name(pids[i],name,sizeof(name));
        if(strstr(name,"Finder")||strstr(name,"Setup")||strstr(path,"Finder")||strstr(path,"Setup")||i<8)
            printf("index=%d pid=%d name=%s path_result=%d errno=%d path=%s\n",i,pids[i],name,result,failure,path);
    }
}
