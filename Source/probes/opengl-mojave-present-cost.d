/* Finite app-side boundary attribution. These are CPU elapsed times, not GPU
 * timer queries or FPS; compare actual presented pixels separately. */
#pragma D option quiet
#pragma D option bufsize=1m
#pragma D option dynvarsize=4m
BEGIN { printf("READY present cost pid=%u\n",$target); }
pid$target:GLEngine*:VIMGLIFlush:entry,
pid$target:GLEngine*:VIMGLIFinish:entry,
pid$target:GLEngine*:VIMGLIFlushRender:entry,
pid$target:GLEngine*:VIMGLIFinishRender:entry
{ @boundaries[probefunc]=count(); }
/* Native AppKit surface-flush callbacks reenter GL on this same thread. Time
 * the outer boundaries inclusively rather than overwriting their timestamps
 * with the nested offscreen work. Optimized private signatures are not read. */
pid$target:GLEngine*:VIMGLISwap:entry { self->sdepth++; }
pid$target:GLEngine*:VIMGLISwap:entry /self->sdepth==1/
{ self->swap=timestamp;self->swap_cpu=vtimestamp;self->swap_context=arg0; }
pid$target:GLEngine*:VIMGLISwap:return /self->sdepth==1/
{ @swaps=quantize((timestamp-self->swap)/1000);
  @swap_context_count[self->swap_context,tid]=count();
  @swap_context_us[self->swap_context,tid]=sum((timestamp-self->swap)/1000);
  @swap_context_cpu_us[self->swap_context,tid]=sum((vtimestamp-self->swap_cpu)/1000);
  self->swap_context=0; }
pid$target:GLEngine*:VIMGLISwap:return /self->sdepth/ { self->sdepth--; }
pid$target:libVIMPassthroughOpenGL.dylib:glGetIntegerv:entry /self->sdepth/
{ self->integer_query=timestamp; }
pid$target:libVIMPassthroughOpenGL.dylib:glGetIntegerv:return /self->integer_query/
{ @swap_integer_queries=count();@swap_integer_query_us=sum((timestamp-self->integer_query)/1000);self->integer_query=0; }
pid$target:GLEngine*:VIMGLIFlushMode:entry { self->fdepth++; }
pid$target:GLEngine*:VIMGLIFlushMode:entry /self->fdepth==1/ { self->flush=timestamp; }
pid$target:GLEngine*:VIMGLIFlushMode:return /self->fdepth==1/
{ @flushes=quantize((timestamp-self->flush)/1000); }
pid$target:GLEngine*:VIMGLIFlushMode:return /self->fdepth/ { self->fdepth--; }
pid$target:GLEngine*:VIMPresentDrawableKnown:entry { self->pdepth++; }
pid$target:GLEngine*:VIMPresentDrawableKnown:entry /self->pdepth==1/ { self->present=timestamp; }
pid$target:GLEngine*:VIMPresentDrawableKnown:return /self->pdepth==1/
{ @presents=quantize((timestamp-self->present)/1000); }
pid$target:GLEngine*:VIMPresentDrawableKnown:return /self->pdepth/ { self->pdepth--; }
/* Split completed readback from AppKit's native layer-consumer work. The
 * consumer reenters GL; do not add its inclusive time to outer presentation.
 * Use -Z on native Intel where project-owned symbols do not exist. */
pid$target:AppKit:NSCGLSurfaceFlush:entry { self->consumer_depth++; }
pid$target:AppKit:NSCGLSurfaceFlush:entry /self->consumer_depth==1/
{ self->consumer_begin=timestamp;self->consumer_cpu=vtimestamp;
  self->consumer_options=arg4;@consumer_options[arg4]=count(); }
