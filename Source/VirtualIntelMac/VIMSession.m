#import "VIMSession.h"
#import "AppDelegate.h"
#import "VIMMachine.h"
#import "VIMMetalView.h"
#import "VIMDisplayConnection.h"
#import "VIMQMPConnection.h"
#import "VIMGuestAgent.h"
#import "VIMGuestPreparation.h"
#import "VIMSMBService.h"
#import "VIMHostUSB.h"
#import "VIMUSBClient.h"
#import "VIMLibraryLocation.h"
#import "VIMGuestInstallation.h"
#import "VIMGuestBootPolicy.h"
#import "VIMGuestProgress.h"
#import "VIMHostSupport.h"
#import "VIMMachineSettings.h"
#import "VIMPlayButton.h"
#import "VIMAlerts.h"
#import "VIMDiscImage.h"
#import "VIMHostAudio.h"
#import "VIMNetworkDefaults.h"
#import "VIMNetworkClient.h"
#import "VIMClipboardBridge.h"
#import "VIMWindowGeometry.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <QuartzCore/QuartzCore.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

static NSMutableDictionary<NSString *,VIMSession *> *sessions;
static NSMutableDictionary<NSString *,NSUUID *> *pendingRosettaStarts;
typedef NS_ENUM(NSUInteger,VIMBootOperation) {VIMBootStart,VIMBootPrepare,VIMBootInstall,VIMBootRepair};
static NSString *SessionKey(VIMMachine *machine) {return machine.configuration[@"Identifier"]?:machine.URL.URLByResolvingSymlinksInPath.path;}
static NSString *VIMOptionPath(NSString *path) { return [path stringByReplacingOccurrencesOfString:@"," withString:@",,"]; }
static NSError *VIMSessionError(NSString *message) {
    return [NSError errorWithDomain:@"VirtualIntelMac.Session" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];
}
static NSString *VIMMultiMachineDiagnosticDirectory(NSArray<NSString *> *arguments,VIMMachine *machine) {
    NSUInteger option=[arguments indexOfObject:@"--guest-diagnostics-root"];
    if(option==NSNotFound||option+1>=arguments.count||![arguments[option+1] isAbsolutePath])return nil;
    NSUUID *identifier=[[NSUUID alloc] initWithUUIDString:machine.configuration[@"Identifier"]];
    if(!identifier)return nil;
    NSString *root=arguments[option+1];
    NSString *directory=[root stringByAppendingPathComponent:identifier.UUIDString];
    // Explicit developer opt-in only. Each session retains its own serialized
    // QMP/QGA owners and command files; no shared Result.json/second client.
    for(NSString *path in @[root,directory]) {
        NSError *error=nil;struct stat info;
        if(![NSFileManager.defaultManager createDirectoryAtPath:path withIntermediateDirectories:YES
            attributes:@{NSFilePosixPermissions:@0700} error:&error]||
            lstat(path.fileSystemRepresentation,&info)||!S_ISDIR(info.st_mode)||
            info.st_uid!=getuid()||(info.st_mode&077))return nil;
    }
    return directory;
}
@interface VIMStoppedMacView : NSView
@property NSImageView *wallpaper;
@property NSString *guest;
@property VIMPlayButton *playButton;
@property NSImageView *mark;
@property NSTextField *nameField;
- (instancetype)initWithGuest:(NSString *)guest name:(NSString *)name target:(id)target action:(SEL)action;
@end
@implementation VIMStoppedMacView
- (instancetype)initWithGuest:(NSString *)guest name:(NSString *)name target:(id)target action:(SEL)action {
    if((self=[super initWithFrame:NSZeroRect])) {
        self.guest=guest;self.wantsLayer=YES;self.layer.masksToBounds=YES;
        NSImage *image=[NSImage imageNamed:[guest stringByAppendingString:@"Wallpaper"]];
        self.wallpaper=[NSImageView imageViewWithImage:image];
        self.wallpaper.imageScaling=NSImageScaleProportionallyUpOrDown;
        // This presentation is frame-laid-out, not a sizing contract for the
        // window. Neither wallpaper size nor a capped proportional Play button
        // may propagate a minimum/maximum window width through Auto Layout.
        self.wallpaper.accessibilityElement=NO;
        [self addSubview:self.wallpaper];
        NSVisualEffectView *material=[NSVisualEffectView new];material.material=NSVisualEffectMaterialUnderWindowBackground;
        material.blendingMode=NSVisualEffectBlendingModeWithinWindow;material.state=NSVisualEffectStateActive;
        material.frame=self.bounds;material.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
        material.accessibilityElement=NO;[self addSubview:material];
        self.playButton=[VIMPlayButton new];self.playButton.target=target;self.playButton.action=action;
        self.playButton.bordered=NO;self.playButton.contentTintColor=NSColor.controlAccentColor;
        self.playButton.imageScaling=NSImageScaleProportionallyDown;
        self.playButton.toolTip=@"Start this Mac";self.playButton.accessibilityLabel=[@"Start " stringByAppendingString:name];self.playButton.keyEquivalent=@"\r";
        [self addSubview:self.playButton];
        self.mark=[NSImageView imageViewWithImage:[NSImage imageNamed:[guest stringByAppendingString:@"ProductIcon"]]?:NSApp.applicationIconImage];
        self.mark.imageScaling=NSImageScaleProportionallyUpOrDown;[self addSubview:self.mark];
        self.nameField=[NSTextField labelWithString:name];self.nameField.font=[NSFont systemFontOfSize:38 weight:NSFontWeightLight];
        self.nameField.textColor=NSColor.labelColor;self.nameField.lineBreakMode=NSLineBreakByTruncatingTail;
        [self addSubview:self.nameField];
    }return self;
}
- (void)layout {
    [super layout];
    NSSize image=self.wallpaper.image.size;
    CGFloat scale=MAX(NSWidth(self.bounds)/MAX(1,image.width),NSHeight(self.bounds)/MAX(1,image.height));
    NSSize size=NSMakeSize(image.width*scale,image.height*scale);
    self.wallpaper.frame=NSMakeRect(NSMidX(self.bounds)-size.width/2,NSMidY(self.bounds)-size.height/2,size.width,size.height);
    CGFloat diameter=MIN(260,MAX(160,NSWidth(self.bounds)*.205));
    diameter=MIN(diameter,MAX(120,NSHeight(self.bounds)-56-28-24));
    CGFloat bottom=NSMidY(self.bounds)-(diameter+28+56)/2;
    self.playButton.frame=NSMakeRect(NSMidX(self.bounds)-diameter/2,bottom+56+28,diameter,diameter);
    // A truncating NSTextField's intrinsic width can depend on its last frame.
    // Measure the full title instead, so repeated layouts never shrink it.
    NSSize titleSize=[self.nameField.stringValue sizeWithAttributes:@{NSFontAttributeName:self.nameField.font}];
    CGFloat nameWidth=MIN(ceil(titleSize.width)+8,MAX(0,NSWidth(self.bounds)-64-56-12));
    CGFloat identityWidth=56+12+nameWidth,nameHeight=ceil(titleSize.height)+4;
    CGFloat left=NSMidX(self.bounds)-identityWidth/2;
    self.mark.frame=NSMakeRect(left,bottom,56,56);
    self.nameField.frame=NSMakeRect(left+56+12,bottom+(56-nameHeight)/2,nameWidth,nameHeight);
}
- (void)viewDidChangeEffectiveAppearance {
    [super viewDidChangeEffectiveAppearance];
    self.wallpaper.image=[NSImage imageNamed:[self.guest stringByAppendingString:@"Wallpaper"]];
    self.needsLayout=YES;
}
@end
static NSString *const VIMUSBMenuTitle=@"USB Devices";
static NSString *const VIMToolbarMenuTitleIdentifier=@"VIMToolbarMenuTitle";
static NSString *ToolbarMenuTitle(NSMenu *menu) {
    NSMenuItem *first=menu.itemArray.firstObject;
    return [first.identifier isEqual:VIMToolbarMenuTitleIdentifier]?first.title:nil;
}
static NSMenu *WithToolbarMenuTitle(NSMenu *menu,NSString *title) {
    if(title.length&&!ToolbarMenuTitle(menu)) {
        // Rebuilding a grouped pull-down menu removes AppKit's title slot.
        // Restore it before real actions/devices, or entry zero disappears.
        NSMenuItem *item=[[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
        item.identifier=VIMToolbarMenuTitleIdentifier;item.enabled=NO;
        [menu insertItem:item atIndex:0];
    }
    return menu;
}
// NSMenuToolbarItem/NSToolbarItemGroup copy menus while constructing their
// controls. NSMenu does not retain/copy its delegate contract for us. Preserve
// the logical kind and delegate so these copies stay live, not boot snapshots.
@interface VIMLiveDeviceMenu : NSMenu
@property(copy) NSString *deviceKind;
@end
@implementation VIMLiveDeviceMenu
- (id)copyWithZone:(NSZone *)zone {
    VIMLiveDeviceMenu *copy=[super copyWithZone:zone];
    copy.deviceKind=self.deviceKind;copy.delegate=self.delegate;return copy;
}
@end
@interface VIMSession () <NSWindowDelegate,NSMenuItemValidation,NSMenuDelegate,NSToolbarDelegate>
@property VIMMachine *machine;
@property NSTask *task;
@property NSFileHandle *log;
@property VIMMetalView *displayView;
@property VIMDisplayConnection *displayConnection;
@property VIMQMPConnection *control;
@property VIMGuestAgent *guestAgent;
@property VIMGuestProgress *guestProgress;
@property NSData *guestStatusTool;
@property NSTextField *guestStatus;
@property NSTimer *guestTimer;
@property BOOL guestPending;
@property NSString *diagnosticDirectory;
@property BOOL diagnosticPending;
@property NSDictionary *observedDisplayMode;
- (void)applyDisplayMode:(NSString *)mode completion:(void (^)(NSDictionary *,NSError *))completion;
@property(weak) NSWindow *diagnosticAboutWindow;
@property(nonatomic) BOOL preparationRequested;
@property(nonatomic) BOOL installationRequested;
@property BOOL automaticInstallation;
@property BOOL relaunchForGraphics;
@property BOOL relaunchForGuestReboot;
@property NSArray<NSDate *> *recentGuestReboots;
@property NSTimer *handoffTimer;
@property NSString *reconciledBoot;
@property(nonatomic) NSDictionary *completionGuestReport;
@property(nonatomic) NSUInteger reconciliationAttempts;
@property(nonatomic) BOOL repairRequested;
@property BOOL verifiedUpdateRepair;
@property BOOL explicitRepair;
@property BOOL repairArchivePending;
@property(nonatomic) BOOL preparing;
@property VIMInstallationActivity *sleepActivity;
@property NSString *lastGuestMessage;
@property NSTextField *status;
@property NSString *runtimeDirectory;
@property NSButton *shutdownButton;
@property NSTimer *resizeTimer;
@property NSSize lastRequestedSize;
@property BOOL resizeArmed;
@property NSString *displaySizedBoot;
@property NSString *sleepDisabledBoot;
@property NSString *usernameBoot;
@property VIMClipboardBridge *clipboard;
@property VIMClipboardBridge *powerBridge;
@property NSString *powerConfiguredBoot;
@property NSDate *powerRetryAfter;
@property NSUInteger audioTraceEpoch;
@property NSNumber *clipboardUID;
@property VIMSMBService *smbService;
@property VIMNetworkClient *networkClient;
@property BOOL networkStarting;
@property BOOL networkLinkConnected;
@property BOOL clipboardPending;
@property BOOL clipboardSharingEnabled;
@property BOOL bootPolicyPending;
@property NSString *bootPolicyCheckedBoot;
@property NSString *clipboardFailure;
@property NSString *clipboardConfiguredBoot;
@property NSDate *clipboardRetryAfter;
@property NSMutableDictionary<NSString *,NSString *> *usbAttached;
@property NSArray<NSDictionary *> *audioOutputChoices;
@property NSArray<NSDictionary *> *audioInputChoices;
@property BOOL audioChoicesPending;
@property NSArray<NSString *> *guestIPAddresses;
@property NSTimer *statusTimer;
@property BOOL statusPending;
@property BOOL runStateQueryFailed;
@property NSString *lastRunState;
@property BOOL wakePending;
@property BOOL shutdownRequested;
@property BOOL forceResetRequested;
@property NSTimer *controlRetryTimer;
@property VIMUSBClient *usbClient;
@property BOOL usbOperationPending;
@property BOOL closeAfterShutdown;
@property int machineLock;
@property NSStackView *statePanel;
@property NSTextField *stateLabel;
@property NSButton *stateAction;
@property(nonatomic) BOOL retinaEnabled;
@property(nonatomic) NSString *displayMode;
@property NSString *displayToolDirectory;
@property BOOL displayModePending;
@property NSString *insertedMediaPath;
@property NSString *insertedMediaFormat;
@property BOOL mediaChangePending;
@end
@implementation VIMSession
+ (void)initialize {
    if(self!=VIMSession.class)return;
    [NSNotificationCenter.defaultCenter addObserverForName:@"VIMMachineRelocated" object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note){
        VIMSession *session=sessions[note.userInfo[@"Identifier"]];
        if(session&&!session.task.running){VIMMachine *fresh=[VIMMachine loadURL:note.userInfo[@"URL"] error:nil];if(fresh){session.machine=fresh;[session showStoppedScreen];}}
    }];
}
- (void)updateInstallationActivity {
    BOOL setup=self.machine.configuration[@"InstallerMedia"]!=nil||
        [self.machine.configuration[@"InstallState"] isEqual:@"host-populated"];
    BOOL halted=[@[@"paused",@"suspended",@"shutdown",@"io-error",@"guest-panicked",@"internal-error",@"watchdog"] containsObject:self.lastRunState?:@""];
    BOOL active=self.task.running&&!halted&&(self.preparing||self.preparationRequested||self.installationRequested||(setup&&!self.completionGuestReport));
    if(active&&!self.sleepActivity)self.sleepActivity=[VIMInstallationActivity beginWithReason:@"Installing macOS in a virtual Mac"];
    else if(!active)self.sleepActivity=nil;
}
- (void)setPreparing:(BOOL)value {_preparing=value;[self updateInstallationActivity];}
- (void)setPreparationRequested:(BOOL)value {_preparationRequested=value;[self updateInstallationActivity];}
- (void)setInstallationRequested:(BOOL)value {_installationRequested=value;[self updateInstallationActivity];}
- (void)setCompletionGuestReport:(NSDictionary *)value {_completionGuestReport=value;[self updateInstallationActivity];}
- (instancetype)initWithWindow:(NSWindow *)window {
    if((self=[super initWithWindow:window])) {
        _machineLock=-1;
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(refreshUSBToolbar:) name:@"VIMHostUSBChanged" object:nil];
        VIMStartHostUSBNotifications();
    }
    return self;
}
+ (void)showMachineWindow:(VIMMachine *)machine {
    NSAssert(NSThread.isMainThread,@"Session UI is main-thread owned");
    if(!sessions)sessions=[NSMutableDictionary dictionary];
    NSString *key=SessionKey(machine);
    VIMSession *session=sessions[key];
    if(!session){session=[[self alloc] initWithWindow:nil];session.machine=machine;sessions[key]=session;[session showStoppedScreen];}
    else if(!session.task.running){session.machine=machine;[session showStoppedScreen];}
    [session showWindow:nil];[session.window makeKeyAndOrderFront:nil];
}
- (void)ensureWindow {
    [self refreshAudioChoices];
    BOOL created=self.window==nil;
    if(created)self.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,1100,740)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.releasedWhenClosed=NO;self.window.delegate=self;self.window.windowController=self;self.window.title=self.machine.name;
    self.window.tabbingMode=NSWindowTabbingModeDisallowed;
    [self.window setFrameAutosaveName:[@"Guest-" stringByAppendingString:(self.machine.configuration[@"Identifier"]?:self.machine.name)]];
    self.window.minSize=NSMakeSize(640,440);self.window.acceptsMouseMovedEvents=YES;
    if(created) {
        BOOL restored=[self.window setFrameUsingName:self.window.frameAutosaveName];
        NSScreen *screen=self.window.screen?:NSScreen.mainScreen;
        NSRect available=NSInsetRect(screen.visibleFrame,24,24);
        CGFloat chrome=NSHeight(self.window.frame)-NSHeight(self.window.contentView.frame);
        NSSize content=self.window.contentView.bounds.size;
        if(!restored) {
            // Standard logical desktop sizes, fitted to usable space (not the
            // physical panel pixels). Leave host Dock/menu bar and room around
            // the window visible. Saved user sizes still take precedence.
            NSSize chosen=VIMDefaultGuestContentSize(screen.visibleFrame.size,chrome);
            [self.window setContentSize:chosen];[self.window center];
        }
        else if(NSWidth(self.window.frame)>NSWidth(available)||NSHeight(self.window.frame)>NSHeight(available)) {
            CGFloat fit=MIN(NSWidth(available)/MAX(1,content.width),
                            (NSHeight(available)-chrome)/MAX(1,content.height));
            [self.window setContentSize:NSMakeSize(floor(content.width*fit),floor(content.height*fit))];
        }
        // A saved frame from a larger/disconnected monitor must remain usable.
        [self.window setFrame:[self.window constrainFrameRect:self.window.frame toScreen:screen] display:NO];
    }
}
- (void)installWindowControls:(BOOL)running {
    while(self.window.titlebarAccessoryViewControllers.count)[self.window removeTitlebarAccessoryViewControllerAtIndex:0];
    NSToolbar *toolbar=[[NSToolbar alloc] initWithIdentifier:running?@"GuestRunning":@"GuestStopped"];
    toolbar.delegate=self;toolbar.displayMode=NSToolbarDisplayModeIconOnly;toolbar.allowsUserCustomization=NO;
    self.window.toolbar=toolbar;self.window.toolbarStyle=NSWindowToolbarStyleUnifiedCompact;
}
- (NSArray<NSToolbarItemIdentifier> *)toolbarDefaultItemIdentifiers:(NSToolbar *)toolbar {
    NSString *key=[@"VIMExpandedToolbar-" stringByAppendingString:SessionKey(self.machine)];
    if(![NSUserDefaults.standardUserDefaults boolForKey:key])return @[NSToolbarFlexibleSpaceItemIdentifier,@"GuestExpand",@"GuestSettings"];
    return @[NSToolbarFlexibleSpaceItemIdentifier,@"GuestDevices",@"GuestExpand",@"GuestSettings"];
}
- (NSArray<NSToolbarItemIdentifier> *)toolbarAllowedItemIdentifiers:(NSToolbar *)toolbar {return [self toolbarDefaultItemIdentifiers:toolbar];}
- (NSToolbarItem *)toolbar:(NSToolbar *)toolbar itemForItemIdentifier:(NSToolbarItemIdentifier)identifier willBeInsertedIntoToolbar:(BOOL)insert {
    if([identifier isEqual:@"GuestDevices"]) {
        NSToolbarItemGroup *group=[[NSToolbarItemGroup alloc] initWithItemIdentifier:identifier];
        group.label=@"Devices";group.controlRepresentation=NSToolbarItemGroupControlRepresentationExpanded;group.bordered=YES;
        group.selectionMode=NSToolbarItemGroupSelectionModeMomentary;
        NSMutableArray *items=[NSMutableArray new];
        // SDL device selection is currently next-start only. Keep it in Sound
        // settings, not among controls that imply a live device change.
        for(NSString *child in @[@"GuestCPU",@"GuestUSB",@"GuestOptical",@"GuestNetwork",@"GuestSharing",@"GuestControls"])
            [items addObject:[self toolbar:toolbar itemForItemIdentifier:child willBeInsertedIntoToolbar:NO]];
        group.subitems=items;return group;
    }
    if([identifier isEqual:@"GuestExpand"]) {
        BOOL expanded=[NSUserDefaults.standardUserDefaults boolForKey:[@"VIMExpandedToolbar-" stringByAppendingString:SessionKey(self.machine)]];
        NSToolbarItem *item=[[NSToolbarItem alloc] initWithItemIdentifier:identifier];item.label=expanded?@"Hide Devices":@"Show Devices";item.toolTip=item.label;
        item.image=[NSImage imageWithSystemSymbolName:expanded?@"chevron.left":@"chevron.down" accessibilityDescription:item.label];item.target=self;item.action=@selector(toggleDeviceToolbar:);return item;
    }
    NSDictionary *devices=@{@"GuestCPU":@[@"CPU & Memory",@"cpu",@"showCPUSettings:"],@"GuestSharing":@[@"Shared Folders",@"folder",@"showSharingSettings:"]};
    NSArray *device=devices[identifier];if(device){NSToolbarItem *item=[[NSToolbarItem alloc] initWithItemIdentifier:identifier];item.label=device[0];item.toolTip=device[0];item.image=[NSImage imageWithSystemSymbolName:device[1] accessibilityDescription:device[0]];item.target=self;item.action=NSSelectorFromString(device[2]);return item;}
    if([identifier isEqual:@"GuestOptical"]) {
        NSMenuToolbarItem *item=[[NSMenuToolbarItem alloc] initWithItemIdentifier:identifier];item.label=@"CD/DVD";item.toolTip=@"CD/DVD";item.image=[NSImage imageWithSystemSymbolName:@"opticaldisc" accessibilityDescription:nil];
        item.showsIndicator=NO;
        item.menu=WithToolbarMenuTitle([self deviceMenuForKind:@"CD/DVD"],@"CD/DVD");return item;
    }
    if([identifier isEqual:@"GuestNetwork"]||[identifier isEqual:@"GuestAudio"]) {
        BOOL network=[identifier isEqual:@"GuestNetwork"];
        NSMenuToolbarItem *item=[[NSMenuToolbarItem alloc] initWithItemIdentifier:identifier];item.label=network?@"Network":@"Sound";item.toolTip=item.label;
        item.showsIndicator=NO;
        item.image=[NSImage imageWithSystemSymbolName:network?@"network":@"speaker.wave.2" accessibilityDescription:nil];item.menu=WithToolbarMenuTitle([self deviceMenuForKind:network?@"Network":@"Sound Output"],item.label);return item;
    }
    if([identifier isEqual:@"GuestUSB"]) {
        NSMenuToolbarItem *item=[[NSMenuToolbarItem alloc] initWithItemIdentifier:identifier];item.label=@"USB";item.toolTip=@"Connect USB devices to this Mac";
        item.showsIndicator=NO;
        item.image=[NSImage imageWithSystemSymbolName:@"cable.connector" accessibilityDescription:nil];
        item.menu=WithToolbarMenuTitle([self deviceMenuForKind:@"USB"],@"USB");return item;
    }
    if([identifier isEqual:@"GuestControls"]) {
        NSMenuToolbarItem *controls=[[NSMenuToolbarItem alloc] initWithItemIdentifier:identifier];controls.label=@"Controls";controls.toolTip=@"Control this Mac";
        controls.showsIndicator=NO;
        controls.image=[NSImage imageWithSystemSymbolName:@"power" accessibilityDescription:nil];
        NSMenu *menu=[[NSMenu alloc] initWithTitle:@"Controls"];
        NSArray *titles=@[@"Shut Down",@"Reset…",@"Force Stop…"];
        NSArray *actions=@[NSStringFromSelector(@selector(shutdown:)),NSStringFromSelector(@selector(resetMachine:)),NSStringFromSelector(@selector(forceStop:))];
        for(NSUInteger i=0;i<titles.count;i++){NSMenuItem *item=[[NSMenuItem alloc] initWithTitle:titles[i] action:NSSelectorFromString(actions[i]) keyEquivalent:@""];item.target=self;[menu addItem:item];}
        menu.delegate=self;controls.menu=menu;return controls;
    }
    NSToolbarItem *gear=[[NSToolbarItem alloc] initWithItemIdentifier:identifier];gear.label=@"Configure";gear.toolTip=@"Configure this Mac";
    gear.image=[NSImage imageWithSystemSymbolName:@"gearshape" accessibilityDescription:nil];gear.target=self;gear.action=@selector(configureMachine:);return gear;
}
- (void)showStoppedScreen {
    [self ensureWindow];[self installWindowControls:NO];self.window.subtitle=@"";
    self.displayMode=VIMDisplayMode(self.machine.editableConfiguration);self.retinaEnabled=![self.displayMode isEqual:@"scaled"];
    VIMStoppedMacView *content=[[VIMStoppedMacView alloc] initWithGuest:self.machine.guest name:self.machine.name target:self action:@selector(startAgain:)];
    self.window.contentView=content;
    self.window.minSize=NSMakeSize(440,320);self.window.maxSize=NSMakeSize(100000,100000);
    [self.window makeFirstResponder:content.playButton];
}
+ (BOOL)openMachine:(VIMMachine *)machine error:(NSError **)error {
    return [self requestMachine:machine operation:VIMBootStart error:error];
}
+ (BOOL)prepareMachine:(VIMMachine *)machine error:(NSError **)error {
    return [self requestMachine:machine operation:VIMBootPrepare error:error];
}
+ (BOOL)installMachine:(VIMMachine *)machine error:(NSError **)error {
    return [self requestMachine:machine operation:VIMBootInstall error:error];
}
+ (BOOL)repairMachine:(VIMMachine *)machine error:(NSError **)error {
    return [self requestMachine:machine operation:VIMBootRepair error:error];
}
+ (BOOL)performBootOperation:(VIMBootOperation)operation machine:(VIMMachine *)machine error:(NSError **)error {
    switch(operation) {
        case VIMBootStart:return [self openMachineAfterHostCheck:machine error:error];
        case VIMBootPrepare:return [self prepareMachineAfterHostCheck:machine error:error];
        case VIMBootInstall:return [self installMachineAfterHostCheck:machine error:error];
        case VIMBootRepair:return [self repairMachineAfterHostCheck:machine error:error];
    }
    return NO;
}
+ (BOOL)requestMachine:(VIMMachine *)machine operation:(VIMBootOperation)operation error:(NSError **)error {
    NSAssert(NSThread.isMainThread,@"Session UI is main-thread owned");
#if defined(__arm64__)
    NSString *key=SessionKey(machine);
    // Focusing an already running VM is not a new boot. Every actual start,
    // including installer VGA and automatic restarts, must pass a fresh probe.
    VIMSession *existing=sessions[key];
    if(existing.task.running||existing.networkStarting)
        return [self performBootOperation:operation machine:machine error:error];
    if(pendingRosettaStarts[key])return YES;
    if(!pendingRosettaStarts)pendingRosettaStarts=[NSMutableDictionary new];
    NSUUID *request=NSUUID.UUID;pendingRosettaStarts[key]=request;
    VIMCheckRosettaForBoot(^(BOOL missing,NSError *failure){
        if(![pendingRosettaStarts[key] isEqual:request])return; // quit/stop cancelled it
        [pendingRosettaStarts removeObjectForKey:key];
        NSWindow *window=sessions[key].window?:NSApp.mainWindow;
        if(failure){VIMPresentError(failure,window);return;}
        if(missing){VIMPresentRosettaRequiredAlert(window);return;}
        // No disk lock, network helper, QEMU process or install transaction is
        // touched until actual Intel execution succeeds. Never retry after an
        // alert: the user starts again after completing Apple's installation.
        if(![self performBootOperation:operation machine:machine error:&failure])VIMPresentError(failure,window);
    });
    return YES; // asynchronous start request accepted, not a running-VM claim
#else
    return [self performBootOperation:operation machine:machine error:error];
#endif
}
+ (BOOL)openMachineAfterHostCheck:(VIMMachine *)machine error:(NSError **)error {
    NSAssert(NSThread.isMainThread,@"Session UI is main-thread owned");
    if(![machine applyPendingChanges:error])return NO;
    machine=[VIMMachine loadURL:machine.URL error:error];if(!machine)return NO;
    NSString *notice=VIMHostUpdateNotice(NSProcessInfo.processInfo.operatingSystemVersion);
    if(notice){if(error)*error=VIMSessionError(notice);return NO;}
    if(!sessions)sessions=[NSMutableDictionary dictionary];
    NSString *key=SessionKey(machine);
    VIMSession *existing=sessions[key];
    if(existing.task.running||existing.networkStarting) { [existing showWindow:nil];[existing.window makeKeyAndOrderFront:nil];return YES; }
    NSError *cleanupError=nil;
    [machine retryInstallerCleanup:&cleanupError];
    // Cleanup may have retired routing even when deletion failed. Reload before
    // constructing arguments so a stale library model cannot remount the ISO.
    machine=[VIMMachine loadURL:machine.URL error:error];if(!machine)return NO;
    // Each boot owns fresh asynchronous transports; only the native window is
    // transferred, so callbacks from an old boot cannot mutate the new one.
    VIMSession *session=[[self alloc] initWithWindow:existing.window];session.machine=machine;
    if(![VIMNetworkMode(machine.configuration) isEqual:@"shared"]) {
        session.networkClient=[VIMNetworkClient new];session.networkStarting=YES;
        if(existing)existing.window=nil;sessions[key]=session;[session showStoppedScreen];
        NSString *bridge=machine.configuration[@"BridgeInterface"];
        if(!bridge.length)bridge=VIMNetworkDefaultBridge()?:@"";
        [session.networkClient startMode:VIMNetworkMode(machine.configuration) bridge:bridge completion:^(NSError *failure){
            if(!session.networkStarting)return;session.networkStarting=NO;
            if(!failure&&[session launch:&failure])return;
            [session cleanUp];[session showStoppedScreen];if(failure)VIMPresentError(failure,session.window);
        }];return YES;
    }
    if(![session launch:error]){if(existing)[session showStoppedScreen];return NO;}
    if(existing){existing.window=nil;session.window.windowController=session;session.window.delegate=session;}
    if(cleanupError) {
        session.guestStatus.stringValue=[NSString stringWithFormat:@"Installer cleanup pending: %@",cleanupError.localizedDescription];
        [session.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] Cleanup retry: %@\n",cleanupError.localizedDescription] dataUsingEncoding:NSUTF8StringEncoding]];
    }
    sessions[key]=session;
    NSMutableDictionary *opened=[[NSUserDefaults.standardUserDefaults dictionaryForKey:@"VIMLastOpened"] mutableCopy]?:[NSMutableDictionary dictionary];opened[SessionKey(machine)]=NSDate.date;[NSUserDefaults.standardUserDefaults setObject:opened forKey:@"VIMLastOpened"];
    [NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];return YES;
}
+ (BOOL)hasRunningMachines {
    for(VIMSession *session in sessions.allValues)if(session.task.running||session.networkStarting)return YES;
    return NO;
}
+ (BOOL)isRunningMachine:(VIMMachine *)machine {
    return sessions[SessionKey(machine)].task.running;
}
+ (void)enableGuestRemoteLogin:(VIMMachine *)machine completion:(void (^)(NSError *))completion {
    VIMSession *session=sessions[SessionKey(machine)];
    if(!session.task.running||session.preparing||session.guestPending||session.diagnosticPending||session.preparationRequested) {
        completion(VIMSessionError(@"Start this Mac and wait for setup to finish, then try again. You can also enable Remote Login in the virtual Mac’s Sharing preferences."));return;
    }
    session.guestPending=YES;
    void (^finish)(NSError *)=^(NSError *error){
        session.guestPending=NO;
        // The one-shot readiness timer may have fired while this user action
        // owned the agent. Resume the production loop, including verification.
        [session refreshGuestStatus];completion(error);
    };
    [session.guestAgent connectWithCompletion:^(NSError *error){
        if(error){finish(error);return;}
        [session.guestAgent runExecutable:@"/usr/sbin/systemsetup" arguments:@[@"-setremotelogin",@"on"] timeout:30 completion:^(NSDictionary *result,NSError *failure){
            if(failure||![result[@"exitcode"] isEqual:@0]) {
                finish(failure?:VIMSessionError(@"macOS could not enable Remote Login automatically. In the virtual Mac, open System Preferences → Sharing and select Remote Login."));return;
            }
            // systemsetup can print an authorization failure yet exit zero.
            // Confirm the actual setting rather than trusting its exit code.
            [session.guestAgent runExecutable:@"/usr/sbin/systemsetup" arguments:@[@"-getremotelogin"] timeout:15 completion:^(NSDictionary *check,NSError *checkError){
                NSString *text=[[NSString alloc] initWithData:check[@"stdout"]?:NSData.data encoding:NSUTF8StringEncoding];
                BOOL enabled=[check[@"exitcode"] isEqual:@0]&&text.length&&[text rangeOfString:@"Remote Login: On"].location!=NSNotFound;
                finish(checkError?:(!enabled?VIMSessionError(@"Remote Login is still off. Enable it in the virtual Mac’s System Preferences → Sharing; macOS may require your approval there."):nil));
            }];
        }];
    }];
}
+ (NSString *)statusForMachine:(VIMMachine *)machine {
    VIMSession *session=sessions[SessionKey(machine)];
    if(session.task.running) {
        if([session.lastRunState isEqual:@"suspended"])return @"Sleeping";
        if([session.lastRunState isEqual:@"paused"])return @"Paused";
        return machine.requiresInstallerDisplay?@"Installing macOS":@"Running";
    }
    return machine.requiresInstallerDisplay?@"Installation in progress":@"Shut down";
}
+ (void)openGuestPreferences:(NSString *)pane machine:(VIMMachine *)machine completion:(void (^)(NSError *))completion {
    VIMSession *session=sessions[SessionKey(machine)];
    if(!session.task.running||session.preparing||session.guestPending||session.diagnosticPending||session.preparationRequested) {
        completion(VIMSessionError(@"Start this Mac, sign in and wait for setup to finish, then try again. You can also open System Preferences inside the virtual Mac."));return;
    }
    session.guestPending=YES;
    void (^finish)(NSError *)=^(NSError *error){session.guestPending=NO;[session refreshGuestStatus];completion(error);};
    [session.guestAgent connectWithCompletion:^(NSError *error){
        if(error){finish(error);return;}
        [session.guestAgent openPreferencesPane:pane completion:finish];
    }];
}
+ (void)openHostConnection:(NSURL *)URL inMachine:(VIMMachine *)machine completion:(void (^)(NSError *))completion {
    VIMSession *session=sessions[SessionKey(machine)];
    NSString *host=[self networkDetailsForMachine:machine][@"HostAddress"];
    if(!host.length||![URL.host isEqual:host]){completion(VIMSessionError(@"This Mac’s network address changed. Reopen File Sharing or Remote Login and try again."));return;}
    if(!session.task.running||session.preparing||session.guestPending||session.diagnosticPending||session.preparationRequested){completion(VIMSessionError(@"Start the virtual Mac and sign in to its desktop, then try again."));return;}
    session.guestPending=YES;
    void (^finish)(NSError *)=^(NSError *error){session.guestPending=NO;[session refreshGuestStatus];completion(error);};
    [session.guestAgent connectWithCompletion:^(NSError *error){if(error){finish(error);return;}[session.guestAgent openHostConnection:URL completion:finish];}];
}
+ (BOOL)prepareMachineAfterHostCheck:(VIMMachine *)machine error:(NSError **)error {
    if(![@[@"mojave",@"catalina",@"bigsur"] containsObject:machine.guest]) {
        if(error)*error=VIMSessionError(@"Guest preparation supports Mojave, Catalina and Big Sur.");return NO;
    }
    if(![self openMachineAfterHostCheck:machine error:error])return NO;
    VIMSession *session=sessions[SessionKey(machine)];
    if(session.preparing||session.preparationRequested){if(error)*error=VIMSessionError(@"Guest preparation is already active.");return NO;}
    session.preparationRequested=YES;[session refreshGuestStatus];return YES;
}
+ (void)stopAll { [pendingRosettaStarts removeAllObjects];for(VIMSession *session in sessions.allValues){if(session.networkStarting){[session cleanUp];[session showStoppedScreen];}if(session.task.running){session.forceResetRequested=NO;session.relaunchForGraphics=NO;session.relaunchForGuestReboot=NO;session.shutdownRequested=YES;[session.task terminate];}} }
+ (NSArray<NSString *> *)runningMachineNames {
    NSMutableArray *names=[NSMutableArray array];for(VIMSession *session in sessions.allValues)if(session.task.running)[names addObject:session.machine.name];return names;
}
+ (void)requestShutdownAll {
    [pendingRosettaStarts removeAllObjects];
    for(VIMSession *session in sessions.allValues)if(session.networkStarting){[session cleanUp];[session showStoppedScreen];}
    for(VIMSession *session in sessions.allValues)if(session.task.running&&!session.preparing&&!session.relaunchForGraphics)[session shutdown:self];
}
+ (BOOL)installMachineAfterHostCheck:(VIMMachine *)machine error:(NSError **)error {
    NSURL *journal=[machine.URL URLByAppendingPathComponent:@"State/GuestInstallation.plist"];
    if([NSFileManager.defaultManager fileExistsAtPath:journal.path]) {
        if(error)*error=VIMSessionError(@"An installation record already exists. Check its saved outcome before starting another installation.");return NO;
    }
    if(![self prepareMachineAfterHostCheck:machine error:error])return NO;
    sessions[SessionKey(machine)].installationRequested=YES;
    return YES;
}
+ (BOOL)repairMachineAfterHostCheck:(VIMMachine *)machine error:(NSError **)error {
    if(![@[@"mojave",@"catalina",@"bigsur"] containsObject:machine.guest]) {
        if(error)*error=VIMSessionError(@"Repair supports Mojave, Catalina and Big Sur.");return NO;
    }
    if(![self openMachineAfterHostCheck:machine error:error])return NO;
    VIMSession *session=sessions[SessionKey(machine)];
    if(session.preparing||session.preparationRequested||session.repairRequested) {
        if(error)*error=VIMSessionError(@"Guest setup is already active.");return NO;
    }
    session.repairRequested=YES;session.explicitRepair=YES;[session refreshGuestStatus];return YES;
}
- (BOOL)launch:(NSError **)error {
    // Development root is an explicit debug preference, never an implicit
    // dependency on a developer's checkout in the shipping app.
    NSString *root=VIMDevelopmentSourceRoot();
#if defined(__arm64__)
    BOOL appleSilicon=YES;
#else
    BOOL appleSilicon=NO;
    int translated=0;size_t translatedSize=sizeof(translated);
    if(sysctlbyname("sysctl.proc_translated",&translated,&translatedSize,NULL,0)==0&&translated) {
        if(error)*error=VIMSessionError(@"Open the Apple silicon version of Virtual Intel Mac. The Intel app cannot use hardware virtualization through Rosetta.");
        return NO;
    }
#endif
    // Explicit development qualification of recovery/update environments that
    // do not contain the installed system's graphics support. This never edits
    // the VM receipt or discards its verified graphics transaction.
    BOOL installerDisplay=self.machine.requiresInstallerDisplay||
        [NSProcessInfo.processInfo.arguments containsObject:@"--installer-display"];
    NSString *qemu=[root stringByAppendingPathComponent:installerDisplay?
        @"artifacts/host-arm64/qemu-arm64-app-installer":@"artifacts/host-arm64/qemu-arm64-alternate-lookup-v2"];
    NSString *helper=[root stringByAppendingPathComponent:@"artifacts/host-arm64/apple-gfx-helper-app"];
    NSString *donor=[root stringByAppendingPathComponent:@"artifacts/host-arm64/donor-v2/Out/System/Library/Frameworks/ParavirtualizedGraphics.framework/Versions/A/ParavirtualizedGraphics"];
    NSString *bios=[root stringByAppendingPathComponent:@"vendor/qemu/pc-bios"];
    NSString *llvm=[root stringByAppendingPathComponent:@"artifacts/mojave/reference/payload/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/31001/Libraries/libLLVM.dylib"];
    if(!appleSilicon) {
        qemu=[root stringByAppendingPathComponent:@"artifacts/host-intel/qemu-system-x86_64-app"];
        helper=[root stringByAppendingPathComponent:@"artifacts/host-intel/apple-gfx-helper-app"];
    }
    if(!root.length) {
        NSString *runtime=[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"Runtime"];
        qemu=[runtime stringByAppendingPathComponent:@"bin/qemu-system-x86_64"];
        helper=[runtime stringByAppendingPathComponent:@"bin/apple-gfx-helper"];
        donor=[runtime stringByAppendingPathComponent:@"Donor/ParavirtualizedGraphics.framework/Versions/A/ParavirtualizedGraphics"];
        bios=[runtime stringByAppendingPathComponent:@"pc-bios"];
        llvm=[runtime stringByAppendingPathComponent:@"Donor/libLLVM.dylib"];
    }
    // macOS 27 removed the Intel guest protocol from system PVG. Earlier
    // systems load their system Intel slice normally (in the helper on ARM).
    BOOL extractedDonor=appleSilicon && NSProcessInfo.processInfo.operatingSystemVersion.majorVersion>=27;
    if(!extractedDonor)donor=@"/System/Library/Frameworks/ParavirtualizedGraphics.framework/Versions/A/ParavirtualizedGraphics";
    BOOL mojave=[self.machine.guest isEqual:@"mojave"];
    // System frameworks can live only in the dyld cache; dlopen in the helper
    // is authoritative for that path, not fileExistsAtPath.
    NSArray *required=installerDisplay?@[qemu,bios]:(appleSilicon?(extractedDonor?@[qemu,helper,donor,bios]:@[qemu,helper,bios]):@[qemu,bios]);
    for(NSString *path in required)if(![NSFileManager.defaultManager fileExistsAtPath:path]) {
        if(error)*error=VIMSessionError([@"Missing runtime resource: " stringByAppendingString:path]);return NO;
    }
    if(appleSilicon && mojave && !installerDisplay && ![NSFileManager.defaultManager fileExistsAtPath:llvm]) {
        if(error)*error=VIMSessionError(@"The Mojave shader compiler repair resource is missing.");return NO;
    }
    // Keep the lock file in place: unlinking it would allow two processes to
    // lock different inodes for the same package. Do not follow a planted link.
    NSURL *lockURL=[self.machine.URL URLByAppendingPathComponent:@"State/Session.lock"];
    self.machineLock=open(lockURL.fileSystemRepresentation,O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    struct stat lockInfo;
    if(self.machineLock<0||fstat(self.machineLock,&lockInfo)||!S_ISREG(lockInfo.st_mode)||
       lockInfo.st_nlink!=1||flock(self.machineLock,LOCK_EX|LOCK_NB)) {
        if(self.machineLock>=0)close(self.machineLock);self.machineLock=-1;
        if(error)*error=VIMSessionError(@"This Mac is already in use, or its session lock could not be opened. Close its other session before starting it here.");
        return NO;
    }
    char directory[]="/tmp/vim-session.XXXXXX";
    if(!mkdtemp(directory)) {
        close(self.machineLock);self.machineLock=-1;
        if(error)*error=VIMSessionError(@"Could not create the VM session directory.");return NO;
    }
    self.runtimeDirectory=@(directory);
    [self ensureWindow];self.window.title=self.machine.name;self.window.contentView=[NSView new];
    NSView *content=self.window.contentView;
    self.displayView=[[VIMMetalView alloc] initWithFrame:NSZeroRect];self.displayView.translatesAutoresizingMaskIntoConstraints=NO;
    self.status=[NSTextField labelWithString:@"Starting…"];self.status.textColor=NSColor.secondaryLabelColor;
    self.guestStatus=[NSTextField labelWithString:@"Waiting for macOS…"];
    self.guestStatus.textColor=NSColor.secondaryLabelColor;
    self.guestStatus.lineBreakMode=NSLineBreakByTruncatingTail;
    self.shutdownButton=[NSButton buttonWithTitle:@"Shut Down" target:self action:@selector(shutdown:)];self.shutdownButton.enabled=NO;
    // Status labels remain diagnostic models, not permanent guest chrome.
    [content addSubview:self.displayView];
    [NSLayoutConstraint activateConstraints:@[
        [self.displayView.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [self.displayView.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
        [self.displayView.topAnchor constraintEqualToAnchor:content.topAnchor],
        [self.displayView.bottomAnchor constraintEqualToAnchor:content.bottomAnchor]]];
    [self installWindowControls:YES];
    if(installerDisplay) {
        // The unaccelerated framebuffer remains 1280x800. Fit its window in
        // logical points on smaller hosts without changing that guest mode.
        NSScreen *screen=self.window.screen?:NSScreen.mainScreen;
        CGFloat chrome=NSHeight(self.window.frame)-NSHeight(content.frame);
        CGFloat fit=MIN(1.,MIN((NSWidth(screen.visibleFrame)-48)/1280.,
                              (NSHeight(screen.visibleFrame)-chrome-48)/800.));
        [self.window setContentSize:NSMakeSize(floor(1280*fit),floor(800*fit))];
        [self.window center];
    }
    self.stateLabel=[NSTextField wrappingLabelWithString:@""];self.stateLabel.textColor=NSColor.whiteColor;self.stateLabel.alignment=NSTextAlignmentCenter;
    self.stateAction=[NSButton buttonWithTitle:@"Start" target:self action:@selector(startAgain:)];
    self.statePanel=[NSStackView stackViewWithViews:@[self.stateLabel,self.stateAction]];self.statePanel.orientation=NSUserInterfaceLayoutOrientationVertical;self.statePanel.spacing=16;self.statePanel.hidden=YES;
    self.statePanel.translatesAutoresizingMaskIntoConstraints=NO;[content addSubview:self.statePanel];
    [NSLayoutConstraint activateConstraints:@[[self.statePanel.centerXAnchor constraintEqualToAnchor:content.centerXAnchor],[self.statePanel.centerYAnchor constraintEqualToAnchor:content.centerYAnchor],[self.statePanel.widthAnchor constraintLessThanOrEqualToConstant:400]]];
    __weak VIMSession *weakSelf=self;
    self.displayConnection=[[VIMDisplayConnection alloc] initWithView:self.displayView failure:^(NSError *failure) {
        VIMSession *session=weakSelf;if(session.task.running)session.status.stringValue=failure.localizedDescription;
    } error:error];
    if(!self.displayConnection) { [self cleanUp];return NO; }
    self.displayMode=installerDisplay?@"scaled":VIMDisplayMode(self.machine.configuration);self.retinaEnabled=![self.displayMode isEqual:@"scaled"];
    [content layoutSubtreeIfNeeded];
    CGFloat initialScale=[self.displayMode isEqual:@"retina"]?2:(self.retinaEnabled?self.window.backingScaleFactor:1);
    NSUInteger initialWidth=MAX(640,MIN(8192,floor(self.displayView.bounds.size.width*initialScale/2)*2));
    NSUInteger initialHeight=MAX(400,MIN(8192,floor(self.displayView.bounds.size.height*initialScale/2)*2));
    // Queue before connection: both in-process PVG and the Rosetta helper
    // receive the window size as soon as their private display channel opens.
    if(!installerDisplay)[self.displayConnection requestWidth:initialWidth height:initialHeight];
    NSString *qmp=[self.runtimeDirectory stringByAppendingPathComponent:@"control"];
    self.control=[[VIMQMPConnection alloc] initWithSocketPath:qmp];
    __weak VIMSession *eventSession=self;
    self.control.lifecycleEventHandler=^(NSDictionary *event){
        VIMSession *session=eventSession;if(!session)return;
        NSString *kind=event[@"event"];
        if([kind isEqual:@"DEVICE_DELETED"])[session.usbClient deviceWasDeleted:event[@"data"][@"device"] control:session.control];
        if([kind isEqual:@"RESET"]) {
            session.guestIPAddresses=nil;
            session.displayToolDirectory=nil;
            session.displayView.displaySleeping=YES;session.displayView.displaySleeping=NO;
        } else if([kind isEqual:@"SUSPEND"]||[kind isEqual:@"SHUTDOWN"]) {
            if([kind isEqual:@"SHUTDOWN"]&&[@[@"guest-reset",@"host-qmp-system-reset"] containsObject:event[@"data"][@"reason"]?:@""]&&
               !session.shutdownRequested&&!session.closeAfterShutdown)session.relaunchForGuestReboot=YES;
            session.displayView.displaySleeping=YES;session.statePanel.hidden=YES;
            if([kind isEqual:@"SUSPEND"]){session.lastRunState=@"suspended";session.status.stringValue=@"Sleeping";}
        } else if([kind isEqual:@"WAKEUP"]) {
            session.lastRunState=@"running";session.displayView.displaySleeping=NO;
            session.statePanel.hidden=YES;session.status.stringValue=@"Running";
        } else if([kind isEqual:@"DEVICE_TRAY_MOVED"]&&[event[@"data"][@"tray-open"] isEqual:@YES]&&!session.mediaChangePending) {
            NSString *identifier=event[@"data"][@"id"]?:event[@"data"][@"device"];
            if([@[@"cdrom-device",@"image-device"] containsObject:identifier]) {
                session.insertedMediaPath=@"";session.insertedMediaFormat=nil;
            }
        }
        NSData *json=[NSJSONSerialization dataWithJSONObject:event options:0 error:NULL];
        NSString *line=[[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding];
        [session.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] Lifecycle: %@\n",line] dataUsingEncoding:NSUTF8StringEncoding]];
    };
    self.guestAgent=[[VIMGuestAgent alloc] initWithSocketPath:[self.runtimeDirectory stringByAppendingPathComponent:@"guest-agent"]];
    NSString *statusTool=root.length?[root stringByAppendingPathComponent:@"prebuilt/tools/guest-status"]:
        [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"Runtime/GuestTools/guest-status"];
    self.guestStatusTool=[NSData dataWithContentsOfFile:statusTool];
    NSDictionary *config=self.machine.configuration;
    self.shutdownRequested=NO;self.closeAfterShutdown=NO;self.displayMode=installerDisplay?@"scaled":VIMDisplayMode(config);self.retinaEnabled=![self.displayMode isEqual:@"scaled"];
    self.observedDisplayMode=nil;
    self.networkLinkConnected=!config[@"NetworkConnected"]||[config[@"NetworkConnected"] boolValue];
    NSString *firmwareRoot=root.length?[root stringByAppendingPathComponent:@"Build/DevelopmentFirmware"]:
        [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"Runtime/Firmware"];
    firmwareRoot=[firmwareRoot stringByAppendingPathComponent:self.machine.guest];
    // One immutable EFI per OS. OpenCore reads these eight bytes once through
    // QEMU fw_cfg, after OpenCore's baseline and before macOS starts.
    // No generated EFI, copied template or separate policy variable in NVRAM.
    NSString *efi=@"EFI.img";
    BOOL preparedGL=[self.machine.guest isEqual:@"bigsur"]&&self.machine.hasPublishedGuestGraphics;
    BOOL signatureOnly=[VIMGuestBootPolicy readyAtURL:[self.machine.URL URLByAppendingPathComponent:@"State/BootPolicy.plist"]
        guest:self.machine.guest identifier:config[@"Identifier"]];
    NSString *bootPolicy=[NSString stringWithFormat:@"name=opt/org.virtualintelmac/boot-policy,string=VIM1%c%c00",
        signatureOnly?'S':'L',preparedGL?'1':'0'];
    NSString *efiOverride=nil;
    NSArray *launchArguments=NSProcessInfo.processInfo.arguments;
    NSUInteger overrideIndex=[launchArguments indexOfObject:@"--guest-efi"];
    if(overrideIndex!=NSNotFound) {
        efiOverride=overrideIndex+1<launchArguments.count?launchArguments[overrideIndex+1]:nil;
        int fd=efiOverride.isAbsolutePath?open(efiOverride.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC):-1;
        char signature[8]={0};struct stat information;
        BOOL valid=fd>=0&&!fstat(fd,&information)&&S_ISREG(information.st_mode)&&pread(fd,signature,8,512)==8&&!memcmp(signature,"EFI PART",8);
        if(fd>=0)close(fd);
        if(!valid){if(error)*error=VIMSessionError(@"--guest-efi requires an absolute path to a regular GPT EFI disk image. The VM’s firmware was not changed.");[self cleanUp];return NO;}
    }
    for(NSString *file in @[@"CODE.fd",efi])if(![NSFileManager.defaultManager fileExistsAtPath:[firmwareRoot stringByAppendingPathComponent:file]]) {
        if(error)*error=VIMSessionError(@"This app is missing its firmware. Rebuild or reinstall the complete app.");
        [self cleanUp];return NO;
    }
    NSString *(^path)(NSString *)=^NSString *(NSString *key){
        if([key isEqual:@"Firmware"])return VIMOptionPath([firmwareRoot stringByAppendingPathComponent:@"CODE.fd"]);
        if([key isEqual:@"BootMedia"])return VIMOptionPath(efiOverride?:[firmwareRoot stringByAppendingPathComponent:efi]);
        if([key isEqual:@"InstallerMedia"]&&[config[@"InstallerMediaShared"] boolValue])return VIMOptionPath(config[key]);
        return VIMOptionPath([self.machine.URL.path stringByAppendingPathComponent:config[key]]);
    };
    NSString *memory=[config[@"MemoryMiB"] stringValue];
    // The documented shared network is IPv4. Do not advertise a second IPv6
    // default route that can black-hole connections on IPv4-only host networks.
    NSMutableString *network=[NSMutableString stringWithString:@"user,id=net0,ipv6=off"];
    NSString *mac=VIMNetworkMACAddress(config);
    if([config[@"SSHEnabled"] boolValue])[network appendFormat:@",hostfwd=tcp:127.0.0.1:%ld-:22",[config[@"SSHPort"] integerValue]];
    if([config[@"FileSharingEnabled"] boolValue])[network appendFormat:@",hostfwd=tcp:127.0.0.1:%ld-:445",[config[@"FileSharingPort"] integerValue]];
    if(![VIMNetworkMode(config) isEqual:@"shared"])network=[NSMutableString stringWithString:@"socket,id=net0,fd=0"];
    NSString *cpu=@"Penryn,vendor=GenuineIntel,+sse4.2,+popcnt,+aes,+pclmulqdq,+pcid";
    if(!appleSilicon)cpu=mojave?@"host,model=94":@"host";
    // Development A/B only: the same disk/firmware/runtime, no saved machine
    // configuration change. Keep SATA as the normal path until qualified.
    NSString *systemDiskDevice=@"ide-hd,bus=ide.1,drive=macos,bootindex=2,rotation_rate=1";
    NSUInteger diskOption=[launchArguments indexOfObject:@"--system-disk-interface"];
    if(diskOption!=NSNotFound) {
        NSString *interface=diskOption+1<launchArguments.count?launchArguments[diskOption+1]:@"";
        if(![@[@"sata",@"nvme"] containsObject:interface]) {
            if(error)*error=VIMSessionError(@"--system-disk-interface requires sata or nvme. The Mac was not started.");
            [self cleanUp];return NO;
        }
        if([interface isEqual:@"nvme"])
            systemDiskDevice=@"nvme,drive=macos,serial=VIM00000001,bus=pcie.0,addr=0x8,bootindex=2";
        [self.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] Development system disk interface: %@\n",interface] dataUsingEncoding:NSUTF8StringEncoding]];
    }
    self.task=[NSTask new];self.task.executableURL=[NSURL fileURLWithPath:qemu];
    // Interactive VM execution is user-initiated work, not a maintenance job.
    // Match UTM's launcher policy; guest realtime audio runs inside ordinary
    // vCPU threads and cannot promote those host threads itself.
    self.task.qualityOfService=NSQualityOfServiceUserInitiated;
    if(self.networkClient.socket)self.task.standardInput=self.networkClient.socket;
    // Input uses USB throughout, including firmware. Do not also publish an
    // unused PS/2 keyboard: the legacy Voodoo driver identifies it as generic
    // and independently triggers Keyboard Setup Assistant despite Apple USB ID.
    NSMutableArray *baseArguments=[NSMutableArray arrayWithArray:@[@"-name",VIMOptionPath(self.machine.name),@"-L",bios,@"-machine",@"q35,i8042=off",@"-accel",appleSilicon?@"tcg,thread=multi":@"hvf",
        // v1 does not implement reliable S3/S4 resume. Do not advertise these
        // states to macOS in the first place; pmset sleep=0 remains a second
        // safeguard, rather than the sole defense against an idle timer.
        @"-global",@"ICH9-LPC.disable_s3=1",@"-global",@"ICH9-LPC.disable_s4=1",
        @"-cpu",cpu,@"-smp",[config[@"CPUCount"] stringValue],@"-m",memory,
        @"-object",[NSString stringWithFormat:appleSilicon?
            @"memory-backend-shm,id=pvg-ram,size=%@M,share=on":
            @"memory-backend-ram,id=pvg-ram,size=%@M",memory],
        @"-machine",@"memory-backend=pvg-ram",@"-nodefaults",@"-vga",@"none",
        @"-device",@"apple-gfx-pci",@"-display",@"none",
        @"-device",@"ich9-usb-ehci1,id=usb",@"-device",@"usb-kbd,id=keyboard,bus=usb.0,apple-ansi=on",
        // USB passthrough bus (USB 2 and 3). A fixed slot after the network
        // adapter's leaves existing device addresses and services unchanged.
        @"-device",@"qemu-xhci,id=xhci,bus=pcie.0,addr=0x7",
        @"-device",@"usb-tablet,id=tablet,bus=usb.0",
        // Apple's Intel guest agent opens this exact VirtIO console name.
        // Keep its control endpoint inside the per-session private directory;
        // unlike network forwarding it is never exposed on a TCP interface.
        @"-device",@"virtio-serial-pci,id=guestserial",
        @"-chardev",[NSString stringWithFormat:@"socket,id=guestagent,path=%@,server=on,wait=off",
            VIMOptionPath([self.runtimeDirectory stringByAppendingPathComponent:@"guest-agent"])],
        @"-device",@"virtserialport,bus=guestserial.0,chardev=guestagent,name=org.qemu.guest_agent.0",
        // A distinct channel, never another client of the guest-agent socket.
        // Present it even when sharing is off so enabling it needs no restart.
        @"-chardev",[NSString stringWithFormat:@"socket,id=clipboard,path=%@,server=on,wait=off",
            VIMOptionPath([self.runtimeDirectory stringByAppendingPathComponent:@"clipboard"])],
        @"-device",@"virtserialport,bus=guestserial.0,chardev=clipboard,name=org.vim.clipboard",
        // Old donated framebuffer drivers do not report DPMS to host PVG.
        // Dedicated metadata events remain available with clipboard disabled.
        @"-chardev",[NSString stringWithFormat:@"socket,id=displaypower,path=%@,server=on,wait=off",
            VIMOptionPath([self.runtimeDirectory stringByAppendingPathComponent:@"displaypower"])],
        @"-device",@"virtserialport,bus=guestserial.0,chardev=displaypower,name=org.vim.display"]];
    if(!efiOverride)[baseArguments addObjectsFromArray:@[@"-fw_cfg",bootPolicy]];
    if(!installerDisplay) {
        // The existing QEMU property supplies the initial native display mode;
        // no fixed 1920-wide default or wait for the desktop/guest tool.
        NSDictionary *graphics=@{@"driver":@"apple-gfx-pci",@"display-modes":@[
            [NSString stringWithFormat:@"%lux%lu@60",(unsigned long)initialWidth,(unsigned long)initialHeight]]};
        NSData *json=[NSJSONSerialization dataWithJSONObject:graphics options:0 error:NULL];
        baseArguments[[baseArguments indexOfObject:@"apple-gfx-pci"]]=[[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding];
    }
    if(!config[@"AudioEnabled"]||[config[@"AudioEnabled"] boolValue]) {
        NSMutableString *audio=[NSMutableString stringWithString:@"sdl,id=audio0"];
        // QEMU otherwise fixes its mixer at 44.1 kHz even on a 48 kHz host.
        // Match the selected output's current rate: avoid a second converter
        // after guest mixing, without changing the host device or queue size.
        [audio appendFormat:@",out.frequency=%lu",(unsigned long)VIMHostOutputSampleRate(VIMAudioSelection(config,NO))];
        // Capture also follows its selected physical device, independently of
        // output. Avoid SDL's native-input -> 44.1 kHz -> guest-rate chain.
        [audio appendFormat:@",in.frequency=%lu",(unsigned long)VIMHostInputSampleRate(VIMAudioSelection(config,YES))];
        if([VIMAudioSelection(config,YES) isEqual:@"disabled"])[audio appendString:@",in.voices=0"];
        if([VIMAudioSelection(config,NO) isEqual:@"disabled"])[audio appendString:@",out.voices=0"];
        [baseArguments addObjectsFromArray:@[@"-audiodev",audio]];
        // Keep this in the original PCI-device order so existing network
        // service associations are not disturbed by the sound implementation.
        // Big Sur ships AppleVirtIOSound (IOVirtIOPrimaryMatch 0x00191af4).
        if([self.machine.guest isEqual:@"bigsur"])
            [baseArguments addObjectsFromArray:@[@"-device",@"virtio-sound-pci,audiodev=audio0,x-apple-reconfigure=on"]];
        else [baseArguments addObjectsFromArray:@[@"-device",@"intel-hda,id=hda",@"-device",@"hda-duplex,bus=hda.0,audiodev=audio0"]];
    }
    [baseArguments addObjectsFromArray:@[
        @"-device",@"isa-applesmc,osk=ourhardworkbythesewordsguardedpleasedontsteal(c)AppleComputerInc",@"-smbios",@"type=2",
        @"-drive",[NSString stringWithFormat:@"if=pflash,format=raw,readonly=on,file=%@",path(@"Firmware")],
        @"-drive",[NSString stringWithFormat:@"if=pflash,format=raw,file=%@",path(@"NVRAM")],
        @"-drive",[NSString stringWithFormat:@"id=opencore,if=none,format=raw,%@,file=%@",
            @"snapshot=on",path(@"BootMedia")],
        @"-device",@"ide-hd,bus=ide.0,drive=opencore,bootindex=1,rotation_rate=1",
        @"-drive",[NSString stringWithFormat:@"id=macos,if=none,format=qcow2,file=%@",path(@"Disk")],
        @"-device",systemDiskDevice,
        // On q35 the auto-assigned slot 5 shares INTx with EHCI (IRQ 21).
        // Apple's 8254X filter can consume the masked initial LSC while
        // servicing that unrelated USB interrupt, leaving en0 inactive.
        // Reserve slot 6 / IRQ 22: no emulated-register or guest-driver patch,
        // link reset, or delayed boot-time reconnect is necessary.
        @"-device",[@"e1000-82545em,id=ethernet,bus=pcie.0,addr=0x6,netdev=net0,mac=" stringByAppendingString:mac],
        @"-qmp",[NSString stringWithFormat:@"unix:%@,server=on,wait=off",qmp],
        @"-serial",[NSString stringWithFormat:@"file:%@",[self.machine.URL.path stringByAppendingPathComponent:@"State/serial.log"]],
        @"-monitor",@"none",@"-action",@"reboot=shutdown"]];
    [baseArguments addObjectsFromArray:@[@"-netdev",network]];
    // Establish an unplugged cable before executing any guest instruction.
    // Keep the backend available so reconnecting never needs a guest reboot.
    if(config[@"NetworkConnected"]&&![config[@"NetworkConnected"] boolValue])[baseArguments addObject:@"-S"];
    NSMutableArray *mediaArguments=baseArguments;
    // Explicit development diagnosis only: capture boot-time PHY accesses
    // through QEMU's disabled-by-default tracing, not a second QMP client.
    if([NSProcessInfo.processInfo.arguments containsObject:@"--trace-network"])
        [mediaArguments addObjectsFromArray:@[@"-trace",@"enable=e1000_*",@"-trace",@"enable=e1000x_link_negotiation_*"]];
    if([NSProcessInfo.processInfo.arguments containsObject:@"--firmware-diagnostics"])
        [mediaArguments addObjectsFromArray:@[@"-debugcon",[@"file:" stringByAppendingString:[self.machine.URL.path stringByAppendingPathComponent:@"State/firmware.log"]],@"-global",@"isa-debugcon.iobase=0x402"]];
    // Independent read-only support media: never take over the user's CD/DVD
    // or the macOS installer. Insert only during setup and eject after copying.
    [mediaArguments addObjectsFromArray:@[@"-drive",@"id=setup-support,if=none,media=cdrom,readonly=on",@"-device",@"ide-cd,id=setup-support-device,bus=ide.4,drive=setup-support"]];
    NSString *optical=config[@"OpticalMedia"];
    NSError *opticalFailure=nil;
    NSString *opticalFormat=optical.length?VIMOpticalImageFormat(optical,&opticalFailure):nil;
    NSString *unavailableOptical=nil;
    if(optical.length&&!opticalFormat) {
        // Removable media can disappear when a library moves to another Mac.
        // Leave its saved selection intact, but boot with an empty drive. This
        // never applies to the system disk or required macOS installer media.
        if([opticalFailure.domain isEqual:NSPOSIXErrorDomain]&&(opticalFailure.code==ENOENT||opticalFailure.code==ENOTDIR)) {
            unavailableOptical=optical;optical=nil;
        } else {
            if(error)*error=VIMFriendlyDiscImageError(optical,opticalFailure);
            [self cleanUp];return NO;
        }
    }
    BOOL diskImage=[opticalFormat isEqual:@"dmg"];
    NSString *opticalDrive=optical.length&&!diskImage?[NSString stringWithFormat:@"id=cdrom,if=none,media=cdrom,format=raw,readonly=on,file=%@",VIMOptionPath(optical)]:@"id=cdrom,if=none,media=cdrom,readonly=on";
    [mediaArguments addObjectsFromArray:@[@"-drive",opticalDrive,@"-device",@"ide-cd,id=cdrom-device,bus=ide.3,drive=cdrom"]];
    // DMGs are disks, not optical sectors: preserve their 512-byte geometry
    // through a stock USB/SCSI removable device. Both backends are read-only;
    // the unused drive has no medium, not a converted or cached placeholder.
    NSString *imageDrive=diskImage?[NSString stringWithFormat:@"id=removable-image,if=none,media=disk,format=dmg,readonly=on,file=%@",VIMOptionPath(optical)]:@"id=removable-image,if=none,media=disk,readonly=on";
    [mediaArguments addObjectsFromArray:@[@"-drive",imageDrive,@"-device",@"usb-bot,id=image-usb,bus=usb.0",@"-device",@"scsi-hd,id=image-device,bus=image-usb.0,drive=removable-image,removable=on,logical_block_size=512,physical_block_size=512"]];
    self.insertedMediaPath=optical?:@"";self.insertedMediaFormat=opticalFormat;
    self.task.arguments=mediaArguments;
    if(installerDisplay || config[@"InstallerMedia"]) {
        NSMutableArray *arguments=[self.task.arguments mutableCopy];
        if(installerDisplay) {
            NSUInteger vga=[arguments indexOfObject:@"-vga"];
            arguments[vga+1]=@"std";
            [arguments addObjectsFromArray:@[@"-global",@"VGA.xres=1280",@"-global",@"VGA.yres=800"]];
            self.displayView.softwareCursor=YES;
            NSUInteger pvg=[arguments indexOfObject:@"apple-gfx-pci"];
            [arguments removeObjectsInRange:NSMakeRange(pvg-1,2)];
        }
        if(config[@"InstallerMedia"]) [arguments addObjectsFromArray:@[@"-drive",[NSString stringWithFormat:@"id=installer,if=none,format=raw,%@,file=%@",
            [config[@"InstallerMediaKind"] isEqual:@"disk"]?@"snapshot=on":@"readonly=on",path(@"InstallerMedia")],
            @"-device",[config[@"InstallerMediaKind"] isEqual:@"disk"]?
                @"ide-hd,bus=ide.2,unit=0,drive=installer,bootindex=3,rotation_rate=1":
                @"ide-cd,bus=ide.2,unit=0,drive=installer,bootindex=3"]];
        self.task.arguments=arguments;
    }
    NSMutableDictionary *environment=[NSProcessInfo.processInfo.environment mutableCopy];
    // Never inherit a developer's routing override into an unrelated VM.
    [environment removeObjectForKey:@"VIM_AUDIO_OUTPUT_DEVICE"];
    [environment removeObjectForKey:@"VIM_AUDIO_INPUT_DEVICE"];
    for(NSNumber *input in @[@NO,@YES]) {
        NSString *uid=VIMAudioSelection(config,input.boolValue);
        if([@[@"default",@"disabled"] containsObject:uid])continue;
        NSArray *devices=VIMHostAudioDevices(input.boolValue);NSDictionary *choice=nil;
        for(NSDictionary *device in devices)if([device[@"UID"] isEqual:uid])choice=device;
        NSUInteger matches=0;for(NSDictionary *device in devices)if([device[@"Name"] isEqual:choice[@"Name"]])matches++;
        if(!choice||matches!=1) {
            if(error)*error=VIMSessionError(@"The selected sound device is unavailable or has a duplicate name. Choose Default or another device in Sound settings.");
            [self cleanUp];return NO;
        }
        environment[input.boolValue?@"VIM_AUDIO_INPUT_DEVICE":@"VIM_AUDIO_OUTPUT_DEVICE"]=choice[@"Name"];
    }
    environment[@"VIM_INPUT_BACKPRESSURE"]=@"1";
    [environment removeObjectForKey:@"VIM_E1000_LINK_HISTORY"];
    if([NSProcessInfo.processInfo.arguments containsObject:@"--record-network-history"])
        environment[@"VIM_E1000_LINK_HISTORY"]=@"1";
    // Host tuning must not leak between development launches or architectures.
    for(NSString *key in environment.allKeys)
        if([key hasPrefix:@"PVG_"])[environment removeObjectForKey:key];
    [environment removeObjectForKey:@"DYLD_INSERT_LIBRARIES"];
    [environment removeObjectForKey:@"VIM_INSTALLER_DISPLAY_SOCKET"];
    [environment addEntriesFromDictionary:@{@"PVG_OOP_HELPER":helper,@"PVG_OOP_DONOR":donor,
        @"PVG_APP_DISPLAY_SOCKET":self.displayConnection.socketPath,@"PVG_TCG_TSO":@"0",
        @"PVG_TCG_LAZY_JMP_CACHE":@"1",@"PVG_TCG_KEEP_PHYS_TLB":@"1",@"PVG_TCG_INLINE_JMP_CACHE":@"1",
        @"PVG_TCG_NATIVE_FP_FLAGS":@"0",@"PVG_TCG_NATIVE_PACKED_FP":@"1",@"PVG_TCG_NATIVE_EXACT_FP":@"1",
        @"PVG_OOP_SCALE_FACTOR":[self.displayMode isEqual:@"retina"]?@"2":@"1",
        @"PVG_APP_DISPLAY_SCALE":[self.displayMode isEqual:@"retina"]?@"2":@"1",
        @"PVG_PRESERVE_IOSURFACE":@"1",@"PVG_SHADOW_COALESCE":@"1",
        @"PVG_SHADOW_MAX_CHUNK":@"1048576",@"PVG_SHADOW_TEXTURE_PAGING":@"1"}];
    // The framework cannot raw-blit packed multisample depth/stencil. The
    // ordered transfer adapter was qualified in the CLI harness, but omitted
    // when that harness became this app. Keep its quiet path enabled on Intel;
    // it preserves samples and the original command-buffer event ordering.
    if(!appleSilicon)environment[@"PVG_TEST_MSAA_TRANSFER"]=@"1";
    if(installerDisplay) {
        [environment removeObjectForKey:@"PVG_APP_DISPLAY_SOCKET"];
        environment[@"VIM_INSTALLER_DISPLAY_SOCKET"]=self.displayConnection.socketPath;
    }
    // These caches/fast paths are guarded by architectural MMU conditions,
    // not by guest macOS version. The AIR repair is genuinely Mojave-only.
    if(mojave)environment[@"PVG_HOST_AIR_LLVM"]=llvm;
    [environment addEntriesFromDictionary:@{@"PVG_TCG_ALTERNATE_LOOKUP":@"1",
        @"PVG_TCG_PCID_CONTEXTS":@"1",@"PVG_TCG_CONTEXT_JMP_CACHE":@"1",@"PVG_TCG_FAST_PTW":@"1",
        @"PVG_TCG_TLB_FLOOR_BITS":@"12",@"PVG_TCG_TLB_FLOOR_MASK":@"4"}];
    if(!appleSilicon)for(NSString *key in environment.allKeys)
        if([key hasPrefix:@"PVG_TCG_"]||[key hasPrefix:@"PVG_SHADOW_"]||[key hasPrefix:@"PVG_OOP_"]||[key isEqual:@"PVG_HOST_AIR_LLVM"]||[key isEqual:@"PVG_PRESERVE_IOSURFACE"])
            [environment removeObjectForKey:key];
    self.task.environment=environment;
    if(![self.machine prepareUpdaterDisplayForBoot:error]){[self cleanUp];return NO;}
    NSString *logPath=[self.machine.URL.path stringByAppendingPathComponent:@"State/session.log"];
    if(![NSFileManager.defaultManager fileExistsAtPath:logPath]) [NSFileManager.defaultManager createFileAtPath:logPath contents:nil attributes:nil];
    self.log=[NSFileHandle fileHandleForWritingAtPath:logPath];
    if(!self.log) { if(error)*error=VIMSessionError(@"Could not open the VM session log.");[self cleanUp];return NO; }
    [self.log seekToEndOfFile];self.task.standardOutput=self.log;self.task.standardError=self.log;
    [self.log writeData:[@"\n[Virtual Intel Mac] Memory ordering: normal runtime barriers\n" dataUsingEncoding:NSUTF8StringEncoding]];
    [self.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] %@ runtime: %@\n",
        root.length?@"Development override":@"Bundled",qemu] dataUsingEncoding:NSUTF8StringEncoding]];
    self.task.terminationHandler=^(NSTask *task){dispatch_async(dispatch_get_main_queue(), ^{
        VIMSession *session=weakSelf;if(!session||session.task!=task)return;
        [session.control drainLifecycleEventsAfterExitWithCompletion:^{
        session.status.stringValue=[NSString stringWithFormat:@"Stopped (exit %d)",task.terminationStatus];
        session.displayView.displaySleeping=YES;
        session.stateLabel.stringValue=task.terminationStatus==0?@"This Mac is shut down.":@"This Mac stopped unexpectedly. You can start it again or inspect Session Details.";
        session.statePanel.hidden=NO;session.stateAction.hidden=NO;
        BOOL forcedReset=session.forceResetRequested;
        BOOL guestReboot=(session.relaunchForGuestReboot||forcedReset)&&!session.shutdownRequested&&!session.closeAfterShutdown;
        NSMutableArray *recent=[NSMutableArray array];
        for(NSDate *date in session.recentGuestReboots)if(-date.timeIntervalSinceNow<60)[recent addObject:date];
        BOOL rebootLoop=guestReboot&&!forcedReset&&recent.count>=6;
        if(rebootLoop)guestReboot=NO;
        if(guestReboot)[recent addObject:NSDate.date];session.recentGuestReboots=recent;
        BOOL graphicsRestart=session.relaunchForGraphics&&!session.shutdownRequested&&!session.closeAfterShutdown;
        BOOL relaunch=(graphicsRestart||guestReboot)&&(forcedReset||(task.terminationReason==NSTaskTerminationReasonExit&&task.terminationStatus==0));
        session.forceResetRequested=NO;
        session.relaunchForGraphics=NO;session.relaunchForGuestReboot=NO;
        session.shutdownButton.enabled=NO;[session cleanUp];
        if(!relaunch&&task.terminationReason==NSTaskTerminationReasonExit&&task.terminationStatus==0)[session showStoppedScreen];
        if(rebootLoop)VIMPresentError(VIMSessionError(@"This Mac restarted repeatedly and was stopped. Open Session Details to check its boot log, then try starting it again."),session.window);
        if(session.closeAfterShutdown&&task.terminationReason==NSTaskTerminationReasonExit&&task.terminationStatus==0)[session.window close];
        [NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
        if(relaunch) {
            NSError *failure=nil;
            VIMMachine *fresh=[VIMMachine loadURL:session.machine.URL error:&failure];
            NSWindow *previousWindow=session.window;NSRect previousFrame=previousWindow.frame;
            if(fresh&&(guestReboot||!fresh.requiresInstallerDisplay)&&[VIMSession openMachine:fresh error:&failure]) {
                VIMSession *replacement=sessions[SessionKey(fresh)];
                if(guestReboot)replacement.recentGuestReboots=recent;
                [replacement.window setFrame:previousFrame display:YES];
                if(replacement.window!=previousWindow)[previousWindow close];
            } else {
                session.guestStatus.stringValue=failure.localizedDescription?:@"Restart paused: graphics installation needs recovery.";
            }
        }
        }];
    });};
    if(![self.task launchAndReturnError:error]) { [self cleanUp];return NO; }
    if([config[@"FileSharingEnabled"] boolValue]&&[VIMNetworkMode(config) isEqual:@"shared"]) {
        self.smbService=[[VIMSMBService alloc] initWithMachine:self.machine port:[config[@"FileSharingPort"] unsignedShortValue]];
        NSError *sharingError=nil;
        if(![self.smbService start:&sharingError])[self.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] %@\n",sharingError.localizedDescription] dataUsingEncoding:NSUTF8StringEncoding]];
    }
    [self updateInstallationActivity];
    [self showWindow:nil];[self.window makeKeyAndOrderFront:nil];
    if(unavailableOptical.length) {
        NSAlert *alert=[NSAlert new];
        alert.messageText=[NSString stringWithFormat:@"“%@” isn’t available",unavailableOptical.lastPathComponent];
        alert.informativeText=@"Your virtual Mac has started without this disc image. To reconnect it, choose Devices → CD/DVD → Connect Image and select its current location.";
        [alert addButtonWithTitle:@"OK"];VIMPresentAlert(alert,self.window,nil);
    }
    self.resizeArmed=!installerDisplay;
    [self connectControlWithCompletion:^(NSError *failure){
        VIMSession *session=weakSelf;if(!session.task.running)return;
        session.status.stringValue=failure?failure.localizedDescription:@"Running";
        session.shutdownButton.enabled=failure==nil;
        if(!failure){[session.window.contentView layoutSubtreeIfNeeded];[session scheduleResize];}
        if(!failure)session.displayView.inputHandler=^(NSArray *events){
            if([weakSelf.lastRunState isEqual:@"suspended"]) {
                if(weakSelf.wakePending)return;
                weakSelf.wakePending=YES;
                [weakSelf.control execute:@"system_wakeup" arguments:@{} completion:^(id result,NSError *error){
                    weakSelf.wakePending=NO;
                    if(!error) {
                        weakSelf.lastRunState=nil;
                        [weakSelf.displayView releasePressedKeys];
                        [weakSelf refreshRunState];
                    } else weakSelf.status.stringValue=error.localizedDescription;
                }];
                return;
            }
            [weakSelf.control execute:@"input-send-event" arguments:@{@"events":events} completion:^(id result,NSError *error){
                if(error && weakSelf.task.running)weakSelf.status.stringValue=error.localizedDescription;
            }];
        };
        if(!failure) {
            if(config[@"NetworkConnected"]&&![config[@"NetworkConnected"] boolValue])
                [session.control execute:@"set_link" arguments:@{@"name":@"ethernet",@"up":@NO} completion:^(id result,NSError *error){
                    if(error){VIMPresentError(error,session.window);return;}
                    [session.control execute:@"cont" arguments:@{} completion:^(id result,NSError *failure){if(failure)VIMPresentError(failure,session.window);}];
                }];
            // A newly opened guest accepts typing immediately, without requiring
            // a mouse click on its framebuffer first. Don't steal focus if the
            // user has already switched to another window during startup.
            if(session.window.isKeyWindow)[session.window makeFirstResponder:session.displayView];
            [session refreshRunState];
            [session refreshGuestStatus];
            session.statusTimer=[NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer *timer){
                [weakSelf refreshRunState];
                [weakSelf serviceDiagnosticRequest];
            }];
            NSArray *args=NSProcessInfo.processInfo.arguments;
            NSUInteger debug=[args indexOfObject:@"--guest-diagnostics"];
            NSUInteger target=NSNotFound;
            for(NSString *operation in @[@"--open-vm",@"--repair-vm",@"--install-vm",@"--prepare-vm"])
                if(target==NSNotFound)target=[args indexOfObject:operation];
            BOOL selected=target!=NSNotFound&&target+1<args.count&&
                [[args[target+1] stringByResolvingSymlinksInPath] isEqual:session.machine.URL.URLByResolvingSymlinksInPath.path];
            if([args containsObject:@"--guest-diagnostics-root"])
                session.diagnosticDirectory=VIMMultiMachineDiagnosticDirectory(args,session.machine);
            else if(selected&&debug!=NSNotFound&&debug+1<args.count&&[args[debug+1] isAbsolutePath])
                session.diagnosticDirectory=args[debug+1];
        }
    }];
    return YES;
}
- (void)connectControlWithCompletion:(void (^)(NSError *))completion {
    if(!self.task.running)return;
    __weak VIMSession *weakSelf=self;
    [self.control connectWithCompletion:^(NSError *error){
        VIMSession *session=weakSelf;if(!session.task.running)return;
        if(!error){completion(nil);return;}
        // Cold Rosetta startup / busy TCG can outlast the initial greeting.
        // Retry the same authenticated app-owned socket, not guest services.
        // Otherwise a visible installer can permanently have no input handler.
        session.status.stringValue=error.localizedDescription;
        [session.controlRetryTimer invalidate];
        session.controlRetryTimer=[NSTimer scheduledTimerWithTimeInterval:2 repeats:NO block:^(NSTimer *timer){
            [weakSelf connectControlWithCompletion:completion];
        }];
    }];
}
- (void)serviceDiagnosticRequest {
    // Explicit development opt-in. Reuse the app-owned serial connection;
    // a second QGA client would steal replies and interrupt guest setup.
    if(!self.diagnosticDirectory||self.diagnosticPending)return;
    NSString *request=[self.diagnosticDirectory stringByAppendingPathComponent:@"Request.json"];
    struct stat st;
    if(lstat(request.fileSystemRepresentation,&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||st.st_size>65536)return;
    NSData *data=[NSData dataWithContentsOfFile:request];
    NSDictionary *command=data?[NSJSONSerialization JSONObjectWithData:data options:0 error:nil]:nil;
    if(![command isKindOfClass:NSDictionary.class])return;
    // Framebuffer and read-only emulator reports use no guest-agent connection
    // and remain available while boot/setup owns that connection.
    BOOL independent=[@[@"capture",@"inspect-hardware",@"cursor-state",@"network-details",@"configuration",@"display-state",@"fullscreen"] containsObject:command[@"Action"]?:@""];
    if(!independent&&(self.guestPending||self.preparing||self.bootPolicyPending||self.clipboardPending))return;
    NSString *consumed=[self.diagnosticDirectory stringByAppendingPathComponent:@"Running.json"];
    if(rename(request.fileSystemRepresentation,consumed.fileSystemRepresentation))return;
    self.diagnosticPending=YES;
    NSString *output=[self.diagnosticDirectory stringByAppendingPathComponent:@"Result.json"];
    void (^finish)(NSDictionary *)=^(NSDictionary *result){
        [[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil] writeToFile:output atomically:YES];
        unlink(consumed.fileSystemRepresentation);self.diagnosticPending=NO;
        // Resume provisioning deferred while this command owned the agent.
        // In particular, an explicit clipboard-on request must not wait for
        // the next minute-long status refresh after releasing that ownership.
        [self updateClipboardForBoot:self.usernameBoot];
    };
    if([command[@"Action"] isEqual:@"capture"]){
        [self.displayView capturePNG:^(NSData *png){
            BOOL saved=[png writeToFile:[self.diagnosticDirectory stringByAppendingPathComponent:@"Frame.png"] atomically:YES];
            finish(@{@"Captured":@(saved)});
        }];return;
    }
    if([command[@"Action"] isEqual:@"cursor-state"]){finish([self.displayView cursorDiagnostics]);return;}
    if([command[@"Action"] isEqual:@"fullscreen"]) {
        if(![command[@"Enabled"] isKindOfClass:NSNumber.class]){finish(@{@"Error":@"Choose fullscreen on or off."});return;}
        BOOL desired=[command[@"Enabled"] boolValue];
        if(((self.window.styleMask&NSWindowStyleMaskFullScreen)!=0)==desired) {
            finish(@{@"Changed":@NO,@"FullScreen":@(desired),@"Frame":self.displayView.frameDiagnostics?:@{}});return;
        }
        __block BOOL ended=NO;__block id observer=nil;
        __weak VIMSession *weakSelf=self;
        void (^complete)(BOOL)=^(BOOL notified){
            if(ended)return;ended=YES;
            if(observer){[NSNotificationCenter.defaultCenter removeObserver:observer];observer=nil;}
            VIMSession *session=weakSelf;
            BOOL actual=(session.window.styleMask&NSWindowStyleMaskFullScreen)!=0;
            if(!notified||actual!=desired){finish(@{@"Error":@"The fullscreen window transition did not finish.",@"FullScreen":@(actual)});return;}
            finish(@{@"Changed":@YES,@"FullScreen":@(actual),@"Frame":session.displayView.frameDiagnostics?:@{}});
        };
        observer=[NSNotificationCenter.defaultCenter addObserverForName:desired?NSWindowDidEnterFullScreenNotification:NSWindowDidExitFullScreenNotification
            object:self.window queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note){
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW,200*NSEC_PER_MSEC),dispatch_get_main_queue(),^{complete(YES);});
            }];
        [NSApp activateIgnoringOtherApps:YES];[self.window makeKeyAndOrderFront:nil];
        // The real View menu targets this exact AppKit action. No alternate
        // emulator/display path, synthetic host input or window geometry hack.
        [self.window toggleFullScreen:nil];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,8*NSEC_PER_SEC),dispatch_get_main_queue(),^{complete(NO);});return;
    }
    if([command[@"Action"] isEqual:@"display-state"]) {
        NSMenu *view=[NSApp.mainMenu itemWithTitle:@"View"].submenu;[view update];
        NSMenu *menu=[view itemWithTitle:@"Retina Resolution"].submenu;[menu update];
        NSMutableArray *items=[NSMutableArray new];
        for(NSMenuItem *item in menu.itemArray)if(item.target==self&&item.action==@selector(changeRetinaMode:))
            [items addObject:@{@"Title":item.title,@"Selected":@(item.state==NSControlStateValueOn),@"Enabled":@(item.enabled)}];
        finish(@{@"Mode":self.displayMode?:@"",@"Pending":@(self.displayModePending),@"FullScreen":@((self.window.styleMask&NSWindowStyleMaskFullScreen)!=0),@"Frame":self.displayView.frameDiagnostics?:@{},
            @"PowerObserver":@{@"Connected":@(self.powerBridge.connected),@"Transport":self.powerBridge.diagnosticState?:@{}},
            @"LastVerifiedSelection":self.observedDisplayMode?:@{},@"MenuItems":items});return;
    }
    if([command[@"Action"] isEqual:@"display-mode"]) {
        if(!self.task.running){finish(@{@"Error":@"Start the virtual Mac before qualifying its display."});return;}
        [self applyDisplayMode:command[@"Mode"] completion:^(NSDictionary *observed,NSError *error){
            finish(error?@{@"Error":error.localizedDescription}:@{@"Applied":@YES,@"Mode":self.displayMode,@"Observed":observed?:@{}});
        }];return;
    }
    if([command[@"Action"] isEqual:@"network-details"]) {
        NSMutableDictionary *details=[[VIMSession networkDetailsForMachine:self.machine] mutableCopy];
        NSMutableArray *items=[NSMutableArray new];for(NSMenuItem *item in [self deviceMenuForKind:@"Network"].itemArray)if(!item.separatorItem)[items addObject:item.title];
        details[@"MenuItems"]=items;finish(details);return;
    }
    if([command[@"Action"] isEqual:@"configuration"]) {
        NSString *pane=command[@"Pane"];
        if(![@[@"Network",@"Sound",@"File Sharing",@"Remote Login"] containsObject:pane]){finish(@{@"Error":@"Choose a supported configuration pane."});return;}
        [VIMMachineSettings showForMachine:self.machine section:[@[@"File Sharing",@"Remote Login"] containsObject:pane]?@"Sharing":@"Hardware" category:pane window:self.window];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,200*NSEC_PER_MSEC),dispatch_get_main_queue(),^{
            NSError *error=nil;BOOL saved=[VIMMachineSettings captureForMachine:self.machine toURL:[NSURL fileURLWithPath:[self.diagnosticDirectory stringByAppendingPathComponent:@"Configuration.png"]] error:&error];
            finish(error?@{@"Error":error.localizedDescription}:@{@"Captured":@(saved)});
        });return;
    }
    if([command[@"Action"] isEqual:@"usb-devices"]) {finish(@{@"Devices":VIMHostUSBDevices(),@"Attached":self.usbAttached?:@{}});return;}
    if([command[@"Action"] isEqual:@"menu-state"]) {
        if([command[@"Context"] isEqual:@"library"])[(id)NSApp.delegate showLibrary:nil];
        else if([command[@"Context"] isEqual:@"about"]) {
            [(id)NSApp.delegate showAbout:nil];
            if([NSApp.keyWindow isKindOfClass:NSPanel.class])self.diagnosticAboutWindow=NSApp.keyWindow;
        } else if([command[@"Context"] isEqual:@"guest"]) {
            [self.diagnosticAboutWindow performClose:nil];self.diagnosticAboutWindow=nil;
            [NSApp activateIgnoringOtherApps:YES];
            [self.window makeKeyAndOrderFront:nil];
        }
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,300*NSEC_PER_MSEC),dispatch_get_main_queue(),^{
            NSMutableDictionary *state=[NSMutableDictionary new];
            for(NSString *name in @[@"File",@"Actions",@"Devices"]) {
                NSMenuItem *root=[NSApp.mainMenu itemWithTitle:name];[root.submenu update];
                NSMutableArray *items=[NSMutableArray new];
                for(NSMenuItem *item in root.submenu.itemArray)if(!item.separatorItem)[items addObject:@{@"Title":item.title,@"Enabled":@(item.enabled)}];
                state[name]=@{@"Present":@(root!=nil),@"Hidden":@(root.hidden),@"Items":items};
            }
            finish(state);
        });return;
    }
    if([command[@"Action"] isEqual:@"usb-storage-access"]) {
        NSDictionary *selected=nil;
        for(NSDictionary *device in VIMHostUSBDevices())if([device[@"LocationID"] isEqual:command[@"LocationID"]])selected=device;
        if(!selected){finish(@{@"Error":@"The requested physical USB device is not connected."});return;}
        NSMutableArray *protected=[NSMutableArray new];for(VIMSession *session in sessions.allValues)if(session.machine.URL)[protected addObject:session.machine.URL];
        VIMHostRequestUSBStorageAccess(selected,protected,^(NSError *error){finish(error?@{@"Error":error.localizedDescription}:@{@"PreflightCompleted":@YES,@"CaptureVerified":@NO});});return;
    }
    if([command[@"Action"] isEqual:@"usb-interface-policy"]) {
        NSDictionary *selected=nil;
        for(NSDictionary *device in VIMHostUSBDevices())if([device[@"LocationID"] isEqual:command[@"LocationID"]])selected=device;
        if(!selected){finish(@{@"Error":@"The requested physical USB device is not connected."});return;}
        if(!self.usbClient)self.usbClient=[VIMUSBClient new];
        [self.usbClient checkStorageInterface:selected inUserSession:[command[@"UserSession"] isEqual:@YES] completion:^(NSDictionary *result,NSError *error){
            finish(error?@{@"Error":error.localizedDescription}:result);
        }];return;
    }
    if([command[@"Action"] isEqual:@"usb-connect"]||[command[@"Action"] isEqual:@"usb-disconnect"]) {
        if(!self.task.running||self.usbOperationPending){finish(@{@"Error":@"USB is not ready for another operation."});return;}
        NSDictionary *selected=nil;
        for(NSDictionary *device in VIMHostUSBDevices())if([device[@"LocationID"] isEqual:command[@"LocationID"]])selected=device;
        if(!selected){finish(@{@"Error":@"The requested physical USB device is not connected."});return;}
        if(!self.usbClient)self.usbClient=[VIMUSBClient new];
        NSString *identifier=selected[@"Identifier"];self.usbOperationPending=YES;
        void (^done)(NSError *)=^(NSError *error){self.usbOperationPending=NO;if(!error){
            if([command[@"Action"] isEqual:@"usb-disconnect"])[self.usbAttached removeObjectForKey:identifier];
            else {if(!self.usbAttached)self.usbAttached=[NSMutableDictionary new];self.usbAttached[identifier]=selected[@"Name"];}
        }finish(error?@{@"Error":error.localizedDescription}:@{@"Attached":self.usbAttached?:@{}});};
        if([command[@"Action"] isEqual:@"usb-disconnect"]){[self.usbClient disconnectDevice:identifier control:self.control completion:done];return;}
        NSMutableArray *protected=[NSMutableArray arrayWithObjects:NSBundle.mainBundle.bundleURL,VIMLibraryLocation.rootURL,nil];
        for(VIMSession *session in sessions.allValues)if(session.machine.URL)[protected addObject:session.machine.URL];
        NSError *error=nil;NSDictionary *storage=VIMHostUSBStorageInfo(selected,protected,&error);
        if(!storage||[storage[@"Protected"] boolValue]){done(error?:VIMSessionError(@"The selected USB device contains the app or a virtual Mac library."));return;}
        void (^connect)(void)=^{[self.usbClient prepareWithCompletion:^(NSError *failure){if(failure){done(failure);return;}
            [self.usbClient connectDevice:selected processID:self.task.processIdentifier directory:self.runtimeDirectory control:self.control completion:done];
        }];};
        BOOL mounted=[storage[@"MountedVolumes"] count]>0;
        if(mounted&&![command[@"Unmount"] isEqual:@YES]){done(VIMSessionError(@"Explicit normal-unmount confirmation is required."));return;}
        VIMHostRequestUSBStorageAccess(selected,protected,^(NSError *failure){
            if(failure){done(failure);return;}
            if(!self.task.running){done(VIMSessionError(@"This virtual Mac has stopped."));return;}
            // Developer harness can confirm only normal unmount, never force.
            if(mounted)VIMHostReleaseUSBDisks(selected,protected,NO,^(NSError *error){if(error)done(error);else connect();});
            else connect();
        });return;
    }
    if([command[@"Action"] isEqual:@"insert-disc"]) {
        NSString *path=command[@"Path"];
        if(![path isKindOfClass:NSString.class]||(path.length&&!path.isAbsolutePath)){finish(@{@"Error":@"Choose an absolute disc-image path, or an empty path to eject."});return;}
        [VIMSession insertOpticalMedia:path machine:self.machine completion:^(NSError *error){finish(error?@{@"Error":error.localizedDescription}:@{@"Inserted":@(path.length>0)});}];return;
    }
    if([command[@"Action"] isEqual:@"shutdown-vm"]) {
        if(self.preparing||self.preparationRequested){finish(@{@"Error":@"Wait for guest setup to finish before shutting down."});return;}
        [self shutdown:self];finish(@{@"Requested":@(self.shutdownRequested)});return;
    }
    if([command[@"Action"] isEqual:@"status-probe"]) {
        // Time the exact read-only production status path without creating a
        // second guest-agent connection or triggering installation/repair.
        double started=NSProcessInfo.processInfo.systemUptime;
        [self.guestAgent connectWithCompletion:^(NSError *error){
            if(error){finish(@{@"Error":error.localizedDescription});return;}
            [self.guestAgent collectStatusWithTool:self.guestStatusTool completion:^(NSDictionary *report,NSError *failure){
                finish(failure?@{@"Error":failure.localizedDescription}:@{@"Report":report,
                    @"Seconds":@(NSProcessInfo.processInfo.systemUptime-started)});
            }];
        }];return;
    }
    if([command[@"Action"] isEqual:@"installer-log"]) {
        // Recovery may lack the privileged installed-system status probe.
        // Read its existing installer log through the same serialized owner;
        // no staging, shell, second agent client, or arbitrary command API.
        [self.guestAgent connectWithCompletion:^(NSError *error){
            if(error){finish(@{@"Error":error.localizedDescription});return;}
            [self.guestAgent runExecutable:@"/bin/cat" arguments:@[@"/var/log/install.log"] timeout:30 completion:^(NSDictionary *result,NSError *failure){
                if(failure){finish(@{@"Error":failure.localizedDescription});return;}
                NSString *text=[[NSString alloc] initWithData:result[@"stdout"]?:NSData.data encoding:NSUTF8StringEncoding];
                NSString *detail=[[NSString alloc] initWithData:result[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding];
                finish(@{@"Log":text?:@"",@"Stderr":detail?:@"",@"ExitCode":result[@"exitcode"]?:@-1});
            }];
        }];return;
    }
    if([command[@"Action"] isEqual:@"open-guest-preferences"]) {
        [self.guestAgent connectWithCompletion:^(NSError *error){
            if(error){finish(@{@"Error":error.localizedDescription});return;}
            [self.guestAgent openPreferencesPane:command[@"Pane"] completion:^(NSError *failure){
                finish(failure?@{@"Error":failure.localizedDescription}:@{@"Opened":@YES});
            }];
        }];return;
    }
    if([command[@"Action"] isEqual:@"trace-usb"]) {
        // Metadata-only, bounded, on the existing production QMP owner.
        // This never turns on usbredir's payload/hex-dump logging.
        BOOL enabled=[command[@"Enabled"] boolValue];
        NSArray *events=@[@"usb_xhci_xfer_*",@"usb_xhci_ep_*",@"usb_xhci_irq_*"];
        __block NSError *failure=nil;
        for(NSString *event in events)[self.control execute:@"trace-event-set-state" arguments:@{@"name":event,@"enable":@(enabled)} completion:^(id result,NSError *error){
            if(error&&!failure)failure=error;
            if([event isEqual:events.lastObject]) {
                if(failure){finish(@{@"Error":failure.localizedDescription});return;}
                // QMP dynamic state alone does not enable the log backend's
                // LOG_TRACE mask. Fixed HMP command; no client-supplied text.
                NSString *log=enabled?@"log trace:usb_xhci_xfer_*,trace:usb_xhci_ep_*,trace:usb_xhci_irq_*":@"log none";
                [self.control execute:@"human-monitor-command" arguments:@{@"command-line":log} completion:^(id output,NSError *error){
                    finish(error?@{@"Error":error.localizedDescription}:@{@"Enabled":@(enabled),@"AutomaticStopSeconds":@15});
                }];
            }
        }];
        if(enabled)dispatch_after(dispatch_time(DISPATCH_TIME_NOW,15*NSEC_PER_SEC),dispatch_get_main_queue(),^{
            [self.control execute:@"human-monitor-command" arguments:@{@"command-line":@"log none"} completion:^(id result,NSError *error){}];
            for(NSString *event in events)[self.control execute:@"trace-event-set-state" arguments:@{@"name":event,@"enable":@NO} completion:^(id result,NSError *error){}];
        });return;
    }
    if([command[@"Action"] isEqual:@"trace-sound"]||[command[@"Action"] isEqual:@"trace-sound-mix"]||[command[@"Action"] isEqual:@"trace-audio-gaps"]) {
        // Development diagnostics use the existing serialized control channel.
        // Exclude per-buffer logging: tracing must not itself stall the audio.
        BOOL enabled=[command[@"Enabled"] boolValue];
        if([command[@"Action"] isEqual:@"trace-audio-gaps"]) {
            // Exceptional gaps plus one continuity summary per second. FIFO
            // clock adjustment can run every backend callback: leave that
            // trace off, especially with the session log on external storage.
            NSArray *events=@[@"audio_sdl_underrun",@"audio_timer_delayed",@"hda_audio_overrun",@"audio_sdl_continuity"];
            NSUInteger epoch=++self.audioTraceEpoch;
            __block NSError *failure=nil;
            for(NSString *event in events)[self.control execute:@"trace-event-set-state" arguments:@{@"name":event,@"enable":@(enabled)} completion:^(id result,NSError *error){
                if(error&&!failure)failure=error;
                if([event isEqual:events.lastObject])finish(failure?@{@"Error":failure.localizedDescription}:@{@"Enabled":@(enabled),@"AutomaticStopSeconds":@15});
            }];
            if(enabled)dispatch_after(dispatch_time(DISPATCH_TIME_NOW,15*NSEC_PER_SEC),dispatch_get_main_queue(),^{
                if(self.audioTraceEpoch!=epoch||!self.task.running)return;
                for(NSString *event in events)[self.control execute:@"trace-event-set-state" arguments:@{@"name":event,@"enable":@NO} completion:^(id result,NSError *error){}];
            });return;
        }
        [self.control execute:@"trace-event-set-state"
            arguments:@{@"name":[command[@"Action"] isEqual:@"trace-sound-mix"]?@"audio_sw_write":@"virtio_snd_handle_code",@"enable":@(enabled)}
            completion:^(id result,NSError *error){
                finish(error?@{@"Error":error.localizedDescription}:@{@"Enabled":@(enabled)});
            }];return;
    }
    if([command[@"Action"] isEqual:@"network-link"]) {
        // Explicit diagnostics only. Isolate device link negotiation without
        // rewriting the guest's DHCP, DNS or user network preferences.
        BOOL enabled=[command[@"Enabled"] isEqual:@YES];
        [VIMSession setNetworkConnected:enabled machine:self.machine completion:^(NSError *error){finish(error?@{@"Error":error.localizedDescription}:@{@"Enabled":@(enabled)});}];return;
    }
    if([command[@"Action"] isEqual:@"inspect-hardware"]) {
        // Whitelisted read-only QMP reports through the production owner.
        // Available before the guest agent/OS boots; never opens another socket.
        if([@[@"phy",@"link-history"] containsObject:command[@"Component"]?:@""]) {
            [self.control execute:@"qom-get" arguments:@{@"path":@"/machine/peripheral/ethernet",@"property":[command[@"Component"] isEqual:@"phy"]?@"x-phy-state":@"x-link-history"}
                completion:^(id result,NSError *error){finish(error?@{@"Error":error.localizedDescription}:@{@"Report":result?:@""});}];return;
        }
        if([command[@"Component"] isEqual:@"registers"]) {
            [self.control execute:@"human-monitor-command" arguments:@{@"command-line":@"info registers -a"} completion:^(id result,NSError *error){
                finish(error?@{@"Error":error.localizedDescription}:@{@"Report":result?:@""});
            }];return;
        }
        NSString *report=@{@"cpu":@"query-cpus-fast",@"pci":@"query-pci",@"network":@"x-query-network"}[command[@"Component"]?:@""];
        if(!report){finish(@{@"Error":@"Choose cpu, pci, network, phy, or link-history."});return;}
        [self.control execute:report arguments:@{}
            completion:^(id result,NSError *error){finish(error?@{@"Error":error.localizedDescription}:@{@"Report":result?:@""});}];return;
    }
    if([command[@"Action"] isEqual:@"input"]) {
        NSArray *events=command[@"Events"];
        if(!self.task.running||!self.displayView.inputHandler||![events isKindOfClass:NSArray.class]||!events.count||events.count>64) {
            finish(@{@"Error":@"The guest is not ready for this input batch."});return;
        }
        for(id event in events)if(![event isKindOfClass:NSDictionary.class]||
            ![@[@"abs",@"btn",@"key"] containsObject:event[@"type"]]||![event[@"data"] isKindOfClass:NSDictionary.class]) {
            finish(@{@"Error":@"Invalid guest input event."});return;
        }
        self.displayView.inputHandler(events);finish(@{@"Queued":@YES});return;
    }
    if([command[@"Action"] isEqual:@"clipboard"]) {
        NSNumber *enabled=command[@"Enabled"];NSError *error=nil;
        if(enabled&&![self.machine saveSettings:@{@"ClipboardEnabled":enabled} error:&error]){finish(@{@"Error":error.localizedDescription});return;}
        if(enabled)[self updateClipboardForBoot:self.usernameBoot];
        finish(@{@"Enabled":@(!self.machine.editableConfiguration[@"ClipboardEnabled"]||[self.machine.editableConfiguration[@"ClipboardEnabled"] isEqual:@YES]),@"Connected":@(self.clipboard.isConnected),@"ExchangeAllowed":@(self.clipboard.exchangeAllowed&&self.clipboard.exchangeAllowed()),@"ApplicationActive":@(NSApp.active),@"WindowKey":@(self.window.keyWindow),@"DisplaySleeping":@(self.displayView.displaySleeping),@"Provisioning":@(self.clipboardPending),@"Failure":self.clipboardFailure?:@"",@"Transport":self.clipboard.diagnosticState?:@{}});return;
    }
    if([command[@"Action"] isEqual:@"stage-probe"]) {
        NSURL *input=[NSURL fileURLWithPath:[self.diagnosticDirectory stringByAppendingPathComponent:@"Transfer.bin"]];
        double start=NSProcessInfo.processInfo.systemUptime;
        [self.guestAgent connectWithCompletion:^(NSError *error){
            if(error){finish(@{@"Error":error.localizedDescription});return;}
            // Exercise the exact production transfer and guest SHA256 check.
            // stageFiles confines output to a fresh root-owned staging dir.
            NSURL *hashTool=[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:@"Runtime/GuestTools/guest-macho"];
            [self.guestAgent stageFiles:@{@"Transfer.bin":input,@"guest-macho":hashTool} progress:nil completion:^(NSDictionary *receipt,NSError *failure){
                if(failure){finish(@{@"Error":failure.localizedDescription});return;}
                finish(@{@"Transfer":receipt,@"Seconds":@(NSProcessInfo.processInfo.systemUptime-start)});
            }];
        }];return;
    }
    NSString *path=command[@"Executable"];NSArray *arguments=command[@"Arguments"];
    if(![path isKindOfClass:NSString.class]||!path.isAbsolutePath||![arguments isKindOfClass:NSArray.class]){finish(@{@"Error":@"Invalid command"});return;}
    for(id argument in arguments)if(![argument isKindOfClass:NSString.class]){finish(@{@"Error":@"Invalid argument"});return;}
    [self.guestAgent connectWithCompletion:^(NSError *error){
        if(error){finish(@{@"Error":error.localizedDescription});return;}
        [self.guestAgent runExecutable:path arguments:arguments timeout:60 completion:^(NSDictionary *result,NSError *failure){
            if(failure){finish(@{@"Error":failure.localizedDescription});return;}
            finish(@{@"ExitCode":result[@"exitcode"]?:@(-1),
                @"Stdout":[[NSString alloc] initWithData:result[@"stdout"]?:NSData.data encoding:NSUTF8StringEncoding]?:@"",
                @"Stderr":[[NSString alloc] initWithData:result[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding]?:@""});
        }];
    }];
}
- (void)refreshGuestStatus {
    if(self.guestPending||self.preparing||self.shutdownRequested||!self.task.running)return;
    if(self.diagnosticPending||self.clipboardPending||self.bootPolicyPending) {
        // A one-shot status timer may fire during a diagnostic command. Keep
        // the normal production readiness/verification loop alive afterward.
        [self.guestTimer invalidate];
        __weak VIMSession *weakSelf=self;
        self.guestTimer=[NSTimer scheduledTimerWithTimeInterval:15 repeats:NO block:^(NSTimer *timer){[weakSelf refreshGuestStatus];}];
        return;
    }
    if(!self.guestStatusTool.length) {
        self.guestStatus.stringValue=@"Guest setup tool is missing";return;
    }
    self.guestPending=YES;
    __weak VIMSession *weakSelf=self;
    [self.guestAgent connectWithCompletion:^(NSError *error) {
        VIMSession *session=weakSelf;if(!session.task.running)return;
        if(error){[session finishGuestStatus:nil error:error];return;}
        [session.guestAgent collectStatusWithTool:session.guestStatusTool completion:^(NSDictionary *report,NSError *failure) {
            [weakSelf finishGuestStatus:report error:failure];
        }];
    }];
}
- (void)finishGuestStatus:(NSDictionary *)report error:(NSError *)error {
    self.guestPending=NO;if(!self.task.running)return;
    NSArray *previousAddresses=self.guestIPAddresses;
    if(!error&&[report[@"ConsoleUID"] isKindOfClass:NSNumber.class]) {
        NSNumber *uid=[report[@"ConsoleUID"] unsignedIntValue]>=501?report[@"ConsoleUID"]:nil;
        if(uid!=self.clipboardUID&&![uid isEqual:self.clipboardUID]) {
            [self.clipboard stop];self.clipboard=nil;self.clipboardConfiguredBoot=nil;
            self.clipboardUID=uid;self.clipboardRetryAfter=nil;
            self.usernameBoot=nil;
            // Preserve an explicitly entered Remote Login account while no
            // console user is known; a fresh valid observation replaces it.
        }
    }
    if(!error&&[report[@"NetworkInterfaces"] isKindOfClass:NSArray.class]) {
        NSMutableArray *addresses=[NSMutableArray new];NSString *mac=VIMNetworkMACAddress(self.machine.configuration);
        for(id interface in report[@"NetworkInterfaces"])if([interface isKindOfClass:NSDictionary.class]&&[interface[@"MACAddress"] isKindOfClass:NSString.class]&&[interface[@"MACAddress"] caseInsensitiveCompare:mac]==NSOrderedSame) {
            for(id address in [interface[@"Addresses"] isKindOfClass:NSArray.class]?interface[@"Addresses"]:@[])
                if(VIMNetworkUsableIPv4(address)&&![addresses containsObject:address])[addresses addObject:address];
        }self.guestIPAddresses=addresses;
    } else if(!error&&[report[@"Network"] isKindOfClass:NSDictionary.class]) {
        NSMutableArray *addresses=[NSMutableArray new];
        for(id value in [report[@"Network"] allValues])if([value isKindOfClass:NSDictionary.class]) {
            id list=value[@"Addresses"];
            if([list isKindOfClass:NSArray.class])for(id address in list)
                if(VIMNetworkUsableIPv4(address)&&![addresses containsObject:address])[addresses addObject:address];
        }
        self.guestIPAddresses=addresses;
    }
    if(error)self.guestIPAddresses=nil;
    if(![previousAddresses?:@[] isEqual:self.guestIPAddresses?:@[]])
        [NSNotificationCenter.defaultCenter postNotificationName:@"VIMNetworkChanged" object:self.machine];
    if(self.completionGuestReport&&(error||
       ![report[@"BootSessionUUID"] isEqual:self.completionGuestReport[@"BootSessionUUID"]]||
       ![report[@"ProductBuildVersion"] isEqual:self.completionGuestReport[@"ProductBuildVersion"]]||
       ![report[@"VolumeUUID"] isEqual:self.completionGuestReport[@"VolumeUUID"]]||
       ![report[@"InstalledSystemCandidate"] isEqual:@YES]))self.completionGuestReport=nil;
    NSString *message=nil;
    if(error) {
        // A stock guest has no agent until its VirtIO driver/launchd are ready.
        // Keep the framebuffer/input live and retry without rebooting the VM.
        message=error.localizedDescription;
        self.guestStatus.stringValue=@"Waiting for macOS guest services…";
    } else if(![report[@"Guest"] isEqual:self.machine.guest]) {
        message=@"The guest version does not match this Mac’s configuration.";
        self.guestStatus.stringValue=message;
    } else {
        NSString *version=[report[@"ProductVersion"] isKindOfClass:NSString.class]?report[@"ProductVersion"]:@"";
        BOOL installed=[report[@"InstalledSystemCandidate"] isEqual:@YES] &&
            [report[@"LiveRoot"] isEqual:@YES] && [report[@"KernelMatchesSystem"] isEqual:@YES];
        self.guestStatus.stringValue=[NSString stringWithFormat:@"macOS %@%@",version,installed?@"":@" — setup in progress"];
        // Evidence only. Never modify InstallerMedia/boot routing here: a live
        // desktop is not proof that the required patch transaction succeeded.
        NSDictionary *record=@{@"ObservedAt":NSDate.date,@"Session":self.runtimeDirectory.lastPathComponent,
            @"Transport":@"AppleVirtIO",@"PrivilegedAgentVerified":@YES,@"Report":report};
        NSError *saveError=nil;
        NSData *data=[NSPropertyListSerialization dataWithPropertyList:record format:NSPropertyListBinaryFormat_v1_0 options:0 error:&saveError];
        if(data)[data writeToURL:[self.machine.URL URLByAppendingPathComponent:@"State/GuestStatus.plist"] options:NSDataWritingAtomic error:&saveError];
        message=saveError?saveError.localizedDescription:self.guestStatus.stringValue;
    }
    self.guestStatus.toolTip=message;
    if(![message isEqual:self.lastGuestMessage]) {
        self.lastGuestMessage=message;
        [self.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] Guest status: %@\n",message] dataUsingEncoding:NSUTF8StringEncoding]];
    }
    [self.guestTimer invalidate];
    NSString *boot=[report[@"BootSessionUUID"] isKindOfClass:NSString.class]?report[@"BootSessionUUID"]:nil;
    BOOL priorBootPolicy=[VIMGuestBootPolicy readyAtURL:[self.machine.URL URLByAppendingPathComponent:@"State/BootPolicy.plist"]
        guest:self.machine.guest identifier:self.machine.configuration[@"Identifier"]];
    if(!error&&boot.length&&![boot isEqual:self.bootPolicyCheckedBoot]&&
       [report[@"InstalledSystemCandidate"] isEqual:@YES]&&([report[@"DesktopRunning"] isEqual:@YES]||priorBootPolicy)&&
       [report[@"LiveRoot"] isEqual:@YES]&&[report[@"KernelMatchesSystem"] isEqual:@YES]&&
       [report[@"Guest"] isEqual:self.machine.guest]&&
       ![NSProcessInfo.processInfo.arguments containsObject:@"--guest-efi"]) {
        // Before graphics preparation's normal restart, publish/verify the
        // one guest preference needed by identity-preserving boot firmware.
        // A previously verified, same-volume guest may be checked before the
        // desktop: the missing preference can itself prevent WindowServer.
        // Existing guests are not restarted behind the user's back.
        self.bootPolicyCheckedBoot=boot;self.bootPolicyPending=YES;
        self.guestStatus.stringValue=@"Preparing guest privacy compatibility…";
        NSString *runtime=self.runtimeDirectory;__weak VIMSession *weakSelf=self;
        [VIMGuestBootPolicy prepareWithAgent:self.guestAgent report:report identifier:self.machine.configuration[@"Identifier"]
            journal:[self.machine.URL URLByAppendingPathComponent:@"State/BootPolicy.plist"]
            tools:[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:@"Runtime/GuestTools"]
            completion:^(BOOL ready,NSError *failure){
                VIMSession *session=weakSelf;if(!session.task.running||![session.runtimeDirectory isEqual:runtime])return;
                session.bootPolicyPending=NO;
                NSString *message=ready?@"Guest privacy compatibility verified.":failure.localizedDescription;
                [session.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] Boot policy: %@\n",message?:@"Verification did not finish; legacy boot retained."] dataUsingEncoding:NSUTF8StringEncoding]];
                NSDictionary *policy=[NSDictionary dictionaryWithContentsOfURL:[session.machine.URL URLByAppendingPathComponent:@"State/BootPolicy.plist"]];
                if(ready&&![report[@"DesktopRunning"] isEqual:@YES]&&
                   [policy[@"RecoveryBootSessionUUID"] isEqual:boot]&&
                   [policy[@"Result"][@"Changed"] isEqual:@YES]) {
                    // amfid/WindowServer may already have cached the earlier
                    // policy. Restart only a known repaired, pre-desktop boot;
                    // use the normal shutdown handoff, never a hard reset.
                    [session handoffToGraphics];
                    session.guestStatus.stringValue=@"Guest compatibility restored — restarting this Mac…";
                    return;
                }
                // Collect a fresh boot/build identity after the operation,
                // never feed a stale report back into installation admission.
                [session refreshGuestStatus];
            }];
        return;
    }
    if(!error&&boot.length&&[report[@"DesktopRunning"] isEqual:@YES]&&![boot isEqual:self.displaySizedBoot]) {
        // Guest preferences can restore a saved 1× mode after the initial
        // connection. Reassert the current window's backing size once at login.
        self.displaySizedBoot=boot;self.lastRequestedSize=NSZeroSize;[self scheduleResize];
    }
    if(!error&&boot.length&&![boot isEqual:self.sleepDisabledBoot]) {
        // v1 deliberately does not support guest system sleep. The root-domain
        // SleepDisabled switch blocks idle AND demand sleep; sleep=0 alone
        // only disables the timer. Source: period XNU checkSystemSleepAllowed.
        // Keep display sleep independent and reapply after every guest boot.
        self.sleepDisabledBoot=boot;
        __weak VIMSession *weakSelf=self;
        [self.guestAgent runExecutable:@"/usr/bin/pmset" arguments:@[@"-a",@"sleep",@"0",@"disablesleep",@"1"] timeout:30 completion:^(NSDictionary *result,NSError *failure){
            if(failure||![result[@"exitcode"] isEqual:@0])weakSelf.sleepDisabledBoot=nil;
        }];
    }
    if(!error&&boot.length&&[report[@"SetupComplete"] isEqual:@YES])
        [self updateBridgeForBoot:boot displayPowerOnly:YES];
    if(!error&&boot.length&&self.clipboardUID&&[report[@"DesktopRunning"] isEqual:@YES]&&![boot isEqual:self.usernameBoot]) {
        // Remote Login needs the guest account's short name. Learn it from the
        // signed-in console user once per boot/session; invalidate it when
        // the console owner changes. Settings still asks before first login.
        self.usernameBoot=boot;
        NSNumber *expectedUID=self.clipboardUID;NSString *runtime=self.runtimeDirectory;
        __weak VIMSession *weakSelf=self;
        [self.guestAgent runExecutable:@"/usr/bin/stat" arguments:@[@"-f",@"%u %Su",@"/dev/console"] timeout:15 completion:^(NSDictionary *result,NSError *failure){
            VIMSession *session=weakSelf;
            if(!session.task.running||![session.runtimeDirectory isEqual:runtime]||
               ![session.usernameBoot isEqual:boot]||![session.clipboardUID isEqual:expectedUID])return;
            NSString *text=[[NSString alloc] initWithData:result[@"stdout"]?:NSData.data encoding:NSUTF8StringEncoding];
            NSArray *fields=[[text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet] componentsSeparatedByString:@" "];
            if(failure||![result[@"exitcode"] isEqual:@0]||fields.count!=2||[fields[0] integerValue]<501||
               [fields[0] longLongValue]!=expectedUID.unsignedIntValue||![fields[1] length]) {
                session.usernameBoot=nil;return;
            }
            [NSUserDefaults.standardUserDefaults setObject:fields[1] forKey:[@"VIMGuestUsername-" stringByAppendingString:session.machine.URL.path]];
            session.clipboardUID=@([fields[0] intValue]);[session updateClipboardForBoot:boot];
        }];
    }
    NSURL *installJournal=[self.machine.URL URLByAppendingPathComponent:@"State/GuestInstallation.plist"];
    if(self.explicitRepair&&self.repairRequested&&!error&&![report[@"DesktopRunning"] isEqual:@YES])
        self.verifiedUpdateRepair=[self.machine canRepairUpdatedSystemWithGuestReport:report error:nil];
    if(!error&&!self.repairRequested&&!self.preparationRequested) {
        NSError *claimError=nil;
        if([self.machine claimAutomaticUpdateRepairWithGuestReport:report error:&claimError]) {
            self.repairRequested=YES;self.verifiedUpdateRepair=YES;
            [self.log writeData:[@"\n[Virtual Intel Mac] Guest macOS build changed; beginning graphics repair from the updated system files.\n" dataUsingEncoding:NSUTF8StringEncoding]];
        } else if(claimError) {
            self.guestStatus.stringValue=claimError.localizedDescription;
            [self.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] Automatic update repair: %@\n",claimError.localizedDescription] dataUsingEncoding:NSUTF8StringEncoding]];
        }
    }
    if(self.repairRequested&&!error&&([report[@"DesktopRunning"] isEqual:@YES]||self.verifiedUpdateRepair)&&
       [report[@"InstalledSystemCandidate"] isEqual:@YES]&&[report[@"Guest"] isEqual:self.machine.guest]) {
        self.repairRequested=NO;self.preparing=YES;self.completionGuestReport=nil;
        void (^beginRepair)(NSError *)=^(NSError *failure){
            self.preparing=NO;
            if(failure){self.guestStatus.stringValue=failure.localizedDescription;VIMPresentError(failure,self.window);return;}
            if(!self.task.running)return;
            self.reconciledBoot=nil;self.preparationRequested=YES;self.installationRequested=YES;
            [self refreshGuestStatus];
        };
        // Imported development VMs may predate transactional installation.
        // A missing receipt is not a malformed receipt: create a new fully
        // backed-up transaction, without inventing a successful old outcome.
        // Existing records must still pass reconciliation before any repair.
        if([NSFileManager.defaultManager fileExistsAtPath:installJournal.path]) {
            self.repairArchivePending=YES;
            NSDictionary *updated=[self.machine canRepairUpdatedSystemWithGuestReport:report error:nil]?report:nil;
            [VIMGuestInstallation checkForRepairWithAgent:self.guestAgent journal:installJournal updatedSystem:updated completion:beginRepair];
        }
        else beginRepair(nil);
        return;
    }
    if(!error&&!self.preparationRequested) {
        NSError *claimError=nil;
        if([self.machine claimAutomaticInstallationWithGuestReport:report error:&claimError]) {
            self.preparationRequested=YES;self.installationRequested=YES;self.automaticInstallation=YES;
            [self.log writeData:[@"\n[Virtual Intel Mac] macOS setup completed; beginning initial graphics installation.\n" dataUsingEncoding:NSUTF8StringEncoding]];
        } else if(claimError) {
            self.guestStatus.stringValue=claimError.localizedDescription;
        }
    }
    if(!error&&!self.preparationRequested&&boot.length&&
       (![boot isEqual:self.reconciledBoot]||self.reconciliationAttempts<3)&&
       [report[@"DesktopRunning"] isEqual:@YES]&&
       [report[@"InstalledSystemCandidate"] isEqual:@YES]&&[report[@"Guest"] isEqual:self.machine.guest]&&
       [NSFileManager.defaultManager fileExistsAtPath:installJournal.path]) {
        if(![boot isEqual:self.reconciledBoot])self.reconciliationAttempts=0;
        self.reconciliationAttempts++;
        self.preparing=YES;self.reconciledBoot=boot;self.completionGuestReport=nil;
        NSDictionary *prior=[NSDictionary dictionaryWithContentsOfURL:installJournal];
        BOOL changed=![prior[@"GuestResult"][@"BootSessionUUID"] isEqual:boot];
        __weak VIMSession *weakSession=self;
        [VIMGuestInstallation reconcileWithAgent:self.guestAgent journal:installJournal verifyAfterReboot:changed
            completion:^(NSDictionary *result,NSError *failure) {
                VIMSession *session=weakSession;if(!session)return;session.preparing=NO;
                if(!session.task.running)return;
                BOOL verified=!failure&&[result[@"VerifiedAfterReboot"] isEqual:@YES]&&
                    [result[@"BootSessionUUID"] isEqual:boot];
                // Desktop availability can precede Metal readiness. Permit at
                // most three read-only checks per boot, separated by the normal
                // 60-second timer. Never repeat installation or relax hashes.
                if(verified)session.reconciliationAttempts=3;
                if(verified&&session.machine.configuration[@"InstallerMedia"]) {
                    session.completionGuestReport=report;
                    // The owned backend remains open until shutdown/cleanup,
                    // but its installer volumes need not stay on the desktop.
                    [session ejectInstallerVolumes:@[@"/Volumes/macOS Base System",@"/Volumes/Install macOS Mojave",@"/Volumes/Install macOS Catalina",@"/Volumes/Install macOS Big Sur"]];
                }
                NSString *message=failure.localizedDescription?:(verified?
                    @"Graphics verified — installer cleanup will finish when this Mac shuts down":
                    ([result[@"Phase"] isEqual:@"awaiting-reboot"]?@"Installation status recovered — restart required for graphics verification":
                     [NSString stringWithFormat:@"Installation status: %@",result[@"Phase"]?:@"unknown"]));
                session.guestStatus.stringValue=message;
                [session.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] Reconciliation: %@\n",message] dataUsingEncoding:NSUTF8StringEncoding]];
                // Console discovery may have completed while this read-only
                // check held preparing=YES. Resume clipboard provisioning now,
                // not at the next minute-long desktop status refresh. Do not
                // let a stale callback configure another boot's GUI session.
                if([session.reconciledBoot isEqual:boot])[session updateClipboardForBoot:boot];
                session.guestTimer=[NSTimer scheduledTimerWithTimeInterval:60 repeats:NO block:^(NSTimer *timer){[weakSession refreshGuestStatus];}];
            }];
        return;
    }
    if(self.preparationRequested&&!error&&([report[@"DesktopRunning"] isEqual:@YES]||self.verifiedUpdateRepair)&&[report[@"InstalledSystemCandidate"] isEqual:@YES]&&
       [report[@"Guest"] isEqual:self.machine.guest]) {
        self.preparationRequested=NO;self.preparing=YES;
        self.window.subtitle=@"Finishing setup…";
        NSURL *runtime=[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:@"Runtime"];
        __weak VIMSession *weakSession=self;
        [VIMGuestPreparation prepareWithAgent:self.guestAgent report:report runtime:runtime
            journal:[self.machine.URL URLByAppendingPathComponent:@"State/GuestPreparation.plist"] control:self.control
            allowHeadlessRepair:self.verifiedUpdateRepair&&![report[@"DesktopRunning"] isEqual:@YES]
            progress:^(NSString *progress){
                VIMSession *current=weakSession;current.guestStatus.stringValue=progress;
                if(current.verifiedUpdateRepair&&![report[@"DesktopRunning"] isEqual:@YES]) {
                    current.displayView.displaySleeping=YES;current.statePanel.hidden=NO;current.stateAction.hidden=YES;
                    current.stateLabel.stringValue=[progress stringByAppendingString:@"\nKeep this Mac running while graphics support is restored."];
                }
            }
            completion:^(NSDictionary *receipt,NSError *failure) {
                VIMSession *session=weakSession;if(!session)return;session.preparing=NO;session.window.subtitle=@"";
                if(!session.task.running)return;
                session.guestProgress=[VIMGuestProgress progressWithAgent:session.guestAgent receipt:receipt[@"GuestProgress"]];
                BOOL install=session.installationRequested;session.installationRequested=NO;
                if(install&&!failure) {
                    session.preparing=YES;session.guestStatus.stringValue=@"Installing graphics support — do not shut down this Mac…";
                    session.window.subtitle=@"Installing graphics support…";
                    void (^applyPrepared)(NSError *)=^(NSError *archiveError){
                    if(archiveError) {
                        session.preparing=NO;session.window.subtitle=@"";session.guestStatus.stringValue=archiveError.localizedDescription;
                        [session.guestProgress updateMode:@"failed" fraction:0 message:@"The previous installation record could not be preserved. Nothing was installed." completion:nil];
                        VIMPresentError(archiveError,session.window);return;
                    }
                    [VIMGuestInstallation applyWithAgent:session.guestAgent preparation:receipt
                        journal:[session.machine.URL URLByAppendingPathComponent:@"State/GuestInstallation.plist"]
                        completion:^(NSDictionary *outcome,NSError *installError) {
                            VIMSession *current=weakSession;if(!current)return;current.preparing=NO;current.window.subtitle=@"";
                            NSString *result=installError.localizedDescription?:@"Graphics files installed — restart required for verification";
                            current.guestStatus.stringValue=result;
                            if(installError) {
                                BOOL restored=[outcome[@"Phase"] isEqual:@"rolled-back"];
                                [current.guestProgress updateMode:restored?@"failed":@"working" fraction:restored?0:-1
                                    message:restored?@"Setup couldn’t finish. The original system files were restored.":@"Setup status couldn’t be confirmed. Keep this Mac running and check Session Details in the Virtual Intel Mac app."
                                    completion:nil];
                            }
                            [current.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] Installation: %@\n",result] dataUsingEncoding:NSUTF8StringEncoding]];
                            current.reconciledBoot=nil;
                            if(!installError&&current.task.running&&
                               [outcome[@"Phase"] isEqual:@"awaiting-reboot"]&&[outcome[@"RebootRequired"] isEqual:@YES]&&
                               !current.machine.requiresInstallerDisplay) {
                                if(current.verifiedUpdateRepair&&![report[@"DesktopRunning"] isEqual:@YES]){[current handoffToGraphics];return;}
                                [current offerGraphicsRestart];return;
                            }
                            current.guestTimer=[NSTimer scheduledTimerWithTimeInterval:60 repeats:NO block:^(NSTimer *timer){[weakSession refreshGuestStatus];}];
                            // No unconditional reboot: uncertain outcomes and
                            // rollback must never be mistaken for success.
                        }];
                    };
                    // Keep the prior valid display handoff until preparation
                    // succeeds. Otherwise a staging-only failure incorrectly
                    // turns the next boot back into an unaccelerated installer.
                    if(session.repairArchivePending) {
                        session.repairArchivePending=NO;
                        [VIMGuestInstallation archiveForRepairWithAgent:session.guestAgent
                            journal:[session.machine.URL URLByAppendingPathComponent:@"State/GuestInstallation.plist"]
                            updatedSystem:([session.machine canRepairUpdatedSystemWithGuestReport:report error:nil]?report:nil) completion:applyPrepared];
                    } else applyPrepared(nil);
                    return;
                }
                NSString *message=failure.localizedDescription?:@"Guest files prepared — installation pending";
                session.guestStatus.stringValue=message;
                if(!failure)[session.guestProgress updateMode:@"complete" fraction:1 message:@"Support files are prepared. No system files were installed." completion:nil];
                [session.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] Preparation: %@\n",message] dataUsingEncoding:NSUTF8StringEncoding]];
                // Explicit request runs once. No automatic retry of uncertain writes.
                session.guestTimer=[NSTimer scheduledTimerWithTimeInterval:60 repeats:NO block:^(NSTimer *timer){[weakSession refreshGuestStatus];}];
            }];
        return;
    }
    __weak VIMSession *weakSelf=self;
    if(!error&&[report[@"DesktopRunning"] isEqual:@YES])[self updateClipboardForBoot:boot];
    self.guestTimer=[NSTimer scheduledTimerWithTimeInterval:(error||self.preparationRequested||![report[@"DesktopRunning"] isEqual:@YES]||(!self.guestIPAddresses.count&&self.networkLinkConnected))?15:60 repeats:NO block:^(NSTimer *timer){
        [weakSelf refreshGuestStatus];
    }];
}
- (void)refreshRunState {
    if(self.statusPending||!self.task.running)return;
    self.statusPending=YES;
    __weak VIMSession *weakSelf=self;
    [self.control execute:@"query-status" arguments:@{} completion:^(id result,NSError *error){
        VIMSession *session=weakSelf;session.statusPending=NO;
        if(!session.task.running)return;
        if(error){session.runStateQueryFailed=YES;session.status.stringValue=error.localizedDescription;return;}
        NSString *state=[result isKindOfClass:NSDictionary.class]?result[@"status"]:nil;
        if(![state isKindOfClass:NSString.class]||([state isEqual:session.lastRunState]&&!session.runStateQueryFailed))return;
        // Restore the visible status after a transient control timeout even if
        // the VM stayed running throughout it. Preserve the last known sleep
        // state while disconnected so input can still request a wakeup.
        session.runStateQueryFailed=NO;
        session.lastRunState=state;
        [session updateInstallationActivity];
        [NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
        session.displayView.displaySleeping=[@[@"suspended",@"shutdown"] containsObject:state];
        session.statePanel.hidden=YES;
        session.stateAction.hidden=YES;
        NSDictionary *labels=@{@"running":@"Running",@"paused":@"Paused",@"io-error":@"Paused: a disk operation failed",
            @"guest-panicked":@"The guest reported a kernel panic",@"internal-error":@"Stopped: an internal emulator error",
            @"shutdown":@"Guest shut down",@"suspended":@"Sleeping",@"prelaunch":@"Starting…",@"watchdog":@"Paused: guest watchdog expired"};
        session.status.stringValue=labels[state]?:[NSString stringWithFormat:@"Guest state: %@",state];
        if([@[@"io-error",@"guest-panicked",@"internal-error",@"watchdog"] containsObject:state]) {
            session.displayView.displaySleeping=YES;session.statePanel.hidden=NO;
            session.stateLabel.stringValue=[session.status.stringValue stringByAppendingString:@". Open Controls → Session Details for more information."];
        }
        NSData *line=[[NSString stringWithFormat:@"\n[Virtual Intel Mac] QEMU state: %@\n",state] dataUsingEncoding:NSUTF8StringEncoding];
        [session.log writeData:line];
    }];
}
- (void)offerGraphicsRestart {
    if(self.guestProgress) {
        self.preparing=YES;
        __weak VIMSession *weakSelf=self;
        [self.guestProgress updateMode:@"restart" fraction:1 message:@"Graphics support is installed. Restart your virtual Mac to activate it." completion:^(NSError *error){
            VIMSession *session=weakSelf;if(!session.task.running)return;
            if(error){session.preparing=NO;[session offerHostGraphicsRestart];return;}
            [session.guestProgress waitForRestartChoice:^(NSString *choice,NSError *failure){
                VIMSession *current=weakSelf;if(!current.task.running)return;current.preparing=NO;
                if(failure){current.guestStatus.stringValue=failure.localizedDescription;[current offerHostGraphicsRestart];return;}
                [current.guestProgress updateMode:@"close" fraction:1 message:@"" completion:^(NSError *closeError){
                    if([choice isEqual:@"restart"])[current handoffToGraphics];
                    else {
                        current.guestStatus.stringValue=@"Graphics support installed. Restart when you’re ready to finish setup.";
                        current.guestTimer=[NSTimer scheduledTimerWithTimeInterval:60 repeats:NO block:^(NSTimer *timer){[weakSelf refreshGuestStatus];}];
                    }
                }];
            }];
        }];return;
    }
    [self offerHostGraphicsRestart];
}
- (void)offerHostGraphicsRestart {
    NSAlert *alert=[NSAlert new];alert.messageText=@"Restart to finish graphics setup?";
    alert.informativeText=@"Graphics support has been installed. Save your work before restarting this virtual Mac. You can also shut down and open it again later.";
    [alert addButtonWithTitle:@"Restart Now"];[alert addButtonWithTitle:@"Later"];
    VIMSetInstallerAlertIcon(alert,self.machine.guest);
    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response){
        if(response==NSAlertFirstButtonReturn&&self.task.running)[self handoffToGraphics];
        else self.guestStatus.stringValue=@"Graphics support installed. Restart when you’re ready to finish setup.";
    }];
}
- (void)handoffToGraphics {
    // A late installation/status callback must not override an explicit user
    // shutdown/close or revive an emulator that has already stopped.
    if(self.shutdownRequested||self.closeAfterShutdown||!self.task.running)return;
    self.automaticInstallation=NO;self.relaunchForGraphics=YES;
    self.guestStatus.stringValue=@"Graphics support installed — restarting this Mac…";
    __weak VIMSession *weakSelf=self;
    // Changing emulated display hardware needs a new QEMU process, not an
    // in-guest reboot. Never force termination if macOS cannot shut down.
    self.handoffTimer=[NSTimer scheduledTimerWithTimeInterval:180 repeats:NO block:^(NSTimer *timer){
        VIMSession *session=weakSelf;session.relaunchForGraphics=NO;
        if(session.task.running) {
            session.guestStatus.stringValue=@"Shutdown is taking longer than expected. Shut down this Mac, then open it again to finish setup.";
            [session refreshGuestStatus];
        }
    }];
    [self.guestAgent runExecutable:@"/sbin/shutdown" arguments:@[@"-h",@"now"] timeout:20
        completion:^(NSDictionary *result,NSError *error){
            // The transport normally disappears during shutdown. Only a
            // definite command failure cancels the handoff immediately.
            VIMSession *session=weakSelf;
            if(session.task.running&&!error&&result&&![result[@"exitcode"] isEqual:@0]) {
                session.relaunchForGraphics=NO;[session.handoffTimer invalidate];
                session.guestStatus.stringValue=@"Automatic shutdown failed. Shut down this Mac, then open it again to finish setup.";
                [session refreshGuestStatus];
            }
        }];
}
- (BOOL)validateMenuItem:(NSMenuItem *)item {
    if(item.action==@selector(startAgain:)){item.state=self.task.running?NSControlStateValueOn:NSControlStateValueOff;return !self.task.running;}
    if(item.action==@selector(resetMachine:))return self.task.running&&!self.forceResetRequested;
    if(item.action==@selector(sendForceQuitKey:)||item.action==@selector(takeScreenshot:))return self.task.running&&!self.preparing;
    if([@[@"undo:",@"cut:",@"copy:",@"paste:",@"selectAll:"] containsObject:NSStringFromSelector(item.action)])return self.task.running;
    if(item.action==@selector(shutdown:))return self.task.running&&self.shutdownButton.enabled&&!self.preparing&&!self.shutdownRequested;
    if(item.action==@selector(forceStop:))return self.task.running;
    if(item.action==@selector(toggleUSBDevice:))return self.task.running&&self.shutdownButton.enabled;
    if(item.action==@selector(copyGuestIPAddress:))return self.task.running&&self.networkLinkConnected&&[self.guestIPAddresses containsObject:item.representedObject];
    if(item.action==@selector(ejectOpticalMedia:))return !self.mediaChangePending&&(self.task.running?self.insertedMediaPath.length>0:[self.machine.editableConfiguration[@"OpticalMedia"] length]>0);
    if(item.action==@selector(sendMenuShortcut:))return self.task.running&&!self.preparing;
    if(item.action==@selector(changeRetinaMode:)) {
        item.state=[self.displayMode isEqual:@[@"space",@"retina",@"scaled"][item.tag]]?NSControlStateValueOn:NSControlStateValueOff;
        return !self.displayModePending;
    }
    if(item.action==@selector(showGuestDiagnostics:))return YES;
    BOOL idle=self.task.running&&!self.preparing&&!self.preparationRequested&&!self.guestPending&&!self.relaunchForGraphics;
    BOOL recorded=[NSFileManager.defaultManager fileExistsAtPath:[self.machine.URL URLByAppendingPathComponent:@"State/GuestInstallation.plist"].path];
    if(item.action==@selector(checkGraphicsSetup:))return idle&&recorded;
    if(item.action==@selector(finishGraphicsSetup:))return idle&&[@[@"mojave",@"catalina",@"bigsur"] containsObject:self.machine.guest];
    return YES;
}
- (void)menuWillOpen:(NSMenu *)menu {
    [self.displayView releasePressedKeys];
    // Toolbar menus can be opened without AppKit requesting menuNeedsUpdate.
    // Enumerate metadata at opening, just like the menu-bar Devices submenu.
    [self menuNeedsUpdate:menu];
}
- (void)ejectInstallerVolumes:(NSArray<NSString *> *)volumes {
    if(!volumes.count||!self.task.running||!self.completionGuestReport)return;
    NSString *volume=volumes.firstObject;
    __weak VIMSession *weakSelf=self;
    // Resolve and check the mounted installer before ejecting. A physical USB
    // volume with the same name must never be mistaken for our QEMU media.
    [self.guestAgent runExecutable:@"/usr/sbin/diskutil" arguments:@[@"info",@"-plist",volume] timeout:15 completion:^(NSDictionary *result,NSError *error){
        NSDictionary *info=(!error&&[result[@"exitcode"] isEqual:@0])?[NSPropertyListSerialization propertyListWithData:result[@"stdout"] options:0 format:NULL error:nil]:nil;
        NSArray *remaining=[volumes subarrayWithRange:NSMakeRange(1,volumes.count-1)];
        NSString *media=info[@"MediaName"];
        BOOL installer=[info[@"MountPoint"] isEqual:volume]&&[media isKindOfClass:NSString.class]&&[media hasPrefix:@"QEMU "];
        if(!installer){[weakSelf ejectInstallerVolumes:remaining];return;}
        [weakSelf.guestAgent runExecutable:@"/usr/sbin/diskutil" arguments:@[@"eject",volume] timeout:30 completion:^(NSDictionary *ignored,NSError *issue){
            // Never force/retry an uncertain guest eject; stopped-VM cleanup
            // remains authoritative for releasing/deleting the owned ISO.
            [weakSelf ejectInstallerVolumes:remaining];
        }];
    }];
}
- (NSMenu *)deviceMenuForKind:(NSString *)kind {
    VIMLiveDeviceMenu *menu=[[VIMLiveDeviceMenu alloc] initWithTitle:[kind isEqual:@"USB"]?VIMUSBMenuTitle:kind];menu.deviceKind=menu.title;menu.delegate=self;
    if([kind isEqual:@"USB"])[self populateUSBMenu:menu];else [self populateDeviceMenu:menu];return menu;
}
- (NSMenuItem *)addDeviceItem:(NSString *)title action:(SEL)action toMenu:(NSMenu *)menu {
    NSMenuItem *item=[menu addItemWithTitle:title action:action keyEquivalent:@""];item.target=self;return item;
}
- (void)refreshAudioChoices {
    if(self.audioChoicesPending)return;self.audioChoicesPending=YES;
    __weak VIMSession *weakSelf=self;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
        NSArray *outputs=VIMHostAudioDevices(NO),*inputs=VIMHostAudioDevices(YES);
        dispatch_async(dispatch_get_main_queue(),^{weakSelf.audioOutputChoices=outputs;weakSelf.audioInputChoices=inputs;weakSelf.audioChoicesPending=NO;});
    });
}
- (void)populateDeviceMenu:(NSMenu *)menu {
    if([menu isKindOfClass:VIMLiveDeviceMenu.class])menu.title=((VIMLiveDeviceMenu *)menu).deviceKind;
    NSString *toolbarTitle=ToolbarMenuTitle(menu);
    [menu removeAllItems];WithToolbarMenuTitle(menu,toolbarTitle);NSDictionary *config=self.machine.editableConfiguration;
    if([menu.title isEqual:@"CD/DVD"]) {
        NSString *path=self.task.running?self.insertedMediaPath:config[@"OpticalMedia"];
        [self addDeviceItem:path.length?path.lastPathComponent:@"No Image Connected" action:nil toMenu:menu].enabled=NO;
        if(path.length)[self addDeviceItem:@"Disconnect" action:@selector(ejectOpticalMedia:) toMenu:menu];
        else {[menu addItem:NSMenuItem.separatorItem];[self addDeviceItem:@"Connect Image…" action:@selector(chooseOpticalMedia:) toMenu:menu];}
        [menu addItem:NSMenuItem.separatorItem];[self addDeviceItem:@"Configure…" action:@selector(showOpticalSettings:) toMenu:menu];return;
    }
    if([menu.title isEqual:@"Network"]) {
        BOOL connected=self.task.running?self.networkLinkConnected:(!config[@"NetworkConnected"]||[config[@"NetworkConnected"] boolValue]);
        NSString *mode=VIMNetworkMode(self.task.running?self.machine.configuration:config);
        NSMenuItem *off=[self addDeviceItem:@"Disconnect" action:@selector(changeNetworkConnection:) toMenu:menu];off.tag=0;off.state=connected?NSControlStateValueOff:NSControlStateValueOn;
        [menu addItem:NSMenuItem.separatorItem];
        NSArray *modes=@[@"shared",@"host",@"bridged"],*titles=@[@"Shared Network (Recommended)",@"Host-Only Network",@"Bridged Network"];
        for(NSUInteger i=0;i<modes.count;i++) {
            NSMenuItem *item=[self addDeviceItem:titles[i] action:@selector(changeNetworkConnection:) toMenu:menu];item.tag=i+1;
            item.state=connected&&[mode isEqual:modes[i]]?NSControlStateValueOn:NSControlStateValueOff;
        }
        if(self.task.running&&connected&&self.guestIPAddresses.count) {
            [menu addItem:NSMenuItem.separatorItem];[self addDeviceItem:@"IP Addresses:" action:nil toMenu:menu].enabled=NO;
            for(NSString *address in self.guestIPAddresses)
                [self addDeviceItem:address action:@selector(copyGuestIPAddress:) toMenu:menu].representedObject=address;
        }
        [menu addItem:NSMenuItem.separatorItem];[self addDeviceItem:@"Configure…" action:@selector(showNetworkSettings:) toMenu:menu];return;
    }
    if([menu.title isEqual:@"Sound Output"]||[menu.title isEqual:@"Microphone"]) {
        BOOL input=[menu.title isEqual:@"Microphone"];NSString *selected=VIMAudioSelection(config,input);
        NSMutableArray *choices=[NSMutableArray arrayWithArray:@[@{@"Name":@"Default",@"UID":@"default"}]];
        [choices addObjectsFromArray:(input?self.audioInputChoices:self.audioOutputChoices)?:@[]];
        for(NSDictionary *choice in choices) {
            NSMenuItem *item=[self addDeviceItem:choice[@"Name"] action:@selector(changeAudioChoice:) toMenu:menu];item.representedObject=@{@"Input":@(input),@"UID":choice[@"UID"]};item.state=[selected isEqual:choice[@"UID"]]?NSControlStateValueOn:NSControlStateValueOff;
        }
        [menu addItem:NSMenuItem.separatorItem];
        NSMenuItem *off=[self addDeviceItem:@"Disconnected" action:@selector(changeAudioChoice:) toMenu:menu];
        off.representedObject=@{@"Input":@(input),@"UID":@"disabled"};off.state=[selected isEqual:@"disabled"]?NSControlStateValueOn:NSControlStateValueOff;
        [self refreshAudioChoices];return;
    }
    if([menu.title isEqual:@"Keyboard"]) {
        NSArray *shortcuts=@[@[@"Command–Option–Escape",@[@"meta_l",@"alt",@"esc"]],
            @[@"Command–Tab",@[@"meta_l",@"tab"]],@[@"Command–Space",@[@"meta_l",@"spc"]],
            @[@"Control–Command–Q",@[@"ctrl",@"meta_l",@"q"]],@[@"Control–Up Arrow",@[@"ctrl",@"up"]],
            @[@"Control–Down Arrow",@[@"ctrl",@"down"]],@[@"Control–Left Arrow",@[@"ctrl",@"left"]],
            @[@"Control–Right Arrow",@[@"ctrl",@"right"]],@[@"Shift–Command–3",@[@"shift",@"meta_l",@"3"]],
            @[@"Shift–Command–4",@[@"shift",@"meta_l",@"4"]],@[@"Shift–Command–5",@[@"shift",@"meta_l",@"5"]]];
        for(NSArray *shortcut in shortcuts)[self addDeviceItem:shortcut[0] action:@selector(sendMenuShortcut:) toMenu:menu].representedObject=shortcut[1];
    }
}
- (void)showOpticalSettings:(id)sender {[VIMMachineSettings showForMachine:self.machine section:@"Hardware" category:@"CD/DVD" window:self.window];}
- (void)copyGuestIPAddress:(NSMenuItem *)sender {
    // The menu may have stayed open through a network change. Never copy a
    // stale address or a non-address represented object from another action.
    if(!self.task.running||!self.networkLinkConnected||![self.guestIPAddresses containsObject:sender.representedObject])return;
    NSPasteboard *pasteboard=NSPasteboard.generalPasteboard;
    [pasteboard clearContents];[pasteboard setString:sender.representedObject forType:NSPasteboardTypeString];
}
- (void)changeNetworkConnection:(NSMenuItem *)sender {
    NSString *mode=sender.tag?@[@"shared",@"host",@"bridged"][sender.tag-1]:VIMNetworkMode(self.machine.editableConfiguration);
    [VIMSession changeNetworkMode:mode connected:sender.tag!=0 machine:self.machine window:self.window completion:^(NSError *error){if(error)VIMPresentError(error,self.window);}];
}
- (void)changeAudioChoice:(NSMenuItem *)sender {
    NSDictionary *choice=sender.representedObject;NSError *error=nil;
    BOOL input=[choice[@"Input"] boolValue];
    if([VIMAudioSelection(self.machine.editableConfiguration,input) isEqual:choice[@"UID"]])return;
    if(![self.machine saveSettings:@{@"AudioEnabled":@YES,(input?@"AudioInputDevice":@"AudioOutputDevice"):choice[@"UID"]} error:&error]){VIMPresentError(error,self.window);return;}
    if(self.task.running) {
        NSAlert *alert=[NSAlert new];alert.messageText=input?@"Restart to change the microphone":@"Restart to change sound output";
        alert.informativeText=@"Your selection is saved. It will take effect the next time this virtual Mac starts.";
        [alert addButtonWithTitle:@"OK"];VIMPresentAlert(alert,self.window,nil);
    }
    else [NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
}
- (void)menuNeedsUpdate:(NSMenu *)menu {
    if([menu isKindOfClass:VIMLiveDeviceMenu.class])menu.title=((VIMLiveDeviceMenu *)menu).deviceKind;
    if([menu.title isEqual:VIMUSBMenuTitle]||[menu.title isEqual:@"USB"])[self populateUSBMenu:menu];
    else if([@[@"CD/DVD",@"Network",@"Sound Output",@"Microphone",@"Keyboard"] containsObject:menu.title])[self populateDeviceMenu:menu];
}
- (void)refreshUSBToolbar:(NSNotification *)notification {
    for(NSToolbarItem *top in self.window.toolbar.items) {
        NSArray *items=[top isKindOfClass:NSToolbarItemGroup.class]?[(NSToolbarItemGroup *)top subitems]:@[top];
        for(NSToolbarItem *item in items)if([item.itemIdentifier isEqual:@"GuestUSB"]&&[item isKindOfClass:NSMenuToolbarItem.class])
            [self populateUSBMenu:((NSMenuToolbarItem *)item).menu];
        if([top isKindOfClass:NSToolbarItemGroup.class])((NSToolbarItemGroup *)top).subitems=items;
    }
}
/// Lists host devices; a checkmark means the device is connected to the guest.
- (void)populateUSBMenu:(NSMenu *)menu {
    NSString *toolbarTitle=ToolbarMenuTitle(menu);
    [menu removeAllItems];WithToolbarMenuTitle(menu,toolbarTitle);
    NSArray *devices=VIMHostUSBDevices();
    NSMutableSet *listed=[NSMutableSet set];
    for(NSDictionary *device in devices) {
        NSMenuItem *item=[menu addItemWithTitle:device[@"Name"] action:@selector(toggleUSBDevice:) keyEquivalent:@""];
        item.target=self;item.representedObject=device;[listed addObject:device[@"Identifier"]];
        item.state=self.usbAttached[device[@"Identifier"]]?NSControlStateValueOn:NSControlStateValueOff;
        item.enabled=!self.usbOperationPending;
        if([device[@"Vendor"] length])item.toolTip=device[@"Vendor"];
    }
    // A device unplugged from the Mac while connected can still be released.
    for(NSString *identifier in self.usbAttached) if(![listed containsObject:identifier]) {
        NSMenuItem *item=[menu addItemWithTitle:[self.usbAttached[identifier] stringByAppendingString:@" (unplugged)"] action:@selector(toggleUSBDevice:) keyEquivalent:@""];
        item.target=self;item.representedObject=@{@"Identifier":identifier,@"Name":self.usbAttached[identifier]};item.state=NSControlStateValueOn;
    }
    if(!devices.count&&!self.usbAttached.count) {
        NSMenuItem *empty=[menu addItemWithTitle:@"No USB Devices Connected to This Mac" action:nil keyEquivalent:@""];
        empty.enabled=NO;
    }
}
- (void)toggleUSBDevice:(NSMenuItem *)sender {
    if(self.usbOperationPending||!self.task.running)return;
    NSDictionary *device=sender.representedObject;NSString *identifier=device[@"Identifier"];
    if(!self.usbAttached)self.usbAttached=[NSMutableDictionary dictionary];
    __weak VIMSession *weakSelf=self;
    if(self.usbAttached[identifier]) {
        self.usbOperationPending=YES;
        [self.usbClient disconnectDevice:identifier control:self.control completion:^(NSError *error){
            weakSelf.usbOperationPending=NO;
            if(error)VIMPresentError(error,weakSelf.window);else [weakSelf.usbAttached removeObjectForKey:identifier];
        }];
        return;
    }
    NSMutableArray *protected=[NSMutableArray arrayWithObjects:NSBundle.mainBundle.bundleURL,VIMLibraryLocation.rootURL,nil];
    for(VIMSession *session in sessions.allValues)if(session.machine.URL)[protected addObject:session.machine.URL];
    NSError *failure=nil;NSDictionary *storage=VIMHostUSBStorageInfo(device,protected,&failure);
    if(!storage||[storage[@"Protected"] boolValue]){VIMPresentError(failure?:VIMSessionError(@"This device contains Virtual Intel Mac, a virtual Mac, or its library and cannot be unmounted. Move those files to another drive first."),self.window);return;}
    void (^connect)(void)=^{
        VIMSession *session=weakSelf;if(!session.task.running)return;
        session.usbOperationPending=YES;
        if(!session.usbClient)session.usbClient=[VIMUSBClient new];
        [session.usbClient prepareWithCompletion:^(NSError *error){
            if(error||!weakSelf.task.running){weakSelf.usbOperationPending=NO;if(error)VIMPresentError(error,weakSelf.window);return;}
            [weakSelf.usbClient connectDevice:device processID:weakSelf.task.processIdentifier directory:weakSelf.runtimeDirectory control:weakSelf.control completion:^(NSError *captureError){
                weakSelf.usbOperationPending=NO;
                if(captureError)VIMPresentError(captureError,weakSelf.window);else weakSelf.usbAttached[identifier]=device[@"Name"];
            }];
        }];
    };
    void (^requestAccess)(void (^)(void))=^(void (^next)(void)){
        weakSelf.usbOperationPending=YES;
        VIMHostRequestUSBStorageAccess(device,protected,^(NSError *error){
            weakSelf.usbOperationPending=NO;
            if(error){VIMPresentError(error,weakSelf.window);return;}
            if(weakSelf.task.running)next();
        });
    };
    if(![storage[@"MountedVolumes"] count]){requestAccess(connect);return;}
    NSAlert *alert=[NSAlert new];alert.messageText=[NSString stringWithFormat:@"Connect “%@” to this virtual Mac?",device[@"Name"]];
    alert.informativeText=@"Its disks must first be unmounted from this Mac. Save your work on this device before continuing. They’ll be available to the virtual Mac instead.";
    [alert addButtonWithTitle:@"Unmount and Connect"];[alert addButtonWithTitle:@"Cancel"];[alert addButtonWithTitle:@"Force Unmount…"];
    VIMPresentAlert(alert,self.window,^(NSModalResponse response){
        if(response==NSAlertSecondButtonReturn)return;
        void (^unmount)(BOOL)=^(BOOL force){
            requestAccess(^{
                weakSelf.usbOperationPending=YES;
                [weakSelf releaseUSBDisks:device protected:protected force:force completion:^{connect();}];
            });
        };
        if(response==NSAlertFirstButtonReturn){unmount(NO);return;}
        NSAlert *confirm=[NSAlert new];confirm.messageText=[NSString stringWithFormat:@"Force unmount “%@”?",device[@"Name"]];
        confirm.informativeText=@"Unsaved changes on this device may be lost. Close apps using it first if possible.";
        [confirm addButtonWithTitle:@"Force Unmount and Connect"];[confirm addButtonWithTitle:@"Cancel"];
        VIMPresentAlert(confirm,weakSelf.window,^(NSModalResponse answer){if(answer==NSAlertFirstButtonReturn)unmount(YES);});
    });
}
- (void)releaseUSBDisks:(NSDictionary *)device protected:(NSArray *)protected force:(BOOL)force completion:(void (^)(void))completion {
    __weak VIMSession *weakSelf=self;
    VIMHostReleaseUSBDisks(device,protected,force,^(NSError *error){
        VIMSession *session=weakSelf;session.usbOperationPending=NO;
        if(!error){completion();return;}
        if(force){VIMPresentError(error,session.window);return;}
        NSAlert *alert=[NSAlert new];alert.messageText=[NSString stringWithFormat:@"“%@” couldn’t be unmounted",device[@"Name"]];
        alert.informativeText=@"Close apps using this device and try again, or force unmount it to connect it to the virtual Mac. Forcing an unmount can lose unsaved changes on the device.";
        [alert addButtonWithTitle:@"Cancel"];[alert addButtonWithTitle:@"Force Unmount and Connect"];
        VIMPresentAlert(alert,session.window,^(NSModalResponse answer){
            if(answer==NSAlertSecondButtonReturn&&weakSelf.task.running) {
                weakSelf.usbOperationPending=YES;
                [weakSelf releaseUSBDisks:device protected:protected force:YES completion:completion];
            }
        });
    });
}
- (void)releaseGuestKeys {[self.displayView releasePressedKeys];}
- (void)sendShortcut:(NSArray<NSString *> *)keys {
    [self releaseGuestKeys];NSMutableArray *codes=[NSMutableArray array];for(NSString *key in keys)[codes addObject:@{@"type":@"qcode",@"data":key}];
    [self.control execute:@"send-key" arguments:@{@"keys":codes,@"hold-time":@50} completion:^(id result,NSError *error){if(error)VIMPresentError(error,self.window);}];
}
- (void)undo:(id)sender {[self sendShortcut:@[@"meta_l",@"z"]];}
- (void)cut:(id)sender {[self sendShortcut:@[@"meta_l",@"x"]];}
- (void)copy:(id)sender {[self sendShortcut:@[@"meta_l",@"c"]];}
- (void)paste:(id)sender {[self sendShortcut:@[@"meta_l",@"v"]];}
- (void)selectAll:(id)sender {[self sendShortcut:@[@"meta_l",@"a"]];}
- (void)sendForceQuitKey:(id)sender {[self sendShortcut:@[@"meta_l",@"alt",@"esc"]];}
- (void)sendMenuShortcut:(NSMenuItem *)sender {[self sendShortcut:sender.representedObject];}
- (void)resetMachine:(id)sender {
    if(!self.task.running||self.forceResetRequested)return;
    NSAlert *alert=[NSAlert new];alert.messageText=@"Reset this Mac?";alert.informativeText=@"This forcibly restarts the virtual Mac. Unsaved work will be lost, and an installation in progress may be interrupted.";
    [alert addButtonWithTitle:@"Cancel"];[alert addButtonWithTitle:@"Reset"];
    VIMPresentAlert(alert,self.window,^(NSModalResponse response){
        if(response!=NSAlertSecondButtonReturn||!self.task.running)return;
        self.forceResetRequested=YES;self.shutdownRequested=NO;self.closeAfterShutdown=NO;
        self.relaunchForGraphics=NO;[self.displayView releasePressedKeys];
        self.displayView.displaySleeping=YES;
        NSTask *ownedTask=self.task;[ownedTask terminate];
        // Independent of QMP, QGA and any guest OS service. A wedged BQL must
        // not leave an explicitly confirmed force reset waiting forever.
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC),dispatch_get_main_queue(),^{
            if(self.task==ownedTask&&ownedTask.running)
                kill(ownedTask.processIdentifier,SIGKILL);
        });
    });
}
- (void)changeRetinaMode:(NSMenuItem *)sender {
    if(sender.tag<0||sender.tag>2||self.displayModePending)return;
    if(self.diagnosticPending){VIMPresentError(VIMSessionError(@"Wait for the current guest operation to finish, then change the resolution again."),self.window);return;}
    [self applyDisplayMode:@[@"space",@"retina",@"scaled"][sender.tag] completion:^(NSDictionary *observed,NSError *error){
        if(error)VIMPresentError(error,self.window);
    }];
}
- (void)applyDisplayMode:(NSString *)mode completion:(void (^)(NSDictionary *,NSError *))completion {
    if(![@[@"space",@"retina",@"scaled"] containsObject:mode]){completion(nil,VIMSessionError(@"Choose More Space, Retina or Scaled."));return;}
    NSError *error=nil;
    NSDictionary *settings=@{@"DisplayMode":mode,@"RetinaEnabled":@(![mode isEqual:@"scaled"])};
    if(!self.task.running) {
        if(![self.machine saveSettings:settings error:&error]){completion(nil,error);return;}
        self.displayMode=mode;self.retinaEnabled=![mode isEqual:@"scaled"];
        [NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];completion(nil,nil);return;
    }
    if(!self.resizeArmed||self.preparing||self.guestPending||self.preparationRequested||self.clipboardPending||self.bootPolicyPending||self.displayModePending) {
        completion(nil,VIMSessionError(@"Wait until macOS has reached the desktop and setup has finished, then change the resolution again."));return;
    }
    CGFloat factor=[mode isEqual:@"scaled"]?1:([mode isEqual:@"retina"]?2:self.window.backingScaleFactor);
    NSUInteger width=MAX(640,MIN(8192,floor(self.displayView.bounds.size.width*factor/2)*2));
    NSUInteger height=MAX(400,MIN(8192,floor(self.displayView.bounds.size.height*factor/2)*2));
    [self.resizeTimer invalidate];self.displayModePending=YES;self.guestPending=YES;
    void (^finished)(NSDictionary *,NSError *)=^(NSDictionary *observed,NSError *failure){
        self.displayModePending=NO;self.guestPending=NO;
        if(!failure) {
            NSError *saveError=nil;
            [self.machine saveSettings:settings error:&saveError];
            self.displayMode=mode;self.retinaEnabled=![mode isEqual:@"scaled"];
            self.lastRequestedSize=NSMakeSize(width,height);
            self.observedDisplayMode=observed;
            if(saveError)failure=saveError;
            [NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
        } else {self.lastRequestedSize=NSZeroSize;[self scheduleResize];}
        completion(observed,failure);
        [self refreshGuestStatus];
    };
    void (^selectMode)(void)=^{
        // Keep the existing panel/frame transport; choose the 1×/2× mode
        // actually advertised by macOS, rather than recreating the GPU.
        [self.displayConnection requestWidth:width height:height];
        [self.guestAgent runExecutable:@"/usr/bin/stat" arguments:@[@"-f",@"%u",@"/dev/console"] timeout:10 completion:^(NSDictionary *result,NSError *failure){
            NSString *uid=[[NSString alloc] initWithData:result[@"stdout"]?:NSData.data encoding:NSUTF8StringEncoding];
            uid=[uid stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
            if(failure||![result[@"exitcode"] isEqual:@0]||uid.integerValue<501||[uid rangeOfCharacterFromSet:NSCharacterSet.decimalDigitCharacterSet.invertedSet].location!=NSNotFound) {
                finished(nil,failure?:VIMSessionError(@"Sign in to this Mac before changing its display resolution."));return;
            }
            NSString *tool=[self.displayToolDirectory stringByAppendingPathComponent:@"guest-display"];
            NSArray *arguments=@[@"asuser",uid,tool,@(width).stringValue,@(height).stringValue,[mode isEqual:@"retina"]?@"2":@"1"];
            [self.guestAgent runExecutable:@"/bin/launchctl" arguments:arguments timeout:15 completion:^(NSDictionary *reply,NSError *issue){
                NSString *detail=[[NSString alloc] initWithData:reply[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding];
                id observed=[NSJSONSerialization JSONObjectWithData:reply[@"stdout"]?:NSData.data options:0 error:nil];
                BOOL verified=[observed isKindOfClass:NSDictionary.class]&&[observed[@"PixelWidth"] isEqual:@(width)]&&
                    [observed[@"PixelHeight"] isEqual:@(height)]&&[observed[@"Scale"] isEqual:([mode isEqual:@"retina"]?@2:@1)];
                NSError *error=issue?:([reply[@"exitcode"] isEqual:@0]&&verified?nil:
                    VIMSessionError([@"macOS couldn’t apply and verify that display resolution. " stringByAppendingString:detail.length?detail:@"Try again after the desktop appears."]));
                finished(verified?observed:nil,error);
            }];
        }];
    };
    if(self.displayToolDirectory){selectMode();return;}
    NSURL *tools=[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:@"Runtime/GuestTools"];
    [self.guestAgent stageFiles:@{@"guest-macho":[tools URLByAppendingPathComponent:@"guest-macho"],@"guest-display":[tools URLByAppendingPathComponent:@"guest-display"]}
        progress:nil completion:^(NSDictionary *receipt,NSError *failure){
        if(failure){finished(nil,failure);return;}
        NSString *directory=receipt[@"Directory"];
        [self.guestAgent runExecutable:@"/bin/chmod" arguments:@[@"0700",[directory stringByAppendingPathComponent:@"guest-display"]] timeout:10 completion:^(NSDictionary *result,NSError *issue){
            if(issue||![result[@"exitcode"] isEqual:@0]){finished(nil,issue?:VIMSessionError(@"The display helper couldn’t be prepared."));return;}
            self.displayToolDirectory=directory;selectMode();
        }];
    }];
}
- (void)showNetworkSettings:(id)sender {[VIMMachineSettings showForMachine:self.machine section:@"Hardware" category:@"Network" window:self.window];}
+ (void)clipboardPreferencesChangedForMachine:(VIMMachine *)machine {
    VIMSession *session=sessions[SessionKey(machine)];
    [session updateClipboardForBoot:session.usernameBoot];
}
- (void)updateClipboardForBoot:(NSString *)boot {
    [self updateBridgeForBoot:boot displayPowerOnly:YES];
    [self updateBridgeForBoot:boot displayPowerOnly:NO];
}
- (void)updateBridgeForBoot:(NSString *)boot displayPowerOnly:(BOOL)powerOnly {
    // Installer VGA's legacy wire reader does not implement PVG power requests.
    // Do not send them into that unaccelerated producer.
    if(powerOnly&&self.displayView.softwareCursor)return;
    NSDictionary *settings=self.machine.editableConfiguration;
    BOOL enabled=!settings[@"ClipboardEnabled"]||[settings[@"ClipboardEnabled"] isEqual:@YES];
    if(!powerOnly)self.clipboardSharingEnabled=enabled;
    if(!powerOnly&&!enabled){[self.clipboard stop];self.clipboard=nil;self.clipboardConfiguredBoot=nil;self.clipboardRetryAfter=nil;return;}
    if(!boot.length||(!powerOnly&&!self.clipboardUID)||!self.task.running||self.preparing||self.preparationRequested||self.installationRequested||self.clipboardPending||self.diagnosticPending||self.guestPending||self.bootPolicyPending)return;
    VIMClipboardBridge *current=powerOnly?self.powerBridge:self.clipboard;
    if(current.isConnected&&[boot isEqual:powerOnly?self.powerConfiguredBoot:self.clipboardConfiguredBoot])return;
    if((powerOnly?self.powerRetryAfter:self.clipboardRetryAfter).timeIntervalSinceNow>0)return;
    NSError *failure=nil;
    if(!current) {
        current=[VIMClipboardBridge new];current.displayPowerOnly=powerOnly;
        if(powerOnly)self.powerBridge=current;else self.clipboard=current;
        __weak VIMSession *weakSelf=self;
        // This is evaluated by the 200 ms pasteboard timer and every receive.
        // Settings changes explicitly call updateClipboardForBoot; never read
        // PendingConfiguration.plist from a removable disk in this hot path.
        current.exchangeAllowed=^BOOL{VIMSession *session=weakSelf;return !powerOnly&&session&&session.task.running&&NSApp.active&&session.window.keyWindow&&!session.displayView.displaySleeping&&session.clipboardSharingEnabled;};
        current.displayPowerChanged=^(BOOL sleeping){if(powerOnly)[weakSelf.displayConnection requestDisplaySleeping:sleeping];};
        __weak VIMClipboardBridge *bridge=current;
        current.connectionLost=^{
            VIMSession *session=weakSelf;
            if(!session.task.running||!bridge||(powerOnly?session.powerBridge:session.clipboard)!=bridge)return;
            [bridge stop];
            if(powerOnly){session.powerBridge=nil;session.powerConfiguredBoot=nil;session.powerRetryAfter=[NSDate dateWithTimeIntervalSinceNow:60];[session.displayConnection requestDisplaySleeping:NO];}
            else {session.clipboard=nil;session.clipboardConfiguredBoot=nil;session.clipboardRetryAfter=[NSDate dateWithTimeIntervalSinceNow:60];}
            if(!powerOnly){session.clipboardUID=nil;session.usernameBoot=nil;}
            // Re-observe the console before provisioning: after logout, the
            // old UID/token is not authority to restart a GUI helper.
            [session refreshGuestStatus];
        };
    }
    if(![current startWithSocketPath:[self.runtimeDirectory stringByAppendingPathComponent:powerOnly?@"displaypower":@"clipboard"] error:&failure]) {
        [current stop];
        if(powerOnly){self.powerBridge=nil;self.powerRetryAfter=[NSDate dateWithTimeIntervalSinceNow:60];}
        else {self.clipboard=nil;self.clipboardRetryAfter=[NSDate dateWithTimeIntervalSinceNow:60];}return;
    }
    NSString *token=current.token,*configName=powerOnly?@"DisplayPower.json":@"Clipboard.json";
    NSData *data=[NSJSONSerialization dataWithJSONObject:@{@"UID":self.clipboardUID?:@0,@"Transport":@"virtio-serial",@"Token":token,@"DisplayPowerOnly":@(powerOnly)} options:0 error:NULL];
    NSURL *configuration=[NSURL fileURLWithPath:[self.runtimeDirectory stringByAppendingPathComponent:configName]];
    if(![data writeToURL:configuration options:NSDataWritingAtomic error:NULL])return;
    [NSFileManager.defaultManager setAttributes:@{NSFilePosixPermissions:@0600} ofItemAtPath:configuration.path error:NULL];
    NSURL *tools=[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:@"Runtime/GuestTools"];
    self.clipboardPending=YES;
    if(powerOnly)self.powerRetryAfter=[NSDate dateWithTimeIntervalSinceNow:60];else self.clipboardRetryAfter=[NSDate dateWithTimeIntervalSinceNow:60];
    __weak VIMSession *weakSelf=self;
    __block NSString *step=@"transfer";
    void (^finish)(BOOL)=^(BOOL ok){
        VIMSession *session=weakSelf;session.clipboardPending=NO;
        VIMClipboardBridge *bridge=powerOnly?session.powerBridge:session.clipboard;
        if(![bridge.token isEqual:token])return;
        if(ok){if(powerOnly)session.powerConfiguredBoot=boot;else {session.clipboardFailure=nil;session.clipboardConfiguredBoot=boot;}[bridge awaitGuestHandshake];}
        else {
            NSString *message=[NSString stringWithFormat:@"Guest %@ %@ did not finish.",powerOnly?@"display-power observer":@"clipboard",step];
            if(![session.clipboardFailure isEqual:message])[session.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] %@\n",message] dataUsingEncoding:NSUTF8StringEncoding]];
            [bridge stop];if(powerOnly)session.powerBridge=nil;else {session.clipboardFailure=message;session.clipboard=nil;}
        }
        // Let the independent clipboard provision after display setup releases
        // ownership; do not wait another minute or race the guest-agent channel.
        if(powerOnly&&ok)[session updateBridgeForBoot:boot displayPowerOnly:NO];
    };
    [self.guestAgent stageFiles:@{@"guest-macho":[tools URLByAppendingPathComponent:@"guest-macho"],@"guest-clipboard":[tools URLByAppendingPathComponent:@"guest-clipboard"],configName:configuration}
        progress:nil completion:^(NSDictionary *receipt,NSError *error){
        VIMSession *session=weakSelf;
        if(error||!session.task.running||![(powerOnly?session.powerBridge:session.clipboard).token isEqual:token]){finish(NO);return;}
        step=@"executable preparation";
        NSString *directory=receipt[@"Directory"],*helper=[directory stringByAppendingPathComponent:@"guest-clipboard"];
        [session.guestAgent runExecutable:@"/bin/chmod" arguments:@[@"0700",helper] timeout:10 completion:^(NSDictionary *mode,NSError *issue){
            if(issue||![mode[@"exitcode"] isEqual:@0]){finish(NO);return;}
            step=@"private configuration preparation";
            NSString *configuration=[directory stringByAppendingPathComponent:configName];
            // guest-file-open follows the agent's umask, not the source file's
            // mode. Tighten the private configuration explicitly before its
            // native validator reads it (the staging directory is root-only).
            [weakSelf.guestAgent runExecutable:@"/bin/chmod" arguments:@[@"0600",configuration] timeout:10 completion:^(NSDictionary *privacy,NSError *privacyError){
                if(privacyError||![privacy[@"exitcode"] isEqual:@0]){finish(NO);return;}
                step=@"GUI helper setup";
                [weakSelf.guestAgent runExecutable:helper arguments:@[@"--provision",configuration] timeout:30 completion:^(NSDictionary *result,NSError *problem){
                    if(result[@"exitcode"])step=[NSString stringWithFormat:@"GUI helper setup (exit %@)",result[@"exitcode"]];
                    finish(!problem&&[result[@"exitcode"] isEqual:@0]);
                }];
            }];
        }];
    }];
}
+ (void)setNetworkConnected:(BOOL)connected machine:(VIMMachine *)machine completion:(void (^)(NSError *))completion {
    VIMSession *session=sessions[SessionKey(machine)];
    if(!session.task.running){completion(nil);return;}
    [session.control execute:@"set_link" arguments:@{@"name":@"ethernet",@"up":@(connected)} completion:^(id result,NSError *error){
        if(!error){session.networkLinkConnected=connected;if(!connected)session.guestIPAddresses=nil;
            [NSNotificationCenter.defaultCenter postNotificationName:@"VIMNetworkChanged" object:machine];
            if(connected)[session refreshGuestStatus];
        }completion(error);
    }];
}
+ (NSDictionary *)networkDetailsForMachine:(VIMMachine *)machine {
    VIMSession *session=sessions[SessionKey(machine)];BOOL running=session.task.running;
    NSDictionary *config=running?session.machine.configuration:machine.editableConfiguration;
    NSString *mode=VIMNetworkMode(config),*host=nil;
    if([mode isEqual:@"shared"])host=@"10.0.2.2";
    else if(running)host=[mode isEqual:@"bridged"]?VIMNetworkHostAddress(session.networkClient.details[@"BridgeInterface"]):session.networkClient.details[@"HostAddress"];
    BOOL connected=running&&session.networkLinkConnected;
    return @{@"Mode":mode,@"Running":@(running),@"Connected":@(connected),@"HostAddress":host?:@"",@"GuestAddresses":connected?(session.guestIPAddresses?:@[]):@[]};
}
+ (void)changeNetworkMode:(NSString *)mode connected:(BOOL)connected machine:(VIMMachine *)machine window:(NSWindow *)window completion:(void (^)(NSError *))completion {
    VIMSession *session=sessions[SessionKey(machine)];BOOL different=session.task.running&&![VIMNetworkMode(session.machine.configuration) isEqual:mode];
    NSError *error=nil;
    if(![machine saveSettings:@{@"NetworkMode":mode,@"NetworkConnected":@(connected)} error:&error]){completion(error);return;}
    if(different&&connected) {
        NSAlert *alert=[NSAlert new];alert.messageText=@"Restart to change networks";alert.informativeText=@"Your network selection is saved and will take effect the next time this virtual Mac starts. Its current connection stays active until then.";
        [alert addButtonWithTitle:@"OK"];VIMPresentAlert(alert,window,nil);completion(nil);
    } else [self setNetworkConnected:connected machine:machine completion:completion];
    [NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
}
- (void)toggleDeviceToolbar:(id)sender {
    NSString *key=[@"VIMExpandedToolbar-" stringByAppendingString:SessionKey(self.machine)];[NSUserDefaults.standardUserDefaults setBool:![NSUserDefaults.standardUserDefaults boolForKey:key] forKey:key];[self installWindowControls:self.task.running];
}
- (void)showCPUSettings:(id)sender {[VIMMachineSettings showForMachine:self.machine section:@"Hardware" category:@"CPU & Memory" window:self.window];}
- (void)showDiskSettings:(id)sender {[VIMMachineSettings showForMachine:self.machine section:@"Hardware" category:@"Hard Disk" window:self.window];}
+ (void)chooseOpticalMediaForMachine:(VIMMachine *)machine window:(NSWindow *)window {
    if([self connectedOpticalMediaForMachine:machine].length) {
        NSAlert *alert=[NSAlert new];alert.messageText=@"Disconnect the current image first";
        alert.informativeText=@"Eject the image in the virtual Mac, then choose Disconnect before connecting a different image.";
        [alert addButtonWithTitle:@"OK"];VIMPresentAlert(alert,window,nil);return;
    }
    NSOpenPanel *panel=[NSOpenPanel openPanel];panel.allowedContentTypes=@[[UTType typeWithFilenameExtension:@"iso"],[UTType typeWithFilenameExtension:@"cdr"],[UTType typeWithFilenameExtension:@"dmg"]];panel.prompt=@"Insert Disc";panel.message=@"Choose an ISO, CDR or unencrypted DMG. The original image is used read-only, without copying or conversion.";
    [panel beginSheetModalForWindow:window completionHandler:^(NSModalResponse response){if(response==NSModalResponseOK)[self setOpticalMedia:panel.URL.path machine:machine window:window];}];
}
+ (NSString *)connectedOpticalMediaForMachine:(VIMMachine *)machine {
    VIMSession *session=sessions[SessionKey(machine)];
    return (session.task.running?session.insertedMediaPath:machine.editableConfiguration[@"OpticalMedia"])?:@"";
}
+ (void)checkGuestFileSharing:(VIMMachine *)machine completion:(void (^)(NSError *))completion {
    VIMSession *session=sessions[SessionKey(machine)];
    if(!session.task.running){completion(VIMSessionError(@"Start the virtual Mac and sign in before opening its files."));return;}
    [session.guestAgent runExecutable:@"/usr/bin/nc" arguments:@[@"-z",@"-w",@"2",@"127.0.0.1",@"445"] timeout:8 completion:^(NSDictionary *result,NSError *error){
        if(!error&&![result[@"exitcode"] isEqual:@0])error=[NSError errorWithDomain:@"VirtualIntelMac.FileSharingDisabled" code:1 userInfo:@{
            NSLocalizedDescriptionKey:[NSString stringWithFormat:@"Turn on File Sharing in “%@”",machine.name],
            NSLocalizedRecoverySuggestionErrorKey:@"In the virtual Mac, open System Preferences → Sharing and turn on File Sharing. Click Options, enable SMB for your account, and enter the virtual Mac’s password. Then try Open in Finder again."}];
        completion(error);
    }];
}
+ (void)ejectOpticalMediaForMachine:(VIMMachine *)machine window:(NSWindow *)window {
    [self insertOpticalMedia:@"" machine:machine completion:^(NSError *error){
        if(!error)return;
        VIMSession *session=sessions[SessionKey(machine)];
        if(!session.task.running||session.mediaChangePending){VIMPresentError(error,window);return;}
        NSAlert *alert=[NSAlert new];alert.messageText=@"The image couldn’t be disconnected";
        alert.informativeText=@"Close apps using the image and eject it in the virtual Mac’s Finder, then try again. If it still can’t be ejected, shut down the virtual Mac before disconnecting it. Forcing a mounted image to disconnect can leave macOS waiting for a disk that is no longer available.";
        [alert addButtonWithTitle:@"OK"];
        VIMPresentAlert(alert,window,nil);
    }];
}
+ (void)setOpticalMedia:(NSString *)path machine:(VIMMachine *)machine window:(NSWindow *)window {
    [self insertOpticalMedia:path machine:machine completion:^(NSError *error){if(error)VIMPresentError(error,window);}];
}
+ (void)insertOpticalMedia:(NSString *)path machine:(VIMMachine *)machine completion:(void (^)(NSError *))completion {
    NSError *validation=nil;
    NSString *format=path.length?VIMOpticalImageFormat(path,&validation):nil;
    if(path.length&&!format){completion(validation);return;}
    VIMSession *session=sessions[SessionKey(machine)];
    void (^save)(NSError *)=^(NSError *failure){NSError *error=failure;if(!error)[machine saveSettings:@{@"OpticalMedia":path} error:&error];if(!error)[NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];completion(error);};
    void (^apply)(NSError *)=^(NSError *error){
        if(error){completion(error);return;}
        NSString *current=[self connectedOpticalMediaForMachine:machine];
        if(path.length&&current.length) {
            completion([current isEqual:path]?nil:VIMSessionError(@"Disconnect the current image before choosing another."));return;
        }
        if(!session.task.running){save(nil);return;}
        [session changeRemovableMedia:path format:format force:NO completion:save];
    };
    if(!path.length){apply(nil);return;}
    NSString *root=VIMDevelopmentSourceRoot();
    NSString *tool=root.length?[NSDictionary dictionaryWithContentsOfFile:[root stringByAppendingPathComponent:@"Build/RuntimeLocation.plist"]][@"Directory"]:
        [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"Runtime/bin"];
    if(!tool.length){completion(VIMSessionError(@"Open the complete Virtual Intel Mac app to connect disk images."));return;}
    VIMCheckOpticalImage(path,format,[tool stringByAppendingPathComponent:@"qemu-img"],apply);
}
- (void)changeRemovableMedia:(NSString *)path format:(NSString *)format force:(BOOL)force completion:(void (^)(NSError *))completion {
    if(self.mediaChangePending){completion(VIMSessionError(@"Wait for the current disc change to finish, then try again."));return;}
    self.mediaChangePending=YES;
    NSString *oldPath=self.insertedMediaPath?:@"",*oldFormat=self.insertedMediaFormat;
    NSString *(^device)(NSString *)=^NSString *(NSString *kind){return [kind isEqual:@"dmg"]?@"image-device":@"cdrom-device";};
    void (^finish)(NSError *)=^(NSError *error){
        self.mediaChangePending=NO;
        if(error&&[error.localizedDescription.localizedLowercaseString containsString:@"locked"])
            error=[NSError errorWithDomain:@"VirtualIntelMac.Media" code:1 userInfo:@{NSLocalizedDescriptionKey:@"This disc is in use",
                NSLocalizedRecoverySuggestionErrorKey:@"In the virtual Mac, eject the disc in Finder, then try again. Close any apps using it first. Your image hasn’t been changed.",NSUnderlyingErrorKey:error}];
        if(error&&path.length)error=VIMFriendlyDiscImageError(path,error);
        completion(error);
    };
    void (^insert)(NSString *,NSString *,void (^)(NSError *))=^(NSString *file,NSString *kind,void (^done)(NSError *)){
        [self.control execute:@"blockdev-change-medium" arguments:@{@"id":device(kind),@"filename":file,@"format":kind,@"read-only-mode":@"read-only"} completion:^(id result,NSError *error){done(error);}];
    };
    if(!path.length) {
        if(!oldPath.length){finish(nil);return;}
        [self.control execute:@"eject" arguments:@{@"id":device(oldFormat),@"force":@(force)} completion:^(id result,NSError *error){
            if(!error){self.insertedMediaPath=@"";self.insertedMediaFormat=nil;}finish(error);
        }];return;
    }
    void (^load)(void)=^{insert(path,format,^(NSError *error){
        if(!error){self.insertedMediaPath=path;self.insertedMediaFormat=format;finish(nil);return;}
        // A failed cross-device insertion restores the previous read-only
        // medium. Never force a guest-locked eject or save a failed selection.
        if(oldPath.length&&![oldFormat isEqual:format]) {
            insert(oldPath,oldFormat,^(NSError *restoreError){
                if(!restoreError){self.insertedMediaPath=oldPath;self.insertedMediaFormat=oldFormat;}
                finish(restoreError?VIMSessionError([NSString stringWithFormat:@"%@ The previous image also couldn’t be reinserted: %@",error.localizedDescription,restoreError.localizedDescription]):error);
            });
        }else finish(error);
    });};
    if(oldPath.length&&![oldFormat isEqual:format]) {
        [self.control execute:@"eject" arguments:@{@"id":device(oldFormat)} completion:^(id result,NSError *error){
            if(error){finish(error);return;}self.insertedMediaPath=@"";self.insertedMediaFormat=nil;load();
        }];
    }else load();
}
- (void)chooseOpticalMedia:(id)sender {[VIMSession chooseOpticalMediaForMachine:self.machine window:self.window];}
- (void)ejectOpticalMedia:(id)sender {[VIMSession ejectOpticalMediaForMachine:self.machine window:self.window];}
- (void)showSoundSettings:(id)sender {[VIMMachineSettings showForMachine:self.machine section:@"Hardware" category:@"Sound" window:self.window];}
- (void)showMicrophoneSettings:(id)sender {[VIMMachineSettings requestMicrophoneAccessFromWindow:self.window];}
- (void)showSharingSettings:(id)sender {[VIMMachineSettings showForMachine:self.machine section:@"Sharing" category:@"File Sharing" window:self.window];}
- (void)showSSHSettings:(id)sender {[VIMMachineSettings showForMachine:self.machine section:@"Options" category:@"Remote Login" window:self.window];}
- (void)showKeyboardSettings:(id)sender {[VIMMachineSettings showForMachine:self.machine section:@"Hardware" category:@"Mouse & Keyboard" window:self.window];}
- (void)takeScreenshot:(id)sender {
    NSSavePanel *panel=[NSSavePanel savePanel];panel.allowedContentTypes=@[UTTypePNG];panel.nameFieldStringValue=[self.machine.name stringByAppendingString:@".png"];
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response){
        if(response!=NSModalResponseOK)return;
        [self.displayView capturePNG:^(NSData *png){NSError *error=nil;if(!png)error=VIMSessionError(@"There isn’t a guest picture to save yet. Wake this Mac or wait for its desktop, then try again.");else [png writeToURL:panel.URL options:NSDataWritingAtomic error:&error];if(error)VIMPresentError(error,self.window);}];
    }];
}
- (IBAction)configureMachine:(id)sender {[VIMMachineSettings showForMachine:self.machine window:self.window completion:nil];}
- (IBAction)showInstallationGuide:(id)sender {
    NSAlert *alert=[NSAlert new];alert.messageText=@"Installing macOS";
    VIMSetInstallerAlertIcon(alert,self.machine.guest);
    alert.informativeText=@"In macOS Utilities, choose Reinstall macOS, then select Macintosh HD as the destination. Don’t select macOS Base System; it contains the installer.\n\nFollow the setup screens. Your Mac may restart several times. Graphics support is installed automatically after you reach the desktop. Keep this Mac running while setup finishes.";
    [alert beginSheetModalForWindow:self.window completionHandler:nil];
}
- (IBAction)showSessionDetails:(id)sender {
    NSAlert *alert=[NSAlert new];alert.messageText=@"Session Details";
    VIMSetAppAlertIcon(alert);
    alert.informativeText=[NSString stringWithFormat:@"%@\n\n%@",self.status.stringValue,self.guestStatus.stringValue];
    [alert addButtonWithTitle:@"Done"];[alert addButtonWithTitle:@"Show Diagnostic Files"];
    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){if(result==NSAlertSecondButtonReturn)[self showGuestDiagnostics:self];}];
}
- (IBAction)startAgain:(id)sender {
    NSError *error=nil;if(![VIMSession openMachine:self.machine error:&error])VIMPresentError(error,self.window);
}
- (BOOL)windowShouldClose:(NSWindow *)sender {
    if(self.networkStarting){[self cleanUp];return YES;}
    if(!self.task.running)return YES;
    [self.displayView releasePressedKeys];
    NSAlert *alert=[NSAlert new];alert.messageText=@"Close this Mac’s window?";
    VIMSetAppAlertIcon(alert);
    alert.informativeText=@"Keep it running in the background, or shut down macOS. Save your work inside this Mac before shutting down. You can return to it from the library.";
    [alert addButtonWithTitle:@"Keep Running"];[alert addButtonWithTitle:@"Shut Down"];[alert addButtonWithTitle:@"Cancel"];
    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result==NSAlertFirstButtonReturn)[self.window orderOut:nil];
        else if(result==NSAlertSecondButtonReturn){self.closeAfterShutdown=YES;[self shutdown:self];}
    }];return NO;
}
- (IBAction)showGuestDiagnostics:(id)sender {
    [NSWorkspace.sharedWorkspace openURL:[self.machine.URL URLByAppendingPathComponent:@"State"]];
}
- (IBAction)checkGraphicsSetup:(id)sender {
    if(!self.task.running||self.preparing||self.preparationRequested||self.relaunchForGraphics)return;
    self.reconciledBoot=nil;[self refreshGuestStatus];
}
- (IBAction)finishGraphicsSetup:(id)sender {
    if(!self.task.running||self.preparing||self.preparationRequested||self.relaunchForGraphics)return;
    NSURL *journal=[self.machine.URL URLByAppendingPathComponent:@"State/GuestInstallation.plist"];
    BOOL repair=[NSFileManager.defaultManager fileExistsAtPath:journal.path];
    NSAlert *alert=[NSAlert new];alert.messageText=repair?@"Repair graphics support?":@"Finish graphics setup?";
    VIMSetInstallerAlertIcon(alert,self.machine.guest);
    alert.informativeText=repair?@"Save your work first. We’ll check the previous installation, retain its recovery records, and prepare fresh graphics support for this macOS build. Keep this Mac running until repair finishes, then restart it. An unfinished or uncertain installation will not be replaced.":@"Save your work in this Mac first. Virtual Intel Mac will prepare and install its graphics support. This can take several minutes. Keep this Mac running until setup finishes, then shut it down and open it again.";
    [alert addButtonWithTitle:repair?@"Repair":@"Finish Setup"];[alert addButtonWithTitle:@"Cancel"];
    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response){
        if(response!=NSAlertFirstButtonReturn||!self.task.running)return;
        if(repair) {
            NSError *failure=nil;
            if(![VIMSession repairMachine:self.machine error:&failure])VIMPresentError(failure,self.window);
            return;
        }
        NSError *error=nil;
        if(![VIMSession installMachine:self.machine error:&error]) {
            self.guestStatus.stringValue=error.localizedDescription;
            VIMPresentError(error,self.window);
        }
    }];
}
- (void)shutdown:(id)sender {
    if(!self.task.running||self.shutdownRequested||self.preparing)return;
    self.relaunchForGraphics=NO;
    self.forceResetRequested=NO;self.shutdownRequested=YES;self.shutdownButton.enabled=NO;[self.displayView releasePressedKeys];
    self.guestStatus.stringValue=@"Asking macOS to shut down…";
    void (^request)(void)=^{[self.guestAgent connectWithCompletion:^(NSError *connectionError){
        if(!self.task.running)return;
        if(connectionError) {
            self.shutdownRequested=NO;self.shutdownButton.enabled=YES;
            self.guestStatus.stringValue=@"Guest tools are unavailable. Choose Shut Down from the Apple menu inside this Mac, or use Force Stop if it is unresponsive.";
            VIMPresentError(VIMSessionError(self.guestStatus.stringValue),self.window);return;
        }
        [self.guestAgent requestShutdownWithCompletion:^(NSError *error){
            if(!self.task.running)return;
            // Do not send a second shutdown or an ACPI sleep request after an
            // ambiguous agent disconnect. The task's exit is the completion.
            self.guestStatus.stringValue=error?@"Shutdown has not been confirmed. Wait for macOS to finish, or use Force Stop if it is unresponsive.":@"macOS is shutting down…";
        }];
    }];};
    if([self.lastRunState isEqual:@"paused"]||[self.lastRunState isEqual:@"suspended"]) {
        [self.control execute:[self.lastRunState isEqual:@"paused"]?@"cont":@"system_wakeup" arguments:@{} completion:^(id result,NSError *error){
            if(error){self.shutdownRequested=NO;self.shutdownButton.enabled=YES;VIMPresentError(error,self.window);return;}
            request();
        }];
    } else request();
}
- (void)forceStop:(id)sender {
    if(!self.task.running)return;
    NSAlert *alert=[NSAlert new];alert.messageText=@"Force stop this Mac?";
    VIMSetAppAlertIcon(alert);
    alert.informativeText=@"Unsaved work will be lost. Try Shut Down first.";
    [alert addButtonWithTitle:@"Cancel"];[alert addButtonWithTitle:@"Force Stop"];
    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result==NSAlertSecondButtonReturn && self.task.running){self.forceResetRequested=NO;self.relaunchForGraphics=NO;self.relaunchForGuestReboot=NO;self.shutdownRequested=YES;[self.task terminate];}
    }];
}
- (void)cleanUp {
    [self.controlRetryTimer invalidate];self.controlRetryTimer=nil;
    self.bootPolicyPending=NO;self.bootPolicyCheckedBoot=nil;
    self.networkStarting=NO;[self.networkClient stop];self.networkClient=nil;self.guestIPAddresses=nil;
    [self.usbClient stop];self.usbClient=nil;self.usbOperationPending=NO;self.usbAttached=nil;
    [self.smbService stop];self.smbService=nil;
    [self.clipboard stop];self.clipboard=nil;self.clipboardPending=NO;self.clipboardConfiguredBoot=nil;
    [self.powerBridge stop];self.powerBridge=nil;self.powerConfiguredBoot=nil;self.powerRetryAfter=nil;
    self.sleepActivity=nil;
    [self.handoffTimer invalidate];self.handoffTimer=nil;
    [self.guestTimer invalidate];self.guestTimer=nil;[self.guestAgent close];
    [self.statusTimer invalidate];self.statusTimer=nil;
    [self.resizeTimer invalidate];self.resizeTimer=nil;
    self.displayView.inputHandler=nil;
    [self.displayConnection stop];[self.displayView discardFrameStorage];
    [self.control close];[self.log closeFile];self.log=nil;
    if(self.runtimeDirectory) {
        unlink([self.runtimeDirectory stringByAppendingPathComponent:@"control"].fileSystemRepresentation);
        unlink([self.runtimeDirectory stringByAppendingPathComponent:@"guest-agent"].fileSystemRepresentation);
        rmdir(self.runtimeDirectory.fileSystemRepresentation);
    }
    // Only called before launch or after NSTask reports termination.
    if(self.machineLock>=0){close(self.machineLock);self.machineLock=-1;}
    if(self.completionGuestReport&&!self.task.running) {
        NSError *error=nil;
        BOOL finished=[self.machine finishInstallationWithGuestReport:self.completionGuestReport error:&error];
        self.guestStatus.stringValue=finished?@"Installation complete — installer media removed":
            [NSString stringWithFormat:@"Installer cleanup pending: %@",error.localizedDescription];
        self.completionGuestReport=nil;
    }
    if(self.machine&&!self.task.running) {
        NSError *error=nil;
        if(![self.machine retryInstallerCleanup:&error])self.guestStatus.stringValue=error.localizedDescription;
        error=nil;
        if(![self.machine applyPendingChanges:&error])self.guestStatus.stringValue=error.localizedDescription;
        self.window.title=self.machine.name;
    }
}
- (void)windowDidResignKey:(NSNotification *)notification { [self.displayView releasePressedKeys]; }
- (void)windowDidResize:(NSNotification *)notification { [self scheduleResize]; }
- (void)windowDidChangeBackingProperties:(NSNotification *)notification { [self scheduleResize]; }
- (void)windowDidEndLiveResize:(NSNotification *)notification { [self scheduleResize]; }
- (NSApplicationPresentationOptions)window:(NSWindow *)window willUseFullScreenPresentationOptions:(NSApplicationPresentationOptions)options {
    return options|NSApplicationPresentationAutoHideToolbar|NSApplicationPresentationAutoHideMenuBar;
}
- (void)scheduleResize {
    if(!self.resizeArmed||self.displayModePending)return;
    [self.resizeTimer invalidate];
    __weak VIMSession *weakSelf=self;
    self.resizeTimer=[NSTimer scheduledTimerWithTimeInterval:0.15 repeats:NO block:^(NSTimer *timer){
        VIMSession *session=weakSelf;
        if(!session.task.running)return;
        NSSize pixels=[session.displayView convertRectToBacking:session.displayView.bounds].size;
        if([session.displayMode isEqual:@"retina"])pixels=NSMakeSize(session.displayView.bounds.size.width*2,session.displayView.bounds.size.height*2);
        else if(!session.retinaEnabled)pixels=session.displayView.bounds.size;
        // Even pixel dimensions preserve the 2× logical mode on Retina panels.
        pixels.width=MAX(640,MIN(8192,floor(pixels.width/2)*2));
        pixels.height=MAX(400,MIN(8192,floor(pixels.height/2)*2));
        if(NSEqualSizes(pixels,session.lastRequestedSize))return;
        session.lastRequestedSize=pixels;
        [session.displayConnection requestWidth:pixels.width height:pixels.height];
    }];
}
@end
