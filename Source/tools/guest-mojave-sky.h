// Symbol/branch-validated SkyLight port. All addresses come from this image.
static NSMutableData *PatchMojaveSky(NSData *original) {
    PVGCoreImage *image=CoreParse(original);
    NSRange bounds=CoreFunction(image,@"_CGXInitializeGL");
    uint64_t select=CoreSymbol(image,@"__ZL27activate_compositor_defaultv"),once=CoreSymbol(image,@"__ZZ15WSGVAInitializeE4once");
    (void)CoreFunction(image,@"__ZL27activate_compositor_defaultv");[image writable:once];
    NSArray *(^find)(NSString *,NSRange)=^NSArray *(NSString *pattern,NSRange range){return [image matches:pattern start:range.location end:NSMaxRange(range)];};
    void (^put)(uint64_t,NSData *)=^(uint64_t address,NSData *code){[image.data replaceBytesInRange:NSMakeRange([image offset:address size:code.length],code.length) withBytes:code.bytes];};
    NSMutableArray *calls=[NSMutableArray array],*guards=[NSMutableArray array];
    for(NSNumber *at in find(@"e8 ?? ?? ?? ??",bounds))if([image relative:at.unsignedLongLongValue displacement:1 length:5]==select)[calls addObject:at];
    for(NSNumber *at in find(@"48 83 3d ?? ?? ?? ?? ff 75 ??",bounds))if([image relative:at.unsignedLongLongValue displacement:3 length:8]==once)[guards addObject:at];
    BOOL old=calls.count==0;uint64_t common=0,cave=0;
    NSData *(^compositorCode)(uint64_t,uint64_t)=^NSData *(uint64_t c,uint64_t destination) {
        NSMutableData *code=[NSMutableData data];CoreRelative(code,@"e8",c,select,nil);
        CoreRelative(code,@"48833d",c,once,Hex(@"ff"));CoreRelative(code,@"e9",c,destination+8,nil);return code;
    };
    if(old) {
        NSMutableArray *matches=[NSMutableArray array];NSMutableDictionary *caves=[NSMutableDictionary dictionary];
        for(NSNumber *at in find(@"e9 ?? ?? ?? ?? 90 90 90 75 ??",bounds)) {
            uint64_t p=at.unsignedLongLongValue,c=[image relative:p displacement:1 length:5];NSData *expected=compositorCode(c,p);
            @try {if([[image read:c size:expected.length] isEqual:expected]){[matches addObject:at];caves[at]=@(c);}}
            @catch(NSException *exception){if(![exception.name isEqual:@"PatchRejected"])@throw;}
        }
        common=CoreOne(matches,@"existing compositor trampoline");cave=[caves[@(common)] unsignedLongLongValue];
        Require(guards.count==0,@"Mixed patched/unpatched compositor sites");
    } else {
        uint64_t success=CoreOne(calls,@"default compositor call");common=CoreOne(guards,@"common exit guard");
        Require(success<common,@"Unexpected compositor control flow");BOOL incoming=NO;
        for(NSNumber *at in find(@"e9 ?? ?? ?? ??",bounds))incoming|=[image relative:at.unsignedLongLongValue displacement:1 length:5]==common;
        for(unsigned condition=0x80;condition<=0x8f;condition++)
            for(NSNumber *at in find([NSString stringWithFormat:@"0f %02x ?? ?? ?? ??",condition],bounds))
                incoming|=[image relative:at.unsignedLongLongValue displacement:2 length:6]==common;
        Require(incoming,@"Common exit has no incoming branch");
        struct segment_command_64 s=image.textSegment;
        Require(s.fileoff==0&&(s.initprot&VM_PROT_EXECUTE),@"No executable header padding");
        NSUInteger offset=(image.commandEnd+256+15)&~(NSUInteger)15;
        Require(Bounds(offset,128,image.firstSection)&&Bounds(offset,128,image.data.length),@"Insufficient empty header padding");
        const uint8_t *bytes=image.data.bytes;for(NSUInteger i=offset;i<offset+128;i++)Require(bytes[i]==0,@"Nonzero header padding");
        cave=s.vmaddr+offset;put(cave,compositorCode(cave,common));put(success,Hex(@"9090909090"));
        NSMutableData *jump=[NSMutableData data];CoreRelative(jump,@"e9",common,cave,nil);[jump appendData:Hex(@"909090")];put(common,jump);
    }
    NSRange reportBounds=CoreFunction(image,@"__XServerMightHaveAcceleration");
    uint64_t gl=CoreSymbol(image,@"__cglsContext"),metal=CoreSymbol(image,@"_WSMetalCompositorEnabled");
    [image writable:gl];(void)CoreFunction(image,@"_WSMetalCompositorEnabled");
    NSMutableArray *reports=[NSMutableArray array];
    for(NSNumber *at in find(@"48 83 3d ?? ?? ?? ?? 00 0f 95 46 24",reportBounds))
        if([image relative:at.unsignedLongLongValue displacement:3 length:8]==gl)[reports addObject:at];
    uint64_t rc=cave+32;
    NSData *(^reportCode)(uint64_t)=^NSData *(uint64_t report) {
        NSMutableData *code=[NSMutableData data];CoreRelative(code,@"48833d",rc,gl,Hex(@"00"));
        [code appendData:Hex(@"75115756")];CoreRelative(code,@"e8",rc,metal,nil);[code appendData:Hex(@"5e5f884624")];
        CoreRelative(code,@"e9",rc,report+12,nil);[code appendData:Hex(@"c6462401")];CoreRelative(code,@"e9",rc,report+12,nil);return code;
    };
    if(reports.count) {
        uint64_t report=CoreOne(reports,@"acceleration report");
        Require([[image read:rc size:48] isEqual:[NSData dataWithBytes:(uint8_t[48]){0} length:48]],@"Report trampoline padding occupied");
        put(rc,reportCode(report));NSMutableData *jump=[NSMutableData data];CoreRelative(jump,@"e9",report,rc,nil);
        [jump appendData:Hex(@"90909090909090")];put(report,jump);
    } else {
        Require(old,@"Unknown acceleration report");
        for(NSNumber *at in find(@"e9 ?? ?? ?? ?? 90 90 90 90 90 90 90",reportBounds))
            if([image relative:at.unsignedLongLongValue displacement:1 length:5]==rc)[reports addObject:at];
        uint64_t report=CoreOne(reports,@"existing acceleration report");NSData *expected=reportCode(report);
        Require([[image read:rc size:expected.length] isEqual:expected],@"Unknown acceleration trampoline");
    }
    uint64_t report=CoreOne(reports,@"acceleration report relocation");
    CoreFixSplitSeg(image,@[@[@(cave+1),@(select)],@[@(cave+8),@(once)],
        @[@(cave+14),@(common+8)],@[@(common+1),@(cave)],
        @[@(rc+3),@(gl)],@[@(rc+13),@(metal)],@[@(rc+23),@(report+12)],
        @[@(rc+32),@(report+12)],@[@(report+1),@(rc)]],
        @[[NSValue valueWithRange:NSMakeRange(common,8)],[NSValue valueWithRange:NSMakeRange(report,12)]]);
    // Mojave's IOKit opens legacy surface client 0 even for Surface2 requests.
    // PVG requires client 32. Retarget only this x86_64 SkyLight dependency;
    // native i386 and all other IOKit clients stay untouched. Keep command
    // sizes/ordinals stable so existing bind and split-segment data stays valid.
    struct mach_header_64 currentHeader;memcpy(&currentHeader,image.data.bytes,sizeof(currentHeader));
    NSUInteger cursor=sizeof(currentHeader),matches=0,commandEnd=sizeof(currentHeader)+currentHeader.sizeofcmds;
    Require(commandEnd<=image.data.length,@"Invalid SkyLight commands");
    while(cursor<commandEnd) {
        struct load_command lc;memcpy(&lc,(const uint8_t *)image.data.bytes+cursor,sizeof(lc));
        Require(lc.cmdsize>=sizeof(lc)&&Bounds(cursor,lc.cmdsize,commandEnd),@"Invalid SkyLight dependency command");
        if(lc.cmd==LC_LOAD_DYLIB) {
            struct dylib_command dylib;Require(lc.cmdsize>=sizeof(dylib),@"Short SkyLight dylib command");
            memcpy(&dylib,(const uint8_t *)image.data.bytes+cursor,sizeof(dylib));
            Require(dylib.dylib.name.offset>=sizeof(dylib)&&dylib.dylib.name.offset<lc.cmdsize,@"Invalid SkyLight dylib name");
            NSUInteger offset=cursor+dylib.dylib.name.offset,capacity=lc.cmdsize-dylib.dylib.name.offset;
            const char *name=(const char *)image.data.bytes+offset;
            Require(memchr(name,0,capacity)!=NULL,@"Unterminated SkyLight dependency");
            if(!strcmp(name,"/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit")||
               !strcmp(name,"@loader_path/PVGGLIOKit.dylib")||!strcmp(name,"/usr/lib/PVGGLIOKit.dylib")) {
                // Restricted dyld3 launch closures (e.g. XProtect) need an
                // absolute dependency, even though ordinary GUI apps resolve
                // the former loader-relative spelling successfully.
                const char *replacement="/usr/lib/PVGGLIOKit.dylib";
                Require(strlen(replacement)+1<=capacity,@"Insufficient Surface2 dependency space");
                memset((uint8_t *)image.data.mutableBytes+offset,0,capacity);
                memcpy((uint8_t *)image.data.mutableBytes+offset,replacement,strlen(replacement));matches++;
            }
        }
        cursor+=lc.cmdsize;
    }
    Require(matches==1,@"Expected one SkyLight IOKit/Surface2 dependency");
    return CorePreservingSlices(original,image);
}
