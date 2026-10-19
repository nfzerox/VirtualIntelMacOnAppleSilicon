#pragma D option quiet
/* Pass current-boot pipe address from mojave-display-state.d. Read-only. */
dtrace:::BEGIN
{
    this->pipe=(uintptr_t)$1;
    this->shared=*(uintptr_t *)(this->pipe+0x378);
    printf("pipe=%p shared=%p pipe+0x240:\n",this->pipe,this->shared);
    tracemem((void *)(this->pipe+0x240),0x170);
    printf("shared RAM:\n");
    tracemem((void *)this->shared,0x220);
    exit(0);
}
