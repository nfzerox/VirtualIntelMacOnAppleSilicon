#pragma once
#import <Foundation/Foundation.h>
#import <CommonCrypto/CommonDigest.h>
#include <sys/stat.h>

// Host-only, cold installer preparation. The original DVD is always read-only.
// Accept usable Intel media, not just the originally qualified restore disc.
static inline NSString *VIMTigerDistributionSHA256(NSData *data) {
    if(!data||data.length>262144)return nil;
    unsigned char bytes[CC_SHA256_DIGEST_LENGTH];CC_SHA256(data.bytes,(CC_LONG)data.length,bytes);
    NSMutableString *text=[NSMutableString new];
    for(NSUInteger i=0;i<sizeof(bytes);i++)[text appendFormat:@"%02x",bytes[i]];
    return text;
}
static inline BOOL VIMTigerProfileError(NSError **error,NSString *text) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.TigerRestore" code:1 userInfo:@{NSLocalizedDescriptionKey:text}];
    return NO;
}
static inline BOOL VIMTigerProfileRegularFile(NSURL *URL) {
    struct stat info;return URL.isFileURL&&!lstat(URL.fileSystemRepresentation,&info)&&S_ISREG(info.st_mode);
}
static inline NSXMLDocument *VIMTigerSourceDistribution(NSData *data,NSError **error) {
    if(!data.length||data.length>262144){VIMTigerProfileError(error,@"The Tiger installation plan is missing or incomplete.");return nil;}
    NSXMLDocument *document=[[NSXMLDocument alloc] initWithData:data
        options:NSXMLNodePreserveAll|NSXMLNodeLoadExternalEntitiesNever error:error];
    NSXMLElement *root=document.rootElement;
    if(!document||document.DTD||![root.name isEqual:@"installer-gui-script"]) {
        VIMTigerProfileError(error,@"The Tiger installation plan could not be read safely. Its original disc was not changed.");return nil;
    }
    return document;
}
static inline BOOL VIMTigerValidateRestoreDVD(NSURL *mount,NSError **error) {
    if(!mount.isFileURL)return VIMTigerProfileError(error,@"Choose a local Tiger restore disc.");
    NSDictionary *version=[NSDictionary dictionaryWithContentsOfURL:[mount URLByAppendingPathComponent:@"System/Library/CoreServices/SystemVersion.plist"]];
    NSURL *package=[mount URLByAppendingPathComponent:@"System/Installation/Packages/OSInstall.mpkg"];
    NSURL *distribution=[package URLByAppendingPathComponent:@"Contents/OSInstall.dist"];
    NSString *release=version[@"ProductVersion"];
    if(![release isKindOfClass:NSString.class]||!([release isEqual:@"10.4"]||[release hasPrefix:@"10.4."])||
       !VIMTigerProfileRegularFile(distribution)||
       !VIMTigerProfileRegularFile([mount URLByAppendingPathComponent:@"mach_kernel"])||
       !VIMTigerProfileRegularFile([mount URLByAppendingPathComponent:@"System/Library/CoreServices/boot.efi"])||
       !VIMTigerProfileRegularFile([mount URLByAppendingPathComponent:@"Applications/Utilities/Installer.app/Contents/MacOS/Installer"]))
        return VIMTigerProfileError(error,@"Choose a complete Intel Tiger installation or restore disc with its native boot and installation files.");
    NSXMLDocument *document=VIMTigerSourceDistribution([NSData dataWithContentsOfURL:distribution],error);
    if(!document)return NO;
    // Native bundle distributions resolve relative URLs from the package
    // directory, NOT Contents/ (where this older Distribution is stored).
    for(NSXMLElement *reference in [document.rootElement elementsForName:@"pkg-ref"]) {
        NSString *path=[reference.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
        if(!path.length||[path hasPrefix:@"x-disc:"])continue;
        if([path hasPrefix:@"file:"])path=[path substringFromIndex:5];
        path=path.stringByRemovingPercentEncoding;
        if(!path.length||path.isAbsolutePath||[path containsString:@":"])
            return VIMTigerProfileError(error,@"The Tiger disc contains an unexpected installation package location.");
        NSURL *file=[[package URLByAppendingPathComponent:path] URLByStandardizingPath];
        NSString *prefix=[[mount URLByStandardizingPath].path stringByAppendingString:@"/"];
        if(![file.path hasPrefix:prefix])return VIMTigerProfileError(error,@"The Tiger disc contains a package location outside the disc.");
        // A different restore set may omit optional packages from Disc 1.
        // Preserve native package resolution instead of rejecting that set
        // before the user can try it. External-disc choices are pruned below.
    }
    return YES;
}
static inline NSData *VIMTigerPrepareDiscOneDistribution(NSData *source,NSDictionary **receipt,NSError **error) {
    NSXMLDocument *document=VIMTigerSourceDistribution(source,error);if(!document)return nil;
    NSXMLElement *root=document.rootElement;
    NSArray<NSXMLElement *> *checks=[root elementsForName:@"installation-check"],*outlines=[root elementsForName:@"choices-outline"];
    // Different retail/restore discs use different wrapper names and model
    // lists. Disable their installation eligibility hook in our COPY, rather
    // than requiring one exact model script. Volume checks, package scripts,
    // licenses and OS payload remain native and may still report real errors.
    for(NSXMLElement *check in checks){
        NSXMLNode *script=[check attributeForName:@"script"];
        if(script)script.stringValue=@"true";
    }
    NSMutableArray<NSXMLElement *> *branches=[NSMutableArray new];
    for(NSXMLElement *outline in outlines)
        for(NSXMLElement *line in [outline nodesForXPath:@".//line" error:NULL])
            if([[line attributeForName:@"choice"].stringValue isEqual:@"BundledSoftware.mpkg"])[branches addObject:line];
    NSMutableSet<NSString *> *optional=[NSMutableSet new];NSMutableArray<NSXMLElement *> *pending=[branches mutableCopy];
    while(pending.count){NSXMLElement *line=pending.lastObject;[pending removeLastObject];
        NSString *name=[line attributeForName:@"choice"].stringValue;
        if(!name.length){VIMTigerProfileError(error,@"The Tiger disc contains an incomplete package selection.");return nil;}
        [optional addObject:name];[pending addObjectsFromArray:[line elementsForName:@"line"]];
    }
    NSMutableSet *external=[NSMutableSet new];
    for(NSXMLElement *reference in [root elementsForName:@"pkg-ref"])
        if([[reference.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet] hasPrefix:@"x-disc:"]&&[reference attributeForName:@"id"].stringValue.length)
            [external addObject:[reference attributeForName:@"id"].stringValue];
    for(NSXMLElement *choice in [root elementsForName:@"choice"])
        for(NSXMLElement *reference in [choice elementsForName:@"pkg-ref"])
            if([external containsObject:[reference attributeForName:@"id"].stringValue]&&[choice attributeForName:@"id"].stringValue.length)
                [optional addObject:[choice attributeForName:@"id"].stringValue];
    NSMutableSet<NSString *> *removedReferences=[NSMutableSet new],*remainingReferences=[NSMutableSet new];NSUInteger removedChoices=0;
    for(NSXMLElement *choice in [[root elementsForName:@"choice"] copy]) {
        BOOL remove=[optional containsObject:[choice attributeForName:@"id"].stringValue];
        for(NSXMLElement *reference in [choice elementsForName:@"pkg-ref"]) {
            NSString *identifier=[reference attributeForName:@"id"].stringValue;
            if(identifier.length)[remove?removedReferences:remainingReferences addObject:identifier];
        }
        if(remove){[choice detach];removedChoices++;}
    }
    for(NSXMLElement *outline in outlines)
        for(NSXMLElement *line in [[outline nodesForXPath:@".//line" error:NULL] copy])
            if([optional containsObject:[line attributeForName:@"choice"].stringValue])[line detach];
    NSUInteger removedPackages=0,discTwoPackages=0;
    for(NSXMLElement *reference in [[root elementsForName:@"pkg-ref"] copy]) {
        NSString *identifier=[reference attributeForName:@"id"].stringValue;
        BOOL discTwo=[[reference.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet] hasPrefix:@"x-disc:"];
        if(discTwo||([removedReferences containsObject:identifier]&&![remainingReferences containsObject:identifier])) {
            removedPackages++;if(discTwo)discTwoPackages++;[reference detach];
        }
    }
    // Do not hardcode the names or number of system/app choices on a retail
    // or other model's restore disc. Keep every non-optional record unchanged.
    NSData *output=document.XMLData;
    if(!output.length||output.length>262144){VIMTigerProfileError(error,@"The prepared Tiger installation plan could not be saved.");return nil;}
    if(receipt)*receipt=@{@"FormatVersion":@1,@"TigerRestorePolicyVersion":@1,
        @"OriginalDistributionSHA256":VIMTigerDistributionSHA256(source),@"PreparedDistributionSHA256":VIMTigerDistributionSHA256(output),
        @"RestoreModelRestrictionRemoved":@YES,@"DiscTwoExcluded":@YES,@"RemovedOptionalChoices":@(removedChoices),@"RemovedOptionalPackages":@(removedPackages),@"RemovedDiscTwoPackages":@(discTwoPackages)};
    return output;
}