pid$target:AppKit:NSCGLSurfaceFlush:return /self->consumer_depth==1/
{ @consumer_us=quantize((timestamp-self->consumer_begin)/1000);
  @consumer_context_count[self->swap_context,tid]=count();
  @consumer_context_us[self->swap_context,tid]=sum((timestamp-self->consumer_begin)/1000);
  @consumer_context_cpu_us[self->swap_context,tid]=sum((vtimestamp-self->consumer_cpu)/1000); }
pid$target:AppKit:NSCGLSurfaceFlush:return /self->consumer_depth/ { self->consumer_depth--; }
/* Inclusive per-function attribution, not an additive pipeline total.
 * Some optimized exports have entry but no return probes: keep independent
 * timestamps so those cannot suppress attribution of subsequent calls.
 * On Intel the GLEngine Exec boundary is the oracle; no private state offsets
 * or application pixels are inspected. */
pid$target:libVIMPassthroughOpenGL.dylib:gl*:entry
/self->consumer_depth/
{ api_begin[tid,probefunc]=timestamp; }
pid$target:libVIMPassthroughOpenGL.dylib:gl*:return
/self->consumer_depth && api_begin[tid,probefunc]/
{ @consumer_api_count[probefunc]=count();
  @consumer_api_us[probefunc]=sum((timestamp-api_begin[tid,probefunc])/1000);
  api_begin[tid,probefunc]=0; }
pid$target:GLEngine:gl*_Exec:entry /self->consumer_depth/
{ @native_consumer_api_count[probefunc]=count(); }
pid$target:GLEngine*:glSwap*_Exec:entry
{ self->native_swap=timestamp;@native_swaps=count(); }
pid$target:GLEngine*:glSwap*_Exec:return /self->native_swap/
{ @native_swap_us=quantize((timestamp-self->native_swap)/1000);self->native_swap=0; }
/* UCMethods.h's appended DirectGLScatter selector31. Read the public private-
 * protocol action word only, not any caller pointer, descriptor or pixels.
 * Separate page preparation/release from completed stream I/O. */
pid$target:IOKit:IOConnectCallStructMethod:entry /arg1==31 && arg3>=16/
{ self->scatter_begin=timestamp;self->scatter_cpu=vtimestamp;
  self->scatter_action=*(uint32_t *)copyin(arg2,4);
  self->scatter_consumer=self->consumer_depth!=0; }
pid$target:IOKit:IOConnectCallStructMethod:return /self->scatter_begin/
{ @scatter_count[self->scatter_consumer,self->scatter_action]=count();
  @scatter_us[self->scatter_consumer,self->scatter_action]=sum((timestamp-self->scatter_begin)/1000);
  @scatter_cpu_us[self->scatter_consumer,self->scatter_action]=sum((vtimestamp-self->scatter_cpu)/1000);
  self->scatter_begin=0; }
pid$target:libVIMPassthroughOpenGL.dylib:DGReadDrawableRectRows:entry
{ self->frame_read_begin=timestamp; }
pid$target:libVIMPassthroughOpenGL.dylib:DGReadDrawableRectRows:return /self->frame_read_begin/
{ @frame_read_us[self->consumer_depth!=0]=quantize((timestamp-self->frame_read_begin)/1000);self->frame_read_begin=0; }
/* Some optimized client exports have no return probe on Mojave. Time their
 * GLI owners instead of mistaking an empty aggregation for no work. */
pid$target:GLEngine*:VIMPublishIOSurfaceWritesLocked:entry
{ self->import_read_begin=timestamp; }
pid$target:GLEngine*:VIMPublishIOSurfaceWritesLocked:return /self->import_read_begin/
{ @import_read_us[self->consumer_depth!=0]=quantize((timestamp-self->import_read_begin)/1000);self->import_read_begin=0; }
pid$target:GLEngine*:VIMRefreshSurface:entry
{ self->import_upload_begin=timestamp;self->import_uploaded=0; }
/* RefreshSurface's unchanged-seed check is not an upload. Keep it separate
 * from the actual import, which includes the guest-to-host pixel transfer. */
