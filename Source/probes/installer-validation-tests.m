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
    for(NSString *path in [guest isEqual:@"bigsur"]?@[@"Contents/Resources/createinstallmedia",@"Contents/SharedSupport/SharedSupport.dmg"]:
        @[@"Contents/Resources/createinstallmedia",@"Contents/SharedSupport/InstallESD.dmg",@"Contents/SharedSupport/BaseSystem.dmg"])
        Require([[([path hasSuffix:@"SharedSupport.dmg"]?@"xar!fixture":@"fixture") dataUsingEncoding:NSUTF8StringEncoding] writeToURL:[app URLByAppendingPathComponent:path] atomically:YES],@"write fixture file");
    return app;
}
static void Accepted(NSURL *app,NSString *guest) {
    NSError *error=nil;
    Require([[VIMInstallerAssembly guestForInstallerApplication:app error:&error] isEqual:guest],error.description?:@"manual detection");
    for(NSString *expected in @[@"mojave",@"catalina",@"bigsur"])
        Require([VIMInstallerAssembly validateInstallerApplication:app guest:expected error:nil]==[expected isEqual:guest],@"resume OS match/mismatch");
    printf("PASS: manual detection and resume: %s (%s)\n",app.path.UTF8String,guest.UTF8String);
}
int main(int argc,const char **argv){@autoreleasepool {
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
    for(NSArray *spec in @[@[@"mojave",@"com.apple.InstallAssistant.Mojave",@"10.14"],
                           @[@"mojave",@"com.apple.InstallAssistant.Mojave",@"10.14.6"],
                           @[@"catalina",@"com.apple.InstallAssistant.Catalina",@"10.15.7"],
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
