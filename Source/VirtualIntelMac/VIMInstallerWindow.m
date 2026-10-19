#import "VIMInstallerWindow.h"
#import "VIMInstallerCatalog.h"
#import "VIMLibraryLocation.h"
#import "VIMDownload.h"
#import "VIMInstallerAssembly.h"
#import "VIMNewMachineWindow.h"
#import "VIMAlerts.h"
#import <CommonCrypto/CommonDigest.h>
@interface VIMInstallerWindow ()
@property VIMInstallerCatalog *catalog;
@property NSPopUpButton *selection;
@property NSTextField *status;
@property NSTextField *location;
@property NSProgressIndicator *progress;
@property NSButton *downloadButton;
@property NSButton *pauseButton;
@property NSButton *refreshButton;
@property VIMDownload *transfer;
@property NSArray *files;
@property NSUInteger index;
@property NSURL *downloadRoot;
@property BOOL active;
@property NSButton *prepareButton;
@property VIMInstallerAssembly *assembly;
@property NSMutableDictionary<NSString *,NSURL *> *downloadedFiles;
@property NSString *downloadedGuest;
@property(copy) void (^terminationCompletion)(void);
@end
@implementation VIMInstallerWindow
static VIMInstallerWindow *installerController;
+ (BOOL)stopForTermination:(void (^)(void))completion {
    NSAssert(NSThread.isMainThread,@"Installer lifecycle is main-thread owned");
    if(!installerController.active)return NO;
    installerController.terminationCompletion=completion;
    [installerController pause:nil];
    return YES;
}
- (BOOL)finishTerminationIfNeeded {
    if(!self.terminationCompletion)return NO;
    void (^completion)(void)=self.terminationCompletion;self.terminationCompletion=nil;
    [self setBusy:NO];
    // Reply after applicationShouldTerminate has returned NSTerminateLater.
    dispatch_async(dispatch_get_main_queue(),completion);return YES;
}
+ (void)showInstaller {
    static VIMInstallerWindow *controller;static dispatch_once_t once;
    dispatch_once(&once, ^{controller=[self new];installerController=controller;});
    [controller showWindow:nil];[controller.window makeKeyAndOrderFront:nil];
}
- (instancetype)init {
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,620,410)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable
        backing:NSBackingStoreBuffered defer:NO];
    if((self=[super initWithWindow:window])) {
        window.title=@"macOS Installers";window.releasedWhenClosed=NO;
        self.selection=[NSPopUpButton new];
        self.status=[NSTextField wrappingLabelWithString:@"Choose a macOS installer to download. Existing downloads resume automatically."];
        self.location=[NSTextField wrappingLabelWithString:@""];self.location.selectable=YES;
        self.location.textColor=NSColor.secondaryLabelColor;
        self.progress=[NSProgressIndicator new];self.progress.indeterminate=NO;self.progress.maxValue=1;
        [self.progress.widthAnchor constraintEqualToConstant:540].active=YES;
        self.downloadButton=[NSButton buttonWithTitle:@"Download" target:self action:@selector(download:)];
        self.pauseButton=[NSButton buttonWithTitle:@"Pause" target:self action:@selector(pause:)];self.pauseButton.enabled=NO;
        self.refreshButton=[NSButton buttonWithTitle:@"Update Installer List" target:self action:@selector(refresh:)];
        self.prepareButton=[NSButton buttonWithTitle:@"Prepare Installer…" target:self action:@selector(prepare:)];
        self.prepareButton.enabled=NO;
        NSStackView *buttons=[NSStackView stackViewWithViews:@[self.downloadButton,self.pauseButton,self.refreshButton]];buttons.spacing=12;
        NSButton *reveal=[NSButton buttonWithTitle:@"Show Downloads in Finder" target:self action:@selector(reveal:)];
        NSStackView *stack=[NSStackView stackViewWithViews:@[self.selection,self.status,self.progress,buttons,self.prepareButton,self.location,reveal]];
        stack.orientation=NSUserInterfaceLayoutOrientationVertical;stack.alignment=NSLayoutAttributeLeading;stack.spacing=20;
        stack.translatesAutoresizingMaskIntoConstraints=NO;[window.contentView addSubview:stack];
        [NSLayoutConstraint activateConstraints:@[[stack.leadingAnchor constraintEqualToAnchor:window.contentView.leadingAnchor constant:40],
            [stack.trailingAnchor constraintEqualToAnchor:window.contentView.trailingAnchor constant:-40],
            [stack.topAnchor constraintEqualToAnchor:window.contentView.topAnchor constant:28]]];
        NSError *error=nil;self.catalog=[VIMInstallerCatalog currentCatalogWithError:&error];
        [self populate];if(error&&!self.catalog)self.status.stringValue=error.localizedDescription;
        [window center];
    }
    return self;
}
- (void)showWindow:(id)sender {
    if(!self.active)self.location.stringValue=VIMLibraryLocation.downloadsURL.path;
    [super showWindow:sender];
}
- (void)populate {
    [self.selection removeAllItems];
    for(NSDictionary *entry in self.catalog.supportedInstallers)[self.selection addItemWithTitle:entry[@"Name"]];
    self.downloadButton.enabled=self.catalog!=nil&&!self.active;
}
- (void)setBusy:(BOOL)busy {
    self.active=busy;self.selection.enabled=!busy;self.refreshButton.enabled=!busy;
    self.downloadButton.enabled=!busy&&self.catalog!=nil;self.pauseButton.enabled=busy;
    BOOL inputs=self.downloadedFiles[@"assistant"]!=nil&&([self.downloadedGuest isEqual:@"bigsur"]||
        (self.downloadedFiles[@"recovery"]!=nil&&self.downloadedFiles[@"esd"]!=nil));
    self.prepareButton.enabled=!busy&&inputs;
}
- (void)download:(id)sender {
    NSInteger selected=self.selection.indexOfSelectedItem;
    if(self.active||selected<0||selected>=self.catalog.supportedInstallers.count)return;
    self.files=self.catalog.supportedInstallers[selected][@"Files"];self.index=0;
    self.downloadedGuest=self.catalog.supportedInstallers[selected][@"Guest"];
    self.downloadedFiles=[NSMutableDictionary dictionary];
    self.downloadRoot=VIMLibraryLocation.downloadsURL;self.location.stringValue=self.downloadRoot.path;
    [self setBusy:YES];[self nextFile];
}
- (void)nextFile {
    if(self.index>=self.files.count) {
        [self setBusy:NO];self.progress.doubleValue=1;
        self.status.stringValue=@"Download complete. Choose Prepare Installer to verify Apple's package and create the installation disk image.";
        return;
    }
    NSURL *source=[NSURL URLWithString:self.files[self.index][@"URL"]];
    NSData *identity=[source.absoluteString dataUsingEncoding:NSUTF8StringEncoding];unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(identity.bytes,(CC_LONG)identity.length,digest);NSMutableString *key=[NSMutableString string];
    for(unsigned i=0;i<sizeof(digest);i++)[key appendFormat:@"%02x",digest[i]];
    NSURL *directory=[self.downloadRoot URLByAppendingPathComponent:key isDirectory:YES];
    NSError *error=nil;self.transfer=[[VIMDownload alloc] initWithSourceURL:source directory:directory error:&error];
    if(!self.transfer) { [self setBusy:NO];self.status.stringValue=error.localizedDescription;return; }
    self.status.stringValue=[NSString stringWithFormat:@"Downloading %@ (%lu of %lu)…",source.lastPathComponent,self.index+1,self.files.count];
    __weak VIMInstallerWindow *weakSelf=self;
    self.transfer.progress=^(int64_t received,int64_t total){
        VIMInstallerWindow *controller=weakSelf;
        controller.progress.indeterminate=total<=0;
        if(total>0)controller.progress.doubleValue=(double)received/total;
        controller.status.stringValue=[NSString stringWithFormat:@"%@ — %@ of %@",source.lastPathComponent,
            [NSByteCountFormatter stringFromByteCount:received countStyle:NSByteCountFormatterCountStyleFile],
            total>0?[NSByteCountFormatter stringFromByteCount:total countStyle:NSByteCountFormatterCountStyleFile]:@"unknown size"];
    };
    [self.transfer startWithCompletion:^(NSURL *file,NSError *error,BOOL paused){
        VIMInstallerWindow *controller=weakSelf;controller.transfer=nil;
        if([controller finishTerminationIfNeeded])return;
        if(error||paused) {
            [controller setBusy:NO];controller.status.stringValue=error?error.localizedDescription:@"Paused. Click Download to resume; completed files will be reused.";
        } else {controller.downloadedFiles[controller.files[controller.index][@"Role"]]=file;controller.index++;[controller nextFile];}
    }];
}
- (void)pause:(id)sender {
    self.pauseButton.enabled=NO;
    if(self.assembly){self.assembly.cancelled=YES;self.status.stringValue=@"Stopping preparation; downloaded files will be kept…";}
    else [self.transfer pause];
}
- (void)prepare:(id)sender {
    if(self.active||!self.prepareButton.enabled)return;
    NSString *guest=[self.downloadedGuest copy];
    // Capture all inputs: changing the library later must not redirect a job.
    NSDictionary *files=[self.downloadedFiles copy];
    NSURL *parent=self.downloadRoot.URLByDeletingLastPathComponent;
    NSURL *scratch=[parent URLByAppendingPathComponent:@"Installers" isDirectory:YES];
    if(![[NSFileManager defaultManager] fileExistsAtPath:[parent URLByAppendingPathComponent:@".VirtualIntelMacLibrary.plist"].path])scratch=[self.downloadRoot URLByAppendingPathComponent:@"Preparation" isDirectory:YES];
    NSError *error=nil;
    if(![NSFileManager.defaultManager createDirectoryAtURL:scratch withIntermediateDirectories:YES attributes:nil error:&error]) {
        self.status.stringValue=error.localizedDescription;return;
    }
    NSString *identifier=NSUUID.UUID.UUIDString;
    NSURL *assemblyJob=[scratch URLByAppendingPathComponent:[identifier stringByAppendingString:@"-Assembly"]];
    NSURL *mediaJob=[scratch URLByAppendingPathComponent:[identifier stringByAppendingString:@"-Media"]];
    VIMInstallerAssembly *worker=[VIMInstallerAssembly new];self.assembly=worker;
    [self setBusy:YES];self.pauseButton.title=@"Cancel";
    self.progress.indeterminate=YES;[self.progress startAnimation:nil];
    worker.progress=^(NSString *stage){self.status.stringValue=stage;};
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        NSError *failure=nil;
        NSURL *installer=[worker assembleGuest:guest files:files jobDirectory:assemblyJob error:&failure];
        NSURL *media=nil;
        if(installer)media=[guest isEqual:@"bigsur"]?
            [worker createBigSurMediaFromInstaller:installer jobDirectory:mediaJob error:&failure]:
            [worker createLegacyMediaFromInstaller:installer guest:guest jobDirectory:mediaJob error:&failure];
        if(media) {
            [VIMInstallerAssembly retireCompletedDownloads:files];
            if(![NSUserDefaults.standardUserDefaults boolForKey:@"VIMKeepInstallers"])
                [NSFileManager.defaultManager removeItemAtURL:assemblyJob error:nil];
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            worker.progress=nil;self.assembly=nil;[self setBusy:NO];self.pauseButton.title=@"Pause";
            [self.progress stopAnimation:nil];self.progress.indeterminate=NO;self.progress.doubleValue=media?1:0;
            if([self finishTerminationIfNeeded])return;
            self.status.stringValue=media?@"Installer disk image ready.":
                (failure.localizedDescription?:@"Installer preparation stopped. Your downloads have been kept.");
            if(media)[VIMNewMachineWindow showWithInstaller:media guest:guest];
        });
    });
}
- (void)refresh:(id)sender {
    self.refreshButton.enabled=NO;
    [VIMInstallerCatalog refreshWithCompletion:^(VIMInstallerCatalog *catalog,NSError *error){
        self.refreshButton.enabled=YES;
        if(catalog){self.catalog=catalog;[self populate];}
        self.status.stringValue=error?error.localizedDescription:@"Installer list updated.";
    }];
}
- (void)reveal:(id)sender {
    NSURL *directory=self.active?self.downloadRoot:VIMLibraryLocation.downloadsURL;
    NSError *error=nil;
    if([NSFileManager.defaultManager createDirectoryAtURL:directory withIntermediateDirectories:YES attributes:nil error:&error])
        [NSWorkspace.sharedWorkspace openURL:directory];
    else {
        NSAlert *alert=[NSAlert alertWithError:error];
        VIMSetInstallerAlertIcon(alert,self.downloadedGuest?:@"mojave");
        VIMPresentAlert(alert,self.window,nil);
    }
}
@end
