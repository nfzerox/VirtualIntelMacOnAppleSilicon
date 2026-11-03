/* Read-only Darwin9 WindowServer compositor metadata. This research probe
 * never suspends a task or writes memory. Require the named server and the
 * measured 9J3050 i386 CoreGraphics image at the same shared-cache address
 * before reading only the five explicit configuration/counter words below.
 * No arbitrary address argument, window/document contents or graphics buffers. */
#include <ApplicationServices/ApplicationServices.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <sys/sysctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#if !defined(__i386__)
#error This research contract is qualified only for native i386 9J3050.
#endif
static const unsigned char expected[16]={0xa2,0xd7,0x66,0xbf,0x70,0x8e,0x1a,0xd7,
    0xfc,0x86,0x1d,0x35,0xad,0x23,0xe2,0x6d};
static int qualified(const void *bytes,size_t size)
{
    if(size<sizeof(struct mach_header))return 0;
    const struct mach_header *h=bytes;
    if(h->magic!=MH_MAGIC||h->sizeofcmds>size-sizeof(*h))return 0;
    const unsigned char *p=(const void *)(h+1),*end=p+h->sizeofcmds;
    for(unsigned i=0;i<h->ncmds;++i){
        if(end-p<sizeof(struct load_command))return 0;
        const struct load_command *c=(const void *)p;
        if(c->cmdsize<sizeof(*c)||c->cmdsize>(size_t)(end-p))return 0;
        if(c->cmd==LC_UUID&&c->cmdsize==sizeof(struct uuid_command)&&
           !memcmp(((const struct uuid_command *)c)->uuid,expected,16))return 1;
        p+=c->cmdsize;
    }
    return 0;
}
int main(int argc,char **argv)
{
    if(argc!=2||geteuid()!=0)return 64;
    char *end=0;long pid=strtol(argv[1],&end,10);
    if(!end||*end||pid<2)return 64;
    struct kinfo_proc proc;size_t size=sizeof(proc);int mib[]={CTL_KERN,KERN_PROC,KERN_PROC_PID,(int)pid};
    if(sysctl(mib,4,&proc,&size,NULL,0)||size!=sizeof(proc)||strcmp(proc.kp_proc.p_comm,"WindowServer"))return 77;
    vm_address_t base=0;
    for(unsigned i=0;i<_dyld_image_count();++i){
        const char *path=_dyld_get_image_name(i);
        if(path&&strstr(path,"/CoreGraphics.framework/")){
            const struct mach_header *h=_dyld_get_image_header(i);
            if(h->magic==MH_MAGIC&&h->sizeofcmds<65536&&qualified(h,sizeof(*h)+h->sizeofcmds))base=(vm_address_t)h;
        }
    }
    if(!base)return 77;
    task_t task=MACH_PORT_NULL;kern_return_t k=task_for_pid(mach_task_self(),(int)pid,&task);
    if(k){fprintf(stderr,"task_for_pid: %s\n",mach_error_string(k));return 1;}
    unsigned char image[8192];vm_size_t got=0;
    k=vm_read_overwrite(task,base,sizeof(image),(vm_address_t)image,&got);
    if(k||got!=sizeof(image)||!qualified(image,sizeof(image))){
        fprintf(stderr,"Remote CoreGraphics did not match; no globals read.\n");
        mach_port_deallocate(mach_task_self(),task);return 77;
    }
    /* The shared cache independently relocates DATA; header+original-RVA is
     * valid for TEXT, not for these globals. Use the qualified remote load
     * command and the original image's DATA vmaddr instead. */
    vm_address_t data=0;const struct mach_header *remote=(const void *)image;
    const struct load_command *command=(const void *)(remote+1);
    for(unsigned i=0;i<remote->ncmds;++i){
        if(command->cmd==LC_SEGMENT&&command->cmdsize>=sizeof(struct segment_command)){
            const struct segment_command *s=(const void *)command;
            if(!strcmp(s->segname,"__DATA")&&s->vmsize==0x22000)data=s->vmaddr;
        }
        command=(const void *)((const unsigned char *)command+command->cmdsize);
    }
    if(!data)return 77;
    const struct {const char *name;unsigned rva;} words[]={
        {"minimum-depth",0x6a1320},{"tile-height",0x6a1324},{"tile-width",0x6a1328},
        {"display-context-count",0x6bf32c},{"cgls-initialized",0x6bf300}};
    for(unsigned i=0;i<sizeof(words)/sizeof(words[0]);++i){
        unsigned value=0;got=0;k=vm_read_overwrite(task,data+words[i].rva-0x6a1000,sizeof(value),(vm_address_t)&value,&got);
        if(k||got!=sizeof(value)){fprintf(stderr,"%s unreadable\n",words[i].name);return 2;}
        /* The last word is a private state pointer: disclose presence only. */
        printf("%s=%u\n",words[i].name,i==4?value!=0:value);
    }
    mach_port_deallocate(mach_task_self(),task);return 0;
}
