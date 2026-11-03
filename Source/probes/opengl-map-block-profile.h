/* Private, fail-CLOSED Mavericks laboratory profile. No kernel patch/KPI
 * bypass. Unknown kernels/map subclasses use the existing synchronous path.
 * Actual13F1911 kernel UUID8926E512-6714-34FC-B27C-6AE90EB51D5E disassembly
 * proves redirect reads/writes fRedirUPL+0x50 and can return0 when it is NULL.
 * Source XNU2422 and the exact400-byte method fingerprint qualify the layout.
 * Caller retains its UNIQUE map and serializes redirect/destruction. A real
 * non-NULL UPL proves block admission, NOT object isolation or GPU completion.
 */
#ifndef VIM_OPENGL_MAP_BLOCK_PROFILE_H
#define VIM_OPENGL_MAP_BLOCK_PROFILE_H
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#if defined(__LP64__)
struct VIMLegacyMapLayout {
    unsigned char object[sizeof(OSObject)];
    IOMemoryDescriptor *memory;
    IOMemoryMap *supermap;
    mach_vm_size_t offset;
    mach_vm_address_t address;
    mach_vm_size_t length;
    task_t task;
    void *address_map;
    IOOptionBits options;
    void *blocking_upl,*redirect_entry;
    IOMemoryDescriptor *owner;
    uint8_t user_unmap;
};
typedef char VIMLegacyMapSize[(sizeof(VIMLegacyMapLayout)==0x70)?1:-1];
typedef char VIMLegacyUPLOffset[(__builtin_offsetof(VIMLegacyMapLayout,blocking_upl)==0x50)?1:-1];
static bool VIMLegacyMapProfile(IOMemoryMap *mapping,IOMemoryDescriptor *memory,VIMLegacyMapLayout *out,
    const unsigned char **qualified_code=0)
{
    if(!mapping || !memory || !out)return false;
    const OSMetaClass *meta=mapping->getMetaClass();
    if(!meta || strcmp(meta->getClassName(),"IOMemoryMap") || meta->getClassSize()!=sizeof(*out))return false;
    // Resolve the virtual method through the compiler's own member-pointer
    // ABI, not a guessed vtable slot or a slid kernel address.
    typedef IOReturn (IOMemoryMap::*Redirect)(IOMemoryDescriptor *,IOOptionBits,mach_vm_size_t);
    Redirect method=static_cast<Redirect>(&IOMemoryMap::redirect);
    struct Member { intptr_t entry,adjustment; } member;
    typedef char MemberSize[(sizeof(method)==sizeof(member))?1:-1];(void)sizeof(MemberSize);
    memcpy(&member,&method,sizeof(member));
    if(member.adjustment || member.entry<=0 || !(member.entry&1) || member.entry>4096)return false;
    void *const *table=*reinterpret_cast<void *const *const *>(mapping);
    const unsigned char *code=static_cast<const unsigned char *>(table[(member.entry-1)/sizeof(void *)]);
    // Never cross an unknown text page. The actual qualified method and its
    // alignment padding are400bytes entirely inside one4KiB kernel page.
    if(!code || (reinterpret_cast<uintptr_t>(code)&4095)+400>4096)return false;
    uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned i=0;i<400;++i)hash=(hash^code[i])*UINT64_C(1099511628211);
    if(hash!=UINT64_C(0x419385bb9ac35b17))return false;
    memcpy(out,static_cast<const void *>(mapping),sizeof(*out));
    bool valid=!out->supermap && out->memory==memory && out->memory==mapping->getMemoryDescriptor() &&
        out->address && out->address==mapping->getAddress() && out->length==mapping->getLength() &&
        out->task==mapping->getAddressTask() && out->options==mapping->getMapOptions() &&
        out->address_map && (out->options&kIOMapUnique);
    if(valid && qualified_code)*qualified_code=code;
    return valid;
}
static IOReturn VIMLegacyMapBlock(IOMemoryMap *mapping,IOMemoryDescriptor *memory)
{
    VIMLegacyMapLayout before,after;
    if(!VIMLegacyMapProfile(mapping,memory,&before) || before.blocking_upl)return kIOReturnUnsupported;
    IOReturn error=mapping->redirect(NULL,kIOMapDefaultCache,0ULL);
    if(error)return error;
    return VIMLegacyMapProfile(mapping,memory,&after) && after.blocking_upl?kIOReturnSuccess:kIOReturnCannotLock;
}
/* EXCLUSIVELY owned standalone-lab map only, never an exposed/source map.
 * Transfer this ONE retained UPL out of the mapping before CPU access may
 * fault. Its later public ubc_upl_commit retires it without IOMemory's global
 * recursive lock; map destruction must not commit/deallocate it a second time.
 * The qualified layout is writable OBJECT state, never patched kernel code.
 */
static void *VIMLegacyMapTakeBlockingUPL(IOMemoryMap *mapping,IOMemoryDescriptor *memory)
{
    VIMLegacyMapLayout state;
    if(!VIMLegacyMapProfile(mapping,memory,&state) || !state.blocking_upl)return 0;
    void *empty=0;
    memcpy(reinterpret_cast<unsigned char *>(mapping)+__builtin_offsetof(VIMLegacyMapLayout,blocking_upl),
        &empty,sizeof(empty));
    return state.blocking_upl;
}
#else
// No 32-bit kernel layout is qualified. Decline before inspecting private
// memory; callers retain their established synchronous ownership path.
static IOReturn VIMLegacyMapBlock(IOMemoryMap *,IOMemoryDescriptor *)
{ return kIOReturnUnsupported; }
static void *VIMLegacyMapTakeBlockingUPL(IOMemoryMap *,IOMemoryDescriptor *)
{ return 0; }
#endif
#endif
