#import "VIMBigSurBootAssets.h"

static NSString *const Boot=@"AssetData/boot/";
static NSString *const Firmware=@"AssetData/boot/Firmware/";
static NSString *const Core=@"System/Library/CoreServices/";
static NSString *const KernelCollection=@"System/Library/KernelCollections/BootKernelExtensions.kc";

static id AssetError(NSError **error,NSString *message) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurBootAssets" code:1
        userInfo:@{NSLocalizedDescriptionKey:message}];
    return nil;
}

// Copy only the requested artifact's Apple manifests, not unrelated paths
// that happen to share the first part of its name.
static BOOL Manifests(NSString *prefix,NSString *artifact,BOOL flat,NSArray<NSString *> *members,
                      NSMutableDictionary<NSString *,NSString *> *output) {
    NSString *base=[prefix stringByAppendingString:artifact];
    NSString *start=[base stringByAppendingString:@"."];
    BOOL intel=NO;
    for(NSString *member in members) {
        if(![member hasPrefix:start])continue;
        NSString *suffix=[member substringFromIndex:start.length];
        if([suffix rangeOfString:@"^[a-z0-9]+\\.im4m$" options:NSRegularExpressionSearch].location==NSNotFound)continue;
        NSString *relative=[member substringFromIndex:prefix.length];
        if(flat)relative=relative.lastPathComponent;
        if(output[relative])return NO;
        output[relative]=member;
        if([suffix isEqual:@"x86legacyap.im4m"])intel=YES;
    }
    return intel;
}

NSDictionary *VIMBigSurBootAssetPlan(NSArray<NSString *> *members,NSError **error) {
    NSCountedSet *names=[NSCountedSet setWithArray:members];
    NSString *rootMember=nil;
    for(NSString *member in names)if([member hasPrefix:Firmware]&&[member hasSuffix:@".x86.root_hash"]&&
        [[member substringFromIndex:Firmware.length] isEqual:member.lastPathComponent]) {
        if(rootMember)return AssetError(error,@"The Big Sur Intel root hash is ambiguous.");
        rootMember=member;
    }
    if(!rootMember)return AssetError(error,@"The Big Sur Intel root hash is missing.");
    NSString *rootStem=[rootMember substringToIndex:rootMember.length-@"root_hash".length];
    NSMutableDictionary<NSString *,NSString *> *preboot=[NSMutableDictionary dictionary];
    NSMutableDictionary<NSString *,NSString *> *recovery=[NSMutableDictionary dictionary];
    for(NSString *name in @[@"BridgeVersion.plist",@"BridgeVersion.bin",@"PlatformSupport.plist",@"SystemVersion.plist"])
        preboot[[Core stringByAppendingString:name]]=[Boot stringByAppendingString:name];
    preboot[[Core stringByAppendingString:@"boot.efi"]]=[Firmware stringByAppendingString:@"usr/standalone/i386/boot.efi"];
    preboot[@"restore/BuildManifest.plist"]=[Boot stringByAppendingString:@"BuildManifest.plist"];
    NSString *fullRecovery=@"AssetData/Restore/BaseSystem.dmg";
    recovery[@"BaseSystem.dmg"]=[names containsObject:fullRecovery]?fullRecovery:VIMIntelRecoveryPatchMember;
    recovery[@"BaseSystem.chunklist"]=@"AssetData/Restore/BaseSystem.chunklist";
    for(NSString *name in @[@"PlatformSupport.plist",@"SystemVersion.plist"])
        recovery[name]=[Boot stringByAppendingString:name];

    NSString *prePrefix=[Firmware stringByAppendingString:@"Manifests/Preboot/"];
    NSString *recoveryPrefix=[Firmware stringByAppendingString:@"Manifests/RecoveryBoot/"];
    NSMutableArray *artifacts=[NSMutableArray arrayWithArray:@[[Core stringByAppendingString:@"boot.efi"],
        KernelCollection,[@"boot/" stringByAppendingString:KernelCollection]]];
    for(NSString *kind in @[@"root_hash",@"mtree"]) {
        NSString *relative=[@"usr/standalone/OS.dmg." stringByAppendingString:kind];
        preboot[relative]=[rootStem stringByAppendingString:kind];
        [artifacts addObject:relative];
    }
    for(NSString *kind in @[@"root_hash",@"mtree",@"trustcache"])
        [artifacts addObject:[@"Firmware/" stringByAppendingString:[rootStem.lastPathComponent stringByAppendingString:kind]]];
    for(NSString *artifact in artifacts)
        if(!Manifests(prePrefix,artifact,NO,members,preboot))
            return AssetError(error,[NSString stringWithFormat:@"The Intel Preboot manifest for %@ is missing or ambiguous.",artifact]);
    for(NSString *artifact in @[@"Firmware/BaseSystem.dmg.x86.trustcache",[Core stringByAppendingString:@"boot.efi"],
        [Core stringByAppendingString:@"bootbase.efi"],KernelCollection])
        if(!Manifests(recoveryPrefix,artifact,YES,members,recovery))
            return AssetError(error,[NSString stringWithFormat:@"The Intel Recovery manifest for %@ is missing or ambiguous.",artifact]);
    for(NSDictionary *tree in @[preboot,recovery])for(NSString *member in tree.allValues)
        if([names countForObject:member]!=1)
            return AssetError(error,[NSString stringWithFormat:@"An original Big Sur boot asset is missing or ambiguous: %@.",member]);
    return @{@"Preboot":preboot.copy,@"Recovery":recovery.copy,@"IntelRootHashMember":rootMember};
}

typedef struct {const uint8_t *p;size_t n;} DER;

static BOOL DERRead(DER *input,uint8_t tag,DER *value) {
    if(input->n<2||input->p[0]!=tag)return NO;
    size_t length=input->p[1],header=2;
    if(length&0x80) {
        size_t width=length&0x7f;length=0;
        if(!width||width>4||input->n<header+width||!input->p[header])return NO;
        while(width--)length=(length<<8)|input->p[header++];
        if(length<128)return NO;
    }
    if(length>input->n-header)return NO;
    *value=(DER){input->p+header,length};input->p+=header+length;input->n-=header+length;
    return YES;
}

NSData *VIMBigSurIntelRootHash(NSData *asset,NSError **error) {
    DER file={asset.bytes,asset.length},seq,magic,kind,description,payload;
    static const uint8_t geometry[16]={2,0,0,0,0,0,0,0,1,0,0,0,32,0,0,0};
    if(!DERRead(&file,0x30,&seq)||file.n||!DERRead(&seq,0x16,&magic)||magic.n!=4||memcmp(magic.p,"IM4P",4)||
       !DERRead(&seq,0x16,&kind)||kind.n!=4||memcmp(kind.p,"xsys",4)||!DERRead(&seq,0x16,&description)||
       !DERRead(&seq,0x04,&payload)||seq.n||payload.n<48||memcmp(payload.p,geometry,sizeof(geometry)))
        return AssetError(error,@"The Intel Big Sur root hash has an unsupported format.");
    return [NSData dataWithBytes:payload.p+sizeof(geometry) length:32];
}
