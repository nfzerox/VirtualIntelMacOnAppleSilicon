/* Actual owning-client transfer metadata inside full compositor updates.
 * Fixed VIMGLTransfer/VIMGLExchange ABI only, never pixel/payload reads.
 * No method-wrapper double counting or claims of clean frame timing. */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=8m
BEGIN {began=timestamp;printf("READY actual transport shapes target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->update++;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->update/ {self->update--;}
pid$target:IOKit:IOConnectCallStructMethod:entry /self->update && arg1==28 && arg3==16/
{
    this->r=(uint32_t *)copyin(arg2,16);
    self->transfer_at=timestamp;self->action=this->r[0];self->bytes=this->r[1];
}
pid$target:IOKit:IOConnectCallStructMethod:return /self->transfer_at/
{
    this->us=(timestamp-self->transfer_at)/1000;
    @transfer_calls[self->action,self->bytes]=count();
    @transfer_us[self->action,self->bytes]=sum(this->us);
    @transfer_max[self->action,self->bytes]=max(this->us);
    self->transfer_at=0;self->action=0;self->bytes=0;
}
pid$target:IOKit:IOConnectTrap4:entry /self->update && arg1==0 && arg3>=16 && arg3<=4096/
{
    this->r=(uint32_t *)copyin(arg2,16);
    self->exchange_at=timestamp;self->send=this->r[1];self->receive=this->r[2];
    self->single_op=this->r[1]==144?*(uint32_t *)copyin(arg2+16,4):0xffffffff;
}
pid$target:IOKit:IOConnectTrap4:return /self->exchange_at/
{
    this->us=(timestamp-self->exchange_at)/1000;
    @exchange_calls[self->send,self->receive]=count();
    @exchange_us[self->send,self->receive]=sum(this->us);
    @single_calls[self->single_op,self->send,self->receive]=count();
    @single_us[self->single_op,self->send,self->receive]=sum(this->us);
    @single_max[self->single_op,self->send,self->receive]=max(this->us);
    self->exchange_at=0;self->send=0;self->receive=0;
}
tick-1s /timestamp-began>=32000000000/ {exit(0);}
END {
    printf("TRANSFER action bytes calls inclusive_us maximum_us\n");
    printa("%u %u %@d %@d %@d\n",@transfer_calls,@transfer_us,@transfer_max);
    printf("EXCHANGE send_bytes receive_bytes calls inclusive_us\n");
    printa("%u %u %@d %@d\n",@exchange_calls,@exchange_us);
    printf("SINGLE command opcode send_bytes receive_bytes calls inclusive_us maximum_us (ffffffff=mixed)\n");
    printa("%x %u %u %@d %@d %@d\n",@single_calls,@single_us,@single_max);
}
