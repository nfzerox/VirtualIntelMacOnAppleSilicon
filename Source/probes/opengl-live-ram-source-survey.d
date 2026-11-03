/* PRIVATE read-only diagnostic, not a runtime ownership/admission API.
 * ONLY the verified13F1911 kernel and paired accelerator: pass TWO actual
 * return PCs from context_retain_ram_backing's getLength calls as $1/$2.
 * Confirm loaded UUID and disassemble its call sites BEFORE running.
 * Actual kernel initWithOptions/getPhysicalSegment + period SDK qualify
 * IOGeneralMemoryDescriptor ranges+0x60,count+0x68,task+0x70. Our surface
 * source creates ONLY IOGeneral/IOBuffer descriptors (which inherit it).
 * Never generalize these fields to arbitrary IOMemoryDescriptor subclasses.
 * One sample per descriptor, max48,12seconds; source alive at actual getter.
 */
#pragma D option quiet
#pragma D option bufsize=4m
dtrace:::BEGIN { printf("READY live RAM source survey target=%d\n",$target); }
fbt:mach_kernel:_ZNK18IOMemoryDescriptor9getLengthEv:entry
/pid==$target && (caller==$1 || caller==$2) && arg0 && ram_count<48 && !ram_seen[arg0]/
{
    ram_seen[arg0]=1;++ram_count;
    self->ranges=*(uint64_t *)(arg0+0x60);
    self->range_count=*(uint32_t *)(arg0+0x68);
    printf("SOURCE md=%llx flags=%x entry=%llx length=%llx task=%llx count=%u\n",arg0,
        *(uint32_t *)(arg0+0x20),*(uint64_t *)(arg0+0x28),*(uint64_t *)(arg0+0x50),
        *(uint64_t *)(arg0+0x70),self->range_count);
}
fbt:mach_kernel:_ZNK18IOMemoryDescriptor9getLengthEv:entry
/self->ranges && self->range_count==1/
{
    printf("VIRTUAL address=%llx length=%llx\n",*(uint64_t *)(self->ranges),*(uint64_t *)(self->ranges+8));
    self->ranges=0;self->range_count=0;
}
tick-12sec { exit(0); }
dtrace:::END { printf("DONE live RAM source survey count=%d\n",ram_count); }
