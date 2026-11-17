#import "VIMAssistantCryptex.h"

static NSDictionary *CryptexError(NSError **error,NSString *message) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.AssistantCryptex" code:1
        userInfo:@{NSLocalizedDescriptionKey:message}];
    return nil;
}

NSDictionary *VIMAssistantIntelCryptexPlan(NSArray<NSString *> *members,NSDictionary *manifest,NSError **error) {
    NSArray *identities=manifest[@"BuildIdentities"];
    if(![identities isKindOfClass:NSArray.class])
        return CryptexError(error,@"The original macOS Cryptex manifest has no build identities.");
    NSMutableArray *retained=[NSMutableArray array];
    NSDictionary *intel=nil;
    for(id row in identities) {
        id info=[row isKindOfClass:NSDictionary.class]?row[@"Info"]:nil;
        id variant=[info isKindOfClass:NSDictionary.class]?info[@"Variant"]:nil;
        if(![variant isKindOfClass:NSString.class])
            return CryptexError(error,@"The original macOS Cryptex manifest has an invalid build identity.");
        // Apple leaves out these software-update-only identities from the
        // installed Cryptex manifest. Preserve every other signed identity
        // in its original order and leave their contents untouched.
        if([variant isEqual:@"Customer Software Update"])continue;
        [retained addObject:row];
        if([variant isEqual:@"CustomerInstall"]&&[info[@"DeviceClass"] isEqual:@"x86legacyap"]) {
            if(intel)return CryptexError(error,@"The original macOS installer has ambiguous Intel Cryptex identities.");
            if(![row[@"Manifest"] isKindOfClass:NSDictionary.class])
                return CryptexError(error,@"The original macOS installer has an invalid Intel Cryptex identity.");
            intel=row[@"Manifest"];
        }
    }
    if(![intel isKindOfClass:NSDictionary.class])
        return CryptexError(error,@"The original macOS installer is missing its Intel Cryptex identity.");

    NSString *boot=@"AssetData/boot/";
    NSString *ticketPrefix=[boot stringByAppendingString:@"Firmware/Manifests/Cryptex1Boot/"];
    NSMutableDictionary *files=[@{@"RestoreVersion.plist":[boot stringByAppendingString:@"RestoreVersion.plist"],
        @"SystemVersion.plist":[boot stringByAppendingString:@"SystemVersion.plist"]} mutableCopy];
    NSDictionary *images=@{@"app.dmg":@"AssetData/payloadv2/image_patches/cryptex-app",
        @"os.dmg":@"AssetData/payloadv2/image_patches/cryptex-system-x86_64"};
    NSDictionary *sidecars=@{@"app.dmg.root_hash":@"AppVolume",@"app.dmg.trustcache":@"AppTrustCache",
        @"os.dmg.root_hash":@"SystemVolume",@"os.dmg.trustcache":@"SystemTrustCache"};
    for(NSString *name in sidecars) {
        id item=intel[[@"Cryptex1," stringByAppendingString:sidecars[name]]];
        id info=[item isKindOfClass:NSDictionary.class]?item[@"Info"]:nil;
        id path=[info isKindOfClass:NSDictionary.class]?info[@"Path"]:nil;
        NSString *pattern=[name hasSuffix:@".root_hash"]?@"^Firmware/[A-Za-z0-9_-]+\\.dmg\\.root_hash$":
            @"^Firmware/[A-Za-z0-9_-]+\\.dmg\\.trustcache$";
        if(![path isKindOfClass:NSString.class]||[path rangeOfString:pattern options:NSRegularExpressionSearch].location==NSNotFound)
            return CryptexError(error,@"The original macOS installer has an invalid Intel Cryptex sidecar path.");
        files[name]=[boot stringByAppendingString:path];
    }
    for(NSString *member in members) {
        if(![member hasPrefix:ticketPrefix])continue;
        NSString *leaf=[member substringFromIndex:ticketPrefix.length];
        if([leaf rangeOfString:@"^apticket\\.[a-z0-9]+\\.im4m$" options:NSRegularExpressionSearch].location!=NSNotFound)
            files[leaf]=member;
    }
    if(!files[@"apticket.x86legacyap.im4m"])
        return CryptexError(error,@"The original macOS installer is missing its Intel Cryptex boot ticket.");
    NSCountedSet *names=[NSCountedSet setWithArray:members];
    for(NSDictionary *group in @[files,images])for(NSString *member in group.allValues)
        if([names countForObject:member]!=1)
            return CryptexError(error,[NSString stringWithFormat:@"An original macOS Cryptex asset is missing or ambiguous: %@.",member]);
    NSMutableDictionary *installedManifest=[manifest mutableCopy];
    installedManifest[@"BuildIdentities"]=retained.copy;
    return @{@"Files":files.copy,@"ImagePatches":images,@"BuildManifest":installedManifest.copy,
        @"ImageClones":@{@"os.clone.dmg":@"os.dmg"}};
}
