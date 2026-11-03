/* Own-process native Tiger protection survey. No code bytes are changed and
 * no other task's mappings are touched. Test only the selected CGL text page,
 * then restore its actual original permissions, never invented RX defaults. */
#include <mach/mach.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    setvbuf(stdout,NULL,_IOLBF,0);
    void *image=dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL",RTLD_LAZY|RTLD_LOCAL);
    void *function=image?dlsym(image,"CGLCreateContext"):NULL;
    if(!function)return 1;
    vm_address_t page=(vm_address_t)function&~((vm_address_t)vm_page_size-1),address=page;
    vm_size_t size=0;vm_region_basic_info_data_t info={0};
    mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT;mach_port_t object=0;
    kern_return_t error=vm_region(mach_task_self(),&address,&size,VM_REGION_BASIC_INFO,
        (vm_region_info_t)&info,&count,&object);
    printf("region=%d address=%x size=%x protection=%x maximum=%x shared=%d\n",error,
        (unsigned)address,(unsigned)size,(unsigned)info.protection,(unsigned)info.max_protection,(int)info.shared);
    if(object)mach_port_deallocate(mach_task_self(),object);
    if(error||address>page||size<vm_page_size||page-address>size-vm_page_size)return 1;
    error=vm_protect(mach_task_self(),page,vm_page_size,FALSE,VM_PROT_COPY|VM_PROT_READ|VM_PROT_WRITE);
    printf("private_copy_RW=%d\n",error);
    if(error)return 1;
    kern_return_t restore=vm_protect(mach_task_self(),page,vm_page_size,FALSE,info.protection);
    printf("restore_original=%d\n",restore);
    return restore?1:0;
}
