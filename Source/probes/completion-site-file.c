#include <stdio.h>
#include <stdlib.h>
#include "../compat/PVGMetalCompat/PVGCompletionSite.h"
/* Offline test: map __TEXT using its section VM offsets, not executable code. */
int main(int argc,char **argv) {
    if(argc!=2)return 64;
    FILE *f=fopen(argv[1],"rb");if(!f)return 1;
    fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    if(size<32 || size>64*1024*1024)return 2;
    uint8_t *bytes=malloc(size);if(fread(bytes,1,size,f)!=(size_t)size)return 3;fclose(f);
    struct mach_header_64 *h=(void *)bytes;
    if(h->magic!=MH_MAGIC_64 || h->sizeofcmds>(uint64_t)size-32)return 4;
    uint8_t *p=bytes+32;struct segment_command_64 *text=NULL;
    for(uint32_t i=0;i<h->ncmds;i++) {
        struct load_command *lc=(void *)p;if(lc->cmdsize<8 || p+lc->cmdsize>bytes+32+h->sizeofcmds)return 5;
        if(lc->cmd==LC_SEGMENT_64 && !strncmp(((struct segment_command_64 *)p)->segname,"__TEXT",16))text=(void *)p;
        p+=lc->cmdsize;
    }
    if(!text || text->vmsize>64*1024*1024)return 6;
    uint8_t *mapped=calloc(1,text->vmsize);
    memcpy(mapped,bytes,32+h->sizeofcmds);
    struct section_64 *s=(void *)(text+1);
    for(uint32_t i=0;i<text->nsects;i++) {
        if(strncmp(s[i].sectname,"__text",16))continue;
        if(s[i].offset+s[i].size>(uint64_t)size || s[i].addr<text->vmaddr || s[i].addr-text->vmaddr+s[i].size>text->vmsize)return 7;
        memcpy(mapped+s[i].addr-text->vmaddr,bytes+s[i].offset,s[i].size);
    }
    uint8_t *site=pvgFindCompletionSite((void *)mapped);
    if(!site){fprintf(stderr,"No unique completion site\n");return 8;}
    printf("PASS completion site at image-relative 0x%lx\n",(unsigned long)(site-mapped));
    memcpy(site,pvgNewCompletion,sizeof(pvgNewCompletion));
    if(pvgFindCompletionSite((void *)mapped)!=site)return 9;
    puts("PASS already-patched recognition");free(bytes);free(mapped);return 0;
}
