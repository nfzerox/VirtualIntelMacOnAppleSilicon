/* Actual guest client source API, scalar arguments only. DGImportSurfaceRegion
 * is an exported C function, not a private OS object layout. No client pointers,
 * pixels, names, shader text or wire payloads are read. Metadata, not FPS.
 */
#pragma D option quiet
BEGIN {printf("READY guest surface import geometry target=%d\n",$target);}
pid$target:libVIMPassthroughOpenGL.dylib:DGImportSurfaceRegion:entry
{
    @imports[(int32_t)arg2,(int32_t)arg3,(int32_t)arg4,(int32_t)arg5,(int32_t)arg6]=count();
}
pid$target:libVIMPassthroughOpenGL.dylib:DGAttachSurfaceDrawable:entry
{@direct_attachments=count();}
tick-30s {exit(0);}
END {
    printf("source_x source_y width height flip calls\n");
    printa("%d %d %d %d %d %@d\n",@imports);
    printf("direct attachments\n");printa(@direct_attachments);
}
