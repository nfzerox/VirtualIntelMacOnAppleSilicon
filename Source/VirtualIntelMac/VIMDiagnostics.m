#import "VIMDiagnostics.h"
#import "VIMLibraryLocation.h"
#import "VIMAlerts.h"

static NSString *Redact(NSString *text) {
    // Logs can mention a guest username, home/library path, MAC or forwarded IP.
    // Export neither user content nor QEMU command lines containing the SMC key.
    NSArray *patterns=@[@"/Users/[^/\\s\\\"']+",@"/Volumes/[^/\\s\\\"']+",
        @"(?i)\\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\\b",@"\\b(?:[0-9]{1,3}\\.){3}[0-9]{1,3}\\b",
        @"(?i)(?:password|passphrase|osk)=[^\\s,]+"];
    text=[text stringByReplacingOccurrencesOfString:NSHomeDirectory() withString:@"<home>"];
    for(NSString *pattern in patterns) {
        NSRegularExpression *expression=[NSRegularExpression regularExpressionWithPattern:pattern options:0 error:NULL];
        text=[expression stringByReplacingMatchesInString:text options:0 range:NSMakeRange(0,text.length) withTemplate:@"<redacted>"];
    }
    return text;
}
static NSData *Tail(NSURL *URL) {
    // Refuse symlinks/devices and bound work even for a huge development log.
    NSDictionary *attributes=[NSFileManager.defaultManager attributesOfItemAtPath:URL.path error:NULL];
    if(![attributes[NSFileType] isEqual:NSFileTypeRegular])return nil;
    NSFileHandle *file=[NSFileHandle fileHandleForReadingFromURL:URL error:NULL];
    if(!file)return nil;
    @try {
        unsigned long long size=[file seekToEndOfFile];
        [file seekToFileOffset:size>262144?size-262144:0];
        NSData *data=[file readDataOfLength:262144];[file closeFile];return data;
    } @catch(NSException *exception) { [file closeFile];return nil; }
}
static NSDictionary *ReadRecord(NSURL *URL) {
    NSData *data=Tail(URL);
    id value=data?[NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:NULL error:NULL]:nil;
    return [value isKindOfClass:NSDictionary.class]?value:nil;
}
static id RedactValue(id value) {
    if([value isKindOfClass:NSString.class])return Redact(value);
    if([value isKindOfClass:NSArray.class]) {
        NSMutableArray *result=[NSMutableArray new];for(id item in value)[result addObject:RedactValue(item)];return result;
    }
    if([value isKindOfClass:NSDictionary.class]) {
        NSMutableDictionary *result=[NSMutableDictionary new];for(id key in value)result[key]=RedactValue(value[key]);return result;
    }
    return value;
}
static BOOL WriteRecord(NSDictionary *record,NSURL *URL,NSError **error) {
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:RedactValue(record) format:NSPropertyListXMLFormat_v1_0 options:0 error:error];
    return data&&[data writeToURL:URL options:NSDataWritingAtomic error:error];
}
@implementation VIMDiagnostics
+ (void)exportFromWindow:(NSWindow *)window {
    NSSavePanel *panel=[NSSavePanel savePanel];panel.title=@"Export Diagnostics";
    panel.nameFieldStringValue=@"Virtual Intel Mac Diagnostics.zip";
    panel.message=@"Includes app and virtual Mac configuration summaries and recent logs. Review the archive before sharing it.";
    panel.allowedFileTypes=@[@"zip"];panel.canCreateDirectories=YES;
    NSURL *library=VIMLibraryLocation.machinesURL;
    void (^complete)(NSModalResponse)=^(NSModalResponse response){
        if(response!=NSModalResponseOK)return;
        NSURL *destination=panel.URL;
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
            NSError *error=nil;
            [self writeArchiveToURL:destination library:library error:&error];
            dispatch_async(dispatch_get_main_queue(),^{
                if(error)VIMPresentError(error,window);
                else [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[destination]];
            });
        });
    };
    if(window)[panel beginSheetModalForWindow:window completionHandler:complete];
    else [panel beginWithCompletionHandler:complete];
}
+ (BOOL)writeArchiveToURL:(NSURL *)destination library:(NSURL *)library error:(NSError **)outError {
            NSFileManager *fm=NSFileManager.defaultManager;NSError *error=nil;
            NSURL *temporary=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString] isDirectory:YES];
            NSURL *folder=[temporary URLByAppendingPathComponent:@"Virtual Intel Mac Diagnostics" isDirectory:YES];
            if([fm createDirectoryAtURL:folder withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:&error]) {
                NSDictionary *info=NSBundle.mainBundle.infoDictionary;
#if defined(__arm64__)
                NSString *architecture=@"arm64";
#else
                NSString *architecture=@"x86_64";
#endif
                BOOL ok=WriteRecord(@{@"AppVersion":info[@"CFBundleShortVersionString"]?:@"",
                    @"AppBuild":info[@"CFBundleVersion"]?:@"",@"HostOS":NSProcessInfo.processInfo.operatingSystemVersionString,
                    @"AppArchitecture":architecture,
                    @"ExportedAt":NSDate.date},[folder URLByAppendingPathComponent:@"App.plist"],&error);
                NSArray *keys=@[@"Guest",@"CPUCount",@"MemoryMiB",@"DiskGiB",@"DisplayMode",@"RetinaEnabled",@"NetworkConnected",@"NetworkMode",@"BridgeInterface",@"AudioEnabled"];
                NSArray *records=@[@"GuestStatus.plist",@"GuestInstallation.plist",@"GuestPreparation.plist"];
                NSArray *reportKeys=@[@"Phase",@"Guest",@"ProductVersion",@"ProductBuildVersion",@"KernelBuild",@"DesktopRunning",@"SetupComplete",@"RebootRequired",@"ProtocolVersion",@"UpdatedAt",@"Error",@"Message"];
                NSUInteger index=0;
                for(NSURL *machine in [fm contentsOfDirectoryAtURL:library includingPropertiesForKeys:@[NSURLIsSymbolicLinkKey] options:NSDirectoryEnumerationSkipsHiddenFiles error:NULL]) {
                    if(!ok||index>=100)break;
                    NSNumber *link=nil;[machine getResourceValue:&link forKey:NSURLIsSymbolicLinkKey error:NULL];
                    if(link.boolValue||![machine.pathExtension isEqual:@"vintelmac"])continue;
                    NSURL *out=[folder URLByAppendingPathComponent:[NSString stringWithFormat:@"Mac %lu",(unsigned long)++index]];
                    ok=[fm createDirectoryAtURL:out withIntermediateDirectories:NO attributes:nil error:&error];
                    NSDictionary *config=ReadRecord([machine URLByAppendingPathComponent:@"Machine.plist"]);
                    NSMutableDictionary *summary=[NSMutableDictionary new];
                    for(NSString *key in keys)if(config[key])summary[key]=config[key];
                    if(ok)ok=WriteRecord(summary,[out URLByAppendingPathComponent:@"Configuration.plist"],&error);
                    for(NSString *name in records) {
                        NSDictionary *record=ReadRecord([machine URLByAppendingPathComponent:[@"State/" stringByAppendingString:name]]);
                        if(!record)continue;
                        NSMutableDictionary *selected=[NSMutableDictionary new];
                        NSDictionary *report=[record[@"Report"] isKindOfClass:NSDictionary.class]?record[@"Report"]:record;
                        for(NSString *key in reportKeys)if(report[key])selected[key]=report[key];
                        if(ok)ok=WriteRecord(selected,[out URLByAppendingPathComponent:name],&error);
                    }
                    for(NSString *name in @[@"session.log",@"serial.log",@"firmware.log"]) {
                        NSData *data=Tail([machine URLByAppendingPathComponent:[@"State/" stringByAppendingString:name]]);
                        NSString *text=data?[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]:nil;
                        if(ok&&text)ok=[[Redact(text) dataUsingEncoding:NSUTF8StringEncoding] writeToURL:[out URLByAppendingPathComponent:name] options:NSDataWritingAtomic error:&error];
                    }
                }
                if(ok) {
                    NSFileCoordinator *coordinator=[[NSFileCoordinator alloc] initWithFilePresenter:nil];
                    __block NSError *copyError=nil;
                    [coordinator coordinateReadingItemAtURL:folder options:NSFileCoordinatorReadingForUploading error:&error byAccessor:^(NSURL *archive){
                        // Save-panel confirmation authorizes replacing only this exact file.
                        if([fm fileExistsAtPath:destination.path]) {
                            NSURL *staged=[temporary URLByAppendingPathComponent:@"Export.zip"];
                            if([fm copyItemAtURL:archive toURL:staged error:&copyError])
                                [fm replaceItemAtURL:destination withItemAtURL:staged backupItemName:nil options:0 resultingItemURL:NULL error:&copyError];
                        } else [fm copyItemAtURL:archive toURL:destination error:&copyError];
                    }];
                    if(copyError)error=copyError;
                }
            }
            [fm removeItemAtURL:temporary error:NULL];
            if(outError)*outError=error;
            return error==nil;
}
@end
