// Post-reboot evidence only. GUI usability and host media cleanup remain the
// coordinator's responsibility; this helper never deletes installer media.
// The live paravirtualized Metal device, after an actual GPU command
// returned the expected data. Shared by the transaction and native checks.
static id<MTLDevice> VerifiedPVGMetalDevice(void) {
    // Load Metal only during verification. Preparation must remain usable before
    // the guest has a driver, and does not link against the donated Metal stack.
    void *metal=dlopen("/System/Library/Frameworks/Metal.framework/Metal",RTLD_NOW|RTLD_LOCAL);
    Check(metal!=NULL,@"Cannot load Metal for driver verification");
    // A privileged agent need not have a system-default display device. On
    // Catalina MTLCreateSystemDefaultDevice can be nil while PVG is rendering
    // the desktop. Enumerate and require the actual PVG driver instead.
    CFArrayRef (*copyDevices)(void)=dlsym(metal,"MTLCopyAllDevices");
    Check(copyDevices!=NULL,@"Cannot enumerate Metal devices for verification");
    NSArray *devices=CFBridgingRelease(copyDevices());
    id<MTLDevice> device=nil;
    for(id<MTLDevice> candidate in devices) {
        for(Class cls=[candidate class];cls;cls=class_getSuperclass(cls)) {
            if([NSStringFromClass(cls) containsString:@"AppleParavirt"]){device=candidate;break;}
        }
        if(device)break;
    }
    Check(device!=nil,@"No paravirtualized Metal device is available after restart");
    id<MTLCommandQueue> queue=[device newCommandQueue];
    id<MTLBuffer> buffer=[device newBufferWithLength:4096 options:MTLResourceStorageModeShared];
    Check(queue&&buffer&&buffer.contents,@"Cannot allocate GPU verification resources");
    memset(buffer.contents,0,4096);
    id<MTLCommandBuffer> command=[queue commandBuffer];id<MTLBlitCommandEncoder> encoder=[command blitCommandEncoder];
    Check(command&&encoder,@"Cannot create GPU verification command");
    [encoder fillBuffer:buffer range:NSMakeRange(0,4096) value:0xA5];[encoder endEncoding];[command commit];
    // Let Metal signal completion or a real command error. First-boot TCG
    // scheduling is not a GPU failure, and a millisecond polling loop wastes
    // emulated CPU while the driver already provides a blocking wait.
    [command waitUntilCompleted];
    Check(command.status==MTLCommandBufferStatusCompleted,command.error.localizedDescription?:@"GPU verification did not complete successfully");
    for(NSUInteger i=0;i<4096;i++)Check(((unsigned char *)buffer.contents)[i]==0xA5,@"GPU verification returned incorrect data");
    return device;
}
// The prepared Metal plugin's helper, inside this boot's WindowServer, made
// SkyLight's window tiles private textures (PVGTiledBackingCompat.h), so the
// guest survives displays wider than 2048 pixels. Evidence, never a failure.
static BOOL PrivateWindowTilesActive(NSString *bootSession) {
    int fd=open("/private/var/tmp/com.pvgbackport.private-window-tiles",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return NO;
    struct stat info;char text[64]={0};ssize_t count=-1;
    BOOL regular=!fstat(fd,&info)&&S_ISREG(info.st_mode)&&info.st_nlink==1&&info.st_size<(off_t)sizeof(text);
    if(regular)count=read(fd,text,sizeof(text)-1);
    close(fd);
    struct passwd *owner=regular?getpwuid(info.st_uid):NULL;
    return count>0&&owner&&!strcmp(owner->pw_name,"_windowserver")&&
        [bootSession isKindOfClass:NSString.class]&&[[[NSString alloc] initWithBytes:text length:(NSUInteger)count encoding:NSUTF8StringEncoding] isEqual:bootSession];
}
static NSDictionary *VerifyLivePVG(NSDictionary *admission) {
    id<MTLDevice> device=VerifiedPVGMetalDevice();
    NSString *guest=VIMGuestForSystemVersion(admission[@"ProductVersion"]);
    NSMutableDictionary *evidence=[@{@"MetalCommandVerified":@YES,@"Device":device.name?:@"Apple PVG",
        @"ProductBuildVersion":admission[@"ProductBuildVersion"],@"VolumeUUID":admission[@"VolumeUUID"],
        @"BootSessionUUID":admission[@"BootSessionUUID"]} mutableCopy];
    if([VIMAssistantGuests() containsObject:guest?:@""]&&PrivateWindowTilesActive(admission[@"BootSessionUUID"]))
        evidence[@"PrivateWindowTiles"]=@YES;
    return evidence;
}
static NSDictionary *VerifyInstalledAfterReboot(NSURL *journal) {
    NSDictionary *admission=LiveAdmission();
    TrustedStagingPath(journal.path,NO,0);
    TrustedStagingPath([journal.path stringByAppendingString:@".result.plist"],NO,0);
    NSError *error=nil;VIMFileTransaction *transaction=[VIMFileTransaction loadURL:journal error:&error];
    Check(transaction!=nil,error.localizedDescription);
    NSDictionary *state=transaction.state;
    NSDictionary *result=[NSDictionary dictionaryWithContentsOfFile:[journal.path stringByAppendingString:@".result.plist"]];
    Check([result[@"Phase"] isEqual:@"awaiting-reboot"]&&[result[@"TransactionID"] isEqual:state[@"ID"]]&&
          [result[@"TransactionJournal"] isEqual:journal.path]&&[state[@"Root"] isEqual:@"/"],@"No matching completed installation awaiting reboot");
    Check([result[@"BootSessionUUID"] isKindOfClass:NSString.class]&&[result[@"BootSessionUUID"] length]>0&&
          ![result[@"BootSessionUUID"] isEqual:admission[@"BootSessionUUID"]],@"Restart the guest before verifying installation");
    Check([result[@"ProductBuildVersion"] isEqual:admission[@"ProductBuildVersion"]]&&
          [state[@"Metadata"][@"ProductBuildVersion"] isEqual:admission[@"ProductBuildVersion"]],@"Guest build changed after installation");
    Check([result[@"VolumeUUID"] isEqual:admission[@"VolumeUUID"]],@"Installation belongs to a different system volume");
    BOOL mojave=[admission[@"ProductVersion"] hasPrefix:@"10.14."];
    NSSet *retired=mojave?[NSSet setWithArray:@[@"Library/Preferences/com.apple.Aperture.plist",
        @"Library/Preferences/com.apple.iPhoto.plist"]]:[NSSet set];
    Check([transaction verifyPublishedExcludingPaths:retired error:&error],error.localizedDescription);
    NSString *family=VIMGuestForSystemVersion(admission[@"ProductVersion"]);
    BOOL bigsur=[VIMAssistantGuests() containsObject:family?:@""];
    NSString *guest=bigsur?family:(mojave?@"mojave":@"catalina");
    BOOL passthroughOpenGL=[state[@"Metadata"][@"Preparation"][@"PassthroughOpenGL"] isEqual:@YES];
    Check(TransactionTargetsMatchPolicy([state[@"Entries"] valueForKey:@"RelativePath"],guest,passthroughOpenGL),@"Installed targets differ from release policy");
    if(bigsur)Check([result[@"Cache"] isEqual:@{@"CacheRequired":@NO,@"Policy":@"library-only"}],@"Invalid library-only installation outcome");
    else Check([result[@"Cache"][@"CacheRebuilt"] isEqual:@YES],@"No successful boot-cache build was recorded");
    // macOS may legitimately regenerate its prelinked kernel after first boot.
    // Record that change; correctness is established by the published driver
    // files above and by an actual command on the live PVG device below.
    BOOL bootCacheChanged=NO;
    for(NSString *path in bigsur?@[]:CachePaths(mojave))
        bootCacheChanged|=![CacheHash([@"/" stringByAppendingPathComponent:path]) isEqual:result[@"Cache"][@"SHA256"]];
    id<MTLDevice> device=VerifiedPVGMetalDevice();
    // Big Sur must show both repairs. Later releases use the same recipe, but
    // only a working Metal command is required of them for now; their OpenGL
    // registration and texture repair are kept and checked separately later.
    if([guest isEqual:@"bigsur"]) {
        // Check the loaded native plugin, not just a copied file or a hardware
        // name. This metadata is what Core Image uses to assemble image tiles.
        Check([[[NSBundle bundleForClass:[device class]] bundlePath]
            isEqual:@"/Library/GPUBundles/AppleParavirtGPUMetal.bundle"],@"The repaired Metal plugin was not loaded");
        Class descriptorClass=NSClassFromString(@"MTLTextureDescriptor");
        MTLTextureDescriptor *descriptor=[descriptorClass texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:16 height:16 mipmapped:NO];
        descriptor.storageMode=MTLStorageModeManaged;descriptor.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;
        id<MTLTexture> texture=[device newTextureWithDescriptor:descriptor];
        Check(texture&&texture.usage==descriptor.usage,@"Metal texture metadata repair is not active");
        io_service_t accelerator=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("AppleParavirtAccelerator"));
        Check(accelerator!=IO_OBJECT_NULL,@"The live PVG accelerator is missing");
        id renderer=CFBridgingRelease(IORegistryEntryCreateCFProperty(accelerator,CFSTR("IOGLBundleName"),kCFAllocatorDefault,0));
        IOObjectRelease(accelerator);
        Check([renderer isEqual:@"AppleMetalOpenGLRenderer"],@"OpenGL registration is not active; restart with the app's prepared-guest EFI");
    }
    NSMutableDictionary *evidence=[NSMutableDictionary dictionary];
    if(bigsur&&PrivateWindowTilesActive(admission[@"BootSessionUUID"]))evidence[@"PrivateWindowTiles"]=@YES;
    [evidence addEntriesFromDictionary:@{@"VerifiedAfterReboot":@YES,@"MetalCommandVerified":@YES,@"BootCacheChangedAfterInstallation":@(bootCacheChanged),@"Device":device.name?:@"Apple PVG",
        @"ProductBuildVersion":admission[@"ProductBuildVersion"],@"VolumeUUID":admission[@"VolumeUUID"],
        @"BootSessionUUID":admission[@"BootSessionUUID"],@"TransactionID":state[@"ID"],@"Ready":@NO}];
    return evidence;
}
