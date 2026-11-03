/* Mavericks 13F1911 IOKit x64 UUID
 * 10932113-9F7E-38A0-A158-A019A555CAC3: IOServiceOpen's actual ret is
 * 0x50ad; IOServiceClose's actual ret is 0x50d6. Neither tail-calls.
 * Snow10J3250 IOKit x64 UUID94384F69-7EDA-3528-7DB1-EF023D29B96A
 * independently has real returns at0x44b5/0x4ed7, also without tail exits.
 * Symbolic boundary probes below support both, not those numeric offsets.
 * Only these low-frequency connection operations are timed. GLI attach
 * entries are counted, not timed: their final import can be a tail call.
 * Inclusive wall time is not CPU time or an uninstrumented FPS result.
 * At most 32 call-site stacks per operation identify remaining sources.
 * No caller buffers, data pointers or rendered contents are recorded.
 */
#pragma D option quiet
#pragma D option aggsize=4m
#pragma D option dynvarsize=4m
BEGIN { began=timestamp;open_budget=32;close_budget=32;printf("READY compositor drawable connection costs target=%d\n",$target); }
pid$target:GLEngine:VIMCreateAcceleratorDrawable:entry { @attachments=count(); }
pid$target:IOKit:IOServiceGetMatchingService:entry
{ @matching_callers[ustack(8)]=count(); }
pid$target:IOKit:IOServiceOpen:entry
{ self->opened=timestamp;self->type=(uint32_t)arg2;@open_services[(uint32_t)arg2,(uint32_t)arg0]=count(); }
pid$target:IOKit:IOServiceOpen:entry /open_budget>0/
{ @open_callers[(uint32_t)arg2,ustack(8)]=count();open_budget--; }
pid$target:IOKit:IOServiceOpen:return /self->opened/
{
    this->us=(timestamp-self->opened)/1000;
    @opens[self->type]=count();@open_us[self->type]=sum(this->us);
    @open_max[self->type]=max(this->us);@open_dist[self->type]=quantize(this->us);
    @open_result[self->type,(uint32_t)arg1]=count();self->opened=0;
}
pid$target:IOKit:IOServiceClose:entry {self->closed=timestamp;}
pid$target:IOKit:IOServiceClose:entry /close_budget>0/
{ @close_callers[ustack(8)]=count();close_budget--; }
pid$target:IOKit:IOServiceClose:return /self->closed/
{
    this->us=(timestamp-self->closed)/1000;
    @closes=count();@close_us=sum(this->us);@close_max=max(this->us);
    @close_dist=quantize(this->us);@close_result[(uint32_t)arg1]=count();self->closed=0;
}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
END {
    printf("ATTACH entries\n");printa(@attachments);
    printf("OPEN type calls inclusive_us max_us\n");printa("%u %@d %@d %@d\n",@opens,@open_us,@open_max);
    printf("OPEN type public_service_port calls (task-local names, not kernel addresses)\n");printa("%u %u %@d\n",@open_services);
    printa(@open_dist);printf("OPEN result\n");printa(@open_result);
    printf("CLOSE calls inclusive_us max_us\n");printa("%@d %@d %@d\n",@closes,@close_us,@close_max);
    printa(@close_dist);printf("CLOSE result\n");printa(@close_result);
    printf("BOUNDED OPEN callers\n");printa(@open_callers);
    printf("BOUNDED CLOSE callers\n");printa(@close_callers);
    printf("MATCHING SERVICE callers (discovery, not connection opens)\n");printa(@matching_callers);
    printf("END compositor drawable connection costs\n");
}
