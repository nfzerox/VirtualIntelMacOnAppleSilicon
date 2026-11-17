/* Adapted from LightClick (MIT), commit52d12048; its notice accompanies the
 * bundled HybridHypervisor source. Included after the host structs. No logging.
 * CGS attachment imports before returning control to the client. Initially
 * undefined BACK may take the same snapshot as FRONT; subsequent imports may
 * not replace rendered BACK. A prior BACK export can admit foreign writes and
 * therefore disqualifies initialization, even if its producer serial is zero. */
static pthread_once_t dg_initial_backing_once = PTHREAD_ONCE_INIT;
static int dg_initial_backing_enabled;
static void DGInitialBackingInit(void) {
    dg_initial_backing_enabled = VIMInitialBackingEnabled(getenv("VIM_INITIAL_BACK_SEED"));
}
static int DGInitialBackingEnabled(void) {
    pthread_once(&dg_initial_backing_once, DGInitialBackingInit);
    return dg_initial_backing_enabled;
}
/* The normal FRONT import has already flushed GPU ownership. Failure here
 * requests the existing normalized texture-upload path, without changing the
 * normal FRONT result or consuming/overwriting its GL error state. */
static int DGInitialBackingCPU(struct DGHostContext *c, const void *pixels, size_t stride,
                                size_t width, size_t height, int reverse) {
    IOSurfaceRef back = c->back_storage;
    if (!back || IOSurfaceGetPlaneCount(back) || IOSurfaceGetBytesPerElement(back) != 4 ||
        IOSurfaceGetPixelFormat(back) != 0x42475241 || IOSurfaceGetWidth(back) < width ||
        IOSurfaceGetHeight(back) < height)
        return 0;
    size_t pitch = IOSurfaceGetBytesPerRow(back), bytes = IOSurfaceGetAllocSize(back);
    if (pitch > DG_MAX_BYTES || bytes > DG_MAX_BYTES || pitch < width * 4 ||
        (height - 1) * pitch + width * 4 > bytes)
        return 0;
    if (IOSurfaceLock(back, 0, NULL) != kIOReturnSuccess)
        return 0;
    unsigned char *base = IOSurfaceGetBaseAddress(back);
    if (base)
        for (size_t row = 0; row < height; ++row)
            memcpy(base + row * pitch, (const unsigned char *)pixels +
                       (reverse ? height - 1 - row : row) * stride, width * 4);
    kern_return_t unlocked = IOSurfaceUnlock(back, 0, NULL);
    return base && unlocked == kIOReturnSuccess;
}
/* Caller owns the existing canonical unpack/color-transfer scope and restores
 * its texture binding. Like the normal FRONT upload, GL errors stay observable
 * through the normal error path; no added glGetError query consumes them. */
static void DGInitialBackingUpload(struct DGHostContext *c, const void *pixels, size_t width, size_t height) {
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, c->back);
    glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, (GLsizei)width, (GLsizei)height, GL_BGRA,
                   GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
}
