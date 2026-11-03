// Finite, read-only ABI check for the period kernel's stable process birth
// record. No root access, file writes, graphics selection or polling loop.
#include <libproc.h>
#include <unistd.h>
#include <stdio.h>
int main(void){
    struct proc_bsdinfo first,second;
    int a=proc_pidinfo(getpid(),PROC_PIDTBSDINFO,0,&first,sizeof(first));
    int b=proc_pidinfo(getpid(),PROC_PIDTBSDINFO,0,&second,sizeof(second));
    int valid=a==(int)sizeof(first)&&b==a&&first.pbi_pid==(unsigned)getpid()&&
        first.pbi_uid==getuid()&&first.pbi_start_tvsec>0&&
        first.pbi_start_tvsec==second.pbi_start_tvsec&&
        first.pbi_start_tvusec==second.pbi_start_tvusec;
    printf("process-birth-ABI=%s record-bytes=%d expected=%zu\n",valid?"PASS":"FAIL",a,sizeof(first));
    return valid?0:1;
}
