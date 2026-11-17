#import <Foundation/Foundation.h>
#import "VIMFlatHostUpdateRecipes.h"

NS_INLINE BOOL VIMHostInstallUpdatesEnabled(NSDictionary *job) {
    NSNumber *choice=job[@"DownloadSecurityUpdates"];
    return choice?choice.boolValue:![job[@"UserSelectedInstaller"] boolValue];
}
NS_INLINE BOOL VIMHostInstallWantsUpdates(NSDictionary *job,BOOL downloadOnly,NSDictionary *installer) {
    return [job[@"HostInstall"] boolValue]&&!downloadOnly&&VIMHostInstallUpdatesEnabled(job)&&
        installer[@"HostUpdates"]!=nil;
}

NS_INLINE BOOL VIMOptionalUpdateDownloadUnavailable(NSError *error) {
    if(!error)return NO;
    if([error.domain isEqual:NSURLErrorDomain]) {
        // A missing CDN payload may fall back to the genuine base installer.
        // Cancelling or failing to write the local download must not do so.
        switch(error.code) {
            case NSURLErrorCancelled:
            case NSURLErrorFileDoesNotExist:
            case NSURLErrorFileIsDirectory:
            case NSURLErrorNoPermissionsToReadFile:
            case NSURLErrorCannotCreateFile:
            case NSURLErrorCannotOpenFile:
            case NSURLErrorCannotCloseFile:
            case NSURLErrorCannotWriteToFile:
            case NSURLErrorCannotRemoveFile:
            case NSURLErrorCannotMoveFile:
                return NO;
            default:return YES;
        }
    }
    return [error.domain isEqual:@"VirtualIntelMac.Download"]&&error.code==2;
}

// The complete original sequence is required; never install a lone security hop.
NS_INLINE NSDictionary *VIMHostInstallUpdateRequest(NSDictionary *job,NSDictionary *media) {
    if(!VIMHostInstallUpdatesEnabled(job))return @{};
    if(job[@"HostUpdateNotice"])return @{@"HostUpdateNotice":job[@"HostUpdateNotice"]};
    if([job[@"Guest"] isEqual:@"tiger"]&&media[@"update-combo"]&&media[@"update-security"])
        return @{@"TigerUpdates":@{@"Combo":media[@"update-combo"],@"Security":media[@"update-security"]}};
    if([job[@"Guest"] isEqual:@"leopard"]&&media[@"update-combo"]&&media[@"update-security"])
        return @{@"LeopardUpdates":@{@"Combo":media[@"update-combo"],@"Security":media[@"update-security"]}};
    NSString *key=@{@"snowleopard":@"SnowLeopardUpdates",@"lion":@"LionUpdates",
                    @"mountainlion":@"MountainLionUpdates",@"mavericks":@"MavericksUpdates",
                    @"yosemite":@"YosemiteUpdates",@"elcapitan":@"ElCapitanUpdates",
                    @"sierra":@"SierraUpdates",@"highsierra":@"HighSierraUpdates",@"mojave":@"MojaveUpdates",
                    @"catalina":@"CatalinaUpdates"}[job[@"Guest"]?:@""];
    if(key) {
        NSMutableDictionary *inputs=[NSMutableDictionary new];
        for(NSDictionary *step in VIMFlatHostUpdateRecipe(job[@"Guest"])) {
            NSString *role=step[@"Role"],*path=media[[@"update-" stringByAppendingString:role.lowercaseString]];
            if(!path)return @{};
            inputs[role]=path;
        }
        return @{key:inputs};
    }
    return @{};
}
