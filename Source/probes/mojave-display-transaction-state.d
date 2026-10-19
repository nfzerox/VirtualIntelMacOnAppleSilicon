#pragma D option quiet
/* Current-boot pipe address, from startup trace. Read-only, no MMIO. */
dtrace:::BEGIN
{
    this->pipe=(uintptr_t)$1;
    this->tx=*(uintptr_t *)(this->pipe+0x248);
    printf("pipe=%p active=%p queue:\n",this->pipe,this->tx);
    tracemem((void *)(this->pipe+0x118),0x190);
}
dtrace:::BEGIN
/this->tx != 0/
{
    printf("active transaction:\n");
    tracemem((void *)this->tx,0x180);
    this->owner=*(uintptr_t *)(this->pipe+0x88);
    this->machine=*(uintptr_t *)(this->owner+0x380);
    this->index=*(uint32_t *)(this->tx+0xa0);
    this->target=*(uint32_t *)(this->tx+0xa4);
    this->table=*(uintptr_t *)(this->machine+0x28);
    this->stamp=*(uintptr_t *)(this->table+this->index*8);
    printf("event machine=%p index=%u target=%u stamp=%p actual=%u\n",
        this->machine,this->index,this->target,this->stamp,
        *(uint32_t *)this->stamp);
}
dtrace:::BEGIN { exit(0); }
