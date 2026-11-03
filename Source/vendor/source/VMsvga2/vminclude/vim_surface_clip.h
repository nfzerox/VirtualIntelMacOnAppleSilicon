/* Normalize the public IOAccel region into the explicit research clip ABI.
 * Include after IOAccelSurfaceConnect.h and vim_framebuffer.h. Kept free of
 * kernel objects so the exact production conversion can be tested in user
 * space without weakening WindowServer's exclusive surface-creation policy. */
#ifndef VIM_SURFACE_CLIP_CONVERSION_H
#define VIM_SURFACE_CLIP_CONVERSION_H
static inline bool VIMSurfaceClipFromRegion(const IOAccelDeviceRegion *region,
    size_t bytes,int32_t buffer_x,int32_t buffer_y,struct VIMSurfaceClip *clip)
{
    if(!clip || !region || bytes<sizeof(*region) ||
       region->num_rects>VIM_SURFACE_CLIP_RECTS ||
       bytes<sizeof(*region)+(size_t)region->num_rects*sizeof(IOAccelBounds))return false;
    clip->version=VIM_SURFACE_CLIP_VERSION;
    /* IOAccel count zero means bounds, NOT empty. An empty shape is cleared
     * by set_shape and returns NotReady before reaching this conversion. */
    clip->count=region->num_rects?region->num_rects:1;
    clip->origin_x=(int32_t)region->bounds.x-buffer_x;
    clip->origin_y=(int32_t)region->bounds.y-buffer_y;
    for(uint32_t i=0;i<clip->count;++i) {
        const IOAccelBounds *r=region->num_rects?&region->rect[i]:&region->bounds;
        clip->rects[i].x=r->x;clip->rects[i].y=r->y;
        clip->rects[i].w=r->w;clip->rects[i].h=r->h;
    }
    return true;
}
#endif
