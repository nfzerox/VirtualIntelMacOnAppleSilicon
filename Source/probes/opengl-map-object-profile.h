/* PRIVATE guest laboratory profile, not a general VM introspection API.
 * Exact13F1911 method disassembly + live kernel CTF independently qualify these
 * layouts. Unknown code declines BEFORE traversing a private pointer/lock.
 * Public VM region IDs are NOT used as capabilities or alias identities.
 * Caller retains the prepared descriptor AND its unique kernel mapping until
 * the admission key is released; the named entry owns the underlying object.
 * No leaked convert_port_entry_to_object reference or private KPI lookup.
 */
#ifndef VIM_OPENGL_MAP_OBJECT_PROFILE_H
#define VIM_OPENGL_MAP_OBJECT_PROFILE_H
#include "opengl-map-block-profile.h"
#include <kern/locks.h>
#if defined(__LP64__)
struct VIMLegacyNamedObject {
    uint64_t lock[2]; // SDK lock type opaque; live CTF confirms16bytes
    void *object;
    uint64_t offset,length,data_offset;
    uint32_t protection,references,flags;
};
struct VIMLegacyObjectPrefix {
    unsigned char memq[16];
    uint64_t lock[2]; // address of EXISTING lock, never copied/initialized
    uint64_t length;
    void *hint;
    uint32_t references,resident,wired,reusable;
    void *copy,*shadow;
    unsigned char remaining[0x78-0x50];
    uint32_t flags;
};
typedef char VIMNamedBackingOffset[(__builtin_offsetof(VIMLegacyNamedObject,object)==0x10)?1:-1];
typedef char VIMNamedFlagsOffset[(__builtin_offsetof(VIMLegacyNamedObject,flags)==0x38)?1:-1];
typedef char VIMObjectSizeOffset[(__builtin_offsetof(VIMLegacyObjectPrefix,length)==0x20)?1:-1];
typedef char VIMObjectFlagsOffset[(__builtin_offsetof(VIMLegacyObjectPrefix,flags)==0x78)?1:-1];

static void *VIMLegacyWholeObject(IOMemoryMap *mapping,IOMemoryDescriptor *memory)
{
    VIMLegacyMapLayout map;const unsigned char *redirect=0;
    if(!VIMLegacyMapProfile(mapping,memory,&map,&redirect) || map.blocking_upl || map.offset ||
       !map.length || (map.length&4095) || map.length!=memory->getLength() ||
       !OSDynamicCast(IOGeneralMemoryDescriptor,memory))return 0;
    // The qualified redirect has a direct rel32 call at+a6. Its target is the
    // exact memory_object_iopl_request implementation, independently inspected
    // with scripts/inspect-opengl-map-profile.py --symbol. No scanned symbols,
    // hardcoded slid address, call to a discovered function, or text writes.
    if(redirect[0xa6]!=0xe8)return 0;
    int32_t displacement;memcpy(&displacement,redirect+0xa7,sizeof(displacement));
    const unsigned char *request=reinterpret_cast<const unsigned char *>(
        reinterpret_cast<uintptr_t>(redirect+0xab)+displacement);
    if((reinterpret_cast<uintptr_t>(request)&4095)+672>4096)return 0;
    uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned i=0;i<672;++i)hash=(hash^request[i])*UINT64_C(1099511628211);
    if(hash!=UINT64_C(0x679cbace3b927ae7))return 0;
    // A unique retained map keeps this descriptor's established _memEntry
    // alive. Never create/mutate a memory entry by poking private fields.
    const unsigned char *descriptor=reinterpret_cast<const unsigned char *>(memory);
    void *port=0;memcpy(&port,descriptor+0x28,sizeof(port));
    if(!port || (reinterpret_cast<uintptr_t>(port)&7))return 0;
    const unsigned char *port_bytes=static_cast<const unsigned char *>(port);
    uint32_t bits;memcpy(&bits,port_bytes,sizeof(bits));
    if((bits&0x80000fffU)!=0x8000001cU)return 0; // active IKOT_NAMED_ENTRY
    VIMLegacyNamedObject *entry=0;memcpy(&entry,port_bytes+0x60,sizeof(entry));
    if(!entry || (reinterpret_cast<uintptr_t>(entry)&7))return 0;
    void *key=0;
    lck_mtx_lock(reinterpret_cast<lck_mtx_t *>(entry->lock));
    if(!(entry->flags&14) && entry->object && !entry->offset && !entry->data_offset &&
       entry->length==map.length && (entry->protection&3)==3) {
        VIMLegacyObjectPrefix *object=static_cast<VIMLegacyObjectPrefix *>(entry->object);
        lck_rw_lock_shared(reinterpret_cast<lck_rw_t *>(object->lock));
        // Internal, alive, true-shared; no device/pageout/terminating object.
        // true_share explicitly forbids coalescing in the period XNU. Whole
        // object coverage avoids blocking unrelated neighbours or shadows.
        if((object->flags&0x04110000U)==0x04110000U && !(object->flags&0x080c0000U) &&
           object->references && object->length==map.length && !object->copy && !object->shadow)
            key=object;
        lck_rw_unlock_shared(reinterpret_cast<lck_rw_t *>(object->lock));
    }
    lck_mtx_unlock(reinterpret_cast<lck_mtx_t *>(entry->lock));
    return key; // borrowed ONLY while the retained descriptor/map own it
}
#else
static void *VIMLegacyWholeObject(IOMemoryMap *,IOMemoryDescriptor *)
{ return 0; }
#endif
#endif
