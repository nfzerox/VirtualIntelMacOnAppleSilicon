// Mojave installs both renderer choices in one PVG graphics transaction. The
// boot-time selector adds only a command transport when selected in VM config;
// PVG remains the display and Metal accelerator in either mode.
static NSString *const VIMPassthroughOpenGLTransportTarget =
    @"Library/Extensions/PassthroughOpenGLTransport.kext";
static NSString *const VIMOriginalGLEngineRelativePath =
    @"Versions/A/Resources/PassthroughOpenGL/OriginalGLEngine";
static NSString *const VIMGLEngineRelativePath =
    @"Versions/A/Resources/GLEngine.bundle/GLEngine";

static inline BOOL VIMIsPassthroughOpenGLEngine(NSData *image) {
    // An exported project API, not an OS build number, digest or fixed offset.
    static const char symbol[]="_VIMGLINotifyCPUSurfaceCopy";
    NSData *name=[NSData dataWithBytes:symbol length:sizeof(symbol)];
    return [image rangeOfData:name options:0 range:NSMakeRange(0,image.length)].location!=NSNotFound;
}

static inline NSArray<NSString *> *VIMTargetsWithPassthroughOpenGL(NSArray<NSString *> *targets) {
    return [targets arrayByAddingObject:VIMPassthroughOpenGLTransportTarget];
}
