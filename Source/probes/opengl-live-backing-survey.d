/* Read-only existing-descriptor survey, NOT an exclusion/admission policy.
 * Qualified actual 13F1911 kernel UUID8926E512-6714-34FC-B27C-6AE90EB51D5E:
 * IOMemoryDescriptor::getLength loads+0x50; IOMemoryMap::redirect reads its
 * descriptor flags+0x20 and memory-entry+0x28. Published XNU2422 confirms.
 * Each object/caller is sampled ONCE; bounded96 records,12seconds. Descriptors
 * are alive during their actual getter. No later raw-pointer dereference.
 * Kernel addresses and memEntry identity do NOT prove VM-object isolation.
 */
#pragma D option quiet
#pragma D option bufsize=4m
dtrace:::BEGIN { printf("READY existing backing survey target=%d\n",$target); }
fbt:mach_kernel:_ZNK18IOMemoryDescriptor9getLengthEv:entry
/pid==$target && arg0 && live_count<96 && !seen[arg0,caller]/
{
    seen[arg0,caller]=1;++live_count;
    printf("MD %llx flags=%x entry=%llx length=%llx caller=%llx\n",arg0,
        *(uint32_t *)(arg0+0x20),*(uint64_t *)(arg0+0x28),*(uint64_t *)(arg0+0x50),(unsigned long long)caller);
    stack(8);
}
tick-12sec { exit(0); }
dtrace:::END { printf("DONE existing backing survey count=%d\n",live_count); }
