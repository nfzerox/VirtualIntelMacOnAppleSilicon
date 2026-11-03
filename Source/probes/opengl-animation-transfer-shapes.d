/* Matched Mavericks IOConnectCallStructMethod wrapper has a genuine ret,
 * verified from the actual guest IOKit binary before using its return probe.
 * Read ONLY the private transfer's action/size/result metadata, never pixels,
 * addresses, file contents or caller payloads. Finite observation. These are
 * device transfers, not rendered FPS. Selector 28 is private to this client;
 * do not use these labels for a physical Intel driver's different selectors.
 */
#pragma D option quiet
#pragma D option aggsize=4m
#pragma D option dynvarsize=4m
BEGIN {printf("READY bounded transfer shape survey target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->display_depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->display_depth/ {self->display_depth--;}
pid$target:IOKit:IOConnectCallStructMethod:entry
/self->display_depth && arg1==28 && arg2 && arg3==16 && arg4/
{
    this->header=(uint32_t *)copyin(arg2,8);
    self->action=this->header[0];self->requested=this->header[1];
    self->output=arg4;self->began=timestamp;
}
pid$target:IOKit:IOConnectCallStructMethod:return
/self->began && (self->action==2 || self->action==3)/
{
    this->response=(int32_t *)copyin(self->output,8);
    @calls[self->action,self->requested,this->response[0],(uint32_t)arg1]=count();
    @us[self->action,self->requested,this->response[0],(uint32_t)arg1]=sum((timestamp-self->began)/1000);
}
pid$target:IOKit:IOConnectCallStructMethod:return
/self->began/ {self->began=0;self->output=0;}
tick-30s {exit(0);}
END {
    printf("action requested returned native_status calls inclusive_us\n");
    printa("%u %u %d %u %@d %@d\n",@calls,@us);
}
