/* Native high-precision CGLS buffers. GPU rendering/sharing stays in a typed
 * IOSurface. Flush completes the exact native samples into locked guest RAM;
 * no BGRA8 conversion, deferred CPU access, display blit or app-specific path.
 * The existing32-bit fast paths are untouched. */
static CGLError VIMImportTypedPBuffer(struct VIMContext *c,struct VIMCGSDrawable *d)
{
    if(d->pixel_format!=VIM_GL_PIXEL_RGBA16 || !d->unflipped || c->format->double_buffer ||
       d->surface<=2 || d->region_view || d->origin_x || d->origin_y ||
       d->width!=(int)d->layout.width || d->height!=(int)d->layout.height)return kCGLBadDrawable;
    struct VIMSurfaceMapping mapping;struct VIMSurfaceState state={0};
    if(VIMLockSurface(d->updates,&mapping))return kCGLBadDrawable;
    unsigned format=0;
    int good=VIMDrawableMappingMatches(d,&mapping) && VIMCGSState(d->updates,&state) &&
        VIMSurfaceFormat(d->updates,&mapping,&format) && format==d->pixel_format;
    if(good && d->gpu_lease && (d->gpu_object!=state.object || d->gpu_incarnation!=state.incarnation)) {
        DGReleaseSurface(d->gpu_lease);d->gpu_lease=0;
    }
    if(good && !d->gpu_lease){
        d->gpu_lease=DGAcquireTypedSurface(state.object,state.incarnation,d->width,d->height,format);
        d->gpu_object=state.object;d->gpu_incarnation=state.incarnation;
    }
    uint64_t epoch=0;good=good && d->gpu_lease && DGSurfaceEpoch(d->gpu_lease,&epoch);
    if(good && epoch<state.content_epoch)
        good=DGUploadSurface(d->gpu_lease,(void *)(uintptr_t)(mapping.address+mapping.layout.offset),
            d->width,d->height,mapping.layout.row_bytes,state.content_epoch);
    if(good)good=DGAttachSurfaceDrawable(d->gpu_lease);
    good=VIMCGSReadFinished(d->updates,&state) && good;
    return good?kCGLNoError:kCGLBadDrawable;
}
static void VIMPresentTypedPBuffer(struct VIMContext *c,struct VIMCGSDrawable *d)
{
    struct VIMSurfaceMapping mapping;struct VIMSurfaceState committed={0};
    if(VIMLockSurface(d->updates,&mapping)){c->error=GL_INVALID_OPERATION;return;}
    int good=VIMDrawableMappingMatches(d,&mapping) && d->pixel_format==VIM_GL_PIXEL_RGBA16;
    size_t row=(size_t)d->width*8,bytes=row*(size_t)d->height;
    good=good && bytes<=64u*1024u*1024u; // Transport bound, before padded staging.
    void *address=(void *)(uintptr_t)(mapping.address+mapping.layout.offset),*staged=NULL;
    int written=0;
    if(good && mapping.layout.row_bytes!=row){staged=malloc(bytes);if(!staged)good=0;}
    if(good){
        if(!staged)written=1; // A failed direct receive may have written a prefix.
        good=DGReadTypedDrawable(staged?staged:address,d->width,d->height,d->pixel_format);
        if(good && staged){
            for(int y=0;y<d->height;++y)memcpy((char *)address+(size_t)y*mapping.layout.row_bytes,
                (char *)staged+(size_t)y*row,row);
            written=1;
        }
    }
    free(staged);
    IOReturn unlocked=VIMUnlockSurfaceMode(d->updates,!written,&committed);
    good=good && !unlocked && committed.version==VIM_SURFACE_STATE_VERSION &&
        committed.object==d->gpu_object && committed.incarnation==d->gpu_incarnation &&
        d->gpu_lease && DGPublishSurfaceEpoch(d->gpu_lease,0,0,committed.content_epoch);
    if(!good)c->error=GL_INVALID_OPERATION;
}
