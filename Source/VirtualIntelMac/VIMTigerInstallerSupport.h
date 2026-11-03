#pragma once
#import "VIMTigerRestoreProfile.h"
#include <string.h>
#include <errno.h>
#include "../tools/opengl-tiger-control-files.h"

// Host-only preparation of the generated DVD, not the original download or
// an installed system. Darwin8 uses its real rc.cdrom.local/minstall contract;
// do not rename Installer.app or donate the Darwin9 bootstrap/injection path.
static inline NSString *VIMTigerControlSHA256(NSData *data) {
    if(!data.length||data.length>8*1024*1024)return nil;
    unsigned char hash[CC_SHA256_DIGEST_LENGTH];CC_SHA256(data.bytes,(CC_LONG)data.length,hash);
    NSMutableString *text=[NSMutableString new];
    for(NSUInteger i=0;i<sizeof(hash);i++)[text appendFormat:@"%02x",hash[i]];
    return text;
}
static inline NSData *VIMTigerControlFile(NSURL *URL) {
    struct stat st;
    return URL.isFileURL&&!lstat(URL.fileSystemRepresentation,&st)&&S_ISREG(st.st_mode)&&
        st.st_size>0&&st.st_size<=8*1024*1024?[NSData dataWithContentsOfURL:URL]:nil;
}
static inline NSURL *VIMTigerTools(NSURL *support) {
    // Build inputs use GuestTools/ like every other release. The app packages
    // that directory's contents directly in OpenGLGuests/tiger, also like peers.
    NSURL *tools=[support URLByAppendingPathComponent:@"GuestTools"];
    BOOL directory=NO;
    return [NSFileManager.defaultManager fileExistsAtPath:tools.path isDirectory:&directory]&&directory?tools:support;
}
static inline NSDictionary<NSString *,NSData *> *VIMTigerControlPayload(NSURL *support,NSError **error) {
    NSData *manifestData=VIMTigerControlFile([support URLByAppendingPathComponent:@"Support.plist"]);
    NSDictionary *manifest=manifestData?[NSPropertyListSerialization propertyListWithData:manifestData options:NSPropertyListImmutable format:nil error:nil]:nil;
    NSURL *controls=VIMTigerTools(support);
    NSData *recordData=VIMTigerControlFile([controls URLByAppendingPathComponent:@"BuildReceipt.plist"]);
    NSDictionary *record=recordData?[NSPropertyListSerialization propertyListWithData:recordData options:NSPropertyListImmutable format:nil error:nil]:nil;
    NSSet *names=[NSSet setWithArray:VIMTigerControlNames()];
    if(![manifest isKindOfClass:NSDictionary.class]||![record isKindOfClass:NSDictionary.class]||
       ![manifest[@"FormatVersion"] isEqual:@1]||![manifest[@"Guest"] isEqual:@"tiger"]||
       ![record[@"FormatVersion"] isEqual:@1]||![record[@"MinimumOS"] isEqual:@"10.4"]||
       ![record[@"Architecture"] isEqual:@"i386"]||![record[@"Files"] isKindOfClass:NSDictionary.class]||
       ![[NSSet setWithArray:[record[@"Files"] allKeys]] isEqual:names]) {
        VIMTigerProfileError(error,@"This app’s Tiger support is incomplete. Reinstall the complete app, then prepare the installer again.");return nil;
    }
    NSMutableDictionary *payload=[NSMutableDictionary new];
    for(NSString *name in names) {
        NSData *data=VIMTigerControlFile([controls URLByAppendingPathComponent:name]);
        uint32_t header[4]={0};if(data.length>=sizeof(header))memcpy(header,data.bytes,sizeof(header));
        BOOL valid=NO;
        if([name hasSuffix:@".txt"])valid=data.length>0;
        else if([name hasSuffix:@".plist"]){
            NSDictionary *info=data?[NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:nil error:nil]:nil;
            valid=[info isKindOfClass:NSDictionary.class];
            if([name isEqual:@"TigerSMBClient-Info.plist"])
                valid=valid&&[info[@"CFBundleIdentifier"] isEqual:@"com.apple.filesystems.smbfs"]&&[info[@"CFBundleVersion"] isEqual:@"1.3.7"];
            else valid=valid&&[info[@"Guest"] isEqual:@"tiger"]&&[info[@"PolicyVersion"] isEqual:@2];
        }else{
            uint32_t kind=[name isEqual:@"tiger-smbfs"]?1:([name isEqual:@"tiger-opendirectory.so"]?8:2);
            valid=header[0]==0xfeedface&&header[1]==7&&header[3]==kind;
        }
        if(!valid) {
            VIMTigerProfileError(error,@"A matching native Tiger setup tool is missing or changed. Reinstall the complete app, then try again.");return nil;
        }
        payload[name]=data;
    }
    return payload;
}
static inline NSDictionary<NSString *,NSData *> *VIMTigerInstallerGraphics(NSURL *support,NSError **error) {
    NSData *outerData=VIMTigerControlFile([support URLByAppendingPathComponent:@"Support.plist"]);
    NSDictionary *outer=outerData?[NSPropertyListSerialization propertyListWithData:outerData options:NSPropertyListImmutable format:nil error:nil]:nil;
    NSData *recordData=VIMTigerControlFile([VIMTigerTools(support) URLByAppendingPathComponent:@"Graphics.plist"]);
    NSDictionary *record=recordData?[NSPropertyListSerialization propertyListWithData:recordData options:NSPropertyListImmutable format:nil error:nil]:nil;
    NSSet *names=[NSSet setWithArray:@[@"GLEngine",@"libVIMPassthroughOpenGL.dylib",@"opengl-tiger-graphics",
        @"VMsvga2GA.plugin/Contents/Info.plist",@"VMsvga2GA.plugin/Contents/MacOS/VMsvga2GA"]];
    if(![outer isKindOfClass:NSDictionary.class]||
       ![outer[@"Guest"] isEqual:@"tiger"]||![outer[@"FormatVersion"] isEqual:@1]||
       [record[@"DiagnosticOnly"] boolValue]||
       ![record isKindOfClass:NSDictionary.class]||![record[@"Files"] isKindOfClass:NSDictionary.class]||
       ![[NSSet setWithArray:[record[@"Files"] allKeys]] isEqual:names]||
       ![record[@"FormatVersion"] isEqual:@1]||![record[@"Architecture"] isEqual:@"i386"]||
       ![record[@"MinimumOS"] isEqual:@"10.4"]||![record[@"DispatchCount"] isEqual:@759]||
       ![record[@"NativeDispatchCounts"] isEqual:@[@686,@759]]||
       ![record[@"NativePrebootInstallVersion"] isEqual:@1]||
       ![record[@"NativeEngineSHA256"] isKindOfClass:NSString.class]||[record[@"NativeEngineSHA256"] length]!=64) {
        VIMTigerProfileError(error,@"This app’s native Tiger graphics support is incomplete. Reinstall the complete app, then prepare the installer again.");return nil;
    }
    NSMutableDictionary *payload=[NSMutableDictionary new];payload[@"Graphics/BuildReceipt.plist"]=recordData;
    for(NSString *name in names){
        NSString *relative=[@"Graphics/" stringByAppendingString:name];
        NSData *data=VIMTigerControlFile([VIMTigerTools(support) URLByAppendingPathComponent:name]);
        uint32_t header[4]={0};if(data.length>=sizeof(header))memcpy(header,data.bytes,sizeof(header));
        BOOL metadata=[name hasSuffix:@".plist"];
        uint32_t type=[name isEqual:@"opengl-tiger-graphics"]?2:[name hasSuffix:@".dylib"]?6:8;
        if(!data||(!metadata&&(header[0]!=0xfeedface||header[1]!=7||header[3]!=type))){
            VIMTigerProfileError(error,@"A matching native Tiger graphics file is missing or changed. Reinstall the complete app, then try again.");return nil;
        }
        payload[relative]=data;
    }
    return payload;
}
static inline NSDictionary<NSString *,NSData *> *VIMTigerInstallerAudio(NSURL *support,NSError **error) {
    NSDictionary *outer=[NSDictionary dictionaryWithContentsOfURL:[support URLByAppendingPathComponent:@"Support.plist"]];
    NSData *data=VIMTigerControlFile([support URLByAppendingPathComponent:@"Audio/BuildReceipt.plist"]);
    NSDictionary *record=data?[NSPropertyListSerialization propertyListWithData:data options:0 format:nil error:nil]:nil;
    NSSet *names=[NSSet setWithArray:@[@"VoodooHDA.kext/Contents/Info.plist",@"VoodooHDA.kext/Contents/MacOS/VoodooHDA"]];
    if(![outer[@"HasNativeAudioPayload"] isEqual:@YES]||![record[@"AudioOnly"] isEqual:@YES]||
       ![record[@"SDKVersion"] isEqual:@"10.4"]||![record[@"KernelArchitecture"] isEqual:@"i386"]||
       ![record[@"Files"] isKindOfClass:NSDictionary.class]||![[NSSet setWithArray:[record[@"Files"] allKeys]] isEqual:names]){
        VIMTigerProfileError(error,@"This app’s native Tiger sound support is incomplete. Reinstall the complete app, then prepare the installer again.");return nil;
    }
    NSMutableDictionary *payload=[NSMutableDictionary new];payload[@"Audio/BuildReceipt.plist"]=data;
    for(NSString *name in names){
        NSString *relative=[@"Audio/" stringByAppendingString:name];NSData *file=VIMTigerControlFile([support URLByAppendingPathComponent:relative]);
        uint32_t header[4]={0};if(file.length>=sizeof(header))memcpy(header,file.bytes,sizeof(header));
        if(!file||
           (![name hasSuffix:@".plist"]&&(header[0]!=0xfeedface||header[1]!=7||header[3]!=1))){
            VIMTigerProfileError(error,@"A matching native Tiger sound file is missing or changed. Reinstall the complete app, then try again.");return nil;
        }
        payload[relative]=file;
    }
    return payload;
}
static inline NSData *VIMTigerNativePostflight(NSData *original,NSError **error) {
    NSString *text=original?[[NSString alloc] initWithData:original encoding:NSUTF8StringEncoding]:nil;
    if(![VIMTigerControlSHA256(original) isEqual:@"47cf5b22dc946b896e6f6f5290ece2173106bc61bd4fa9ccbb1831b260d8a96b"]||
       ![text hasSuffix:@"\nexit 0\n"]) {
        VIMTigerProfileError(error,@"This Tiger disc uses a different installation handoff. Its original files weren’t changed.");return nil;
    }
    NSString *completion=@"\n/System/Installation/Packages/Extras/VirtualIntelMac/opengl-tiger-install-config --finish-install \"$3\" || exit $?\nexit 0\n";
    return [[[text substringToIndex:text.length-[@"\nexit 0\n" length]] stringByAppendingString:completion] dataUsingEncoding:NSUTF8StringEncoding];
}
static inline BOOL VIMTigerVerifyInstallerControls(NSURL *mount,NSURL *support,NSDictionary *receipt,NSError **error) {
    NSDictionary *payload=VIMTigerControlPayload(support,error),*graphics=VIMTigerInstallerGraphics(support,error),*audio=VIMTigerInstallerAudio(support,error);if(!payload||!graphics||!audio)return NO;
    NSFileManager *fm=NSFileManager.defaultManager;
    NSDictionary *links=@{@"etc/rc.cdrom.local":@"/System/Installation/Packages/Extras/VirtualIntelMac/opengl-tiger-installer-bootstrap",
        @"etc/minstallconfig.xml":@"/var/tmp/VirtualIntelMac-minstallconfig.plist"};
    if(![receipt[@"TigerControlBootstrapVersion"] isEqual:@8]||
       ![receipt[@"TigerControlFiles"] isKindOfClass:NSDictionary.class]||
       ![[NSSet setWithArray:[receipt[@"TigerControlFiles"] allKeys]] isEqual:[NSSet setWithArray:payload.allKeys]]||
       ![receipt[@"TigerGraphicsFiles"] isKindOfClass:NSDictionary.class]||
       ![[NSSet setWithArray:[receipt[@"TigerGraphicsFiles"] allKeys]] isEqual:[NSSet setWithArray:graphics.allKeys]]||
       ![receipt[@"TigerAudioFiles"] isKindOfClass:NSDictionary.class]||
       ![[NSSet setWithArray:[receipt[@"TigerAudioFiles"] allKeys]] isEqual:[NSSet setWithArray:audio.allKeys]])
        return VIMTigerProfileError(error,@"The prepared Tiger support record is incomplete. Prepare this installer again.");
    for(NSString *name in links)
        if(![[fm destinationOfSymbolicLinkAtPath:[mount URLByAppendingPathComponent:name].path error:nil] isEqual:links[name]])
            return VIMTigerProfileError(error,@"The prepared Tiger startup handoff couldn’t be verified. Its working files were retained.");
    for(NSString *name in payload) {
        NSString *relative=[@"System/Installation/Packages/Extras/VirtualIntelMac" stringByAppendingPathComponent:name];
        NSData *actual=VIMTigerControlFile([mount URLByAppendingPathComponent:relative]);
        if(![actual isEqual:payload[name]])
            return VIMTigerProfileError(error,@"The prepared Tiger setup tools couldn’t be verified. Its working files were retained.");
    }
    NSData *controlRecord=VIMTigerControlFile([mount URLByAppendingPathComponent:@"System/Installation/Packages/Extras/VirtualIntelMac/BuildReceipt.plist"]);
    if(![controlRecord isEqual:VIMTigerControlFile([VIMTigerTools(support) URLByAppendingPathComponent:@"BuildReceipt.plist"])])
        return VIMTigerProfileError(error,@"The prepared Tiger control record couldn’t be verified. Its working files were retained.");
    for(NSString *name in graphics){
        NSURL *file=[mount URLByAppendingPathComponent:[@"System/Installation/Packages/Extras/VirtualIntelMac" stringByAppendingPathComponent:name]];
        NSData *actual=VIMTigerControlFile(file);
        if(![actual isEqual:graphics[name]])
            return VIMTigerProfileError(error,@"The prepared Tiger graphics files couldn’t be verified. Its working files were retained.");
    }
    for(NSString *name in audio){
        NSURL *file=[mount URLByAppendingPathComponent:[@"System/Installation/Packages/Extras/VirtualIntelMac" stringByAppendingPathComponent:name]];
        NSData *actual=VIMTigerControlFile(file);
        if(![actual isEqual:audio[name]])
            return VIMTigerProfileError(error,@"The prepared Tiger sound files couldn’t be verified. Its working files were retained.");
    }
    NSData *postflight=VIMTigerControlFile([mount URLByAppendingPathComponent:@"System/Installation/Packages/OSInstall.mpkg/Contents/Resources/postflight"]);
    return [[[NSString alloc] initWithData:postflight encoding:NSUTF8StringEncoding] hasSuffix:@"\n/System/Installation/Packages/Extras/VirtualIntelMac/opengl-tiger-install-config --finish-install \"$3\" || exit $?\nexit 0\n"]||
        VIMTigerProfileError(error,@"The prepared Tiger installation handoff couldn’t be verified. Its working files were retained.");
}
static inline BOOL VIMTigerStageInstallerControls(NSURL *mount,NSURL *support,NSDictionary **receipt,NSError **error) {
    NSDictionary *payload=VIMTigerControlPayload(support,error),*graphics=VIMTigerInstallerGraphics(support,error),*audio=VIMTigerInstallerAudio(support,error);if(!payload||!graphics||!audio)return NO;
    NSFileManager *fm=NSFileManager.defaultManager;
    NSString *rc=[NSString stringWithContentsOfURL:[mount URLByAppendingPathComponent:@"etc/rc.cdrom"] encoding:NSUTF8StringEncoding error:error];
    NSURL *postflight=[mount URLByAppendingPathComponent:@"System/Installation/Packages/OSInstall.mpkg/Contents/Resources/postflight"];
    NSData *prepared=VIMTigerNativePostflight(VIMTigerControlFile(postflight),error);
    if(!prepared)return NO;
    if([rc componentsSeparatedByString:@"\t/etc/rc.cdrom.local\n"].count!=2)
        return VIMTigerProfileError(error,@"This Tiger disc uses a different startup interface. Its original files weren’t changed.");
    NSURL *destination=[mount URLByAppendingPathComponent:@"System/Installation/Packages/Extras/VirtualIntelMac"];
    // Reject an existing hook, even a dangling symlink. Never overwrite an
    // unknown preparation or transplant this hook to an installed OS.
    for(NSString *path in @[@"etc/rc.cdrom.local",@"etc/minstallconfig.xml",@"System/Installation/Packages/Extras/VirtualIntelMac"]) {
        struct stat st;NSURL *URL=[mount URLByAppendingPathComponent:path];
        if(!lstat(URL.fileSystemRepresentation,&st)||errno!=ENOENT)
            return VIMTigerProfileError(error,@"This Tiger working copy already contains setup changes. Prepare a new copy from the original installer.");
    }
    if(![fm createDirectoryAtURL:destination withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0755} error:error])return NO;
    NSMutableDictionary *hashes=[NSMutableDictionary new];
    for(NSString *name in payload) {
        NSURL *file=[destination URLByAppendingPathComponent:name];
        if(![payload[name] writeToURL:file options:NSDataWritingAtomic error:error]||
           ![fm setAttributes:@{NSFilePosixPermissions:@0755} ofItemAtPath:file.path error:error])return NO;
        hashes[name]=@YES;
    }
    NSURL *controlRecord=[destination URLByAppendingPathComponent:@"BuildReceipt.plist"];
    NSData *controlData=VIMTigerControlFile([VIMTigerTools(support) URLByAppendingPathComponent:@"BuildReceipt.plist"]);
    if(![controlData writeToURL:controlRecord options:NSDataWritingAtomic error:error]||
       ![fm setAttributes:@{NSFilePosixPermissions:@0644} ofItemAtPath:controlRecord.path error:error])return NO;
    NSMutableDictionary *graphicsHashes=[NSMutableDictionary new];
    for(NSString *name in graphics){
        NSURL *file=[destination URLByAppendingPathComponent:name];
        if(![fm createDirectoryAtURL:file.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0755} error:error]||
           ![graphics[name] writeToURL:file options:NSDataWritingAtomic error:error]||
           ![fm setAttributes:@{NSFilePosixPermissions:([name hasSuffix:@".plist"]?@0644:@0755)} ofItemAtPath:file.path error:error])return NO;
        graphicsHashes[name]=@YES;
    }
    NSMutableDictionary *audioHashes=[NSMutableDictionary new];
    for(NSString *name in audio){
        NSURL *file=[destination URLByAppendingPathComponent:name];
        if(![fm createDirectoryAtURL:file.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0755} error:error]||
           ![audio[name] writeToURL:file options:NSDataWritingAtomic error:error]||
           ![fm setAttributes:@{NSFilePosixPermissions:([name hasSuffix:@".plist"]?@0644:@0755)} ofItemAtPath:file.path error:error])return NO;
        audioHashes[name]=@YES;
    }
    if(![fm createSymbolicLinkAtURL:[mount URLByAppendingPathComponent:@"etc/rc.cdrom.local"]
        withDestinationURL:[NSURL fileURLWithPath:@"/System/Installation/Packages/Extras/VirtualIntelMac/opengl-tiger-installer-bootstrap"] error:error]||
       ![fm createSymbolicLinkAtURL:[mount URLByAppendingPathComponent:@"etc/minstallconfig.xml"]
        withDestinationURL:[NSURL fileURLWithPath:@"/var/tmp/VirtualIntelMac-minstallconfig.plist"] error:error])return NO;
    NSNumber *mode=[fm attributesOfItemAtPath:postflight.path error:error][NSFilePosixPermissions];
    if(!mode)return NO;
    BOOL written=[fm setAttributes:@{NSFilePosixPermissions:@(mode.unsignedIntValue|0200)} ofItemAtPath:postflight.path error:error]&&
        [prepared writeToURL:postflight options:0 error:error];
    NSError *restore=nil;
    if(![fm setAttributes:@{NSFilePosixPermissions:mode} ofItemAtPath:postflight.path error:&restore]){if(error)*error=restore;return NO;}
    if(!written)return NO;
    NSDictionary *record=@{@"TigerControlBootstrapVersion":@8,@"TigerControlFiles":hashes,@"TigerGraphicsFiles":graphicsHashes,@"TigerAudioFiles":audioHashes,
        @"NativeAutomaticInstallerLanguage":@"English"};
    if(!VIMTigerVerifyInstallerControls(mount,support,record,error))return NO;
    if(receipt)*receipt=record;
    return YES;
}