pid$target:libVIMPassthroughOpenGL.dylib:DGImportIOSurfaceTextureFormat:entry
/self->import_upload_begin/
{ self->import_uploaded=1; }
pid$target:GLEngine*:VIMRefreshSurface:return /self->import_upload_begin/
{ @import_upload_us[self->consumer_depth!=0,self->import_uploaded]=quantize((timestamp-self->import_upload_begin)/1000);
  @import_upload_total_us[self->consumer_depth!=0,self->import_uploaded]=sum((timestamp-self->import_upload_begin)/1000);
  @import_upload_count[self->consumer_depth!=0,self->import_uploaded]=count();
  self->import_upload_begin=0;self->import_uploaded=0; }
pid$target:IOSurface:IOSurfaceLock:entry { self->surface_lock_begin=timestamp; }
pid$target:IOSurface:IOSurfaceLock:return /self->surface_lock_begin/
{ @surface_lock_us[self->consumer_depth!=0]=quantize((timestamp-self->surface_lock_begin)/1000);self->surface_lock_begin=0; }
tick-12s { exit(0); }
END {
    printf("END present cost\n");printa("BOUNDARY %s count %@d\n",@boundaries);
    printa("SWAP microseconds %@d\n",@swaps);printa("FLUSH microseconds %@d\n",@flushes);printa("PRESENT microseconds %@d\n",@presents);
    /* vtimestamp is guest thread-accounted CPU time, not host CPU usage.
     * https://docs.oracle.com/cd/E18752_01/html/819-5488/gcfpz.html
     * Keep contexts separate: multiple windows may swap on the same thread. */
    printa("SWAP context=%p tid=%d count=%@d\n",@swap_context_count);
    printa("SWAP context=%p tid=%d elapsed-us=%@d\n",@swap_context_us);
    printa("SWAP context=%p tid=%d guest-cpu-us=%@d\n",@swap_context_cpu_us);
    printa("CONSUMER context=%p tid=%d count=%@d\n",@consumer_context_count);
    printa("CONSUMER context=%p tid=%d elapsed-us=%@d\n",@consumer_context_us);
    printa("CONSUMER context=%p tid=%d guest-cpu-us=%@d\n",@consumer_context_cpu_us);
    printa("SWAP integer query count %@d\n",@swap_integer_queries);printa("SWAP integer query total microseconds %@d\n",@swap_integer_query_us);
    printa("APPKIT consumer microseconds %@d\n",@consumer_us);
    printa("APPKIT consumer options=%x count %@d\n",@consumer_options);
    printa("CONSUMER API %s count=%@d\n",@consumer_api_count);
    printa("CONSUMER API %s elapsed-us=%@d\n",@consumer_api_us);
    printa("NATIVE CONSUMER API %s count=%@d\n",@native_consumer_api_count);
    printa("NATIVE SWAP count=%@d\n",@native_swaps);
    printa("NATIVE SWAP microseconds %@d\n",@native_swap_us);
    printa("SCATTER inside-consumer=%d action=%d count=%@d\n",@scatter_count);
    printa("SCATTER inside-consumer=%d action=%d elapsed-us=%@d\n",@scatter_us);
    printa("SCATTER inside-consumer=%d action=%d guest-cpu-us=%@d\n",@scatter_cpu_us);
    printa("FRAME READ inside-consumer=%d microseconds %@d\n",@frame_read_us);
    printa("IMPORT READ inside-consumer=%d microseconds %@d\n",@import_read_us);
    printa("IMPORT REFRESH inside-consumer=%d uploaded=%d microseconds %@d\n",@import_upload_us);
    printa("IMPORT REFRESH inside-consumer=%d uploaded=%d total-us=%@d\n",@import_upload_total_us);
    printa("IMPORT REFRESH inside-consumer=%d uploaded=%d count=%@d\n",@import_upload_count);
    printa("IOSURFACE LOCK inside-consumer=%d microseconds %@d\n",@surface_lock_us);
}
