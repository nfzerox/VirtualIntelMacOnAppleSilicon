/* Fixed per-thread scopes: do NOT instrument CGS*; that includes very hot
 * internal stream/event routines and can drop dynamic variables on Mavericks.
 * Inclusive wall costs, not additive CPU time. No pixels or addresses.
 */
#pragma D option quiet
BEGIN { printf("READY activation client costs\n"); }
pid$target:CoreGraphics:CGSDeviceLock:entry {self->lock_at=timestamp;}
pid$target:CoreGraphics:CGSDeviceLock:return /self->lock_at/
{ @lock_calls=count();@lock_us=sum((timestamp-self->lock_at)/1000);@lock_dist=quantize((timestamp-self->lock_at)/1000);self->lock_at=0; }
pid$target:CoreGraphics:_CGSSynchronizeWindowBackingStore:entry {self->sync_at=timestamp;}
pid$target:CoreGraphics:_CGSSynchronizeWindowBackingStore:return /self->sync_at/
{ @sync_calls=count();@sync_us=sum((timestamp-self->sync_at)/1000);@sync_dist=quantize((timestamp-self->sync_at)/1000);self->sync_at=0; }
pid$target:CoreGraphics:CGSDoDeferredOrdering:entry {self->order_at=timestamp;}
pid$target:CoreGraphics:CGSDoDeferredOrdering:return /self->order_at/
{ @order_calls=count();@order_us=sum((timestamp-self->order_at)/1000);self->order_at=0; }
pid$target:CoreGraphics:CGSSetWindowShadowAndRimParametersWithStretch:entry {self->shadow_at=timestamp;}
pid$target:CoreGraphics:CGSSetWindowShadowAndRimParametersWithStretch:return /self->shadow_at/
{ @shadow_calls=count();@shadow_us=sum((timestamp-self->shadow_at)/1000);self->shadow_at=0; }
tick-12sec { exit(0); }
END {
    printf("DEVICE LOCK calls/wall-us\n");printa("%@d %@d\n",@lock_calls,@lock_us);printa(@lock_dist);
    printf("BACKING SYNC calls/wall-us\n");printa("%@d %@d\n",@sync_calls,@sync_us);printa(@sync_dist);
    printf("DEFERRED ORDER calls/wall-us\n");printa("%@d %@d\n",@order_calls,@order_us);
    printf("SHADOW PARAMETERS calls/wall-us\n");printa("%@d %@d\n",@shadow_calls,@shadow_us);
}
