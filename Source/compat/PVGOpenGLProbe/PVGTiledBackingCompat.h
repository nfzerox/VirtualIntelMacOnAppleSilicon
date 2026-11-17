#pragma once
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <os/log.h>
#include <fcntl.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

// Evidence for post-boot verification: WindowServer writes this boot's session
// UUID here once the option is on. The app lifts its 2048-pixel display limit
// only for a guest whose verification found it (guest-install-verification.h).
#define PVG_PRIVATE_WINDOW_TILES_MARKER "/private/var/tmp/com.pvgbackport.private-window-tiles"

// Sonoma and later x86_64 SkyLight draws a window of at least 1024 x 1024 pixels
// in 1024-pixel tiles, each a no-copy texture over the window's buffer
// (newTiledTextureWithDescriptor:offset:bytesPerRow:). The paravirtual plugin
// (and Apple's host framework) count every tile row's full stride, so they
// refuse the bottom row's tiles after the first. SkyLight skips refused tiles
// but indexes its list unchecked, so with three or more tiles across (a display
// wider than 2048 pixels) WindowServer dies on freed memory. See reims-vgpu's
// report, "x86-64 macOS guests: displays wider than 2048 pixels".
//
// SkyLight's own client-storage debug option (0x800000ee on, 0x800000ef off)
// makes every tile a private texture filled by a blit instead, the path arm64
// SkyLight always takes, and nothing is refused. Only WindowServer reads it.
// The option's flag is found from MetalContextSetDebugOptions' code: the
// byte stored on each constant's "equal" path (0 for off, 1 for on). No
// build offsets: a SkyLight without that shape is left as it is.
static const struct mach_header_64 *pvgSkyLightImage(intptr_t *slide) {
    for(uint32_t i=0,count=_dyld_image_count();i<count;i++) {
        const char *name=_dyld_get_image_name(i);
        const char *tail=name?strrchr(name,'/'):NULL;
        if(tail&&!strcmp(tail,"/SkyLight")&&strstr(name,"/SkyLight.framework/")) {
            *slide=_dyld_get_image_vmaddr_slide(i);
            return (const struct mach_header_64 *)_dyld_get_image_header(i);
        }
    }
    return NULL;
}

static BOOL pvgSkyLightRange(const struct mach_header_64 *header,intptr_t slide,BOOL writable,
                             const char *section,uint8_t **start,uint8_t **end,uintptr_t address) {
    if(header->magic!=MH_MAGIC_64)return NO;
    const uint8_t *cursor=(const uint8_t *)(header+1);
    for(uint32_t i=0;i<header->ncmds;i++) {
        const struct load_command *command=(const struct load_command *)cursor;
        if(command->cmd==LC_SEGMENT_64) {
            const struct segment_command_64 *segment=(const struct segment_command_64 *)command;
            if(section) {
                const struct section_64 *sections=(const struct section_64 *)(segment+1);
                for(uint32_t s=0;s<segment->nsects;s++)
                    if(!strncmp(sections[s].segname,"__TEXT",16)&&!strncmp(sections[s].sectname,section,16)) {
                        *start=(uint8_t *)(sections[s].addr+slide);*end=*start+sections[s].size;return YES;
                    }
            } else if(writable&&(segment->initprot&VM_PROT_WRITE)&&
                      address>=segment->vmaddr+slide&&address<segment->vmaddr+slide+segment->vmsize)
                return YES;
        }
        cursor+=command->cmdsize;
    }
    return NO;
}

// movb $value, disp32(%rip): C6 05 disp32 value. Returns the stored-to address.
static uintptr_t pvgRIPByteStore(const uint8_t *instruction,uint8_t value) {
    if(instruction[0]!=0xC6||instruction[1]!=0x05||instruction[6]!=value)return 0;
    int32_t displacement;memcpy(&displacement,instruction+2,4);
    return (uintptr_t)(instruction+7)+(intptr_t)displacement;
}

static void pvgEnableSkyLightClientStorage(void) {
    if(NSProcessInfo.processInfo.operatingSystemVersion.majorVersion<14)return; // Ventura and earlier keep full Retina as measured
    if(strcmp(getprogname(),"WindowServer"))return;
    os_log_t log=os_log_create("com.pvgbackport.metal-compat","tiles");
    intptr_t slide=0;const struct mach_header_64 *header=pvgSkyLightImage(&slide);
    uint8_t *start=NULL,*end=NULL;
    if(!header||!pvgSkyLightRange(header,slide,NO,"__text",&start,&end,0)||end-start<16) {
        os_log_error(log,"SkyLight text not found; window tiles unchanged");return;
    }
    // Each option constant is compared, then branched on (je/jne, short or
    // near). Follow the "equal" path (branch target for je, fall-through for
    // jne) to its first rip-relative byte store: "off" must store 0 and "on"
    // 1, both to the same flag. Builds differ in layout, not in this shape.
    uintptr_t flag=0;BOOL ambiguous=NO,sawOn=NO,sawOff=NO;
    for(uint8_t *p=start;p+10<=end&&!ambiguous;p++) {
        BOOL on=p[0]==0xEE,off=p[0]==0xEF;
        if((!on&&!off)||p[1]||p[2]||p[3]!=0x80)continue;
        const uint8_t *branch=p+4,*equal=NULL;
        if(branch[0]==0x74||branch[0]==0x75) {
            const uint8_t *next=branch+2;
            equal=branch[0]==0x74?next+(int8_t)branch[1]:next;
        } else if(branch[0]==0x0F&&(branch[1]==0x84||branch[1]==0x85)) {
            int32_t displacement;memcpy(&displacement,branch+2,4);const uint8_t *next=branch+6;
            equal=branch[1]==0x84?next+displacement:next;
        }
        if(!equal||equal<start||equal+40>end)continue;
        uintptr_t target=0;
        for(const uint8_t *q=equal;q<equal+32&&!target;q++)
            if(q[0]==0xC6&&q[1]==0x05)target=pvgRIPByteStore(q,on?1:0);
        if(!target)continue;
        if(flag&&flag!=target)ambiguous=YES;
        flag=target;sawOn|=on;sawOff|=off;
    }
    if(!flag||ambiguous||!sawOn||!sawOff||!pvgSkyLightRange(header,slide,YES,NULL,NULL,NULL,flag)) {
        os_log_error(log,"SkyLight client-storage option not recognized; window tiles unchanged");return;
    }
    *(volatile uint8_t *)flag=1;
    os_log(log,"SkyLight window tiles use private textures (client-storage option on)");
    char session[64]={0};size_t length=sizeof(session)-1;
    if(sysctlbyname("kern.bootsessionuuid",session,&length,NULL,0)||!session[0])return;
    unlink(PVG_PRIVATE_WINDOW_TILES_MARKER);
    int fd=open(PVG_PRIVATE_WINDOW_TILES_MARKER,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0644);
    if(fd<0)return;
    ssize_t written=write(fd,session,strlen(session));(void)written;close(fd);
}
