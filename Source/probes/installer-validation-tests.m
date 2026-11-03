// Uses the production validator, plus optional real installers supplied by the
// caller. Fixtures are tiny and temporary; real installer apps are read-only.
#import <Foundation/Foundation.h>
#import "VIMInstallerAssembly.h"

static void Require(BOOL condition,NSString *message) {
    if(!condition){fprintf(stderr,"FAIL: %s\n",message.UTF8String);exit(1);}
}
static void WritePlist(NSDictionary *value,NSURL *URL) {
    Require([value writeToURL:URL atomically:YES],@"write fixture plist");
}
static NSURL *Fixture(NSURL *root,NSString *guest,NSString *identifier,id systemInfo) {
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *app=[root URLByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingPathExtension:@"app"]];
    for(NSString *path in @[@"Contents/SharedSupport",@"Contents/Resources"])
        Require([fm createDirectoryAtURL:[app URLByAppendingPathComponent:path] withIntermediateDirectories:YES attributes:nil error:nil],@"create fixture");
    WritePlist(@{@"CFBundleIdentifier":identifier,@"CFBundleShortVersionString":@"16.9.63"},[app URLByAppendingPathComponent:@"Contents/Info.plist"]);
    if(systemInfo)WritePlist(@{@"System Image Info":systemInfo},[app URLByAppendingPathComponent:@"Contents/SharedSupport/InstallInfo.plist"]);
    BOOL classic=[@[@"lion",@"mountainlion",@"mavericks",@"yosemite",@"elcapitan",@"sierra"] containsObject:guest];
    NSArray *payloads=classic?@[@"Contents/SharedSupport/InstallESD.dmg"]:
        [guest isEqual:@"bigsur"]?@[@"Contents/Resources/createinstallmedia",@"Contents/SharedSupport/SharedSupport.dmg"]:
        @[@"Contents/Resources/createinstallmedia",@"Contents/SharedSupport/InstallESD.dmg",@"Contents/SharedSupport/BaseSystem.dmg"];
    for(NSString *path in payloads)
        Require([[([path hasSuffix:@"SharedSupport.dmg"]?@"xar!fixture":@"fixture") dataUsingEncoding:NSUTF8StringEncoding] writeToURL:[app URLByAppendingPathComponent:path] atomically:YES],@"write fixture file");
    return app;
}
static void Accepted(NSURL *app,NSString *guest) {
    NSError *error=nil;
    Require([[VIMInstallerAssembly guestForInstallerApplication:app error:&error] isEqual:guest],error.description?:@"manual detection");
    for(NSString *expected in @[@"lion",@"mountainlion",@"mavericks",@"yosemite",@"elcapitan",@"sierra",@"highsierra",@"mojave",@"catalina",@"bigsur"])
        Require([VIMInstallerAssembly validateInstallerApplication:app guest:expected error:nil]==[expected isEqual:guest],@"resume OS match/mismatch");
    printf("PASS: manual detection and resume: %s (%s)\n",app.path.UTF8String,guest.UTF8String);
}
int main(int argc,const char **argv){@autoreleasepool {
    if(argc==4&&strcmp(argv[1],"--prepared-media")==0) {
        NSError *error=nil;
        BOOL valid=[VIMInstallerAssembly validatePreparedMedia:[NSURL fileURLWithPath:@(argv[3])] guest:@(argv[2]) error:&error];
        fprintf(valid?stdout:stderr,"%s\n",valid?"PASS: prepared installer receipt, actual size and boot blocks match":error.localizedDescription.UTF8String);
        return valid?0:1;
    }
    // A focused media retry mode also exercises the real task argument builder.
    if(argc==4&&(strcmp(argv[1],"--bigsur-media")==0||strcmp(argv[1],"--assemble-bigsur")==0)) {
        VIMInstallerAssembly *worker=[VIMInstallerAssembly new];
        worker.progress=^(NSString *stage){fprintf(stderr,"%s\n",stage.UTF8String);};
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
            NSError *error=nil;
            NSURL *input=[NSURL fileURLWithPath:@(argv[2])],*job=[NSURL fileURLWithPath:@(argv[3])];
            NSURL *image=strcmp(argv[1],"--assemble-bigsur")==0?
                [worker assembleGuest:@"bigsur" files:@{@"assistant":input} jobDirectory:job error:&error]:
                [worker createBigSurMediaFromInstaller:input jobDirectory:job error:&error];
            fprintf(stderr,"%s\n",image?image.path.UTF8String:error.description.UTF8String);exit(image?0:1);
        });
        dispatch_main();
    }
    Require((argc-1)%2==0,@"arguments: [guest installer.app]… or --bigsur-media installer.app new-job");
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *root=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:[@"vim-installer-validation-" stringByAppendingString:NSUUID.UUID.UUIDString]]];
    Require([fm createDirectoryAtURL:root withIntermediateDirectories:NO attributes:nil error:nil],@"create test root");
    NSURL *media=[root URLByAppendingPathComponent:@"Installer.iso"],*receipt=[root URLByAppendingPathComponent:@"Media.plist"];
    Require([[NSMutableData dataWithLength:4096] writeToURL:media atomically:YES],@"write media fixture");
    Require([VIMInstallerAssembly validatePreparedMedia:media guest:@"mavericks" error:nil],@"ordinary image remains supported");
    WritePlist(@{@"FormatVersion":@1,@"Stage":@"media-created",@"Guest":@"mavericks",@"Media":@"Installer.iso",@"Size":@4096},receipt);
    Require(![VIMInstallerAssembly validatePreparedMedia:media guest:@"mavericks" error:nil],@"same-length unreadable prepared disk rejected");
    NSMutableData *bootBlocks=[NSMutableData dataWithLength:4096];
    memcpy((char *)bootBlocks.mutableBytes+512,"EFI PART",8);
    Require([bootBlocks writeToURL:media atomically:YES],@"write GPT boot-block fixture");
    Require([VIMInstallerAssembly validatePreparedMedia:media guest:@"mavericks" error:nil],@"complete prepared image accepted");
    for(NSString *container in @[@"APM",@"HFS",@"ISO",@"UDF"]) {
        bootBlocks=[NSMutableData dataWithLength:36864];char *bytes=bootBlocks.mutableBytes;
        if([container isEqual:@"APM"]){memcpy(bytes,"ER",2);memcpy(bytes+512,"PM",2);}
        if([container isEqual:@"HFS"])memcpy(bytes+1024,"H+",2);
        if([container isEqual:@"ISO"])memcpy(bytes+32769,"CD001",5);
        if([container isEqual:@"UDF"])memcpy(bytes+32769,"BEA01",5);
        Require([bootBlocks writeToURL:media atomically:YES],@"write supported boot-block fixture");
        WritePlist(@{@"FormatVersion":@1,@"Stage":@"media-created",@"Guest":@"mavericks",@"Media":@"Installer.iso",@"Size":@(bootBlocks.length)},receipt);
        Require([VIMInstallerAssembly validatePreparedMedia:media guest:@"mavericks" error:nil],container);
    }
    Require(![VIMInstallerAssembly validatePreparedMedia:media guest:@"lion" error:nil],@"wrong prepared image OS rejected");
    Require([[NSMutableData dataWithLength:2048] writeToURL:media atomically:YES],@"truncate prepared media fixture");
    Require(![VIMInstallerAssembly validatePreparedMedia:media guest:@"mavericks" error:nil],@"truncated prepared image rejected before boot");
    Require([fm removeItemAtURL:media error:nil]&&[fm removeItemAtURL:receipt error:nil],@"remove media fixtures");
    puts("PASS: ordinary, complete, wrong-OS and truncated prepared media launch checks");
    for(NSArray *spec in @[@[@"lion",@"com.apple.InstallAssistant.Lion",@"10.7.5"],
                           @[@"mountainlion",@"com.apple.InstallAssistant.MountainLion",@"10.8.5"],
                           @[@"mavericks",@"com.apple.InstallAssistant.Mavericks",@"10.9.5"],
                           @[@"mavericks",@"com.apple.InstallAssistant.Mavericks",NSNull.null],
                           @[@"yosemite",@"com.apple.InstallAssistant.Yosemite",@"10.10.5"],
                           @[@"yosemite",@"com.apple.InstallAssistant.Yosemite",NSNull.null],
                           @[@"elcapitan",@"com.apple.InstallAssistant.ElCapitan",@"10.11.6"],
                           @[@"elcapitan",@"com.apple.InstallAssistant.ElCapitan",NSNull.null],
                           @[@"sierra",@"com.apple.InstallAssistant.Sierra",@"10.12.6"],
                           @[@"sierra",@"com.apple.InstallAssistant.Sierra",NSNull.null],
                           @[@"highsierra",@"com.apple.InstallAssistant.HighSierra",@"10.13.6"],
                           @[@"mojave",@"com.apple.InstallAssistant.Mojave",@"10.14"],
                           @[@"mojave",@"com.apple.InstallAssistant.Mojave",@"10.14.6"],
                           @[@"mojave",@"com.apple.InstallAssistant.Mojave",@"10.14.99"],
                           @[@"catalina",@"com.apple.InstallAssistant.Catalina",@"10.15.7"],
                           @[@"catalina",@"com.apple.InstallAssistant.Catalina",@"10.15.8"],
                           @[@"bigsur",@"com.apple.InstallAssistant.macOSBigSur",@"11.7.11"],
                           @[@"bigsur",@"com.apple.InstallAssistant.macOSBigSur",@"10.16"],
                           @[@"bigsur",@"com.apple.InstallAssistant.macOSBigSur",@"10.16.1"],
                           @[@"bigsur",@"com.apple.InstallAssistant.macOSBigSur",NSNull.null]]) {
        NSURL *app=Fixture(root,spec[0],spec[1],spec[2]==NSNull.null?nil:@{@"version":spec[2]});
        Accepted(app,spec[0]);
        NSURL *renamed=[root URLByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingPathExtension:@"APP"]];
        Require([fm moveItemAtURL:app toURL:renamed error:nil],@"rename fixture");Accepted(renamed,spec[0]);
        NSString *payload=[spec[0] isEqual:@"bigsur"]?@"SharedSupport.dmg":@"InstallESD.dmg";
        NSURL *image=[[renamed URLByAppendingPathComponent:@"Contents/SharedSupport"] URLByAppendingPathComponent:payload];
        Require([NSData.data writeToURL:image atomically:YES],@"truncate fixture payload");
        Require(![VIMInstallerAssembly guestForInstallerApplication:renamed error:nil],@"empty payload rejected");
        Require([fm removeItemAtURL:image error:nil],@"remove fixture payload");
        Require(![VIMInstallerAssembly validateInstallerApplication:renamed guest:spec[0] error:nil],@"missing payload rejected");
    }
    for(id system in @[@{@"version":@"10.15.7"},@{@"version":@"10.140.1"},@{@"version":@"10.14.bad"},@{@"version":@14},@"malformed"])
        Require(![VIMInstallerAssembly guestForInstallerApplication:Fixture(root,@"mojave",@"com.apple.InstallAssistant.Mojave",system) error:nil],@"wrong/malformed OS version rejected without crashing");
    Require(![VIMInstallerAssembly guestForInstallerApplication:Fixture(root,@"bigsur",@"com.apple.InstallAssistant.BigSur",nil) error:nil],@"incorrect old Big Sur ID rejected");
    Require(![VIMInstallerAssembly guestForInstallerApplication:Fixture(root,@"bigsur",@"com.apple.InstallAssistant.macOSMonterey",nil) error:nil],@"unsupported OS rejected");
    Require(![VIMInstallerAssembly guestForInstallerApplication:Fixture(root,@"bigsur",@"com.apple.InstallAssistant.macOSBigSur",@"malformed") error:nil],@"malformed optional metadata rejected");
    NSURL *brokenBigSur=Fixture(root,@"bigsur",@"com.apple.InstallAssistant.macOSBigSur",nil);
    Require([[@"inner disk image without signed XAR wrapper" dataUsingEncoding:NSUTF8StringEncoding]
        writeToURL:[brokenBigSur URLByAppendingPathComponent:@"Contents/SharedSupport/SharedSupport.dmg"] atomically:YES],@"write old damaged assembly fixture");
    Require(![VIMInstallerAssembly guestForInstallerApplication:brokenBigSur error:nil],@"inner DMG without the signed installer package must be rejected");
    puts("PASS: incomplete, unsupported, wrong-version and malformed installers rejected");
    for(int i=1;i<argc;i+=2) {
        NSURL *original=[NSURL fileURLWithPath:@(argv[i+1])];Accepted(original,@(argv[i]));
        NSURL *alias=[root URLByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingPathExtension:@"app"]];
        Require([fm createSymbolicLinkAtURL:alias withDestinationURL:original error:nil],@"create read-only renamed installer alias");
        Accepted(alias,@(argv[i]));
    }
    Require([fm removeItemAtURL:root error:nil],@"remove owned test fixtures");
    return 0;
}}
