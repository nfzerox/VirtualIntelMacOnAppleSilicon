#pragma once
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <libkern/OSByteOrder.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
// Tiger lacks kern.osversion. Compare the live KERN_VERSION to the installed
// kernel's actual _version symbol instead. Native Mach-O declarations/widths,
// not an OS-build-to-Darwin-version table or a fixed kernel/file address.
typedef struct {uint32_t strx;uint8_t type,section;uint16_t description;uint32_t value;} VIMTigerNList32;
_Static_assert(sizeof(VIMTigerNList32)==12,"native32-bit nlist width");
static inline int VIMTigerReadKernelAt(int fd,void *buffer,size_t size,uint64_t offset,uint64_t limit) {
    if(offset>limit||size>limit-offset)return 0;
    unsigned char *bytes=buffer;
    while(size){ssize_t read=pread(fd,bytes,size,(off_t)offset);
        if(read<0&&errno==EINTR)continue;if(read<=0)return 0;
        bytes+=read;offset+=(uint64_t)read;size-=(size_t)read;}
    return 1;
}
static inline int VIMTigerKernelVersionSymbol(const char *path,char *version,size_t capacity) {
    int fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return 0;
    fcntl(fd,F_SETFD,FD_CLOEXEC);
    struct stat st;int ok=0;unsigned char *commands=NULL,*strings=NULL;VIMTigerNList32 *symbols=NULL;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<28||st.st_size>128*1024*1024)goto done;
    uint64_t length=(uint64_t)st.st_size,base=0,slice=length;uint32_t magic=0;
    if(!VIMTigerReadKernelAt(fd,&magic,sizeof(magic),0,length))goto done;
    if(magic==FAT_CIGAM){
        struct fat_header fat;
        if(!VIMTigerReadKernelAt(fd,&fat,sizeof(fat),0,length))goto done;
        uint32_t count=OSSwapBigToHostInt32(fat.nfat_arch),matches=0,index;
        if(!count||count>32)goto done;
        for(index=0;index<count;index++){
            struct fat_arch arch;
            if(!VIMTigerReadKernelAt(fd,&arch,sizeof(arch),sizeof(fat)+(uint64_t)index*sizeof(arch),length))goto done;
            if(OSSwapBigToHostInt32(arch.cputype)!=CPU_TYPE_I386)continue;
            base=OSSwapBigToHostInt32(arch.offset);slice=OSSwapBigToHostInt32(arch.size);matches++;
        }
        if(matches!=1||base>length||slice>length-base)goto done;
    }else if(magic!=MH_MAGIC)goto done;
    struct mach_header header;
    if(!VIMTigerReadKernelAt(fd,&header,sizeof(header),base,base+slice)||header.magic!=MH_MAGIC||
       header.cputype!=CPU_TYPE_I386||header.filetype!=MH_EXECUTE||!header.ncmds||header.ncmds>128||
       !header.sizeofcmds||header.sizeofcmds>1024*1024)goto done;
    commands=malloc(header.sizeofcmds);if(!commands)goto done;
    if(!VIMTigerReadKernelAt(fd,commands,header.sizeofcmds,base+sizeof(header),base+slice))goto done;
    struct symtab_command table;memset(&table,0,sizeof(table));size_t cursor=0;unsigned index,tableCount=0;
    for(index=0;index<header.ncmds;index++){
        struct load_command command;
        if(cursor>header.sizeofcmds||sizeof(command)>header.sizeofcmds-cursor)goto done;
        memcpy(&command,commands+cursor,sizeof(command));
        if(command.cmdsize<sizeof(command)||command.cmdsize%4||command.cmdsize>header.sizeofcmds-cursor)goto done;
        if(command.cmd==LC_SYMTAB){if(command.cmdsize!=sizeof(table))goto done;memcpy(&table,commands+cursor,sizeof(table));tableCount++;}
        cursor+=command.cmdsize;
    }
    if(cursor!=header.sizeofcmds||tableCount!=1||!table.nsyms||table.nsyms>500000||!table.strsize||table.strsize>16*1024*1024)goto done;
    size_t symbolBytes=(size_t)table.nsyms*sizeof(*symbols);
    symbols=malloc(symbolBytes);strings=malloc(table.strsize);if(!symbols||!strings)goto done;
    if(!VIMTigerReadKernelAt(fd,symbols,symbolBytes,base+table.symoff,base+slice)||
       !VIMTigerReadKernelAt(fd,strings,table.strsize,base+table.stroff,base+slice))goto done;
    uint32_t address=0;unsigned matches=0;
    for(index=0;index<table.nsyms;index++){
        VIMTigerNList32 *symbol=&symbols[index];
        if((symbol->type&N_STAB)||(symbol->type&N_TYPE)!=N_SECT||symbol->strx>=table.strsize||
           table.strsize-symbol->strx<sizeof("_version"))continue;
        if(!memcmp(strings+symbol->strx,"_version",sizeof("_version"))){address=symbol->value;matches++;}
    }
    if(matches!=1||!address)goto done;
    cursor=0;matches=0;
    for(index=0;index<header.ncmds;index++){
        struct load_command command;memcpy(&command,commands+cursor,sizeof(command));
        if(command.cmd==LC_SEGMENT){
            struct segment_command segment;
            if(command.cmdsize<sizeof(segment))goto done;
            memcpy(&segment,commands+cursor,sizeof(segment));
            if(address>=segment.vmaddr&&(uint64_t)address-segment.vmaddr<segment.filesize){
                uint64_t delta=(uint64_t)address-segment.vmaddr;
                size_t count=capacity;
                if(count>segment.filesize-delta)count=(size_t)(segment.filesize-delta);
                if(!count||!VIMTigerReadKernelAt(fd,version,count,base+segment.fileoff+delta,base+slice)||!memchr(version,0,count))goto done;
                matches++;
            }
        }
        cursor+=command.cmdsize;
    }
    ok=matches==1;
done:
    free(symbols);free(strings);free(commands);close(fd);return ok;
}
static inline int VIMTigerRunningKernelMatchesFile(void) {
    char live[512]={0},installed[512]={0};size_t size=sizeof(live);int mib[]={CTL_KERN,KERN_VERSION};
    return !sysctl(mib,2,live,&size,NULL,0)&&size>0&&size<=sizeof(live)&&memchr(live,0,size)&&
        VIMTigerKernelVersionSymbol("/mach_kernel",installed,sizeof(installed))&&!strcmp(live,installed);
}
#endif
