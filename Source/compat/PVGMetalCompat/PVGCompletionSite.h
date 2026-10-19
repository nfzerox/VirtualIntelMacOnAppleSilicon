#ifndef PVG_COMPLETION_SITE_H
#define PVG_COMPLETION_SITE_H
#include <mach-o/loader.h>
#include <stdint.h>
#include <string.h>

static const uint8_t pvgOldCompletion[] = {
    0x8b,0x4a,0x18,0x48,0x8b,0x72,0x20,0x48,0xc1,0xe6,0x20,0x48,0x09,0xce
};
static const uint8_t pvgNewCompletion[] = {
    0x48,0x8b,0x72,0x18,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90
};

/* A loaded, trusted native IOAccelerator image. Search its executable section,
 * never a fixed offset from an exported function. Exactly one match is required. */
static uint8_t *pvgFindCompletionSite(const struct mach_header_64 *header) {
    if (!header || header->magic != MH_MAGIC_64 || header->ncmds > 4096 ||
        header->sizeofcmds > 1024*1024) return NULL;
    const uint8_t *cursor=(const uint8_t *)(header+1);
    const uint8_t *end=cursor+header->sizeofcmds;
    uint8_t *site=NULL;
    unsigned matches=0;
    for (uint32_t n=0;n<header->ncmds;n++) {
        if ((size_t)(end-cursor)<sizeof(struct load_command)) return NULL;
        const struct load_command *lc=(const void *)cursor;
        if (lc->cmdsize<sizeof(*lc) || lc->cmdsize>(size_t)(end-cursor)) return NULL;
        if (lc->cmd==LC_SEGMENT_64 && lc->cmdsize>=sizeof(struct segment_command_64)) {
            const struct segment_command_64 *seg=(const void *)cursor;
            if (!strncmp(seg->segname,"__TEXT",16)) {
                if (seg->nsects>(lc->cmdsize-sizeof(*seg))/sizeof(struct section_64)) return NULL;
                intptr_t slide=(intptr_t)header-(intptr_t)seg->vmaddr;
                const struct section_64 *sec=(const void *)(seg+1);
                for(uint32_t s=0;s<seg->nsects;s++) {
                    if(strncmp(sec[s].sectname,"__text",16)) continue;
                    if(sec[s].size<sizeof(pvgOldCompletion) || sec[s].size>16*1024*1024) return NULL;
                    const uint8_t *start=(const uint8_t *)(uintptr_t)(sec[s].addr+slide);
                    for(uint64_t i=0;i<=sec[s].size-sizeof(pvgOldCompletion);i++) {
                        if(!memcmp(start+i,pvgOldCompletion,sizeof(pvgOldCompletion)) ||
                           !memcmp(start+i,pvgNewCompletion,sizeof(pvgNewCompletion))) {
                            site=(uint8_t *)(start+i); matches++;
                        }
                    }
                }
            }
        }
        cursor+=lc->cmdsize;
    }
    return matches==1 ? site : NULL;
}
#endif
