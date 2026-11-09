/* Private local-research capability transport; not an upstream device ABI.
 * Export existing anonymous VRAM/main RAM pages, never task ports or a file mapping.
 * Both peers require the exact other PID and effective UID. One mapping is
 * retained until all renderer GL resources using it have been destroyed.
 * This only shares storage: CPU/GPU ownership is a separate protocol. */
#ifndef VIM_VRAM_MEMORY_H
#define VIM_VRAM_MEMORY_H
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <servers/bootstrap.h>
#include <bsm/libbsm.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#define VIM_VRAM_MEMORY_LIMIT (16ull*1024*1024*1024)
/* Main guest RAM is exported whole (with its guest-physical ranges), so it
 * must not inherit the VRAM bound: guests above 16 GB otherwise fail to boot.
 * 1 TiB matches the default 40-bit guest physical address width; this is a
 * sanity bound only, since the export shares existing pages and copies none. */
#define VIM_SHARED_RAM_MEMORY_LIMIT (1ull<<40)
#define VIMSharedMemoryLimit(region_count) ((region_count)?VIM_SHARED_RAM_MEMORY_LIMIT:VIM_VRAM_MEMORY_LIMIT)
#define VIM_VRAM_MEMORY_REQUEST 0x56494d56
#define VIM_SHARED_RAM_REGIONS 16
struct VIMSharedRAMRange { uint64_t guest,offset,bytes; };
struct VIMVRAMMemoryReply {
    mach_msg_header_t header;
    mach_msg_body_t body;
    mach_msg_port_descriptor_t memory;
    uint64_t bytes;
    uint32_t version,reserved;
    uint32_t region_count,padding;
    struct VIMSharedRAMRange regions[VIM_SHARED_RAM_REGIONS];
};
struct VIMVRAMMemoryServer {
    mach_port_t receive,memory;
    uint64_t bytes;
    uid_t user;
    pid_t consumer;
    pthread_t thread;
    uint32_t region_count;
    struct VIMSharedRAMRange regions[VIM_SHARED_RAM_REGIONS];
};
struct VIMVRAMMemoryMapping {
    mach_vm_address_t address;mach_vm_size_t bytes;
    uint32_t region_count;
    struct VIMSharedRAMRange regions[VIM_SHARED_RAM_REGIONS];
};
static inline int VIMSharedRAMRangesValid(const struct VIMSharedRAMRange *ranges,unsigned count,uint64_t bytes)
{
    if(count>VIM_SHARED_RAM_REGIONS)return 0;
    for(unsigned i=0;i<count;++i){
        const struct VIMSharedRAMRange *r=&ranges[i];
        if(!r->bytes || ((r->guest|r->offset|r->bytes)&4095) || r->guest>UINT64_MAX-r->bytes ||
           r->offset>bytes || r->bytes>bytes-r->offset ||
           (i && r->guest<ranges[i-1].guest+ranges[i-1].bytes))return 0;
    }
    return 1;
}
static inline int VIMVRAMMemoryPeer(const mach_msg_header_t *h,uid_t user,pid_t process)
{
    const mach_msg_audit_trailer_t *t=(const void *)((const char *)h+round_msg(h->msgh_size));
    return t->msgh_trailer_type==MACH_MSG_TRAILER_FORMAT_0 &&
        t->msgh_trailer_size>=sizeof(*t) && process>0 &&
        audit_token_to_euid(t->msgh_audit)==user && audit_token_to_pid(t->msgh_audit)==process;
}
static inline void *VIMVRAMMemoryServe(void *opaque)
{
    struct VIMVRAMMemoryServer *s=opaque;
    for(;;){
        struct {mach_msg_header_t h;mach_msg_max_trailer_t trailer;} packet;
        memset(&packet,0,sizeof(packet));
        kern_return_t kr=mach_msg(&packet.h,MACH_RCV_MSG|
            MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0)|
            MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT),0,sizeof(packet),s->receive,0,0);
        if(kr)break;
        if(packet.h.msgh_size==sizeof(packet.h) &&
           !(packet.h.msgh_bits&MACH_MSGH_BITS_COMPLEX) &&
           packet.h.msgh_id==VIM_VRAM_MEMORY_REQUEST+2 &&
           VIMVRAMMemoryPeer(&packet.h,s->user,getpid())){
            mach_msg_destroy(&packet.h);break;
        }
        if(packet.h.msgh_size!=sizeof(packet.h) ||
           (packet.h.msgh_bits&MACH_MSGH_BITS_COMPLEX) ||
           packet.h.msgh_id!=VIM_VRAM_MEMORY_REQUEST ||
           MACH_MSGH_BITS_REMOTE(packet.h.msgh_bits)!=MACH_MSG_TYPE_PORT_SEND_ONCE ||
           !VIMVRAMMemoryPeer(&packet.h,s->user,s->consumer)){
            mach_msg_destroy(&packet.h);continue;
        }
        struct VIMVRAMMemoryReply reply;memset(&reply,0,sizeof(reply));
        reply.header.msgh_bits=MACH_MSGH_BITS_COMPLEX|MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE,0);
        reply.header.msgh_remote_port=packet.h.msgh_remote_port;
        reply.header.msgh_size=sizeof(reply);reply.header.msgh_id=VIM_VRAM_MEMORY_REQUEST+1;
        reply.body.msgh_descriptor_count=1;reply.memory.name=s->memory;
        reply.memory.disposition=MACH_MSG_TYPE_COPY_SEND;reply.memory.type=MACH_MSG_PORT_DESCRIPTOR;
        reply.bytes=s->bytes;reply.version=2;reply.region_count=s->region_count;
        memcpy(reply.regions,s->regions,sizeof(reply.regions));
        kr=mach_msg(&reply.header,MACH_SEND_MSG|MACH_SEND_TIMEOUT,sizeof(reply),0,0,1000,0);
        if(kr)mach_port_deallocate(mach_task_self(),packet.h.msgh_remote_port);
    }
    return NULL;
}
static inline struct VIMVRAMMemoryServer *VIMVRAMMemoryServerStartWithRanges(
    const char *service,pid_t consumer,void *address,uint64_t bytes,
    const struct VIMSharedRAMRange *ranges,unsigned count)
{
    if(!service || !*service || strlen(service)>=BOOTSTRAP_MAX_NAME_LEN || consumer<=0 ||
       !address || !bytes || bytes>VIMSharedMemoryLimit(count) ||
       ((uintptr_t)address&(vm_page_size-1)) || (bytes&(vm_page_size-1)) ||
       (count && !ranges) || !VIMSharedRAMRangesValid(ranges,count,bytes))return NULL;
    struct VIMVRAMMemoryServer *s=calloc(1,sizeof(*s));if(!s)return NULL;
    s->bytes=bytes;s->consumer=consumer;s->user=geteuid();memory_object_size_t size=bytes;
    s->region_count=count;if(count)memcpy(s->regions,ranges,count*sizeof(*ranges));
    /* A large RAMBlock can span multiple VM map entries. Without VM_SHARE,
     * this call returned only the first 128 MB of a real 4-GB allocation.
     * VM_SHARE extracts a shared entry list, not a pixel-data copy. */
    kern_return_t entry=mach_make_memory_entry_64(mach_task_self(),&size,(mach_vm_address_t)(uintptr_t)address,
        VM_PROT_READ|VM_PROT_WRITE|MAP_MEM_VM_SHARE,&s->memory,0);
    if(entry || size!=bytes){
        fprintf(stderr,"Shared-memory capability creation status=%x extent=%llu requested=%llu\n",entry,
            (unsigned long long)size,(unsigned long long)bytes);
        if(s->memory)mach_port_deallocate(mach_task_self(),s->memory);free(s);return NULL;
    }
    if(
       mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&s->receive) ||
       mach_port_insert_right(mach_task_self(),s->receive,s->receive,MACH_MSG_TYPE_MAKE_SEND) ||
       bootstrap_register(bootstrap_port,(char *)service,s->receive) ||
       pthread_create(&s->thread,NULL,VIMVRAMMemoryServe,s)){
        if(s->receive)mach_port_destroy(mach_task_self(),s->receive);
        if(s->memory)mach_port_deallocate(mach_task_self(),s->memory);free(s);return NULL;
    }
    return s;
}
static inline struct VIMVRAMMemoryServer *VIMVRAMMemoryServerStart(
    const char *service,pid_t consumer,void *address,uint64_t bytes)
{return VIMVRAMMemoryServerStartWithRanges(service,consumer,address,bytes,NULL,0);}
static inline void VIMVRAMMemoryServerStop(struct VIMVRAMMemoryServer *s)
{
    if(!s)return;
    mach_msg_header_t h;memset(&h,0,sizeof(h));
    h.msgh_bits=MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,0);h.msgh_remote_port=s->receive;
    h.msgh_size=sizeof(h);h.msgh_id=VIM_VRAM_MEMORY_REQUEST+2;
    kern_return_t kr=mach_msg(&h,MACH_SEND_MSG|MACH_SEND_TIMEOUT,sizeof(h),0,0,1000,0);
    if(kr)mach_port_destroy(mach_task_self(),s->receive);
    pthread_join(s->thread,NULL);
    if(!kr)mach_port_destroy(mach_task_self(),s->receive);
    mach_port_deallocate(mach_task_self(),s->memory);free(s);
}
static inline kern_return_t VIMVRAMMemoryImport(const char *service,pid_t producer,
    struct VIMVRAMMemoryMapping *out,mach_msg_timeout_t timeout)
{
    if(!out || out->address || out->bytes || !service || producer<=0)return KERN_INVALID_ARGUMENT;
    mach_port_t server=0,receive=0;
    kern_return_t kr=bootstrap_look_up(bootstrap_port,(char *)service,&server);if(kr)return kr;
    kr=mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&receive);
    if(kr){mach_port_deallocate(mach_task_self(),server);return kr;}
    mach_msg_header_t h;memset(&h,0,sizeof(h));h.msgh_remote_port=server;h.msgh_local_port=receive;
    h.msgh_bits=MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND,MACH_MSG_TYPE_MAKE_SEND_ONCE);
    h.msgh_id=VIM_VRAM_MEMORY_REQUEST;h.msgh_size=sizeof(h);
    kr=mach_msg(&h,MACH_SEND_MSG|MACH_SEND_TIMEOUT,sizeof(h),0,0,timeout,0);
    mach_port_deallocate(mach_task_self(),server);
    if(!kr){
        struct {struct VIMVRAMMemoryReply m;mach_msg_max_trailer_t trailer;} packet;
        memset(&packet,0,sizeof(packet));
        kr=mach_msg(&packet.m.header,MACH_RCV_MSG|MACH_RCV_TIMEOUT|
            MACH_RCV_TRAILER_TYPE(MACH_MSG_TRAILER_FORMAT_0)|
            MACH_RCV_TRAILER_ELEMENTS(MACH_RCV_TRAILER_AUDIT),0,sizeof(packet),receive,timeout,0);
        if(!kr){
            struct VIMVRAMMemoryReply *m=&packet.m;
            int valid=m->header.msgh_size==sizeof(*m) && m->header.msgh_id==VIM_VRAM_MEMORY_REQUEST+1 &&
                (m->header.msgh_bits&MACH_MSGH_BITS_COMPLEX) && m->body.msgh_descriptor_count==1 &&
                m->memory.type==MACH_MSG_PORT_DESCRIPTOR && m->memory.disposition==MACH_MSG_TYPE_PORT_SEND &&
                m->version==2 && !m->reserved && !m->padding && m->bytes && m->bytes<=VIMSharedMemoryLimit(m->region_count) &&
                VIMSharedRAMRangesValid(m->regions,m->region_count,m->bytes) &&
                !(m->bytes&(vm_page_size-1)) && VIMVRAMMemoryPeer(&m->header,geteuid(),producer);
            kr=valid?mach_vm_map(mach_task_self(),&out->address,m->bytes,16383,VM_FLAGS_ANYWHERE,
                m->memory.name,0,FALSE,VM_PROT_READ|VM_PROT_WRITE,VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_NONE):KERN_INVALID_ARGUMENT;
            if(!kr){out->bytes=m->bytes;out->region_count=m->region_count;memcpy(out->regions,m->regions,sizeof(out->regions));}
            mach_msg_destroy(&m->header);
        }
    }
    mach_port_destroy(mach_task_self(),receive);return kr;
}
static inline void VIMVRAMMemoryUnmap(struct VIMVRAMMemoryMapping *mapping)
{
    if(mapping->address)mach_vm_deallocate(mach_task_self(),mapping->address,mapping->bytes);
    memset(mapping,0,sizeof(*mapping));
}
/* Intel host research: reconstruct the real pinned guest backing's page
 * order in an anonymous alias. Reject holes/ROM/MMIO rather than substituting
 * copied pixels. ARM's larger host-page granule needs separate qualification. */
