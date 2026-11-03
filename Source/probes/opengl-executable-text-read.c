/* Read-only, explicit-PID executable TEXT survey for period OS guests without
 * a debugger. Normal task_for_pid authorization is required. Never reads heap,
 * patches a task, changes protections or modifies the original executable.
 * Keep output in private scratch; it is not a redistributable build input.
 */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/loader.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
static int read_task(task_t task,uint64_t address,void *out,size_t bytes) {
    mach_vm_size_t actual=0;
    return mach_vm_read_overwrite(task,address,bytes,(mach_vm_address_t)out,&actual)==KERN_SUCCESS&&actual==bytes;
}
int main(int argc,char **argv) {
    if(argc!=3)return 64;
    char *end=NULL;long pid=strtol(argv[1],&end,10);
    if(!*argv[1]||*end||pid<=1||pid>INT32_MAX||argv[2][0]!='/')return 64;
    task_t task=MACH_PORT_NULL;
    kern_return_t result=task_for_pid(mach_task_self(),(pid_t)pid,&task);
    if(result){fprintf(stderr,"task_for_pid: %s\n",mach_error_string(result));return 77;}
    struct task_dyld_info info={0};mach_msg_type_number_t count=TASK_DYLD_INFO_COUNT;
    struct {uint32_t version,count;uint64_t images;} images={0};
    struct {uint64_t header,path,date;} main_image={0};
    struct mach_header_64 header={0};
    int status=65;
    if(task_info(task,TASK_DYLD_INFO,(task_info_t)&info,&count)||
       !read_task(task,info.all_image_info_addr,&images,sizeof(images))||
       !images.count||images.count>4096||!images.images)goto done;
    // Period dyld's image array is not guaranteed to put the executable first.
    // Inspect only Mach-O headers, then read TEXT for the one MH_EXECUTE image.
    int found=0;
    for(uint32_t i=0;i<images.count;i++){
        if(!read_task(task,images.images+(uint64_t)i*sizeof(main_image),&main_image,sizeof(main_image))||
           !read_task(task,main_image.header,&header,sizeof(header)))goto done;
        if(header.magic==MH_MAGIC_64&&header.cputype==CPU_TYPE_X86_64&&header.filetype==MH_EXECUTE){found=1;break;}
    }
    if(!found||!header.ncmds||header.ncmds>1024||header.sizeofcmds>65536){
        fputs("Executable header validation failed.\n",stderr);goto done;}
    size_t bytes=sizeof(header)+header.sizeofcmds;
    unsigned char *commands=malloc(bytes);
    if(!commands)goto done;
    if(!read_task(task,main_image.header,commands,bytes)){free(commands);goto done;}
    uint64_t length=0;size_t offset=sizeof(header);
    for(uint32_t i=0;i<header.ncmds;i++){
        if(offset>bytes||bytes-offset<sizeof(struct load_command))break;
        struct load_command *command=(void *)(commands+offset);
        if(command->cmdsize<sizeof(*command)||command->cmdsize>bytes-offset)break;
        if(command->cmd==LC_SEGMENT_64&&command->cmdsize>=sizeof(struct segment_command_64)){
            struct segment_command_64 *segment=(void *)command;
            if(!strncmp(segment->segname,"__TEXT",16)&&!segment->fileoff&&
               segment->filesize<=segment->vmsize&&segment->vmsize<=32*1024*1024&&
               (segment->initprot&(VM_PROT_READ|VM_PROT_EXECUTE))==(VM_PROT_READ|VM_PROT_EXECUTE)&&
               !(segment->initprot&VM_PROT_WRITE))length=segment->vmsize;
        }
        offset+=command->cmdsize;
    }
    free(commands);if(offset!=bytes||length<bytes){fprintf(stderr,"Executable TEXT segment validation failed: load command extent=%lu/%lu, length=%llu\n",(unsigned long)offset,(unsigned long)bytes,(unsigned long long)length);goto done;}
    unsigned char *text=malloc((size_t)length);if(!text)goto done;
    if(!read_task(task,main_image.header,text,(size_t)length)){free(text);goto done;}
    int fd=open(argv[2],O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0){perror("new output");free(text);goto done;}
    size_t written=0;
    while(written<length){ssize_t n=write(fd,text+written,(size_t)length-written);
        if(n<0&&errno==EINTR)continue;if(n<=0)break;written+=(size_t)n;}
    free(text);int closed=close(fd);
    if(written==length&&!closed){printf("TEXT header=0x%llx bytes=%llu\n",(unsigned long long)main_image.header,(unsigned long long)length);status=0;}
done:
    mach_port_deallocate(mach_task_self(),task);
    if(status)fputs("No complete executable TEXT survey.\n",stderr);
    return status;
}
