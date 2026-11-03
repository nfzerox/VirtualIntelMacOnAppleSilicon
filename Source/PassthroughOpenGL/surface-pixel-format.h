/* A 64-bit layout is insufficient to distinguish fixed16 from half float.
 * Ask the locked provider for its actual canonical format; never infer it
 * from dimensions, context color depth or the caller's texture request. */
static int VIMSurfaceFormat(io_connect_t connection,const struct VIMSurfaceMapping *mapping,unsigned *format)
{
    struct VIMSurfacePixelFormat observed={0};size_t bytes=sizeof(observed);
    IOReturn error=IOConnectCallStructMethod(connection,kIOVM2DGetSurfacePixelFormat,NULL,0,&observed,&bytes);
    if(error || bytes!=sizeof(observed) || observed.version!=VIM_SURFACE_PIXEL_FORMAT_VERSION || observed.reserved ||
       !VIMGLPixelBytes(observed.format) || observed.bits_per_pixel!=mapping->layout.bits_per_pixel ||
       observed.bits_per_pixel!=VIMGLPixelBytes(observed.format)*8U ||
       (observed.format && !DGSupportsTypedSurfaces()))return 0;
    *format=observed.format;return 1;
}
