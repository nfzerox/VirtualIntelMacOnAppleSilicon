/* Local ARM64 host diagnostic: capture real PCs and current instruction bytes.
 * No ptrace/debugger signal interception, no target writes or inferior calls.
 * Each thread is suspended only for state/read, then resumed before printing.
 * Explicit address range selects JIT code; absence outside it is not a profile.
 */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/arm/thread_status.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5) {
        fprintf(stderr,"usage: %s pid jit-start jit-end [rounds]\n",argv[0]);
        return 64;
    }
    int pid=atoi(argv[1]);
    uint64_t lo=strtoull(argv[2],NULL,0), hi=strtoull(argv[3],NULL,0);
    unsigned rounds=argc==5 ? atoi(argv[4]) : 80;
    if(pid<=1 || !lo || hi<=lo || !rounds || rounds>1000) return 64;
    mach_port_t task=MACH_PORT_NULL;
    kern_return_t kr=task_for_pid(mach_task_self(),pid,&task);
    if(kr!=KERN_SUCCESS) { fprintf(stderr,"task_for_pid: %s\n",mach_error_string(kr)); return 1; }
    unsigned total=0, matched=0, failures=0;
    for(unsigned round=0;round<rounds;round++) {
        thread_act_array_t threads=NULL; mach_msg_type_number_t n=0;
        if(task_threads(task,&threads,&n)!=KERN_SUCCESS) break;
        for(unsigned i=0;i<n;i++) {
            thread_basic_info_data_t basic={0};
            mach_msg_type_number_t basic_count=THREAD_BASIC_INFO_COUNT;
            kern_return_t basic_result=thread_info(threads[i],THREAD_BASIC_INFO,
                                                   (thread_info_t)&basic,&basic_count);
            arm_thread_state64_t state;
            mach_msg_type_number_t count=ARM_THREAD_STATE64_COUNT;
            uint8_t bytes[132]; mach_vm_size_t got=0;
            uint64_t pc=0; kern_return_t read_result=KERN_FAILURE;
            kr=thread_suspend(threads[i]);
            if(kr==KERN_SUCCESS) {
                kr=thread_get_state(threads[i],ARM_THREAD_STATE64,(thread_state_t)&state,&count);
                if(kr==KERN_SUCCESS) {
                    total++;
                    pc=arm_thread_state64_get_pc(state);
                    if(pc>=lo+64 && pc+68<=hi)
                        read_result=mach_vm_read_overwrite(task,pc-64,sizeof(bytes),(mach_vm_address_t)bytes,&got);
                }
                /* Always release exactly the suspension acquired above. */
                kern_return_t resumed=thread_resume(threads[i]);
                if(resumed!=KERN_SUCCESS) {
                    fprintf(stderr,"thread_resume failed: %s\n",mach_error_string(resumed));
                    return 2;
                }
            } else failures++;
            if(read_result==KERN_SUCCESS && got==sizeof(bytes)) {
                matched++;
                printf("{\"round\":%u,\"run_state_before_suspend\":%d,\"pc\":\"0x%llx\",\"start\":\"0x%llx\",\"bytes\":\"",round,basic_result==KERN_SUCCESS ? basic.run_state : -1,(unsigned long long)pc,(unsigned long long)(pc-64));
                for(unsigned j=0;j<sizeof(bytes);j++) printf("%02x",bytes[j]);
                puts("\"}");
            }
            mach_port_deallocate(mach_task_self(),threads[i]);
        }
        vm_deallocate(mach_task_self(),(vm_address_t)threads,n*sizeof(*threads));
        usleep(11000+(round*7919)%17000);
    }
    fprintf(stderr,"thread states=%u matched JIT=%u suspend failures=%u\n",total,matched,failures);
    mach_port_deallocate(mach_task_self(),task);
    return 0;
}
