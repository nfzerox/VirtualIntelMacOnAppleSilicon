/* Read-only, finite actual-process video producer observation on Snow Leopard.
 * Run as guest root AFTER starting playback:
 *   /usr/sbin/dtrace -Z -p QUICKTIME_PID -s opengl-snow-video-producer.d
 * Repeat for an actual QTKit/decoder child if the process inventory finds one.
 * No framework injection, private object offsets, shader/pixel data or files.
 * Exact10J3250 CoreVideo has both public-CGL and private-CGLS import paths;
 * the latter calls CoreGraphics cglsSetInteger with the same0x38e record.
 * Counts are aggregated, imports/stacks are capped, and tracing ends after15s.
 * Instrumentation has overhead: use for causal paths, not FPS measurement.
 */
#pragma D option quiet
#pragma D option bufsize=1m
#pragma D option aggsize=1m

dtrace:::BEGIN
{
    printf("Snow video producer trace pid=%d; 15-second read-only observation\n",$target);
}

pid$target:CoreVideo:CVOpenGLTextureCacheCreate:entry,
pid$target:CoreVideo:CVOpenGLTextureCacheCreateCGLS:entry,
pid$target:CoreVideo:CVOpenGLTextureCacheCreateTextureFromImage:entry,
pid$target:CoreVideo:*texImageIOSurface*:entry,
pid$target:CoreVideo:*createTextureFromImage*:entry,
pid$target:CoreVideo:*createTextureBackingForContext*:entry,
pid$target:CoreVideo:*createStandardTexture*:entry,
pid$target:CoreVideo:*updateStandardTexture*:entry,
pid$target:CoreVideo:*update2vuyPBufferTexture*:entry,
pid$target:CoreVideo:*isPixelBufferBackingSupported*:entry,
pid$target:CoreVideo:CVPixelBufferGetIOSurface:entry,
pid$target:CoreVideo:CVPixelBufferLockBaseAddress:entry,
pid$target:CoreVideo:CVPixelBufferUnlockBaseAddress:entry,
pid$target:*:CGLTexImageIOSurface2D:entry,
pid$target:*:CGLFlushDrawable:entry,
pid$target:*:CGLTexImagePBuffer:entry,
pid$target:*:cglsTexImagePBuffer:entry,
pid$target:*:QTVisualContextCopyImageForTime:entry,
pid$target:*:CAImageQueueInsertImage:entry,
pid$target:*:CAImageQueueCreate:entry,
pid$target:*:glTexSubImage2D:entry,
pid$target:*:glTexImage2D:entry,
pid$target:*:glProgramStringARB:entry,
pid$target:*:glCompileShader:entry,
pid$target:*:glCompileShaderARB:entry
{
    @calls[probemod,probefunc]=count();
}

pid$target:*:gliSetInteger:entry,
pid$target:*:cglsSetInteger:entry
{
    @integerKeys[probefunc,(uint32_t)arg1]=count();
}

pid$target:*:gliSetInteger:entry,
pid$target:*:cglsSetInteger:entry
/(uint32_t)arg1==0x38e && arg2 && imports<8/
{
    imports++;
    this->words=(uint32_t *)copyin(arg2,32);
    printf("IMPORT %s ctx=%p id=%u target=%x internal=%x size=%ux%u format=%x type=%x plane=%u\n",
        probefunc,arg0,this->words[0],this->words[1],this->words[2],this->words[3],
        this->words[4],this->words[5],this->words[6],this->words[7]);
}

pid$target:CoreVideo:*texImageIOSurface*:entry,
pid$target:CoreVideo:CVOpenGLTextureCacheCreateTextureFromImage:entry,
pid$target:*:CGLTexImageIOSurface2D:entry,
pid$target:*:CAImageQueueInsertImage:entry,
pid$target:*:QTVisualContextCopyImageForTime:entry
/stacks<12/
{
    stacks++;
    printf("FIRST-PATH %s:%s pid=%d tid=%d\n",probemod,probefunc,pid,tid);
    ustack(12);
}

pid$target:CoreVideo:CVOpenGLTextureCacheCreateTextureFromImage:return,
pid$target:CoreVideo:CVPixelBufferLockBaseAddress:return,
pid$target:CoreVideo:CVPixelBufferUnlockBaseAddress:return,
pid$target:*:CGLTexImageIOSurface2D:return,
pid$target:*:gliSetInteger:return,
pid$target:*:cglsSetInteger:return,
pid$target:*:QTVisualContextCopyImageForTime:return
{
    @results[probemod,probefunc,(int)arg1]=count();
}

pid$target:CoreVideo:CVPixelBufferGetPixelFormatType:return,
pid$target:IOSurface:IOSurfaceGetPixelFormat:return
{
    @pixelFormats[probemod,probefunc,(uint32_t)arg1]=count();
}

tick-15s
{
    exit(0);
}

dtrace:::END
{
    printf("\n--- producer calls ---\n");
    printa("%-18s %-76s %@d\n",@calls);
    printf("\n--- actual integer keys ---\n");
    printa("%-24s key=%#x count=%@d\n",@integerKeys);
    printf("\n--- native return codes ---\n");
    printa("%-18s %-76s result=%d count=%@d\n",@results);
    printf("\n--- observed native pixel formats ---\n");
    printa("%-18s %-48s format=%#x count=%@d\n",@pixelFormats);
}
