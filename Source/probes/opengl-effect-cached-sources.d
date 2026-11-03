/* Research-engine DWARF only, never native OS structure guesses. Manually
 * verify UUID 947A47CF-7AE9-3E54-BC82-3D46D9402625 / exact installed checksum
 * first. The same verified engine remains installed with SharedMirrors host.
 * 12-second bounded aggregates; no pixels, writes, frame timing or settings.
 */
#pragma D option quiet
#pragma D option aggsize=4m
BEGIN {printf("READY cached-source/current-view survey\n");}
pid$target:GLEngine:VIMBindSurfaceTexture:entry,
pid$target:GLEngine:VIMRefreshSurface:entry,
pid$target:GLEngine:VIMUpdateCGSLocked:entry,
pid$target:GLEngine:VIMGLIFlush:entry
{ @activity[probefunc]=count(); }
pid$target:GLEngine:VIMUpdateCGSLocked:entry
/arg0 && arg1 && arg3/
{
    this->t=(uint32_t *)copyin(arg1,24);
    this->object=*(uint64_t *)copyin(arg3+8,8);
    @sources[this->t[0],this->t[2],this->t[3],this->t[4],this->t[5],this->object,arg4]=count();
}
pid$target:GLEngine:VIMUpdateCGSLocked:entry
/arg0 && arg1 && arg3 && *(uint64_t *)copyin(arg0+0x1fa0,8)/
{
    /* copyin scratch pointers do not survive into a different clause.
     * Reacquire every structure here instead of retaining this->t above. */
    this->d=*(uint64_t *)copyin(arg0+0x1fa0,8);
    this->t=(uint32_t *)copyin(arg1,24);
    this->object=*(uint64_t *)copyin(arg3+8,8);
    this->size=(int32_t *)copyin(this->d+0x14,8);
    this->geometry=(int32_t *)copyin(this->d+0x7c,12);
    this->drawable_object=*(uint64_t *)copyin(this->d+0xc8,8);
    @views[this->t[0],this->t[2],this->t[3],this->size[0],this->size[1],
        this->geometry[0],this->geometry[1],this->geometry[2],this->object==this->drawable_object,arg4]=count();
}
tick-12s {exit(0);}
END {printf("activity\n");printa(@activity);
    printf("cached texture width height backing-width height object initial\n");printa(@sources);
    printf("texture import-width height view-width height origin-x y family same-object initial\n");printa(@views);}
