// Finite native ARM HVF counter/host-clock comparison. No guest disk, driver,
// policy changes or x86 translator. Sign with the normal hypervisor entitlement.
#include <stdio.h>
#if defined(__aarch64__)
#include <Hypervisor/Hypervisor.h>
#include <libkern/OSCacheControl.h>
#include <mach/mach_time.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

__asm__(".text\n.p2align 2\n.globl _VIMCounterCode\n_VIMCounterCode:\n"
        "mrs x0,cntvct_el0\nmrs x1,cntfrq_el0\nhvc #0\n"
        ".globl _VIMCounterCodeEnd\n_VIMCounterCodeEnd:\n");
extern const char VIMCounterCode[],VIMCounterCodeEnd[];

static int check(hv_return_t result,const char *operation) {
    if(result==HV_SUCCESS)return 1;
    fprintf(stderr,"%s: 0x%x\n",operation,(unsigned)result);return 0;
}
int main(void) {
    mach_timebase_info_data_t tb;mach_timebase_info(&tb);
    printf("host_timebase=%u/%u\n",tb.numer,tb.denom);
    size_t length=(size_t)getpagesize();
    void *memory=mmap(NULL,length,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(memory==MAP_FAILED)return 1;
    memcpy(memory,VIMCounterCode,(size_t)(VIMCounterCodeEnd-VIMCounterCode));
    sys_icache_invalidate(memory,(size_t)(VIMCounterCodeEnd-VIMCounterCode));
    if(!check(hv_vm_create(NULL),"create VM")){munmap(memory,length);return 1;}
    int failed=0;hv_vcpu_t cpu;hv_vcpu_exit_t *exit=NULL;
    if(!check(hv_vm_map(memory,0x100000,length,HV_MEMORY_READ|HV_MEMORY_EXEC),"map code")) {failed=1;goto vm_done;}
    if(!check(hv_vcpu_create(&cpu,&exit,NULL),"create vCPU")){failed=1;goto map_done;}
    uint64_t offset=0;
    if(!check(hv_vcpu_get_vtimer_offset(cpu,&offset),"read timer offset")||
       !check(hv_vcpu_set_vtimer_mask(cpu,true),"mask virtual timer")||
       !check(hv_vcpu_set_reg(cpu,HV_REG_CPSR,0x3c5),"select EL1")){failed=1;goto cpu_done;}
    printf("virtual_timer_offset=%llu\n",(unsigned long long)offset);
    uint64_t previousHost=0,previousVirtual=0;
    for(unsigned i=0;i<6;i++) {
        if(!check(hv_vcpu_set_reg(cpu,HV_REG_PC,0x100000),"set PC")){failed=1;break;}
        uint64_t before=mach_absolute_time();
        if(!check(hv_vcpu_run(cpu),"run counter reader")){failed=1;break;}
        uint64_t after=mach_absolute_time(),counter=0,frequency=0;
        if(exit->reason!=HV_EXIT_REASON_EXCEPTION || (exit->exception.syndrome>>26)!=0x16 ||
           !check(hv_vcpu_get_reg(cpu,HV_REG_X0,&counter),"read counter")||
           !check(hv_vcpu_get_reg(cpu,HV_REG_X1,&frequency),"read frequency")){
            fputs("Counter reader did not exit through its HVC\n",stderr);failed=1;break;
        }
        uint64_t midpoint=before+(after-before)/2;
        if(i) {
            double hostNs=(double)(midpoint-previousHost)*tb.numer/tb.denom;
            double virtualNs=(double)(counter-previousVirtual)*125/3;
            double ratio=virtualNs/hostNs;
            printf("sample=%u counter_frequency=%llu host_ns=%.0f virtual_ns=%.0f ratio=%.6f exit_bracket_ns=%.0f\n",
                i,(unsigned long long)frequency,hostNs,virtualNs,ratio,(double)(after-before)*tb.numer/tb.denom);
            if(ratio<.95||ratio>1.05)failed=1;
        }
        previousHost=midpoint;previousVirtual=counter;usleep(20000);
    }
cpu_done:
    hv_vcpu_destroy(cpu);
map_done:
    hv_vm_unmap(0x100000,length);
vm_done:
    hv_vm_destroy();munmap(memory,length);return failed;
}
#else
int main(void){puts("ARM host only");return 77;}
#endif
