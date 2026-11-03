/* Isolated native9J3050 CGLS library admission, not a desktop proof. The
 * measured private cdecl call touches only this probe's CoreGraphics state.
 * Exact Mach-O UUID/text bounds guard its address; never patches an image,
 * attaches to WindowServer or creates a display/primary surface. */
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if !defined(__i386__)
#error This private contract is measured only on native i386 9J3050.
#endif
int main(void)
{
    setbuf(stdout,NULL);
    static const unsigned char expected[16]={0xa2,0xd7,0x66,0xbf,0x70,0x8e,0x1a,0xd7,
        0xfc,0x86,0x1d,0x35,0xad,0x23,0xe2,0x6d};
    uintptr_t initialize=0;
    for(uint32_t i=0;i<_dyld_image_count();++i){
        const char *path=_dyld_get_image_name(i);
        if(!path||!strstr(path,"/CoreGraphics.framework/"))continue;
        const struct mach_header *header=_dyld_get_image_header(i);
        if(header->magic!=MH_MAGIC||header->sizeofcmds>65536)return 77;
        const struct load_command *command=(const void *)(header+1);
        const unsigned char *end=(const unsigned char *)command+header->sizeofcmds;
        // This exact original image has __TEXT vmaddr0. Leopard's shared
        // cache rebases the loaded segment VM addresses themselves; its
        // dyld slide can therefore be0 while the image lives high in memory.
        int matched=0,in_text=0;uintptr_t address=(uintptr_t)header+0xfad0b;
        for(uint32_t j=0;j<header->ncmds;++j){
            if((const unsigned char *)command>end-sizeof(*command)||command->cmdsize<sizeof(*command)||
               command->cmdsize>(size_t)(end-(const unsigned char *)command))return 77;
            if(command->cmd==LC_UUID&&command->cmdsize==sizeof(struct uuid_command))
                matched=!memcmp(((const struct uuid_command *)command)->uuid,expected,16);
            if(command->cmd==LC_SEGMENT&&command->cmdsize>=sizeof(struct segment_command)){
                const struct segment_command *segment=(const void *)command;
                uintptr_t start=_dyld_get_image_vmaddr_slide(i)+segment->vmaddr;
                if(!strcmp(segment->segname,"__TEXT")&&address>=start&&address-start<segment->vmsize)in_text=1;
            }
            command=(const void *)((const unsigned char *)command+command->cmdsize);
        }
        static const unsigned char prologue[]={0x55,0x89,0xe5,0x57,0x56,0x53,0xe8};
        if(matched&&in_text&&!memcmp((const void *)address,prologue,sizeof(prologue)))initialize=address;
    }
    if(!initialize){puts("Unqualified native CoreGraphics; no private call made.");return 77;}
    io_service_t accelerator=0;UInt32 index=0;
    IOReturn status=IOAccelFindAccelerator(CGDisplayIOServicePort(CGMainDisplayID()),&accelerator,&index);
    if(status||!accelerator||index>255){printf("find=%x index=%u\n",status,(unsigned)index);return 1;}
    unsigned char slot=(unsigned char)index;
    int (*call)(const io_service_t *,const unsigned char *,unsigned,void *)=(void *)initialize;
    int result=call(&accelerator,&slot,1,NULL);
    printf("native-cgls-initialize=%d supplied-devices=1 slot=%u\n",result,(unsigned)slot);
    IOObjectRelease(accelerator);return result?2:0;
}
