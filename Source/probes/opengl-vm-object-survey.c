/* Read-only Mach VM metadata for ACTUAL observed backing ranges. No task
 * pixels read, remapped, protected or suspended. Region/object IDs are public
 * diagnostics, not durable capabilities or proof of every cross-task alias. */
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_region.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
struct Region {mach_vm_address_t base;mach_vm_size_t bytes;vm_region_submap_info_data_64_t info;};
int main(int argc,char **argv)
{
    if(argc<3)return 2;
    task_t task=MACH_PORT_NULL;kern_return_t error=task_for_pid(mach_task_self(),atoi(argv[1]),&task);
    if(error){fprintf(stderr,"task metadata access failed=%x\n",error);return 1;}
    struct Region *regions=calloc(8192,sizeof(*regions));if(!regions)return 1;
    unsigned n=0;mach_vm_address_t address=0;natural_t depth=0;
    while(n<8192){
        struct Region *r=regions+n;mach_msg_type_number_t count=VM_REGION_SUBMAP_INFO_COUNT_64;
        r->base=address;
        error=mach_vm_region_recurse(task,&r->base,&r->bytes,&depth,(vm_region_recurse_info_t)&r->info,&count);
        if(error==KERN_INVALID_ADDRESS)break;
        if(error || !r->bytes || r->base>UINT64_MAX-r->bytes){fprintf(stderr,"invalid VM region result=%x\n",error);return 1;}
        if(r->info.is_submap){if(++depth>32)return 1;address=r->base;continue;}
        address=r->base+r->bytes;++n;
    }
    if(n==8192){fputs("refusing truncated region inventory\n",stderr);return 1;}
    for(int input=2;input<argc;++input){
        char *end=0;uint64_t wanted=strtoull(argv[input],&end,16);if(!wanted || !end || *end)return 2;
        unsigned found;
        for(found=0;found<n;++found)if(wanted>=regions[found].base && wanted-regions[found].base<regions[found].bytes)break;
        if(found==n){printf("UNMAPPED address=%llx\n",(unsigned long long)wanted);continue;}
        struct Region *r=regions+found;uint64_t mapped=0;unsigned aliases=0;
        for(unsigned j=0;j<n;++j)if(regions[j].info.object_id==r->info.object_id){mapped+=regions[j].bytes;++aliases;}
        printf("REGION address=%llx base=%llx bytes=%llx object=%x offset=%llx refs=%u shadow=%u sharing=%u tag=%u task_regions_same_object=%u task_bytes_same_object=%llx\n",
            (unsigned long long)wanted,(unsigned long long)r->base,(unsigned long long)r->bytes,r->info.object_id,
            (unsigned long long)r->info.offset,r->info.ref_count,r->info.shadow_depth,r->info.share_mode,r->info.user_tag,
            aliases,(unsigned long long)mapped);
    }
    free(regions);mach_port_deallocate(mach_task_self(),task);return 0;
}
