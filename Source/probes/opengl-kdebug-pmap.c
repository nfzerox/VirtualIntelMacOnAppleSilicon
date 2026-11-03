/* Mavericks-only bounded PMAP/interrupt Kdebug capture. Private ABI declarations below
 * match xnu-2422.115.4 bsd/sys/{kdebug,sysctl}.h and kdbg_control/kdbg_read.
 * Actual 13F1911 kernel disassembly confirms PMAP begin/end events. No kernel
 * patch, private-memory read or FBT on VM primitives. Refuse existing buffers
 * rather than commandeering another profiler. No polling while recording.
 */
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/kdebug.h>
#include <mach/mach_time.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
struct VIMKDBuffer {uint64_t timestamp;uintptr_t args[5];uint32_t debugid,cpu;uintptr_t unused;};
struct VIMKDInfo {int buffers,nolog,flags,threads,owner;};
struct VIMKDFilter {unsigned type,value1,value2,value3,value4;};
_Static_assert(sizeof(struct VIMKDBuffer)==64,"period x64 Kdebug record");
_Static_assert(sizeof(struct VIMKDInfo)==20,"period Kdebug info");
static int owned;
static volatile sig_atomic_t stop_requested;
static int control(int operation,int value,void *data,size_t *bytes)
{
    int mib[]={CTL_KERN,KERN_KDEBUG,operation,value};
    return sysctl(mib,4,data,bytes,NULL,0);
}
static void cleanup(void)
{
    if(!owned)return;
    if(control(KERN_KDENABLE,0,NULL,NULL))perror("Kdebug disable failed");
    if(control(KERN_KDREMOVE,0,NULL,NULL))perror("Kdebug removal failed");
    owned=0;
}
static void cancel(int number){(void)number;stop_requested=1;}
static int fail(const char *what){perror(what);return 1;}
int main(int argc,char** argv)
{
    int irq=(argc==2 || argc==4) && !strcmp(argv[1],"--interrupts");
    unsigned seconds=24;
    if(argc==4 && irq && !strcmp(argv[2],"--seconds")){
        char* end;errno=0;unsigned long value=strtoul(argv[3],&end,10);
        if(errno || !*argv[3] || *end || !value || value>60)return 2;
        seconds=(unsigned)value;
    }else if(argc!=1 && !(argc==2 && irq)){
        fputs("Usage: opengl-kdebug-pmap [--interrupts [--seconds 1..60]]\n",stderr);return 2;
    }
    const char* kind=irq?"IRQ":"PMAP";
    char release[64]={0},uuid[64]={0};size_t size=sizeof(release);
    if(geteuid() || sizeof(uintptr_t)!=8 || sysctlbyname("kern.osrelease",release,&size,NULL,0) || strncmp(release,"13.",3)){
        fputs("Mavericks x64 guest root only; no state changed.\n",stderr);return 2;
    }
    size=sizeof(uuid);
    if(sysctlbyname("kern.uuid",uuid,&size,NULL,0) || strcmp(uuid,"8926E512-6714-34FC-B27C-6AE90EB51D5E")){
        fputs("Unqualified kernel UUID; no state changed.\n",stderr);return 2;
    }
    struct VIMKDInfo info={0};size=sizeof(info);
    if(control(KERN_KDGETBUF,0,&info,&size))return fail("Kdebug passive info");
    printf("KDEBUG prior buffers=%d nolog=%d flags=%x owner=%d\n",info.buffers,info.nolog,info.flags,info.owner);
    if(size!=sizeof(info) || info.buffers || !info.nolog){fputs("Existing Kdebug buffer/consumer; refused.\n",stderr);return 2;}
    const size_t capacity=irq?524288:262144;struct VIMKDBuffer *records=calloc(capacity,sizeof(*records));
    if(!records)return fail("Kdebug result allocation");
    atexit(cleanup);signal(SIGINT,cancel);signal(SIGTERM,cancel);
    if(control(KERN_KDSETBUF,(int)capacity,NULL,NULL)){free(records);return fail("Kdebug buffer count");}
    owned=1;
    if(control(KERN_KDSETUP,0,NULL,NULL)){free(records);return fail("Kdebug setup");}
    const unsigned subclass=irq?DBG_MACH_EXCP_INTR:0x70;
    struct VIMKDFilter filter={0x20000,DBG_MACH,subclass,0,0}; // KDBG_SUBCLSTYPE
    size=sizeof(filter);
    if(control(KERN_KDSETREG,0,&filter,&size)){free(records);return fail("Kdebug PMAP filter");}
    if(control(KERN_KDENABLE,KDEBUG_ENABLE_TRACE,NULL,NULL)){free(records);return fail("Kdebug enable");}
    mach_timebase_info_data_t timebase;mach_timebase_info(&timebase);
    printf("READY %s capture numer=%u denom=%u start=%llu seconds=%u\n",kind,timebase.numer,timebase.denom,mach_absolute_time(),seconds);fflush(stdout);
    struct timespec pause={(time_t)seconds,0};while(!stop_requested && nanosleep(&pause,&pause) && errno==EINTR){}
    if(control(KERN_KDENABLE,0,NULL,NULL)){free(records);return fail("Kdebug stop");}
    size=sizeof(info);
    if(control(KERN_KDGETBUF,0,&info,&size)){free(records);return fail("Kdebug final info");}
    if(info.flags&0x8){free(records);fprintf(stderr,"INVALID %s capture: wrapped buffer\n",kind);return 1;}
    size=capacity*sizeof(*records);
    if(control(KERN_KDREADTR,0,records,&size)){free(records);return fail("Kdebug read");}
    // IMPORTANT: this period implementation accepts BYTES and returns RECORDS.
    if(size>capacity){free(records);fputs("INVALID Kdebug record count\n",stderr);return 1;}
    printf("%s timestamp cpu debugid arg1 arg2 arg3 arg4 tid\n",kind);
    for(size_t i=0;i<size;++i){
        struct VIMKDBuffer *r=&records[i];
        if((r->debugid&0xffff0000u)!=(0x01000000u|(subclass<<16)))continue;
        printf("%llu %u %x %llx %llx %llx %llx %llu\n",r->timestamp,r->cpu,r->debugid,
            (uint64_t)r->args[0],(uint64_t)r->args[1],(uint64_t)r->args[2],(uint64_t)r->args[3],(uint64_t)r->args[4]);
    }
    printf("END %s capture records=%lu end=%llu cancelled=%d\n",kind,size,mach_absolute_time(),stop_requested);
    free(records);cleanup();return 0;
}