static inline kern_return_t VIMSharedRAMMapPages(const struct VIMVRAMMemoryMapping *ram,
    const uint64_t *pages,unsigned count,struct VIMVRAMMemoryMapping *out)
{
    if(!ram || !ram->address || !ram->region_count || !pages || !count || count>16384 ||
       !out || out->address || out->bytes || vm_page_size!=4096)return KERN_INVALID_ARGUMENT;
    mach_vm_size_t bytes=(mach_vm_size_t)count*4096;
    mach_vm_address_t address=0;
    kern_return_t kr=mach_vm_map(mach_task_self(),&address,bytes,16383,VM_FLAGS_ANYWHERE,
        MACH_PORT_NULL,0,FALSE,VM_PROT_READ|VM_PROT_WRITE,VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_NONE);
    if(kr)return kr;
    for(unsigned i=0;i<count;){
        const struct VIMSharedRAMRange *r=NULL;
        if(!(pages[i]&4095))for(unsigned j=0;j<ram->region_count;++j){
            const struct VIMSharedRAMRange *candidate=&ram->regions[j];
            if(pages[i]>=candidate->guest && pages[i]-candidate->guest<candidate->bytes &&
               4096<=candidate->bytes-(pages[i]-candidate->guest)){r=candidate;break;}
        }
        if(!r){kr=KERN_INVALID_ADDRESS;break;}
        /* Preserve the supplied page order and exact storage, but import a
         * contiguous run in ONE kernel call. Never cross a validated range:
         * adjacent GPAs can have non-adjacent offsets in the exported object.
         * Reversed/duplicate/scattered pages retain separate aliases. This
         * does not change CPU/GPU exclusion or the 4-KB-host qualification. */
        unsigned run=1;
        uint64_t remaining=(r->bytes-(pages[i]-r->guest))/4096;
        while(run<count-i && run<remaining &&
              pages[i+run]==pages[i]+(uint64_t)run*4096)++run;
        mach_vm_address_t dest=address+(mach_vm_address_t)i*4096;
        vm_prot_t current=0,maximum=0;
        kr=mach_vm_remap(mach_task_self(),&dest,(mach_vm_size_t)run*4096,0,VM_FLAGS_FIXED|VM_FLAGS_OVERWRITE,
            mach_task_self(),ram->address+r->offset+(pages[i]-r->guest),FALSE,&current,&maximum,VM_INHERIT_NONE);
        if(kr)break;
        i+=run;
    }
    if(kr)mach_vm_deallocate(mach_task_self(),address,bytes);
    else {out->address=address;out->bytes=bytes;}
    return kr;
}
#endif
