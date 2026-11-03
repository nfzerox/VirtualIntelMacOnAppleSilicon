/* Read-only, bounded REAL compositor-object diagnostic. Not a lock/admission
 * API: observations of a live object's metadata do not serialize mutations.
 * Exact 13F1911 kernel + known IOGeneralMemoryDescriptor sources ONLY. Pass
 * freshly verified context_retain_ram_backing getLength return PCs as $1/$2.
 * Kernel CTF supplies VM/IPC field offsets; only descriptor _memEntry+0x28
 * comes from the independently disassembled IOMemoryMap::redirect profile.
 * A retained descriptor owns its named entry while this getter is executing.
 * Never retain these printed pointers or dereference them after the probe.
 */
#pragma D option quiet
#pragma D option bufsize=4m
dtrace:::BEGIN
{
    printf("READY live VM-object survey target=%d object_bytes=%d object_size_offset=%d named_bytes=%d port_kobject_offset=%d\n",
        $target,sizeof(struct vm_object),offsetof(struct vm_object,vo_un1),
        sizeof(struct vm_named_entry),offsetof(struct ipc_port,kdata));
}
fbt:mach_kernel:_ZNK18IOMemoryDescriptor9getLengthEv:entry
/pid==$target && (caller==$1 || caller==$2) && arg0 && object_count<48 && !object_seen[arg0]/
{
    object_seen[arg0]=1;++object_count;
    self->md=arg0;self->length=*(uint64_t *)(arg0+0x50);
    self->port=(struct ipc_port *)*(uint64_t *)(arg0+0x28);
    printf("DESCRIPTOR md=%llx bytes=%llx entry=%llx\n",self->md,self->length,(uint64_t)self->port);
}
fbt:mach_kernel:_ZNK18IOMemoryDescriptor9getLengthEv:entry
/self->md && self->port && (self->port->ip_object.io_bits & 0x80000fff)==0x8000001c/
{
    self->entry=(struct vm_named_entry *)self->port->kdata.kobject;
}
fbt:mach_kernel:_ZNK18IOMemoryDescriptor9getLengthEv:entry
/self->entry && !self->entry->is_sub_map && !self->entry->is_pager && !self->entry->is_copy/
{
    self->object=self->entry->backing.object;
}
fbt:mach_kernel:_ZNK18IOMemoryDescriptor9getLengthEv:entry
/self->object/
{
    printf("OBJECT md=%llx object=%llx object_bytes=%llx entry_bytes=%llx entry_offset=%llx data_offset=%llx internal=%d true_share=%d shadow=%llx copy=%llx refs=%d terminating=%d\n",
        self->md,(uint64_t)self->object,self->object->vo_un1.vou_size,
        self->entry->size,self->entry->offset,self->entry->data_offset,
        self->object->internal,self->object->true_share,(uint64_t)self->object->shadow,
        (uint64_t)self->object->copy,self->object->ref_count,self->object->terminating);
}
fbt:mach_kernel:_ZNK18IOMemoryDescriptor9getLengthEv:entry
/self->md/
{
    self->md=0;self->length=0;self->port=0;self->entry=0;self->object=0;
}
tick-12sec { exit(0); }
dtrace:::END { printf("DONE live VM-object survey count=%d\n",object_count); }
