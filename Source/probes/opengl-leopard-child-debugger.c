/* Finite debugger for an explicitly launched child in our own Leopard VM.
 * Uses the real 10.5 Mach/ptrace contracts. No attachment to existing/system
 * processes, host security changes, memory writes or user-buffer dumping. */
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/i386/thread_status.h>
#include <libproc.h>
#include <objc/objc.h>
#include "../tools/opengl-guest-machine.h"

static void location(pid_t pid,task_t task,uint32_t pc){
    char path[1024]={0};proc_regionfilename(pid,pc,path,sizeof(path));
    mach_vm_address_t address=pc;mach_vm_size_t size=0;
    vm_region_basic_info_data_64_t info;mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object=MACH_PORT_NULL;
    kern_return_t result=mach_vm_region(task,&address,&size,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object);
    printf("pc=%08x region=%llx size=%llx file-offset=%llx region-result=%d image=%s\n",pc,
        (unsigned long long)address,(unsigned long long)size,(unsigned long long)(result==KERN_SUCCESS?info.offset:0),result,path);
    if(object)mach_port_deallocate(mach_task_self(),object);
}
static void dump(pid_t pid){
    task_t task=MACH_PORT_NULL;kern_return_t result=task_for_pid(mach_task_self(),pid,&task);
    printf("child-task-result=%d\n",result);if(result!=KERN_SUCCESS)return;
    thread_act_array_t threads=NULL;mach_msg_type_number_t count=0;
    result=task_threads(task,&threads,&count);printf("thread-result=%d count=%u\n",result,count);
    for(mach_msg_type_number_t i=0;result==KERN_SUCCESS&&i<count&&i<32;i++){
        x86_thread_state32_t state;mach_msg_type_number_t words=x86_THREAD_STATE32_COUNT;
        kern_return_t got=thread_get_state(threads[i],x86_THREAD_STATE32,(thread_state_t)&state,&words);
        printf("thread=%u state-result=%d\n",i,got);
        if(got!=KERN_SUCCESS)continue;
        printf("eip=%08x esp=%08x ebp=%08x eax=%08x ebx=%08x ecx=%08x edx=%08x\n",
            state.__eip,state.__esp,state.__ebp,state.__eax,state.__ebx,state.__ecx,state.__edx);
        location(pid,task,state.__eip);uint32_t frame=state.__ebp;
        if(i==0){
            uint32_t call[5]={0};mach_vm_size_t bytes=0;char selector[128]={0};
            if(mach_vm_read_overwrite(task,state.__esp,sizeof(call),(mach_vm_address_t)(uintptr_t)call,&bytes)==KERN_SUCCESS&&bytes==sizeof(call))
                printf("fault-call return=%08x receiver=%08x selector=%08x argument0=%08x argument1=%08x\n",call[0],call[1],call[2],call[3],call[4]);
            /* objc selector names are runtime metadata, not document contents. */
            if(mach_vm_read_overwrite(task,state.__ecx,sizeof(selector)-1,(mach_vm_address_t)(uintptr_t)selector,&bytes)==KERN_SUCCESS){
                unsigned n=0;while(n<sizeof(selector)-1&&selector[n]>=32&&selector[n]<127)n++;selector[n]=0;
                printf("fault-selector=%s\n",selector);
            }
        }
        for(unsigned f=0;f<8&&frame&&!(frame&3);f++){
            uint32_t pair[2]={0};mach_vm_size_t bytes=0;
            if(mach_vm_read_overwrite(task,frame,sizeof(pair),(mach_vm_address_t)(uintptr_t)pair,&bytes)!=KERN_SUCCESS||bytes!=sizeof(pair))break;
            printf("frame=%u ebp=%08x return=%08x\n",f,frame,pair[1]);location(pid,task,pair[1]);
            if(pair[0]<=frame||pair[0]-frame>1024*1024)break;frame=pair[0];
        }
    }
    if(threads){for(mach_msg_type_number_t i=0;i<count;i++)mach_port_deallocate(mach_task_self(),threads[i]);vm_deallocate(mach_task_self(),(vm_address_t)threads,count*sizeof(*threads));}
    mach_port_deallocate(mach_task_self(),task);
}
int main(int argc,char **argv){
    if(argc<2||geteuid()!=0||!VIMOpenGLGuestMachine())return 77;
    setvbuf(stdout,NULL,_IOLBF,0);pid_t child=fork();if(child<0)return 1;
    if(!child){if(ptrace(PT_TRACE_ME,0,NULL,0)){perror("trace child");_exit(1);}execv(argv[1],argv+1);perror("exec child");_exit(1);}
    for(unsigned stops=0;stops<64;stops++){
        int status;if(waitpid(child,&status,0)!=child)return 1;
        if(WIFEXITED(status)){printf("child-exit=%d\n",WEXITSTATUS(status));return WEXITSTATUS(status);}
        if(WIFSIGNALED(status)){printf("child-signal=%d\n",WTERMSIG(status));return 128+WTERMSIG(status);}
        if(!WIFSTOPPED(status))continue;
        int sig=WSTOPSIG(status);printf("child-stop=%d\n",sig);
        if(sig==SIGBUS||sig==SIGSEGV||sig==SIGILL||sig==SIGABRT)dump(child);
        // Bound our tracing, never the native installation. Detach while the
        // child is stopped so a long successful install can continue normally.
        if(stops==63){
            if(ptrace(PT_DETACH,child,(caddr_t)1,sig==SIGTRAP?0:sig)){perror("detach child");return 1;}
            puts("trace-budget-exhausted-child-detached");return 2;
        }
        if(ptrace(PT_CONTINUE,child,(caddr_t)1,sig==SIGTRAP?0:sig)){perror("continue child");break;}
    }
    /* A diagnostic error must not kill the install child. */
    return 1;
}
