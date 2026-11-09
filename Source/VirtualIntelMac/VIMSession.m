#import "VIMSession.h"
#import "VIMSymbolImages.h"
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
#import "VIMInstallerAssembly.h"
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
#import <AVFoundation/AVFoundation.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#import "VIMResearchInput.h"
#import "VIMAutomaticSetup.h"
#import "VIMModernSetup.h"
#import "VIMOpenGLSetup.h"
#import "VIMOSNaming.h"
#import "VIMInstallerCatalog.h"
#import "VIMImageSlots.h"
#import "VIMLegacyTabletPolicy.h"
#import "VIMFirmwareBoot.h"

static NSMutableDictionary<NSString *,VIMSession *> *sessions;
static NSMutableDictionary<NSString *,NSUUID *> *pendingRosettaStarts;

@interface VIMSetupNoticeView : NSView
@property(nonatomic,weak) NSButton *closeButton;
@end
@implementation VIMSetupNoticeView
- (NSView *)hitTest:(NSPoint)point {
    // The guest display underneath keeps receiving pointer input. Only the
    // actual dismiss button is interactive, not the material or its text.
    return [self.closeButton hitTest:[self.closeButton.superview convertPoint:point fromView:self.superview]];
}
- (void)layout {
    [super layout];
    CGPathRef shape=CGPathCreateWithRoundedRect(NSRectToCGRect(self.bounds),14,14,NULL);
    self.layer.shadowPath=shape;
    CGPathRelease(shape);
}
@end
// Explicit local opt-in for unattended development; ordinary installs ask.
static BOOL VIMSkipsDevelopmentPowerConfirmation(void) {
    return [NSUserDefaults.standardUserDefaults boolForKey:@"VIMSkipPowerConfirmation"]||
        [NSProcessInfo.processInfo.arguments containsObject:@"--development-no-power-confirmation"];
}
typedef NS_ENUM(NSUInteger,VIMBootOperation) {VIMBootStart,VIMBootPrepare,VIMBootInstall,VIMBootRepair};
static NSString *SessionKey(VIMMachine *machine) {return machine.configuration[@"Identifier"]?:machine.URL.URLByResolvingSymlinksInPath.path;}
static NSString *VIMOptionPath(NSString *path) { return [path stringByReplacingOccurrencesOfString:@"," withString:@",,"]; }
static NSError *VIMSessionError(NSString *message) {
    return [NSError errorWithDomain:@"VirtualIntelMac.Session" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];
}
static BOOL VIMPrepareOpenGLSurfaceEndpoint(VIMDisplayConnection *connection,pid_t pid,
    NSString *runtimeDirectory,NSError **error) {
    NSError *failure=nil;NSString *service=[connection enableSurfaceTransportForRendererPID:pid error:&failure];
    if(!service){if(error)*error=failure?:VIMSessionError(@"The OpenGL renderer could not be connected.");return NO;}
    NSMutableArray<NSString *> *paths=[NSMutableArray arrayWithObject:[runtimeDirectory stringByAppendingPathComponent:@"graphics-endpoint.plist"]];
    NSString *explicitEndpoint=NSProcessInfo.processInfo.environment[@"VIM_OPENGL_DISPLAY_ENDPOINT"];
    if(explicitEndpoint.length)[paths addObject:explicitEndpoint];
    NSDictionary *description=@{@"Version":@1,@"Service":service,@"RendererPID":@(pid)};
    for(NSString *path in paths) {
        NSDictionary *attributes=[NSFileManager.defaultManager attributesOfItemAtPath:path.stringByDeletingLastPathComponent error:NULL];
        BOOL privateDirectory=[attributes[NSFileType] isEqual:NSFileTypeDirectory] &&
            [attributes[NSFileOwnerAccountID] unsignedIntValue]==getuid() &&
            !([attributes[NSFilePosixPermissions] unsignedIntValue]&0077);
        if(!privateDirectory || ![description writeToFile:path atomically:YES] ||
           ![NSFileManager.defaultManager setAttributes:@{NSFilePosixPermissions:@0600} ofItemAtPath:path error:NULL]) {
            if(error)*error=VIMSessionError(@"The graphics display connection could not be prepared.");return NO;
        }
    }
    return YES;
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
        NSImage *image=[NSImage imageNamed:[guest stringByAppendingString:@"Wallpaper"]]?:[NSImage imageNamed:@"tigerWallpaper"];
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
        NSImage *product=[NSImage imageNamed:[guest stringByAppendingString:@"ProductIcon"]];
        if(!product&&![@[@"tiger",@"leopard",@"snowleopard"] containsObject:guest])
            product=[NSImage imageNamed:[guest stringByAppendingString:@"Roundel"]];
        self.mark=[NSImageView imageViewWithImage:product?:NSApp.applicationIconImage];
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
@property VIMOpenGLSetup *openGLSetup;
@property BOOL developmentSetupDeferred;
@property BOOL openGLDesktopMonitoring;
@property BOOL openGLLargeResizeReady;
@property BOOL openGLSetupMediaAttached;
@property BOOL openGLRestartAttempted;
@property BOOL legacyTabletGuest;
@property BOOL legacyTabletObserved;
@property NSUInteger tabletTrim;
@property BOOL legacyTabletObservationPending;
@property NSUInteger legacyTabletObservationAttempts;
@property VIMGuestProgress *guestProgress;
@property NSData *guestStatusTool;
@property NSTextField *guestStatus;
@property NSView *setupNotice;
@property(copy) NSString *setupNoticeHeading;
@property(copy) NSString *closedSetupNoticeHeading;
@property BOOL previewSetupNotice;
@property NSTimer *guestTimer;
@property BOOL guestPending;
@property NSURL *pendingHostConnectionURL;
@property(copy) void (^pendingHostConnectionCompletion)(NSError *);
- (void)servicePendingHostConnection;
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
@property NSString *displaySelectedBoot;
@property NSDate *displaySelectionRetryAfter;
@property BOOL displayPolicyRestartRequired;
@property NSString *sleepDisabledBoot;
@property NSString *mavericksSMBCheckedBoot;
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
@property VIMModernSetup *modernSetup;
@property BOOL modernSetupReportActive;
@property NSTimer *controlRetryTimer;
@property VIMUSBClient *usbClient;
- (void)ensureUSBClient;
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
@property NSMutableDictionary<NSNumber *,NSDictionary *> *additionalMedia;
@property NSMutableSet<NSNumber *> *imageDevices;
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
        [self.machine.configuration[@"InstallState"] isEqual:@"patching"]||
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
- (BOOL)deviceToolbarExpanded {
    NSString *key=[@"VIMExpandedToolbar-" stringByAppendingString:SessionKey(self.machine)];
    NSNumber *saved=[NSUserDefaults.standardUserDefaults objectForKey:key];
    return saved?saved.boolValue:YES;
}
- (NSArray<NSToolbarItemIdentifier> *)toolbarDefaultItemIdentifiers:(NSToolbar *)toolbar {
    if(![self deviceToolbarExpanded])return @[NSToolbarFlexibleSpaceItemIdentifier,@"GuestExpand",@"GuestSettings"];
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
        for(NSString *child in @[@"GuestApps",@"GuestCPU",@"GuestMouse",@"GuestUSB",@"GuestOptical",@"GuestNetwork",@"GuestSharing",@"GuestControls"])
            [items addObject:[self toolbar:toolbar itemForItemIdentifier:child willBeInsertedIntoToolbar:NO]];
        group.subitems=items;return group;
    }
    if([identifier isEqual:@"GuestMouse"]) {
        BOOL relative=self.displayView.relativeMouseMode;
        NSToolbarItem *item=[[NSToolbarItem alloc] initWithItemIdentifier:identifier];
        item.label=relative?@"Relative Mouse":@"Tablet Mouse";
        item.toolTip=relative?@"Using a relative mouse, for games with their own cursor or aim. Click to switch back to the tablet pointer for normal use.":
            @"Using the absolute tablet pointer, for normal use. Click to switch to a relative mouse for games (⌘⇧M).";
        item.image=[NSImage imageWithSystemSymbolName:relative?@"computermouse.fill":@"computermouse" accessibilityDescription:item.label];
        item.target=self;item.action=@selector(toggleRelativeMouse:);return item;
    }
    if([identifier isEqual:@"GuestExpand"]) {
        BOOL expanded=[self deviceToolbarExpanded];
        NSToolbarItem *item=[[NSToolbarItem alloc] initWithItemIdentifier:identifier];item.label=expanded?@"Hide Devices":@"Show Devices";item.toolTip=item.label;
        item.image=[NSImage imageWithSystemSymbolName:expanded?@"chevron.left":@"chevron.down" accessibilityDescription:item.label];item.target=self;item.action=@selector(toggleDeviceToolbar:);return item;
    }
    NSDictionary *devices=@{@"GuestApps":@[@"Download Apps…",@"appstore",@"downloadApps:"],@"GuestCPU":@[@"CPU & Memory",@"cpu",@"showCPUSettings:"],@"GuestSharing":@[@"Shared Folders",@"folder",@"showSharingSettings:"]};
    NSArray *device=devices[identifier];if(device){NSToolbarItem *item=[[NSToolbarItem alloc] initWithItemIdentifier:identifier];item.label=device[0];item.toolTip=device[0];item.image=[identifier isEqual:@"GuestApps"]?VIMAppsSymbolImage():[NSImage imageWithSystemSymbolName:device[1] accessibilityDescription:device[0]];item.target=self;item.action=NSSelectorFromString(device[2]);return item;}
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
        [menu addItem:NSMenuItem.separatorItem];
        NSMenuItem *relativeMouse=[[NSMenuItem alloc] initWithTitle:@"Relative Mouse (for Games)" action:@selector(toggleRelativeMouse:) keyEquivalent:@"m"];
        relativeMouse.keyEquivalentModifierMask=NSEventModifierFlagCommand|NSEventModifierFlagShift;
        // The cursor is intentionally hidden while this is on (see
        // -effectiveCursor) since it can no longer show where a click will
        // actually land. A menu click alone isn't reliable then - this must
        // stay reachable by key equivalent, not just by clicking the menu.
        relativeMouse.target=self;relativeMouse.state=self.displayView.relativeMouseMode?NSControlStateValueOn:NSControlStateValueOff;[menu addItem:relativeMouse];
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
    // These guests use the native host PassthroughOpenGL executor, not the
    // Intel PVG helper. Their TCG CPU does not require Rosetta to be installed.
    if(machine.preMetalGuest)return [self performBootOperation:operation machine:machine error:error];
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
        BOOL installing=machine.preMetalGuest?![machine.configuration[@"InstallState"]?:@"installed" isEqual:@"installed"]:machine.requiresInstallerDisplay;
        return installing?@"Installing macOS":@"Running";
    }
    BOOL installing=machine.preMetalGuest?![machine.configuration[@"InstallState"]?:@"installed" isEqual:@"installed"]:machine.requiresInstallerDisplay;
    return installing?@"Installation in progress":@"Shut down";
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
    if(!session.task.running){completion(VIMSessionError(@"Start the virtual Mac and sign in to its desktop, then try again."));return;}
    NSString *host=[self networkDetailsForMachine:machine][@"HostAddress"];
    if(!host.length||![URL.host isEqual:host]){completion(VIMSessionError(@"This Mac’s network address changed. Reopen File Sharing or Remote Login and try again."));return;}
    if(session.pendingHostConnectionURL){completion(VIMSessionError(@"A connection is already waiting to open in this virtual Mac."));return;}
    session.pendingHostConnectionURL=URL;session.pendingHostConnectionCompletion=completion;
    [session servicePendingHostConnection];
}
- (void)servicePendingHostConnection {
    if(!self.pendingHostConnectionURL||!self.task.running||self.preparing||self.guestPending||self.diagnosticPending||
       self.preparationRequested||self.bootPolicyPending||self.clipboardPending||
       [@[@"paused",@"suspended"] containsObject:self.lastRunState])return;
    NSURL *URL=self.pendingHostConnectionURL;void (^completion)(NSError *)=self.pendingHostConnectionCompletion;
    self.pendingHostConnectionURL=nil;self.pendingHostConnectionCompletion=nil;
    NSString *host=[VIMSession networkDetailsForMachine:self.machine][@"HostAddress"];
    if(!host.length||![URL.host isEqual:host]){
        completion(VIMSessionError(@"This Mac’s network address changed. Reopen File Sharing or Remote Login and try again."));return;
    }
    self.guestPending=YES;
    void (^finish)(NSError *)=^(NSError *error){self.guestPending=NO;[self refreshGuestStatus];completion(error);};
    [self.guestAgent connectWithCompletion:^(NSError *error){if(error){finish(error);return;}[self.guestAgent openHostConnection:URL completion:finish];}];
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
    if(machine.preMetalGuest) {
        // The native installer/bootstrap coordinator is created by launch.
        // Do not send these guests through the newer PVG preparation agent.
        if([machine.configuration[@"InstallState"] isEqual:@"installed"]) {
            if(error)*error=VIMSessionError(@"This Mac is already installed. Start it to check and repair its graphics support automatically.");return NO;
        }
        return [self openMachineAfterHostCheck:machine error:error];
    }
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
    NSString *installer=self.machine.configuration[@"InstallerMedia"];
    if(installer.length) {
        NSURL *URL=[self.machine.configuration[@"InstallerMediaShared"] boolValue]?
            [NSURL fileURLWithPath:installer]:[self.machine.URL URLByAppendingPathComponent:installer];
        if(![VIMInstallerAssembly validatePreparedMedia:URL guest:self.machine.guest error:error])return NO;
    }
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
    BOOL installerDisplay=self.machine.requiresInstallerDisplayForBoot||
        [NSProcessInfo.processInfo.arguments containsObject:@"--installer-display"];
    BOOL openGLSVGA=self.machine.preMetalGuest&&
        [self.machine.configuration[@"LegacyDisplayAdapter"] isEqual:@"vmware"]&&
        !self.machine.requiresStandardVGAForUpdate;
    BOOL openGLDirectDisplay=openGLSVGA&&self.machine.passthroughOpenGLEnabled;
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
    // Apply this to the new view, not the view from the previous launch.
    // VGA draws its cursor into the frame and cannot receive PVG power messages.
    self.displayView.softwareCursor=installerDisplay&&!openGLDirectDisplay;
    self.displayView.nearestNeighborScaling=[self.displayScalingFilter isEqual:@"nearest"];
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
    self.previewSetupNotice=[NSUserDefaults.standardUserDefaults boolForKey:@"VIMDebugShowSetupNotice"];
    self.closedSetupNoticeHeading=nil;
    if(self.previewSetupNotice)[self showSetupNoticeAutomatic:YES automaticLogin:YES restarting:NO];
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
    // External renderer: authenticate its exact PID before launch.
    // In-process OpenGL instead authenticates the spawned QEMU below,
    // while the guest remains paused. No arbitrary first-peer authentication.
    NSString *rendererPID=NSProcessInfo.processInfo.environment[@"VIM_OPENGL_RENDERER_PID"];
    NSString *inprocessLibrary=NSProcessInfo.processInfo.environment[@"VIM_PASSTHROUGH_OPENGL_INPROCESS_LIBRARY"];
    if(openGLDirectDisplay && !inprocessLibrary && !rendererPID.length) {
        NSString *bundled=[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"Runtime/bin/libVIMPassthroughOpenGLHost.dylib"];
        if([NSFileManager.defaultManager isReadableFileAtPath:bundled])inprocessLibrary=bundled;
    }
    BOOL openGLInprocess=openGLDirectDisplay && inprocessLibrary!=nil;
    if(openGLInprocess && (!inprocessLibrary.isAbsolutePath ||
        ![NSFileManager.defaultManager isReadableFileAtPath:inprocessLibrary])) {
        if(error)*error=VIMSessionError(@"The OpenGL graphics library is missing or unreadable.");
        [self cleanUp];return NO;
    }
    if(openGLDirectDisplay && !openGLInprocess && rendererPID.length) {
        int pid=0;NSScanner *scanner=[NSScanner scannerWithString:rendererPID];
        NSError *surfaceError=nil;
        if(![scanner scanInt:&pid] || !scanner.isAtEnd || pid<=0 ||
           !VIMPrepareOpenGLSurfaceEndpoint(self.displayConnection,pid,self.runtimeDirectory,&surfaceError)) {
            if(error)*error=surfaceError?:VIMSessionError(@"The OpenGL renderer could not be connected.");
            [self cleanUp];return NO;
        }
    }
    self.displayMode=installerDisplay?@"scaled":VIMDisplayMode(self.machine.configuration);self.retinaEnabled=![self.displayMode isEqual:@"scaled"];
    [content layoutSubtreeIfNeeded];
    CGFloat initialScale=[self.displayMode isEqual:@"retina"]?2:(self.retinaEnabled?self.window.backingScaleFactor:1);
    NSSize initialPixels=VIMRequestedGuestPixelSize(self.displayView.bounds.size,initialScale,
        self.machine.preMetalGuest,[self.displayMode isEqual:@"retina"],NO);
    // Queue before connection: both in-process PVG and the Rosetta helper
    // receive the window size as soon as their private display channel opens.
    if(!installerDisplay)[self.displayConnection requestWidth:initialPixels.width height:initialPixels.height
        density:self.machine.preMetalGuest?([self.displayMode isEqual:@"retina"]?2:1):0];
    NSString *qmp=[self.runtimeDirectory stringByAppendingPathComponent:@"control"];
    self.control=[[VIMQMPConnection alloc] initWithSocketPath:qmp];
    __weak VIMSession *eventSession=self;
    self.control.lifecycleEventHandler=^(NSDictionary *event){
        VIMSession *session=eventSession;if(!session)return;
        NSString *kind=event[@"event"];
        if([kind isEqual:@"DEVICE_DELETED"])[session.usbClient deviceWasDeleted:event[@"data"][@"device"] control:session.control];
        if([kind isEqual:@"DEVICE_DELETED"])for(NSUInteger slot=1;slot<VIMImageSlotCount;slot++)
            if([event[@"data"][@"device"] isEqual:[NSString stringWithFormat:@"image-usb-%lu",(unsigned long)slot]])[session.imageDevices removeObject:@(slot)];
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
            NSMutableDictionary *removed=[NSMutableDictionary new];
            if([identifier isEqual:VIMImageSlotDevice(0,session.insertedMediaFormat)]) {
                if(session.insertedMediaPath.length)removed[@0]=session.insertedMediaPath;
                session.insertedMediaPath=@"";session.insertedMediaFormat=nil;
            }
            for(NSUInteger slot=1;slot<VIMImageSlotCount;slot++)
                if([identifier isEqual:VIMImageSlotDevice(slot,@"raw")]||[identifier isEqual:VIMImageSlotDevice(slot,@"dmg")]) {
                    NSString *path=session.additionalMedia[@(slot)][@"Path"];
                    if(path.length)removed[@(slot)]=path;
                    [session.additionalMedia removeObjectForKey:@(slot)];
                }
            NSDictionary *settings=VIMImageClearedSelections(session.machine.editableConfiguration,removed);
            NSError *failure=nil;
            if(settings.count&&![session.machine saveSettings:settings error:&failure])VIMPresentError(failure,session.window);
            if(settings.count)[NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
        }
        NSData *json=[NSJSONSerialization dataWithJSONObject:event options:0 error:NULL];
        NSString *line=[[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding];
        [session.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] Lifecycle: %@\n",line] dataUsingEncoding:NSUTF8StringEncoding]];
    };
    self.guestAgent=[[VIMGuestAgent alloc] initWithSocketPath:[self.runtimeDirectory stringByAppendingPathComponent:@"guest-agent"]];
    if([@[@"mojave",@"catalina"] containsObject:self.machine.guest])
        self.guestAgent.recoverySocketPath=[self.runtimeDirectory stringByAppendingPathComponent:@"recovery-agent"];
    self.openGLDesktopMonitoring=NO;self.openGLSetupMediaAttached=NO;
    self.openGLLargeResizeReady=NO;
    self.mavericksSMBCheckedBoot=nil;
    self.guestAgent.preMetalGuest=self.machine.preMetalGuest;
    self.guestAgent.tigerGuest=[self.machine.guest isEqual:@"tiger"];
    if(self.machine.preMetalGuest)self.guestAgent.processPollInterval=1;
    if(self.machine.preMetalGuest){
        self.openGLSetup=[[VIMOpenGLSetup alloc] initWithMachine:self.machine agent:self.guestAgent control:self.control];
        self.openGLSetup.showProgress=^(BOOL automatic,BOOL automaticLogin){
            VIMSession *session=weakSelf;
            if(session.task.running&&!session.shutdownRequested)
                [session showSetupNoticeAutomatic:automatic automaticLogin:automaticLogin restarting:NO];
        };
        self.openGLSetup.logVerificationFailure=^(NSString *message){
            [weakSelf.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] %@\n",message]
                dataUsingEncoding:NSUTF8StringEncoding]];
        };
    }
    else {
        self.modernSetup=[[VIMModernSetup alloc] initWithMachine:self.machine agent:self.guestAgent];
        // Progress is only reported while Recovery runs the installer, which
        // can take most of the install time before any step completes.
        self.modernSetup.progress=^(NSString *message){weakSelf.guestStatus.stringValue=message;[weakSelf showRecoveryInstallNotice];};
        self.modernSetup.showAccountProgress=^(BOOL automaticLogin){
            VIMSession *session=weakSelf;
            if(!session.task.running||session.shutdownRequested)return;
            [session showSetupNoticeAutomatic:YES automaticLogin:automaticLogin restarting:NO];
        };
    }
    self.developmentSetupDeferred=[NSProcessInfo.processInfo.arguments containsObject:@"--development-defer-guest-setup"];
    NSString *statusTool=self.guestAgent.tigerGuest?[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"Runtime/OpenGLGuests/tiger/opengl-desktop-status"]:
        self.machine.preMetalGuest?[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:[NSString stringWithFormat:@"Runtime/OpenGLGuests/%@/opengl-desktop-status",self.machine.guest]]:root.length?[root stringByAppendingPathComponent:@"prebuilt/tools/guest-status"]:
        [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"Runtime/GuestTools/guest-status"];
    self.guestStatusTool=[NSData dataWithContentsOfFile:statusTool];
    NSDictionary *config=self.machine.configuration;
    self.shutdownRequested=NO;self.closeAfterShutdown=NO;
    BOOL classicInstaller=self.machine.preMetalGuest&&[config[@"InstallerMedia"] length]>0&&[config[@"InstallState"] isEqual:@"installing"];
    self.displayMode=(installerDisplay&&!openGLDirectDisplay)||classicInstaller?@"scaled":VIMDisplayMode(config);
    if(openGLDirectDisplay&&self.supportsRetinaDisplay&&!config[@"DisplayMode"]&&!config[@"RetinaEnabled"]&&self.window.backingScaleFactor<=1)
        self.displayMode=@"scaled";
    self.retinaEnabled=![self.displayMode isEqual:@"scaled"];
    self.observedDisplayMode=nil;
    self.networkLinkConnected=!config[@"NetworkConnected"]||[config[@"NetworkConnected"] boolValue];
    NSString *firmwareRoot=root.length?[root stringByAppendingPathComponent:@"Build/DevelopmentFirmware"]:
        [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"Runtime/Firmware"];
    NSString *commonFirmware=[firmwareRoot stringByAppendingPathComponent:@"Common"];
    BOOL packageResearchFirmware=self.machine.openGLResearchGuest&&![config[@"FirmwareSource"] isEqual:@"application"];
    firmwareRoot=[firmwareRoot stringByAppendingPathComponent:packageResearchFirmware?@"mojave":self.machine.guest];
    // One immutable EFI per OS. OpenCore reads these eight bytes once through
    // QEMU fw_cfg, after OpenCore's baseline and before macOS starts.
    // No generated EFI, copied template or separate policy variable in NVRAM.
    NSString *firmwareCode=[firmwareRoot stringByAppendingPathComponent:@"CODE.fd"];
    if(![NSFileManager.defaultManager fileExistsAtPath:firmwareCode])
        firmwareCode=[commonFirmware stringByAppendingPathComponent:@"CODE.fd"];
    NSString *firmwareEFI=[commonFirmware stringByAppendingPathComponent:@"EFI.img"];
    if(![NSFileManager.defaultManager fileExistsAtPath:firmwareEFI])
        firmwareEFI=[firmwareRoot stringByAppendingPathComponent:@"EFI.img"];
    BOOL preparedGL=[self.machine.guest isEqual:@"bigsur"]&&self.machine.hasPublishedGuestGraphics;
    BOOL signatureOnly=[VIMGuestBootPolicy readyAtURL:[self.machine.URL URLByAppendingPathComponent:@"State/BootPolicy.plist"]
        guest:self.machine.guest identifier:config[@"Identifier"]];
    NSString *bootPolicy=[NSString stringWithFormat:@"name=opt/org.virtualintelmac/boot-policy,string=VIM1%c%c00",
        signatureOnly?'S':'L',preparedGL?'1':'0'];
    // Old research guests retain their own known-compatible OpenCore and CODE
    // files. Never apply the supported guests' EFI boot policy or root patches.
    NSString *efiOverride=packageResearchFirmware?
        [self.machine.URL.path stringByAppendingPathComponent:config[@"BootMedia"]]:nil;
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
    for(NSString *file in @[firmwareCode,efiOverride?:firmwareEFI])if(![NSFileManager.defaultManager fileExistsAtPath:file]) {
        if(error)*error=VIMSessionError(@"This app is missing its firmware. Rebuild or reinstall the complete app.");
        [self cleanUp];return NO;
    }
    NSString *(^path)(NSString *)=^NSString *(NSString *key){
        if([key isEqual:@"Firmware"])return VIMOptionPath(packageResearchFirmware?
            [self.machine.URL.path stringByAppendingPathComponent:config[@"Firmware"]]:
            firmwareCode);
        if([key isEqual:@"BootMedia"])return VIMOptionPath(efiOverride?:firmwareEFI);
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
    // Use one virtual timestamp rate on ARM. At the default 1 GHz, early
    // XNU releases can wrap their nanotime multiplier or have no matching
    // commpage routine. This is not an instruction-speed limit. Intel HVF
    // keeps its hardware counter.
    if(appleSilicon)
        cpu=[cpu stringByAppendingString:@",tsc-frequency=1200000000"];
    // Mavericks rejects newer Intel models in cpuid_set_info before drivers
    // start (verified "Unsupported CPU" on the Ice Lake HVF host). Keep the
    // same supported Penryn identity as its TCG path; HVF still executes guest
    // instructions natively. Do not change supported production guests.
    if(!appleSilicon&&!self.machine.preMetalGuest)cpu=mojave?@"host,model=94":@"host";
    // Explicit research comparison only: Mavericks knows Haswell, unlike
    // this host's newer CPUID model. Preserve the normal guest and TCG paths.
    NSUInteger openGLCPUOption=[launchArguments indexOfObject:@"--opengl-cpu-model"];
    if(openGLCPUOption!=NSNotFound) {
        NSString *model=openGLCPUOption+1<launchArguments.count?launchArguments[openGLCPUOption+1]:@"";
        if(appleSilicon||!self.machine.openGLResearchGuest||![@[@"penryn",@"haswell"] containsObject:model]) {
            if(error)*error=VIMSessionError(@"--opengl-cpu-model requires an Intel-host research Mac and penryn or haswell. The Mac was not started.");
            [self cleanUp];return NO;
        }
        if([model isEqual:@"haswell"])cpu=@"Haswell,vendor=GenuineIntel,+pcid,-hle,-rtm";
        [self.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] Research CPU comparison: %@\n",model] dataUsingEncoding:NSUTF8StringEncoding]];
    }
    NSString *sataDisk=@"ide-hd,bus=ide.1,drive=macos,bootindex=2,rotation_rate=1";
    NSString *nvmeDisk=@"nvme,drive=macos,serial=VIM00000001,bus=pcie.0,addr=0x8,bootindex=2";
    // Big Sur on ARM TCG can hand off to XNU over SATA and then wait
    // indefinitely for its root volume. The same installed disk boots over
    // native NVMe. Keep the controller stable across later update boots;
    // leave Intel, earlier systems and ongoing installations on SATA.
    BOOL installedBigSur=appleSilicon&&[self.machine.guest isEqual:@"bigsur"]&&
        [@[@"installed",@"imported-prepared"] containsObject:config[@"InstallState"]?:@""];
    NSString *systemDiskDevice=installedBigSur?nvmeDisk:sataDisk;
    NSUInteger diskOption=[launchArguments indexOfObject:@"--system-disk-interface"];
    if(diskOption!=NSNotFound) {
        NSString *interface=diskOption+1<launchArguments.count?launchArguments[diskOption+1]:@"";
        if(![@[@"sata",@"nvme"] containsObject:interface]) {
            if(error)*error=VIMSessionError(@"--system-disk-interface requires sata or nvme. The Mac was not started.");
            [self cleanUp];return NO;
        }
        systemDiskDevice=[interface isEqual:@"nvme"]?nvmeDisk:sataDisk;
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
    // Explicit development isolation: keep the production devices, memory and
    // display path while enabling TCG's guest watchpoints on an Intel builder.
    BOOL developmentTCG=[NSProcessInfo.processInfo.arguments containsObject:@"--development-tcg"];
    BOOL earlyAppleXHCI=[@[@"lion",@"mountainlion",@"mavericks",@"yosemite"] containsObject:self.machine.guest];
    NSString *xhciDevice=@"qemu-xhci,id=xhci,bus=pcie.0,addr=0x7";
    // El Capitan binds the 64-bit BAR, but its MSI-X path delays physical
    // storage completions. Classic MSI also serves the earlier driver.
    if(earlyAppleXHCI||[self.machine.guest isEqual:@"elcapitan"])
        xhciDevice=[xhciDevice stringByAppendingString:@",msi=on,msix=off,conditional-intr-mapping=on"];
    // Only the early AppleUSBXHCI stack needs a low BAR and bulk-ring help.
    if(earlyAppleXHCI)xhciDevice=[xhciDevice stringByAppendingString:@",x-bar-32bit=on,x-bulk-event-data=on"];
    NSMutableArray *baseArguments=[NSMutableArray arrayWithArray:@[@"-name",VIMOptionPath(self.machine.name),@"-L",bios,@"-machine",@"q35,i8042=off",@"-accel",(appleSilicon||developmentTCG)?@"tcg,thread=multi":@"hvf",
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
        // Darwin9's EHCI uses legacy IRQ10. Keep it off VGA's PIRQ F,
        // whose PIC alias is deliberately disabled after _PIC(1). Slot5
        // routes EHCI INTD to PIRQ E instead; newer guests retain their map.
        @"-device",[self.machine.guest isEqual:@"leopard"]?@"ich9-usb-ehci1,id=usb,bus=pcie.0,addr=0x5":@"ich9-usb-ehci1,id=usb",@"-device",@"usb-kbd,id=keyboard,bus=usb.0,port=1,apple-ansi=on",
        // Second, independent controller: usb.0 (EHCI1) already allocates all
        // 6 ports (keyboard, tablet, the primary image slot, three more for
        // user-connected images). The GPU driver support disc needs its own
        // hot-plugged usb-bot/scsi-hd pair (see VIMOpenGLSetup.m) and there is
        // no free port left on usb.0 for it; pre-Metal guests have no xHCI
        // driver to see a device on the USB3 passthrough bus either. A real
        // ICH9 southbridge has two distinct EHCI controllers, so this mirrors
        // actual hardware rather than inventing a fictitious duplicate.
        @"-device",@"ich9-usb-ehci2,id=usb2",
        // USB passthrough bus (USB 2 and 3). A fixed slot after the network
        // adapter's leaves existing device addresses and services unchanged.
        @"-device",xhciDevice,
        @"-device",@"usb-tablet,id=tablet,bus=usb.0,port=2",
        // A second, relative-motion pointer alongside the absolute tablet.
        // Many classic Mac games read the generic USB HID mouse class and
        // treat whatever value arrives in its X/Y fields as a motion delta;
        // fed the tablet's large absolute coordinates (0-32767) instead of
        // small per-report deltas, that misreading looks like wildly
        // oversensitive mouse movement. A true relative mouse alongside the
        // tablet costs nothing for normal GUI use (both can be connected to
        // a real Mac at once) and gives those games correct deltas. Port 1
        // on usb2.0 stays reserved for the GPU driver disc's own hot-plugged
        // usb-bot (see VIMOpenGLSetup.m); this is always present, port 2.
        @"-device",@"usb-mouse,id=relative-mouse,bus=usb2.0,port=2",
        // Apple's Intel guest agent opens this exact VirtIO console name.
        // Keep its control endpoint inside the per-session private directory;
        // unlike network forwarding it is never exposed on a TCP interface.
        @"-device",@"virtio-serial-pci,id=guestserial",
        @"-chardev",[NSString stringWithFormat:@"socket,id=guestagent,path=%@,server=on,wait=off",
            VIMOptionPath([self.runtimeDirectory stringByAppendingPathComponent:@"guest-agent"])],
        // Older systems have Apple16X50Serial but no AppleVirtIOConsole.
        // Keep metadata local and on the same app-owned QGA socket. Graphics
        // and bulk support-media delivery never travel over this slow UART.
        @"-device",self.machine.preMetalGuest?
            @"isa-serial,chardev=guestagent,index=1":
            @"virtserialport,bus=guestserial.0,chardev=guestagent,name=org.qemu.guest_agent.0",
        // A distinct channel, never another client of the guest-agent socket.
        // Present it even when sharing is off so enabling it needs no restart.
        @"-chardev",[NSString stringWithFormat:@"socket,id=clipboard,path=%@,server=on,wait=off",
            VIMOptionPath([self.runtimeDirectory stringByAppendingPathComponent:@"clipboard"])],
        @"-device",self.machine.preMetalGuest?@"isa-serial,chardev=clipboard,index=0":@"virtserialport,bus=guestserial.0,chardev=clipboard,name=org.vim.clipboard",
        // Old donated framebuffer drivers do not report DPMS to host PVG.
        // Dedicated metadata events remain available with clipboard disabled.
        @"-chardev",[NSString stringWithFormat:@"socket,id=displaypower,path=%@,server=on,wait=off",
            VIMOptionPath([self.runtimeDirectory stringByAppendingPathComponent:@"displaypower"])],
        @"-device",@"virtserialport,bus=guestserial.0,chardev=displaypower,name=org.vim.display"]];
    if([@[@"mojave",@"catalina"] containsObject:self.machine.guest]) {
        // These Recovery images ship Apple16X50Serial but not Apple's QGA.
        // Installed macOS keeps its native VirtIO channel; only the Recovery
        // service opens COM2. No graphics or payload archive uses this UART.
        [baseArguments addObjectsFromArray:@[@"-chardev",[NSString stringWithFormat:
            @"socket,id=recoveryagent,path=%@,server=on,wait=off",
            VIMOptionPath([self.runtimeDirectory stringByAppendingPathComponent:@"recovery-agent"])],
            @"-device",@"isa-serial,chardev=recoveryagent,index=1"]];
    }
    // Native Tiger's 16X50 driver requires the dedicated ISA edge/high
    // contract, not upstream AML's shared level interrupt. Repeated native
    // boots and verified bulk/control traffic qualify this Tiger-only path.
    if([self.machine.guest isEqual:@"tiger"])
        [baseArguments addObjectsFromArray:@[@"-global",@"isa-serial.acpi-irq-edge=on"]];
    if(!efiOverride&&!self.machine.preMetalGuest)[baseArguments addObjectsFromArray:@[@"-fw_cfg",bootPolicy]];
    // Boot-time only; older per-OS firmware ignores this named item. The
    // unified image uses it before loading the unchanged OpenCore profile.
    [baseArguments addObjectsFromArray:@[@"-fw_cfg",[NSString stringWithFormat:
        @"name=opt/org.virtualintelmac/firmware-profile,string=%@",self.machine.guest]]];
    if(!installerDisplay) {
        // The existing QEMU property supplies the initial native display mode;
        // no fixed 1920-wide default or wait for the desktop/guest tool.
        NSDictionary *graphics=@{@"driver":@"apple-gfx-pci",@"display-modes":@[
            [NSString stringWithFormat:@"%lux%lu@60",(unsigned long)initialPixels.width,(unsigned long)initialPixels.height]]};
        NSData *json=[NSJSONSerialization dataWithJSONObject:graphics options:0 error:NULL];
        baseArguments[[baseArguments indexOfObject:@"apple-gfx-pci"]]=[[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding];
    }
    if([self.machine.guest isEqual:@"elcapitan"]) {
        // Actual15G31 recovery relocates the low halves of 64-bit PCI BARs
        // below4GB but leaves OVMF's high dwords in the device. Its page table
        // maps XHCI at0x88004000 while the device decodes0x888004000; the first
        // capability read returns all ones and Apple's unplug path panics.
        // OVMF's native zero-aperture policy allocates the complete BAR below
        //4GB up front. Keep USB3 and native drivers, not an Apple kext patch.
        [baseArguments addObjectsFromArray:@[@"-fw_cfg",@"name=opt/ovmf/X-PciMmio64Mb,string=0"]];
    }
    // SDL opens the capture device synchronously during QEMU initialization.
    // Do not let a pending host microphone permission prevent even QMP from
    // starting. The existing Microphone settings action can grant access for
    // a subsequent boot; retain the user's saved routing and enable playback.
    NSString *audioInput=VIMAudioSelection(config,YES);
    if(![audioInput isEqual:@"disabled"]&&
       [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]!=AVAuthorizationStatusAuthorized)
        audioInput=@"disabled";
    if(!config[@"AudioEnabled"]||[config[@"AudioEnabled"] boolValue]) {
        NSMutableString *audio=[NSMutableString stringWithString:@"sdl,id=audio0"];
        // QEMU otherwise fixes its mixer at 44.1 kHz even on a 48 kHz host.
        // Match the selected output's current rate: avoid a second converter
        // after guest mixing, without changing the host device or queue size.
        [audio appendFormat:@",out.frequency=%lu",(unsigned long)VIMHostOutputSampleRate(VIMAudioSelection(config,NO))];
        // Capture also follows its selected physical device, independently of
        // output. Avoid SDL's native-input -> 44.1 kHz -> guest-rate chain.
        [audio appendFormat:@",in.frequency=%lu",(unsigned long)VIMHostInputSampleRate(audioInput)];
        if([audioInput isEqual:@"disabled"])[audio appendString:@",in.voices=0"];
        if([VIMAudioSelection(config,NO) isEqual:@"disabled"])[audio appendString:@",out.voices=0"];
        [baseArguments addObjectsFromArray:@[@"-audiodev",audio]];
        // Keep this in the original PCI-device order so existing network
        // service associations are not disturbed by the sound implementation.
        // Big Sur ships AppleVirtIOSound (IOVirtIOPrimaryMatch 0x00191af4).
        if([self.machine.guest isEqual:@"bigsur"])
            [baseArguments addObjectsFromArray:@[@"-device",@"virtio-sound-pci,audiodev=audio0,x-apple-reconfigure=on"]];
        else [baseArguments addObjectsFromArray:@[@"-device",@"intel-hda,id=hda",@"-device",@"hda-duplex,bus=hda.0,audiodev=audio0"]];
        // Explicit development-only cold-codec trace through the normal app
        // launch. No QEMU debugger or additional control connection is needed.
        if(![self.machine.guest isEqual:@"bigsur"]&&[NSProcessInfo.processInfo.arguments containsObject:@"--trace-hda-codec"])
            [baseArguments addObjectsFromArray:@[@"-global",@"intel-hda.debug=2",@"-global",@"hda-duplex.debug=2"]];
    }
    if(!efiOverride&&[@[@"snowleopard",@"lion",@"mountainlion",@"mavericks",@"yosemite",
                        @"elcapitan",@"sierra",@"highsierra"] containsObject:self.machine.guest]) {
        // The bundled MacPro5,1 profiles preserve the virtual DIMM records.
        // Report a period-appropriate nominal speed in SMBIOS, not a host
        // bandwidth claim. Capacity, memory timing and the TSC are unchanged.
        [baseArguments addObjectsFromArray:@[@"-smbios",@"type=17,speed=1066"]];
    }
    if(!efiOverride&&self.machine.preMetalGuest) {
        // Older bundled profiles leave the serial empty. Supply a clearly
        // virtual, stable identifier without borrowing the host's identity
        // or changing the firmware profile, hardware UUID, or board ID.
        NSUUID *identifier=[[NSUUID alloc] initWithUUIDString:config[@"Identifier"]];
        if(identifier) {
            NSString *uuid=identifier.UUIDString;
            NSString *serial=[uuid substringFromIndex:uuid.length-12];
            [baseArguments addObjectsFromArray:@[@"-smbios",[NSString stringWithFormat:@"type=1,serial=%@",serial]]];
        }
    }
    [baseArguments addObjectsFromArray:@[
        @"-device",@"isa-applesmc,osk=ourhardworkbythesewordsguardedpleasedontsteal(c)AppleComputerInc",@"-smbios",@"type=2",
        @"-drive",[NSString stringWithFormat:@"if=pflash,format=raw,readonly=on,file=%@",path(@"Firmware")],
        @"-drive",[NSString stringWithFormat:@"if=pflash,format=raw,file=%@",path(@"NVRAM")],
        @"-drive",[NSString stringWithFormat:@"id=opencore,if=none,format=raw,%@,file=%@",
            @"snapshot=on",path(@"BootMedia")],
        // OVMF has NvmExpressDxe, while native Tiger has no NVMe block driver.
        // Keep the bootloader disk available to firmware, not Disk Arbitration:
        // an ESP-only IDE disk otherwise races Tiger's unreadable-media dialog
        // before its guest agent can register a per-disk no-mount policy.
        // The installed OS disk stays on the same native IDE controller.
        @"-device",[self.machine.guest isEqual:@"tiger"]?
            @"nvme,bus=pcie.0,addr=0x8,drive=opencore,bootindex=1,serial=VIMFirmwareEFI":
            @"ide-hd,bus=ide.0,drive=opencore,bootindex=1,rotation_rate=1",
        @"-drive",[NSString stringWithFormat:@"id=macos,if=none,format=qcow2,file=%@",path(@"Disk")],
        @"-device",systemDiskDevice,
        // On q35 the auto-assigned slot 5 shares INTx with EHCI (IRQ 21).
        // Apple's 8254X filter can consume the masked initial LSC while
        // servicing that unrelated USB interrupt, leaving en0 inactive.
        // Reserve slot 6 / IRQ 22: no emulated-register or guest-driver patch,
        // link reset, or delayed boot-time reconnect is necessary.
        @"-device",[@"e1000-82545em,id=ethernet,bus=pcie.0,addr=0x6,netdev=net0,mac=" stringByAppendingString:mac],
        @"-qmp",[NSString stringWithFormat:@"unix:%@,server=on,wait=off",qmp],
        @"-serial",self.machine.preMetalGuest?@"none":[NSString stringWithFormat:@"file:%@",[self.machine.URL.path stringByAppendingPathComponent:@"State/serial.log"]],
        @"-monitor",@"none",@"-action",@"reboot=shutdown"]];
    [baseArguments addObjectsFromArray:@[@"-netdev",network]];
    // Establish an unplugged cable before executing any guest instruction.
    // Keep the backend available so reconnecting never needs a guest reboot.
    if(openGLInprocess || (config[@"NetworkConnected"]&&![config[@"NetworkConnected"] boolValue]))[baseArguments addObject:@"-S"];
    NSMutableArray *mediaArguments=baseArguments;
    // Explicit development diagnosis only: capture boot-time PHY accesses
    // through QEMU's disabled-by-default tracing, not a second QMP client.
    if([NSProcessInfo.processInfo.arguments containsObject:@"--trace-network"])
        [mediaArguments addObjectsFromArray:@[@"-trace",@"enable=e1000_*",@"-trace",@"enable=e1000x_link_negotiation_*"]];
    if([NSProcessInfo.processInfo.arguments containsObject:@"--trace-cursor"])
        [mediaArguments addObjectsFromArray:@[@"-trace",@"enable=vmware_cursor_define"]];
    // Explicit development diagnosis of AHCI stalls: command issue/completion
    // and interrupt delivery only, written to its own file, not session.log.
    if([NSProcessInfo.processInfo.arguments containsObject:@"--trace-storage"]) {
        for(NSString *event in @[@"process_ncq_command",@"ncq_finish",@"execute_ncq_command_*",@"ahci_cmd_done",
                                 @"ahci_start_dma",@"ahci_trigger_irq",@"ahci_check_irq",@"ahci_irq_*",
                                 @"ahci_reset",@"ahci_reset_port",@"handle_cmd_busy",@"ahci_dma_prepare_buf_fail",
                                 @"ahci_populate_sglist_*",@"handle_cmd_bad*"])
            [mediaArguments addObjectsFromArray:@[@"-trace",[@"enable=" stringByAppendingString:event]]];
        [mediaArguments addObjectsFromArray:@[@"-D",[self.machine.URL.path stringByAppendingPathComponent:@"State/storage-trace.log"]]];
    }
    if([NSProcessInfo.processInfo.arguments containsObject:@"--trace-image-enumeration"])
        [mediaArguments addObjectsFromArray:@[@"-trace",@"enable=usb_msd_maxlun"]];
    if([NSProcessInfo.processInfo.arguments containsObject:@"--firmware-diagnostics"])
        [mediaArguments addObjectsFromArray:@[@"-debugcon",[@"file:" stringByAppendingString:[self.machine.URL.path stringByAppendingPathComponent:@"State/firmware.log"]],@"-global",@"isa-debugcon.iobase=0x402"]];
    // Kernel debugging for the opt-in legacy harness, bound to loopback only.
    // Does not open a second QMP owner or expose debug access in normal VMs.
    NSInteger openGLGDB=[config[@"LegacyGDBPort"] integerValue];
    if(self.machine.openGLResearchGuest&&openGLGDB>0&&openGLGDB<=65535)
        [mediaArguments addObjectsFromArray:@[@"-gdb",[NSString stringWithFormat:@"tcp:127.0.0.1:%ld",(long)openGLGDB]]];
    // Independent read-only support media: never take over the user's CD/DVD
    // or the macOS installer. Insert only during setup and eject after copying.
    [mediaArguments addObjectsFromArray:@[@"-drive",@"id=setup-support,if=none,media=cdrom,readonly=on",@"-device",@"ide-cd,id=setup-support-device,bus=ide.4,drive=setup-support"]];
    NSString *optical=config[@"OpticalMedia"];
    NSError *opticalFailure=nil;
    NSString *opticalFormat=optical.length?VIMOpticalImageFormat(optical,&opticalFailure):nil;
    NSMutableDictionary<NSNumber *,NSString *> *unavailableImages=[NSMutableDictionary new];
    if(optical.length&&!opticalFormat) {
        // Removable media can disappear when a library moves to another Mac.
        // Boot with an empty drive and forget the unavailable optional image
        // below. This never applies to the system disk or required installer.
        if([opticalFailure.domain isEqual:NSPOSIXErrorDomain]&&(opticalFailure.code==ENOENT||opticalFailure.code==ENOTDIR)) {
            unavailableImages[@0]=optical;optical=nil;
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
    [mediaArguments addObjectsFromArray:@[@"-drive",imageDrive,@"-device",[NSString stringWithFormat:@"usb-bot,id=image-usb,bus=%@,port=%lu",VIMImageUSBBus(self.machine.guest),(unsigned long)VIMImageUSBPort(self.machine.guest,0)],@"-device",@"scsi-hd,id=image-device,bus=image-usb.0,drive=removable-image,removable=on,logical_block_size=512,physical_block_size=512"]];
    self.insertedMediaPath=optical?:@"";self.insertedMediaFormat=opticalFormat;
    self.additionalMedia=[NSMutableDictionary new];
    self.imageDevices=[NSMutableSet new];
    for(NSUInteger slot=1;slot<VIMImageSlotCount;slot++) {
        NSString *path=VIMImageSlotPath(config,slot);NSError *failure=nil;
        NSString *format=path.length?VIMOpticalImageFormat(path,&failure):nil;
        if(failure) {
            if([failure.domain isEqual:NSPOSIXErrorDomain]&&(failure.code==ENOENT||failure.code==ENOTDIR)) {
                unavailableImages[@(slot)]=path;path=@"";
            }
            else {if(error)*error=VIMFriendlyDiscImageError(path,failure);[self cleanUp];return NO;}
        }
        if(path.length)self.additionalMedia[@(slot)]=@{@"Path":path,@"Format":format};
        NSString *backend=[NSString stringWithFormat:@"image-backend-%lu",(unsigned long)slot];
        NSString *drive=[NSString stringWithFormat:@"id=%@,if=none,media=disk,readonly=on%@",backend,
            path.length?[NSString stringWithFormat:@",format=%@,file=%@",format,VIMOptionPath(path)]:@""];
        if(path.length) {
            [mediaArguments addObjectsFromArray:@[@"-drive",drive]];
            NSString *transport=[NSString stringWithFormat:@"image-usb-%lu",(unsigned long)slot];
            NSString *device=[NSString stringWithFormat:@"scsi-hd,id=%@,bus=%@.0,drive=%@,removable=on,logical_block_size=512,physical_block_size=512",
                VIMImageSlotDevice(slot,format),transport,backend];
            [mediaArguments addObjectsFromArray:@[@"-device",[NSString stringWithFormat:@"usb-bot,id=%@,bus=%@,port=%lu",transport,VIMImageUSBBus(self.machine.guest),(unsigned long)VIMImageUSBPort(self.machine.guest,slot)],@"-device",device]];
            [self.imageDevices addObject:@(slot)];
        }
    }
    if(self.machine.preMetalGuest&&config[@"InstallerMedia"]) {
        NSError *setupError=nil;NSURL *policy=[VIMAutomaticSetup configurationMediaForMachine:self.machine error:&setupError];
        if(setupError){if(error)*error=setupError;[self cleanUp];return NO;}
        self.openGLSetupMediaAttached=policy!=nil;
        if(policy)[mediaArguments addObjectsFromArray:@[@"-drive",[NSString stringWithFormat:@"id=opengl-setup,if=none,media=cdrom,format=raw,readonly=on,file=%@",VIMOptionPath(policy.path)],
            @"-device",@"ide-cd,id=opengl-setup-device,bus=ide.5,drive=opengl-setup"]];
    }
    self.task.arguments=mediaArguments;
    if(installerDisplay || config[@"InstallerMedia"]) {
        NSMutableArray *arguments=[self.task.arguments mutableCopy];
        if(installerDisplay) {
            NSUInteger vga=[arguments indexOfObject:@"-vga"];
            arguments[vga+1]=openGLSVGA?@"vmware":@"std";
            if(openGLDirectDisplay) {
                [arguments addObjectsFromArray:@[@"-global",@"vmware-svga.passthrough-opengl-bridge=on",
                    @"-global",@"vmware-svga.vgamem_mb=512",
                    // This research layout's SVGA/EHCI use native GSI21.
                    // Remove only their duplicate PIC fanout after _PIC(1),
                    // preserving SATA's real legacy route and boot PIC mode.
                    @"-global",@"ICH9-LPC.x-apic-pirq-f-native-only=on"]];
                // This installed research guest uses the installer display
                // transport, not installer display semantics. A real panel
                // contract lets Mavericks choose HiDPI without a preference.
                NSString *mode=self.displayMode;
                BOOL explicitMode=config[@"DisplayMode"]!=nil||config[@"RetinaEnabled"]!=nil;
                BOOL retina=[mode isEqual:@"retina"]&&(explicitMode||self.window.backingScaleFactor>1);
                // Panel capability is hardware, not the current UI choice.
                // Booting Scaled must not turn a Retina host into a low-DPI
                // physical panel: 10.8 then omits every native 2x mode and
                // cannot switch back to Retina without another boot. Keep
                // both choices available; guest-display selects the requested
                // 1x/2x mode through the existing production path afterward.
                BOOL retinaPanel=self.supportsRetinaDisplay&&(retina||self.window.backingScaleFactor>1);
                [arguments addObjectsFromArray:@[@"-global",[NSString stringWithFormat:@"vmware-svga.passthrough-opengl-panel-width=%u",retinaPanel?2560:1280],
                    @"-global",[NSString stringWithFormat:@"vmware-svga.passthrough-opengl-panel-height=%u",retinaPanel?1600:800],
                    @"-global",[NSString stringWithFormat:@"vmware-svga.passthrough-opengl-ui-scale=%u",retinaPanel?2:1]]];
            }
            if(!openGLSVGA)
                [arguments addObjectsFromArray:@[@"-global",@"VGA.xres=1280",@"-global",@"VGA.yres=800"]];
            NSUInteger pvg=[arguments indexOfObject:@"apple-gfx-pci"];
            [arguments removeObjectsInRange:NSMakeRange(pvg-1,2)];
        }
        // After startosinstall staged the install, attached installer media
        // gives OpenCore a Recovery entry to fall back to, and it sometimes
        // does, despite efi-boot-device selecting the staged installer.
        // Leave the media off those boots; Recovery is no longer needed.
        NSDictionary *nativeSetup=[NSDictionary dictionaryWithContentsOfURL:[self.machine.URL URLByAppendingPathComponent:@"State/NativeSetup.plist"]];
        BOOL stagedInstall=!self.machine.preMetalGuest&&[nativeSetup[@"InstallDispatched"] isEqual:@YES]&&
            VIMFirmwareHasStagedInstallAtURL([self.machine.URL URLByAppendingPathComponent:config[@"NVRAM"]]);
        if(stagedInstall)[self.log writeData:[@"\n[Virtual Intel Mac] Staged macOS install: installer media left off this boot.\n" dataUsingEncoding:NSUTF8StringEncoding]];
        if(config[@"InstallerMedia"]&&!stagedInstall) [arguments addObjectsFromArray:@[@"-drive",[NSString stringWithFormat:@"id=installer,if=none,format=raw,%@,file=%@",
            [config[@"InstallerMediaKind"] isEqual:@"disk"]?@"snapshot=on":@"readonly=on",path(@"InstallerMedia")],
            @"-device",[config[@"InstallerMediaKind"] isEqual:@"disk"]?
                @"ide-hd,bus=ide.2,unit=0,drive=installer,bootindex=3,rotation_rate=1":
                @"ide-cd,bus=ide.2,unit=0,drive=installer,bootindex=3"]];
        self.task.arguments=arguments;
    }
    NSMutableDictionary *environment=[NSProcessInfo.processInfo.environment mutableCopy];
    if(openGLInprocess) {
        environment[@"VIM_PASSTHROUGH_OPENGL_INPROCESS_LIBRARY"]=inprocessLibrary;
        if(!environment[@"VIM_PASSTHROUGH_OPENGL_INLINE"])environment[@"VIM_PASSTHROUGH_OPENGL_INLINE"]=@"1";
        if(!environment[@"VIM_PASSTHROUGH_OPENGL_SURFACE_IMPORT"])environment[@"VIM_PASSTHROUGH_OPENGL_SURFACE_IMPORT"]=@"1";
        if(!environment[@"VIM_DISPLAY_EVENT_DRIVEN"])environment[@"VIM_DISPLAY_EVENT_DRIVEN"]=@"1";
        if(![environment[@"VIM_OPENGL_VRAM_SERVICE"] length])
            environment[@"VIM_OPENGL_VRAM_SERVICE"]=[@"com.mac.virtualintelmac.ram." stringByAppendingString:NSUUID.UUID.UUIDString];
        if(![environment[@"VIM_OPENGL_VRAM_ENDPOINT"] length])
            environment[@"VIM_OPENGL_VRAM_ENDPOINT"]=[self.runtimeDirectory stringByAppendingPathComponent:@"vram-endpoint.plist"];
        if(![environment[@"VIM_OPENGL_DISPLAY_ENDPOINT"] length])
            environment[@"VIM_OPENGL_DISPLAY_ENDPOINT"]=[self.runtimeDirectory stringByAppendingPathComponent:@"graphics-endpoint.plist"];
    }
    // Never inherit a developer's routing override into an unrelated VM.
    [environment removeObjectForKey:@"VIM_AUDIO_OUTPUT_DEVICE"];
    [environment removeObjectForKey:@"VIM_AUDIO_INPUT_DEVICE"];
    for(NSNumber *input in @[@NO,@YES]) {
        NSString *uid=input.boolValue?audioInput:VIMAudioSelection(config,NO);
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
    [environment removeObjectForKey:@"VIM_DISPLAY_REFRESH_MS"];
    [environment addEntriesFromDictionary:@{@"PVG_OOP_HELPER":helper,@"PVG_OOP_DONOR":donor,
        @"PVG_APP_DISPLAY_SOCKET":self.displayConnection.socketPath,@"PVG_TCG_TSO":@"0",
        @"PVG_TCG_LAZY_JMP_CACHE":@"1",@"PVG_TCG_KEEP_PHYS_TLB":@"1",@"PVG_TCG_INLINE_JMP_CACHE":@"1",
        @"PVG_TCG_NATIVE_FP_FLAGS":@"0",@"PVG_TCG_NATIVE_PACKED_FP":@"1",@"PVG_TCG_NATIVE_EXACT_FP":@"1",
        // Panel capability, not the current logical mode. Creating a1x-only
        // PVG panel prevents macOS from advertising any2x mode for a later
        // live Retina selection. The guest-display tool selects1x or2x.
        @"PVG_OOP_SCALE_FACTOR":@"2",@"PVG_APP_DISPLAY_SCALE":@"2",
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
        if(openGLDirectDisplay) {
            // The ordinary embedded display defaults to a 30-ms poll. That
            // caps the accelerated research scanout at ~33 Hz before any
            // rendering/ACK cost. This does not change supported guest paths.
            NSInteger interval=[NSUserDefaults.standardUserDefaults integerForKey:@"VIMLegacyDisplayRefreshMS"];
            environment[@"VIM_DISPLAY_REFRESH_MS"]=[NSString stringWithFormat:@"%ld",(long)(interval>=5&&interval<=1000?interval:16)];
        }
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
        NSURL *waitingURL=relaunch?session.pendingHostConnectionURL:nil;
        void (^waitingCompletion)(NSError *)=relaunch?session.pendingHostConnectionCompletion:nil;
        if(relaunch){session.pendingHostConnectionURL=nil;session.pendingHostConnectionCompletion=nil;}
        session.shutdownButton.enabled=NO;[session cleanUp];
        if(!relaunch&&task.terminationReason==NSTaskTerminationReasonExit&&task.terminationStatus==0)[session showStoppedScreen];
        if(rebootLoop)VIMPresentError(VIMSessionError(@"This Mac restarted repeatedly and was stopped. Open Session Details to check its boot log, then try starting it again."),session.window);
        if(session.closeAfterShutdown&&task.terminationReason==NSTaskTerminationReasonExit&&task.terminationStatus==0)[session.window close];
        // Successful openMachine publishes its own library notification once
        // the replacement owns the session lock. Do not invite background
        // maintenance into the gap between cleanup and that acquisition.
        if(!relaunch)[NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
        if(relaunch) {
            NSError *failure=nil;
            VIMMachine *fresh=[VIMMachine loadURL:session.machine.URL error:&failure];
            NSWindow *previousWindow=session.window;NSRect previousFrame=previousWindow.frame;
            // The new session chooses its own safe display hardware. A valid
            // setup/privacy restart must also work before graphics publishing;
            // requiring acceleration here strands an otherwise bootable VM.
            if(fresh&&[VIMSession openMachine:fresh error:&failure]) {
                VIMSession *replacement=sessions[SessionKey(fresh)];
                replacement.pendingHostConnectionURL=waitingURL;replacement.pendingHostConnectionCompletion=waitingCompletion;
                if(guestReboot)replacement.recentGuestReboots=recent;
                [replacement.window setFrame:previousFrame display:YES];
                if(replacement.window!=previousWindow)[previousWindow close];
            } else {
                session.guestStatus.stringValue=failure.localizedDescription?:@"Restart paused: graphics installation needs recovery.";
                [NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
                if(waitingCompletion)waitingCompletion(failure?:VIMSessionError(@"The virtual Mac couldn’t restart to open the connection."));
            }
        }
        }];
    });};
    if(![self.task launchAndReturnError:error]) { [self cleanUp];return NO; }
    if(openGLInprocess) {
        rendererPID=[NSString stringWithFormat:@"%d",self.task.processIdentifier];
        if(!VIMPrepareOpenGLSurfaceEndpoint(self.displayConnection,self.task.processIdentifier,self.runtimeDirectory,error)) {
            [self.task terminate];[self cleanUp];return NO;
        }
    }
    if(openGLDirectDisplay) {
        NSDictionary *env=self.task.environment;
        NSString *service=env[@"VIM_OPENGL_VRAM_SERVICE"],*endpoint=env[@"VIM_OPENGL_VRAM_ENDPOINT"];
        int pid=0;NSScanner *scanner=[NSScanner scannerWithString:rendererPID?:@""];
        if(service.length && endpoint.length && [scanner scanInt:&pid] && scanner.isAtEnd && pid>0) {
            NSDictionary *attributes=[NSFileManager.defaultManager attributesOfItemAtPath:endpoint.stringByDeletingLastPathComponent error:NULL];
            BOOL privateDirectory=[attributes[NSFileType] isEqual:NSFileTypeDirectory] &&
                [attributes[NSFileOwnerAccountID] unsignedIntValue]==getuid() &&
                !([attributes[NSFilePosixPermissions] unsignedIntValue]&0077);
            NSDictionary *description=@{@"Version":@1,@"Service":service,@"RendererPID":@(pid),@"ProducerPID":@(self.task.processIdentifier)};
            if(!privateDirectory || ![description writeToFile:endpoint atomically:YES] ||
               ![NSFileManager.defaultManager setAttributes:@{NSFilePosixPermissions:@0600} ofItemAtPath:endpoint error:NULL]) {
                if(openGLInprocess) {
                    if(error)*error=VIMSessionError(@"The graphics memory connection could not be prepared.");
                    [self.task terminate];[self cleanUp];return NO;
                }
                [self.log writeData:[@"\n[Virtual Intel Mac] Shared graphics memory endpoint unavailable; using copied graphics fallback.\n" dataUsingEncoding:NSUTF8StringEncoding]];
            }
        }
    }
    if([config[@"FileSharingEnabled"] boolValue]&&[VIMNetworkMode(config) isEqual:@"shared"]) {
        self.smbService=[[VIMSMBService alloc] initWithMachine:self.machine port:[config[@"FileSharingPort"] unsignedShortValue]];
        NSError *sharingError=nil;
        if(![self.smbService start:&sharingError])[self.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] %@\n",sharingError.localizedDescription] dataUsingEncoding:NSUTF8StringEncoding]];
    }
    [self updateInstallationActivity];
    [self showWindow:nil];[self.window makeKeyAndOrderFront:nil];
    if(unavailableImages.count) {
        NSDictionary *settings=VIMImageClearedSelections(self.machine.editableConfiguration,unavailableImages);
        NSError *failure=nil;
        BOOL forgotten=!settings.count||[self.machine saveSettings:settings error:&failure];
        if(settings.count)[NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
        NSAlert *alert=[NSAlert new];
        alert.messageText=forgotten?(unavailableImages.count==1?@"An unavailable image was disconnected":@"Unavailable images were disconnected"):@"An image couldn’t be found";
        NSMutableArray *names=[NSMutableArray new];
        for(NSNumber *slot in [unavailableImages.allKeys sortedArrayUsingSelector:@selector(compare:)])[names addObject:[unavailableImages[slot] lastPathComponent]];
        alert.informativeText=[NSString stringWithFormat:@"Your virtual Mac started without %@. %@",[names componentsJoinedByString:@", "],
            forgotten?@"To reconnect an image, choose its current location when its file or drive is available.":@"Its saved selection couldn’t be cleared. You can still disconnect it from CD/DVD or choose it again when its drive is available."];
        [alert addButtonWithTitle:@"OK"];[alert addButtonWithTitle:@"Connect Image…"];
        VIMPresentAlert(alert,self.window,^(NSModalResponse response){
            if(response==NSAlertSecondButtonReturn)[VIMSession chooseOpticalMediaForMachine:self.machine window:self.window];
        });
    }
    self.resizeArmed=!installerDisplay||(openGLDirectDisplay&&!classicInstaller);
    [self connectControlWithCompletion:^(NSError *failure){
        VIMSession *session=weakSelf;if(!session.task.running)return;
        session.status.stringValue=failure?failure.localizedDescription:@"Running";
        session.shutdownButton.enabled=failure==nil;
        if(!failure){[session.window.contentView layoutSubtreeIfNeeded];[session scheduleResize];}
        // Seed the known installed/installer range before delivering input;
        // the later bound-driver observation can still override it.
        session.legacyTabletGuest=VIMUsesLegacyTabletCalibration(session.machine.guest);
        session.legacyTabletObserved=NO;session.legacyTabletObservationAttempts=0;
        session.tabletTrim=VIMInitialTabletTrim(session.machine.guest,classicInstaller);
        if(!failure)session.displayView.relativeMouseModeObserver=^{[weakSelf installWindowControls:weakSelf.task.running];};
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
            NSUInteger trim=weakSelf.tabletTrim;
            if(trim) {
                // IOHIDEventDriver splits its removal percentage between both
                // ends of the tablet range. An installed override may be zero.
                NSMutableArray *calibrated=nil;
                const int64_t maximum=32767,inset=maximum*trim/200,span=maximum-2*inset;
                for(NSUInteger index=0;index<events.count;index++) {
                    NSDictionary *event=events[index],*data=event[@"data"];
                    NSNumber *value=data[@"value"];NSString *axis=data[@"axis"];
                    if(![event[@"type"] isEqual:@"abs"]||
                       (![@"x" isEqual:axis]&&![@"y" isEqual:axis])||
                       ![value isKindOfClass:NSNumber.class]||value.longLongValue<0||value.longLongValue>maximum)continue;
                    if(!calibrated)calibrated=[events mutableCopy];
                    NSMutableDictionary *mapped=[data mutableCopy];
                    mapped[@"value"]=@(inset+(value.longLongValue*span+maximum/2)/maximum);
                    calibrated[index]=@{@"type":@"abs",@"data":mapped};
                }
                if(calibrated)events=calibrated;
            }
            void (^observer)(CFTimeInterval,NSError *)=weakSelf.displayView.diagnosticInputObserver;
            CFTimeInterval inputQueued=observer?CACurrentMediaTime():0;
            [weakSelf.control execute:@"input-send-event" arguments:@{@"events":events} completion:^(id result,NSError *error){
                if(observer)observer(inputQueued,error);
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
            if(openGLInprocess && !(config[@"NetworkConnected"]&&![config[@"NetworkConnected"] boolValue]))
                [session.control execute:@"cont" arguments:@{} completion:^(id result,NSError *error){
                    if(error)VIMPresentError(error,session.window);
                }];
            if(session.window.isKeyWindow)[session.window makeFirstResponder:session.displayView];
            [session refreshRunState];
            [session refreshGuestStatus];
            session.statusTimer=[NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer *timer){
                [weakSelf refreshRunState];
                [weakSelf servicePendingHostConnection];
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
    // Framebuffer/emulator reports need no guest agent. Explicit process-only
    // inspection can also coexist with a provisioning child on the existing
    // multiplexed owner; it never stages files or changes that transaction.
    BOOL independent=[@[@"capture",@"inspect-hardware",@"guest-processes",@"guest-process-sample",@"installer-log",@"cursor-state",@"network-details",@"configuration",@"display-state",@"display-fingerprint",@"display-buffer-count",@"display-acquire-async",@"pointer-workload",@"key-workload",@"fullscreen",@"passthrough-opengl-release-bql",@"opengl-pirq-f-pic"] containsObject:command[@"Action"]?:@""];
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
    if([command[@"Action"] isEqual:@"display-fingerprint"]){
        if(![command[@"Enabled"] isKindOfClass:NSNumber.class]){finish(@{@"Error":@"Choose fingerprint measurement on or off."});return;}
        BOOL enabled=[command[@"Enabled"] boolValue];NSDictionary *observed=nil;
        if(!enabled)observed=self.displayView.frameDiagnostics;
        [self.displayView setFingerprintMeasurementEnabled:enabled];
        [self.displayView setFingerprintTraceEnabled:enabled];
        // Return the final observed generation/trace before disarming it.
        finish(@{@"Frame":observed?:self.displayView.frameDiagnostics,@"MeasurementEnabledAfter":@(enabled)});return;
    }
    if([command[@"Action"] isEqual:@"pointer-workload"]){
        __weak VIMSession *weakSelf=self;
        VIMResearchPointerWorkload(self.displayView,command,^BOOL{return weakSelf.task.running;},finish);return;
    }
    if([command[@"Action"] isEqual:@"key-workload"]){
        __weak VIMSession *weakSelf=self;
        VIMResearchKeyWorkload(self.displayView,command,^BOOL{return weakSelf.task.running;},finish);return;
    }
    if([command[@"Action"] isEqual:@"display-buffer-count"]){
        NSNumber *count=command[@"Count"];
        if(!self.machine.openGLResearchGuest || ![count isKindOfClass:NSNumber.class] ||
           (count.doubleValue!=2 && count.doubleValue!=3) ||
           ![self.displayView setPresentationDrawableCount:count.unsignedIntegerValue]){
            finish(@{@"Error":@"Choose a Metal swap-queue depth of 2 or 3 for a research Mac."});return;
        }
        finish(@{@"Frame":self.displayView.frameDiagnostics});return;
    }
    if([command[@"Action"] isEqual:@"display-acquire-async"]){
        if(!self.machine.openGLResearchGuest || ![command[@"Enabled"] isKindOfClass:NSNumber.class]){
            finish(@{@"Error":@"This presentation experiment requires a research Mac and Enabled boolean."});return;
        }
        self.displayView.asynchronousDrawableAcquisition=[command[@"Enabled"] boolValue];
        finish(@{@"Frame":self.displayView.frameDiagnostics});return;
    }
    if([@[@"passthrough-opengl-release-bql",@"opengl-pirq-f-pic"] containsObject:command[@"Action"]?:@""]){
        BOOL routing=[command[@"Action"] isEqual:@"opengl-pirq-f-pic"];
        NSNumber *enabled=command[routing?@"Route":@"Enabled"];
        if(!self.machine.openGLResearchGuest||(enabled&&![enabled isKindOfClass:NSNumber.class])){
            finish(@{@"Error":@"This diagnostic requires a research Mac and an optional Enabled boolean."});return;
        }
        if(routing&&enabled&&(enabled.doubleValue!=enabled.unsignedIntegerValue||enabled.unsignedIntegerValue>255)){
            finish(@{@"Error":@"Choose the exact original or masked routing byte (0–255)."});return;
        }
        NSArray *parents=@[@"/machine/peripheral-anon",@"/machine/unattached",@"/machine/peripheral"];
        __block NSUInteger remaining=parents.count;
        NSMutableArray *matches=[NSMutableArray array];
        for(NSString *parent in parents)[self.control execute:@"qom-list" arguments:@{@"path":parent} completion:^(id objects,NSError *error){
            dispatch_async(dispatch_get_main_queue(),^{
                if(!error&&[objects isKindOfClass:NSArray.class])for(NSDictionary *entry in objects)
                    if([entry[@"type"] isEqual:routing?@"child<ICH9-LPC>":@"child<vmware-svga>"]&&[entry[@"name"] isKindOfClass:NSString.class])
                        [matches addObject:[parent stringByAppendingPathComponent:entry[@"name"]]];
                if(--remaining)return;
                if(matches.count!=1){finish(@{@"Error":@"A unique research hardware device was not found."});return;}
                NSMutableDictionary *args=[@{@"path":matches.firstObject,@"property":routing?@"x-pirq-f-pic-route":@"x-passthrough-opengl-release-bql"} mutableCopy];
                if(enabled)args[@"value"]=routing?enabled:@([enabled boolValue]);
                [self.control execute:enabled?@"qom-set":@"qom-get" arguments:args completion:^(id value,NSError *failure){
                    finish(failure?@{@"Error":failure.localizedDescription}:@{routing?@"Route":@"Enabled":enabled?(routing?enabled:@([enabled boolValue])):value?:@NO});
                }];
            });
        }];return;
    }
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
        NSMenu *menu=[view itemWithTitle:self.supportsRetinaDisplay?@"Retina Resolution":@"Resolution"].submenu;[menu update];
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
        if(![@[@"General",@"Graphics",@"Network",@"Sound",@"File Sharing",@"Remote Login",@"Clipboard"] containsObject:pane]){finish(@{@"Error":@"Choose a supported configuration pane."});return;}
        [VIMMachineSettings showForMachine:self.machine section:[@[@"File Sharing",@"Remote Login",@"Clipboard"] containsObject:pane]?@"Sharing":([pane isEqual:@"General"]?@"General":@"Hardware") category:pane window:self.window];
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
        [self ensureUSBClient];
        [self.usbClient checkStorageInterface:selected inUserSession:[command[@"UserSession"] isEqual:@YES] completion:^(NSDictionary *result,NSError *error){
            finish(error?@{@"Error":error.localizedDescription}:result);
        }];return;
    }
    if([command[@"Action"] isEqual:@"usb-connect"]||[command[@"Action"] isEqual:@"usb-disconnect"]) {
        if(!self.task.running||self.usbOperationPending){finish(@{@"Error":@"USB is not ready for another operation."});return;}
        NSDictionary *selected=nil;
        for(NSDictionary *device in VIMHostUSBDevices())if([device[@"LocationID"] isEqual:command[@"LocationID"]])selected=device;
        if(!selected){finish(@{@"Error":@"The requested physical USB device is not connected."});return;}
        [self ensureUSBClient];
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
        NSNumber *slot=command[@"Slot"]?:@0;
        if(![slot isKindOfClass:NSNumber.class]||slot.doubleValue!=slot.unsignedIntegerValue||slot.unsignedIntegerValue>=VIMImageSlotCount){finish(@{@"Error":@"Choose an image drive from0 to3."});return;}
        [VIMSession insertImage:path slot:slot.unsignedIntegerValue machine:self.machine completion:^(NSError *error){
            if(error){finish(@{@"Error":error.localizedDescription});return;}
            NSMutableArray *images=[NSMutableArray new];
            for(NSDictionary *image in [VIMSession connectedImagesForMachine:self.machine]) {
                NSMenuItem *item=[[NSMenuItem alloc] initWithTitle:@"Disconnect" action:@selector(ejectOpticalMedia:) keyEquivalent:@""];item.representedObject=image[@"Slot"];
                NSMutableDictionary *entry=[image mutableCopy];entry[@"CanDisconnect"]=@([self validateMenuItem:item]);[images addObject:entry];
            }
            finish(@{@"Inserted":@(path.length>0),@"Images":images,@"MediaChangePending":@(self.mediaChangePending)});
        }];return;
    }
    if([command[@"Action"] isEqual:@"shutdown-vm"]) {
        if(self.preparing||self.preparationRequested){finish(@{@"Error":@"Wait for guest setup to finish before shutting down."});return;}
        [self shutdown:self];finish(@{@"Requested":@(self.shutdownRequested)});return;
    }
    if([command[@"Action"] isEqual:@"resume-setup"]) {
        if(!self.machine.preMetalGuest||!self.task.running||self.guestPending||self.preparing||self.preparationRequested){
            finish(@{@"Error":@"This Mac is not ready to resume its saved setup."});return;
        }
        // Explicit diagnostic request, same production coordinator and Keychain
        // input path. Never accept a password in the diagnostic request files.
        self.developmentSetupDeferred=NO;
        [self.openGLSetup requestInstallerResume];
        finish(@{@"Requested":@YES});[self checkGraphicsSetup:self];return;
    }
    if([command[@"Action"] isEqual:@"force-stop-vm"]) {
        if(!VIMSkipsDevelopmentPowerConfirmation()){finish(@{@"Error":@"Force-stop diagnostics require VIMSkipPowerConfirmation or --development-no-power-confirmation."});return;}
        [self forceStop:nil];finish(@{@"Requested":@YES});return;
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
        // Read the current tail through the same serialized owner. Verbose
        // PackageKit logs can exceed the agent's bounded JSON response when
        // captured in full; an oversized diagnostic must not drop control.
        [self.guestAgent connectWithCompletion:^(NSError *error){
            if(error){finish(@{@"Error":error.localizedDescription});return;}
            [self.guestAgent runExecutable:@"/usr/bin/tail" arguments:@[@"-c",@"49152",@"/var/log/install.log"] timeout:30 completion:^(NSDictionary *result,NSError *failure){
                if(failure){finish(@{@"Error":failure.localizedDescription});return;}
                NSData *bytes=result[@"stdout"]?:NSData.data;
                // The byte-bounded tail can begin within a UTF-8 character.
                const uint8_t *start=bytes.bytes;NSUInteger skip=0;
                while(skip<bytes.length&&skip<3&&(start[skip]&0xc0)==0x80)++skip;
                if(skip)bytes=[bytes subdataWithRange:NSMakeRange(skip,bytes.length-skip)];
                NSString *text=[[NSString alloc] initWithData:bytes encoding:NSUTF8StringEncoding]?:
                    [[NSString alloc] initWithData:bytes encoding:NSISOLatin1StringEncoding];
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
    if([@[@"guest-processes",@"guest-process-sample"] containsObject:command[@"Action"]?:@""]) {
        // Explicit read-only development diagnostics while native setup owns
        // a long-running child. Reuse the SAME multiplexed agent owner; no
        // staging, account mutation, input data or second socket is admitted.
        BOOL sample=[command[@"Action"] isEqual:@"guest-process-sample"];
        NSNumber *pid=command[@"PID"];
        if(sample&&(![pid isKindOfClass:NSNumber.class]||pid.longLongValue<2||pid.longLongValue>INT_MAX)){
            finish(@{@"Error":@"Choose a guest process identifier greater than one."});return;
        }
        NSArray *arguments=sample?@[pid.stringValue,@"1",@"-file",@"/dev/stdout"]:@[@"-axo",@"pid,ppid,state,etime,time,comm"];
        [self.guestAgent connectWithCompletion:^(NSError *failure){
            if(failure){finish(@{@"Error":failure.localizedDescription});return;}
            [self.guestAgent runExecutable:sample?@"/usr/bin/sample":@"/bin/ps" arguments:arguments timeout:0
                completion:^(NSDictionary *reply,NSError *error){
                    if(error){finish(@{@"Error":error.localizedDescription});return;}
                    NSString *text=[[NSString alloc] initWithData:reply[@"stdout"]?:NSData.data encoding:NSUTF8StringEncoding];
                    NSString *detail=[[NSString alloc] initWithData:reply[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding];
                    finish(@{@"ExitCode":reply[@"exitcode"]?:@(-1),@"Stdout":text?:@"",@"Stderr":detail?:@""});
                }];
        }];return;
    }
    if([command[@"Action"] isEqual:@"inspect-hardware"]) {
        if([@[@"memory-map",@"devices"] containsObject:command[@"Component"]?:@""]) {
            [self.control execute:@"human-monitor-command" arguments:@{@"command-line":[command[@"Component"] isEqual:@"devices"]?@"info qtree":@"info mtree -f"}
                completion:^(id result,NSError *error){finish(error?@{@"Error":error.localizedDescription}:@{@"Report":result?:@""});}];return;
        }
        // Whitelisted read-only QMP reports through the production owner.
        // Available before the guest agent/OS boots; never opens another socket.
        if([@[@"physical-memory",@"kernel-memory"] containsObject:command[@"Component"]?:@""]) {
            BOOL kernel=[command[@"Component"] isEqual:@"kernel-memory"];
            NSNumber *address=command[@"Address"],*words=command[@"Words"];
            uint64_t ram=[self.machine.configuration[@"MemoryMiB"] unsignedLongLongValue]<<20;
            if(![address isKindOfClass:NSNumber.class]||![words isKindOfClass:NSNumber.class]||
               words.longLongValue<1||words.unsignedLongLongValue>64||
               (kernel?(address.unsignedLongLongValue<UINT64_C(0xffffff8000000000)||
                        address.unsignedLongLongValue>UINT64_MAX-words.unsignedLongLongValue*8):
                       (address.longLongValue<0||address.unsignedLongLongValue>ram-words.unsignedLongLongValue*8))) {
                finish(@{@"Error":@"Choose 1–64 words within this virtual Mac’s RAM or its identified x86 kernel mapping."});return;
            }
            NSString *read=[NSString stringWithFormat:@"%@ /%llugx 0x%llx",kernel?@"x":@"xp",words.unsignedLongLongValue,address.unsignedLongLongValue];
            [self.control execute:@"human-monitor-command" arguments:@{@"command-line":read} completion:^(id result,NSError *error){
                finish(error?@{@"Error":error.localizedDescription}:@{@"Report":result?:@""});
            }];return;
        }
        if([@[@"phy",@"link-history"] containsObject:command[@"Component"]?:@""]) {
            [self.control execute:@"qom-get" arguments:@{@"path":@"/machine/peripheral/ethernet",@"property":[command[@"Component"] isEqual:@"phy"]?@"x-phy-state":@"x-link-history"}
                completion:^(id result,NSError *error){finish(error?@{@"Error":error.localizedDescription}:@{@"Report":result?:@""});}];return;
        }
        if([command[@"Component"] isEqual:@"registers"]) {
            [self.control execute:@"human-monitor-command" arguments:@{@"command-line":@"info registers -a"} completion:^(id result,NSError *error){
                finish(error?@{@"Error":error.localizedDescription}:@{@"Report":result?:@""});
            }];return;
        }
        if([command[@"Component"] isEqual:@"serial"]) {
            // Explicit metadata-only UART inspection. Do not read RBR/FIFOs,
            // IIR, LSR or MSR: those reads consume data or clear status. These
            // three configuration registers are non-destructive even if DLAB
            // is set (IER then aliases the divisor's high configuration byte).
            NSArray *ports=@[@0x3f9,@0x3fb,@0x3fc,@0x2f9,@0x2fb,@0x2fc];
            NSMutableArray *values=[NSMutableArray array];
            __block void (^next)(NSUInteger);
            next=^(NSUInteger index){
                if(index==ports.count){finish(@{@"Registers":values});next=nil;return;}
                NSNumber *port=ports[index];
                NSString *read=[NSString stringWithFormat:@"i /b 0x%lx",port.unsignedLongValue];
                [self.control execute:@"human-monitor-command" arguments:@{@"command-line":read} completion:^(id result,NSError *error){
                    if(error){finish(@{@"Error":error.localizedDescription});next=nil;return;}
                    [values addObject:@{@"Port":port,@"Report":result?:@""}];next(index+1);
                }];
            };next(0);return;
        }
        NSString *report=@{@"cpu":@"query-cpus-fast",@"pci":@"query-pci",@"media":@"query-block",@"network":@"x-query-network",@"interrupts":@"x-query-interrupt-controllers"}[command[@"Component"]?:@""];
        if(!report){finish(@{@"Error":@"Choose cpu, pci, network, interrupts, registers, serial, phy, or link-history."});return;}
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
            // Exercise the production private staging and acknowledged transfer.
            // stageFiles confines output to a fresh root-owned staging dir.
            [self.guestAgent stageFiles:@{@"Transfer.bin":input} progress:nil completion:^(NSDictionary *receipt,NSError *failure){
                if(failure){finish(@{@"Error":failure.localizedDescription});return;}
                finish(@{@"Transfer":receipt,@"Seconds":@(NSProcessInfo.processInfo.systemUptime-start)});
            }];
        }];return;
    }
    NSString *path=command[@"Executable"];NSArray *arguments=command[@"Arguments"];
    if(![path isKindOfClass:NSString.class]||!path.isAbsolutePath||![arguments isKindOfClass:NSArray.class]){finish(@{@"Error":@"Invalid command"});return;}
    for(id argument in arguments)if(![argument isKindOfClass:NSString.class]){finish(@{@"Error":@"Invalid argument"});return;}
    NSData *standardInput=nil;
    if(command[@"PrivateInput"]){
        // Development-only cold operation: secrets go through the existing
        // agent's stdin, never argv/Request.json or diagnostic output. Consume
        // a single private, bounded file inside the already-validated local
        // diagnostics directory; never follow an arbitrary path or symlink.
        if(![command[@"PrivateInput"] isEqual:@"Input.bin"]){finish(@{@"Error":@"Invalid private input"});return;}
        NSString *inputPath=[self.diagnosticDirectory stringByAppendingPathComponent:@"Input.bin"];
        int fd=open(inputPath.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
        struct stat inputInfo;NSMutableData *input=[NSMutableData data];BOOL valid=fd>=0;
        if(valid)valid=!fstat(fd,&inputInfo)&&S_ISREG(inputInfo.st_mode)&&inputInfo.st_uid==getuid()&&
            inputInfo.st_nlink==1&&!(inputInfo.st_mode&0077)&&inputInfo.st_size>=0&&inputInfo.st_size<=16384;
        if(valid){
            unsigned char chunk[2048];ssize_t count;
            while((count=read(fd,chunk,sizeof(chunk)))!=0){
                if(count<0){if(errno==EINTR)continue;valid=NO;break;}
                if(input.length+(NSUInteger)count>16384){valid=NO;break;}[input appendBytes:chunk length:(NSUInteger)count];
            }
            if(valid)valid=input.length==(NSUInteger)inputInfo.st_size;
        }
        if(fd>=0)close(fd);
        if(!valid||unlink(inputPath.fileSystemRepresentation)){finish(@{@"Error":@"Private input could not be consumed safely"});return;}
        standardInput=input;
    }
    [self.guestAgent connectWithCompletion:^(NSError *error){
        if(error){finish(@{@"Error":error.localizedDescription});return;}
        // An observation deadline must not discard the process handle for a
        // native installer/cache rebuild which is still legitimately running.
        // The diagnostic CLI can stop observing without cancelling this task.
        [self.guestAgent runExecutable:path arguments:arguments standardInput:standardInput timeout:0 completion:^(NSDictionary *result,NSError *failure){
            if(failure){finish(@{@"Error":failure.localizedDescription});return;}
            finish(@{@"ExitCode":result[@"exitcode"]?:@(-1),
                @"Stdout":[[NSString alloc] initWithData:result[@"stdout"]?:NSData.data encoding:NSUTF8StringEncoding]?:@"",
                @"Stderr":[[NSString alloc] initWithData:result[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding]?:@""});
        }];
    }];
}
- (void)enforceGuestSleepPolicyForBoot:(NSString *)boot nativeIdleGuard:(BOOL)guarded {
    if(!boot.length||[boot isEqual:self.sleepDisabledBoot]||!self.task.running)return;
    // v1 has no reliable S3/S4 resume on either graphics stack. Apply the
    // same native root-domain policy to Pre-Metal desktops too; their status
    // branch returns before the modern setup path below. Keep display sleep
    // independent. One cold command per boot, no per-frame or idle polling.
    self.sleepDisabledBoot=boot;
    if([self.machine.guest isEqual:@"tiger"]&&guarded)return;
    // Tiger lacks "disablesleep". Its agent normally cancels native idle
    // sleep notifications without changing preferences. Retain a compatible
    // fallback for an older/unavailable guard, not an endlessly failing command.
    NSArray *arguments=[self.machine.guest isEqual:@"tiger"]?@[@"-a",@"sleep",@"0"]:
        @[@"-a",@"sleep",@"0",@"disablesleep",@"1"];
    NSString *runtime=self.runtimeDirectory;
    __weak VIMSession *weakSelf=self;
    [self.guestAgent runExecutable:@"/usr/bin/pmset" arguments:arguments timeout:30 completion:^(NSDictionary *result,NSError *failure){
        VIMSession *session=weakSelf;
        // A late failure from an old session must not clear a newer boot's
        // confirmed policy or schedule work against a replacement VM process.
        if(![session.runtimeDirectory isEqual:runtime]||![session.sleepDisabledBoot isEqual:boot])return;
        if(failure||![result[@"exitcode"] isEqual:@0])session.sleepDisabledBoot=nil;
    }];
}
/*
 * The first installed boot after a classic installer still has the
 * installer attached (InstallState "installing"), so the session starts
 * with the installer's 15% fallback. The installed 10.5-10.9 driver binds
 * the tablet with an explicit zero, and Setup Assistant never reaches the
 * desktop check: input was squeezed into the middle 85% (issues #2, #21).
 * Read the bound driver once per boot as soon as the guest agent answers.
 */
- (void)observeLegacyTabletDuringSetup {
    if(!self.legacyTabletGuest||self.legacyTabletObserved||self.legacyTabletObservationPending)return;
    self.legacyTabletObservationPending=YES;
    __weak VIMSession *weakSelf=self;
    VIMGuestAgent *agent=self.guestAgent;
    [agent connectWithCompletion:^(NSError *error){
        VIMSession *session=weakSelf;
        if(!session||session.guestAgent!=agent||!session.task.running){if(session)session.legacyTabletObservationPending=NO;return;}
        if(error){session.legacyTabletObservationPending=NO;return;}
        [session observeLegacyTabletWithAgent:agent completion:^{weakSelf.legacyTabletObservationPending=NO;}];
    }];
}
- (void)observeLegacyTabletWithAgent:(VIMGuestAgent *)agent completion:(void (^)(void))completion {
    // Bounded per boot: the tablet binds early, and polling is every few seconds.
    if(++self.legacyTabletObservationAttempts>40){self.legacyTabletObserved=YES;completion();return;}
    __weak VIMSession *weakSelf=self;
    [agent runExecutable:@"/usr/sbin/ioreg"
        arguments:@[@"-r",@"-c",@"IOHIDEventDriver",@"-d",@"1",@"-l",@"-w",@"0"] timeout:0
        completion:^(NSDictionary *reply,NSError *ignored){
            VIMSession *live=weakSelf;if(!live||live.guestAgent!=agent||!live.task.running){completion();return;}
            if(!ignored&&[reply[@"exitcode"] isEqual:@0]){
                NSString *registry=[[NSString alloc] initWithData:reply[@"stdout"]?:NSData.data encoding:NSUTF8StringEncoding];
                // Conclusive only once the QEMU tablet's event driver is bound.
                // Without an explicit override (installer safe-boot driver),
                // keep the seeded fallback.
                if([registry containsString:@"\"Product\" = \"QEMU USB Tablet\""]) {
                    live.legacyTabletObserved=YES;
                    NSNumber *trim=VIMObservedTabletTrim(registry);
                    if(trim)live.tabletTrim=trim.unsignedIntegerValue;
                    [live.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] Tablet range removal: %@ (applied %lu%%)\n",
                        trim?:@"none",(unsigned long)live.tabletTrim] dataUsingEncoding:NSUTF8StringEncoding]];
                }
            }
            completion();
        }];
}
- (void)refreshGuestStatus {
    if(self.guestPending||self.preparing||self.shutdownRequested||self.relaunchForGraphics||!self.task.running)return;
    if(self.diagnosticPending||self.clipboardPending||self.bootPolicyPending) {
        // A one-shot status timer may fire during a diagnostic command. Keep
        // the normal production readiness/verification loop alive afterward.
        [self.guestTimer invalidate];
        __weak VIMSession *weakSelf=self;
        self.guestTimer=[NSTimer scheduledTimerWithTimeInterval:15 repeats:NO block:^(NSTimer *timer){[weakSelf refreshGuestStatus];}];
        return;
    }
    if(self.machine.preMetalGuest&&!self.openGLDesktopMonitoring){
        if([self.machine.guest isEqual:@"tiger"]&&!self.machine.passthroughOpenGLEnabled){
            // Native Tiger installation is available before its separate GL
            // payload is qualified. Do not run Darwin9 controls or display a
            // false missing-accelerator error during the native installer.
            // Enabling the actual GL profile later must use the normal strict
            // coordinator/verification gates; never mark setup complete here.
            self.guestStatus.stringValue=@"Mac OS X Tiger — native installation";return;
        }
        // Process-local development opt-in only: keep the real boot, drivers,
        // display and agent available while inspecting a cold native service.
        // Explicit --resume-setup enters the unchanged production coordinator.
        if(self.developmentSetupDeferred){self.guestStatus.stringValue=@"Automatic setup paused for diagnostics";return;}
        [self observeLegacyTabletDuringSetup];
        [self refreshOpenGLSetup];return;
    }
    if(!self.guestStatusTool.length) {
        self.guestStatus.stringValue=@"Guest setup tool is missing";return;
    }
    self.guestPending=YES;
    __weak VIMSession *weakSelf=self;
    [self.guestAgent connectWithCompletion:^(NSError *error) {
        VIMSession *session=weakSelf;if(!session.task.running)return;
        if(error){[session finishGuestStatus:nil error:error];return;}
        if(session.machine.preMetalGuest){
            VIMGuestAgent *agent=session.guestAgent;
            [agent collectInstalledOpenGLDesktopStatus:^(NSDictionary *report,NSError *failure){
                VIMSession *current=weakSelf;if(current.guestAgent!=agent||!current.task.running)return;
                if(failure||!current.legacyTabletGuest||current.legacyTabletObserved){[current finishGuestStatus:report error:failure];return;}
                [current observeLegacyTabletWithAgent:agent completion:^{[weakSelf finishGuestStatus:report error:nil];}];
            }];return;
        }
        [session.guestAgent collectStatusWithTool:session.guestStatusTool completion:^(NSDictionary *report,NSError *failure) {
            [weakSelf finishGuestStatus:report error:failure];
        }];
    }];
}
- (void)dismissSetupNotice {
    if(self.previewSetupNotice)return;
    [self removeSetupNotice];
}
- (void)removeSetupNotice {
    [self.setupNotice removeFromSuperview];self.setupNotice=nil;self.setupNoticeHeading=nil;
}
- (void)closeSetupNotice:(id)sender {
    self.closedSetupNoticeHeading=self.setupNoticeHeading;
    [self removeSetupNotice];
    [self.window makeFirstResponder:self.displayView];
}
- (void)showSetupNoticeAutomatic:(BOOL)automatic automaticLogin:(BOOL)automaticLogin restarting:(BOOL)restarting {
    NSString *title=automatic?@"Automatic Setup in Progress…":@"Installing Graphics Drivers…";
    NSString *detail;
    if(automatic){
        NSString *waiting=self.machine.preMetalGuest?@"The language selection screen is expected to stay visible during Automatic Setup.":
            @"The first setup screen may stay visible during Automatic Setup.";
        NSString *restart=automaticLogin?@"Your virtual Mac will restart and sign in automatically when it’s ready.":
            self.machine.preMetalGuest?@"Your virtual Mac will restart and sign in automatically this first time.":
            @"Your virtual Mac will restart automatically when it’s ready. If you see the login screen, sign in with the account you chose.";
        detail=[NSString stringWithFormat:@"Please do not interact with Setup Assistant. %@\n\n%@ Setup may take up to 30 minutes.",waiting,restart];
    }else{
        detail=restarting?@"Your virtual Mac will restart to finish installing the graphics drivers. Sign in when it starts again. This may take up to 30 minutes.":
            @"Keep your virtual Mac running while the graphics drivers install. This may take up to 30 minutes. If a restart is needed, you’ll be asked first.";
    }
    [self showSetupNoticeWithTitle:title detail:detail];
}
- (void)showRecoveryInstallNotice {
    [self showSetupNoticeWithTitle:@"Installing macOS Automatically…"
        detail:@"Please do not interact with macOS Utilities or the installer. The macOS installer is running in the background, and the macOS Utilities window may stay visible meanwhile.\n\nYour virtual Mac will restart several times on its own. Installation may take up to 2 hours."];
}
- (void)showSetupNoticeWithTitle:(NSString *)heading detail:(NSString *)message {
    if(self.previewSetupNotice&&(self.setupNotice||self.closedSetupNoticeHeading))return;
    if([self.closedSetupNoticeHeading isEqualToString:heading]||
       [self.setupNoticeHeading isEqualToString:heading])return;
    [self removeSetupNotice];
    NSView *content=self.window.contentView;
    VIMSetupNoticeView *notice=[VIMSetupNoticeView new];
    notice.wantsLayer=YES;
    notice.layer.shadowColor=NSColor.blackColor.CGColor;
    notice.layer.shadowOpacity=.32;notice.layer.shadowRadius=14;
    notice.layer.shadowOffset=CGSizeMake(0,-5);
    notice.translatesAutoresizingMaskIntoConstraints=NO;
    NSVisualEffectView *material=[NSVisualEffectView new];
    material.material=NSVisualEffectMaterialHUDWindow;
    material.blendingMode=NSVisualEffectBlendingModeWithinWindow;
    material.state=NSVisualEffectStateActive;
    material.wantsLayer=YES;material.layer.cornerRadius=14;material.layer.masksToBounds=YES;
    material.translatesAutoresizingMaskIntoConstraints=NO;
    [notice addSubview:material];
    NSTextField *title=[NSTextField labelWithString:heading];
    title.font=[NSFont systemFontOfSize:15 weight:NSFontWeightSemibold];
    NSImageView *symbol=[NSImageView imageViewWithImage:[NSImage imageWithSystemSymbolName:@"gearshape.fill" accessibilityDescription:nil]];
    symbol.contentTintColor=NSColor.secondaryLabelColor;
    NSStackView *header=[NSStackView stackViewWithViews:@[symbol,title]];
    header.orientation=NSUserInterfaceLayoutOrientationHorizontal;
    header.alignment=NSLayoutAttributeCenterY;header.spacing=7;
    NSTextField *detail=[NSTextField wrappingLabelWithString:message];
    detail.textColor=NSColor.secondaryLabelColor;
    NSProgressIndicator *progress=[NSProgressIndicator new];progress.style=NSProgressIndicatorStyleBar;
    progress.controlSize=NSControlSizeSmall;progress.indeterminate=YES;
    progress.accessibilityLabel=@"Setup in progress";
    NSStackView *labels=[NSStackView stackViewWithViews:@[header,progress,detail]];
    labels.orientation=NSUserInterfaceLayoutOrientationVertical;
    labels.alignment=NSLayoutAttributeLeading;labels.spacing=6;
    labels.translatesAutoresizingMaskIntoConstraints=NO;
    NSButton *close=[NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:@"xmark.circle.fill"
        accessibilityDescription:@"Close Setup Notice"] target:self action:@selector(closeSetupNotice:)];
    close.bordered=NO;close.toolTip=@"Close Setup Notice";
    [close setAccessibilityLabel:@"Close Setup Notice"];
    close.translatesAutoresizingMaskIntoConstraints=NO;
    [material addSubview:labels];[material addSubview:close];notice.closeButton=close;
    [content addSubview:notice positioned:NSWindowAbove relativeTo:self.displayView];
    NSLayoutConstraint *preferredWidth=[notice.widthAnchor constraintEqualToConstant:420];
    preferredWidth.priority=NSLayoutPriorityDefaultHigh;
    [NSLayoutConstraint activateConstraints:@[
        preferredWidth,
        [notice.centerXAnchor constraintEqualToAnchor:content.centerXAnchor],
        [notice.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-24],
        [notice.widthAnchor constraintLessThanOrEqualToAnchor:content.widthAnchor constant:-32],
        [material.leadingAnchor constraintEqualToAnchor:notice.leadingAnchor],
        [material.trailingAnchor constraintEqualToAnchor:notice.trailingAnchor],
        [material.topAnchor constraintEqualToAnchor:notice.topAnchor],
        [material.bottomAnchor constraintEqualToAnchor:notice.bottomAnchor],
        [labels.leadingAnchor constraintEqualToAnchor:notice.leadingAnchor constant:20],
        [labels.trailingAnchor constraintEqualToAnchor:close.leadingAnchor constant:-10],
        [labels.topAnchor constraintEqualToAnchor:notice.topAnchor constant:16],
        [labels.bottomAnchor constraintEqualToAnchor:notice.bottomAnchor constant:-16],
        [close.trailingAnchor constraintEqualToAnchor:notice.trailingAnchor constant:-12],
        [close.topAnchor constraintEqualToAnchor:notice.topAnchor constant:10],
        [close.widthAnchor constraintEqualToConstant:24],
        [close.heightAnchor constraintEqualToConstant:24],
        [detail.widthAnchor constraintEqualToAnchor:labels.widthAnchor],
        [progress.widthAnchor constraintEqualToAnchor:labels.widthAnchor]]];
    [progress startAnimation:nil];
    self.setupNotice=notice;self.setupNoticeHeading=heading;
}
- (void)refreshOpenGLSetup {
    self.guestPending=YES;
    __weak VIMSession *weakSelf=self;
    [self.openGLSetup advance:^(NSDictionary *outcome,NSError *error){
        VIMSession *session=weakSelf;if(!session)return;
        session.guestPending=NO;if(!session.task.running||session.shutdownRequested)return;
        if(error){
            [session dismissSetupNotice];
            session.guestStatus.stringValue=error.localizedDescription;
            VIMPresentError(error,session.window);return; // no ambiguous automatic retry
        }
        NSString *phase=outcome[@"Phase"];
        if([phase isEqual:@"credential-required"]){
            [session dismissSetupNotice];
            NSAlert *alert=[NSAlert new];VIMSetInstallerAlertIcon(alert,session.machine.guest);
            alert.messageText=@"Enter the virtual Mac’s password";
            alert.informativeText=[NSString stringWithFormat:@"Enter the password you chose for “%@” in this virtual Mac to resume its saved setup. Your existing account won’t be replaced. This password won’t be saved on this Mac.",outcome[@"Username"]?:@""];
            NSSecureTextField *password=[[NSSecureTextField alloc] initWithFrame:NSMakeRect(0,0,280,24)];
            password.placeholderString=@"Virtual Mac password";alert.accessoryView=password;
            [alert addButtonWithTitle:@"Continue"];[alert addButtonWithTitle:@"Not Now"];
            alert.window.initialFirstResponder=password;
            session.guestStatus.stringValue=@"A password is needed to resume setup";
            VIMPresentAlert(alert,session.window,^(NSModalResponse response){
                NSString *value=response==NSAlertFirstButtonReturn?password.stringValue:nil;
                password.stringValue=@"";
                if(!value||!session.task.running||session.shutdownRequested)return;
                NSError *failure=nil;
                if(![session.openGLSetup providePassword:value error:&failure]){VIMPresentError(failure,session.window);return;}
                [session refreshOpenGLSetup];
            });return;
        }
        if([@[@"unmanaged",@"unsupported",@"setup-unavailable"] containsObject:phase]){
            [session dismissSetupNotice];
            if([phase isEqual:@"setup-unavailable"])
                [session.log writeData:[@"\n[Virtual Intel Mac] Automatic OpenGL setup left the current system unchanged; continuing desktop monitoring.\n" dataUsingEncoding:NSUTF8StringEncoding]];
            session.openGLDesktopMonitoring=YES;[session refreshGuestStatus];return;
        }
        if([phase isEqual:@"recovery"]){
            [session dismissSetupNotice];
            session.guestStatus.stringValue=[NSString stringWithFormat:@"Installing %@…",VIMOperatingSystemName(session.machine.guest)];
            // Apple's installer owns the real progress/UI. The next boot
            // creates a new coordinator; no UART polling while copying an OS.
            return;
        }
        if([phase isEqual:@"needs-restart"]){
            if(session.openGLRestartAttempted)return;
            void (^restart)(void)=^{
                if(!session.task.running||session.shutdownRequested)return;
                NSError *failure=nil;
                if(![session.machine prepareOpenGLRestartWithReport:outcome[@"Report"] error:&failure]){
                    [session dismissSetupNotice];
                    VIMPresentError(failure,session.window);return;
                }
                session.openGLRestartAttempted=YES;session.relaunchForGuestReboot=YES;
                [session updateInstallationActivity];
                session.guestStatus.stringValue=@"Finishing setup — restarting this Mac…";
                if(![outcome[@"ExistingGuestUpdate"] boolValue]){
                    NSDictionary *profile=[NSDictionary dictionaryWithContentsOfURL:
                        [session.machine.URL URLByAppendingPathComponent:@"State/AutomaticSetup.plist"]];
                    [session showSetupNoticeAutomatic:[outcome[@"Automatic"] boolValue]
                        automaticLogin:[(profile[@"AutomaticLogin"]?:@YES) boolValue] restarting:YES];
                }
                [session.guestAgent requestRestartWithCompletion:^(NSError *issue){
                    if(!issue){NSError *saveError=nil;[session.machine recordOpenGLRestartAcknowledged:&saveError];
                        if(saveError)VIMPresentError(saveError,session.window);
                    }else if(session.task.running){
                        [session dismissSetupNotice];
                        session.guestStatus.stringValue=@"Restart was not confirmed. Restart from the Apple menu to continue setup. Your setup records were retained.";
                    }
                }];
            };
            if([outcome[@"Automatic"] boolValue])restart();
            else {
                [session dismissSetupNotice];
                NSAlert *alert=[NSAlert new];VIMSetAppAlertIcon(alert);
                BOOL updated=[outcome[@"ExistingGuestUpdate"] boolValue];
                alert.messageText=updated?@"Restart to use updated graphics support?":@"Restart to finish graphics setup?";
                alert.informativeText=updated?@"Graphics support has been updated. Save any open work, then restart this Mac to use it.":@"Graphics support is installed. Restart this Mac to load it. Save any open work first.";
                [alert addButtonWithTitle:@"Restart"];[alert addButtonWithTitle:@"Later"];
                [alert beginSheetModalForWindow:session.window completionHandler:^(NSModalResponse result){
                    if(result==NSAlertFirstButtonReturn)restart();
                    else if([outcome[@"ExistingGuestUpdate"] boolValue]) {
                        // The installed update waits for a user-approved restart;
                        // retain normal clipboard/status/services in this boot.
                        session.openGLDesktopMonitoring=YES;[session refreshGuestStatus];
                    }
                }];
            }return;
        }
        if([phase isEqual:@"complete"]){
            [session dismissSetupNotice];
            NSError *failure=nil;
            if(![session.machine finishOpenGLSetupWithReport:outcome[@"Report"] error:&failure]){
                VIMPresentError(failure?:VIMSessionError(@"Setup could not yet be verified. Its installer and recovery records were retained."),session.window);return;
            }
            session.completionGuestReport=outcome[@"Report"];
            session.openGLLargeResizeReady=YES;
            session.lastRequestedSize=NSZeroSize;
            [session scheduleResize];
            session.guestStatus.stringValue=@"Graphics support is ready";
            [session retireOpenGLSetupDiscWithReport:outcome[@"Report"]];
            [NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
            // The persistent receipt ends setup work for this boot. On a later
            // boot the guest bootstrap checks native files for update repair.
            session.openGLDesktopMonitoring=YES;[session refreshGuestStatus];return;
        }
        session.guestStatus.stringValue=[phase isEqual:@"payload-media-wait"]?@"Waiting for the graphics update disc…":
            [phase isEqual:@"root-volume-wait"]?@"Waiting for this Mac’s startup disk…":
            [phase isEqual:@"desktop-wait"]?@"Waiting for this Mac’s desktop…":
            [@[@"desktop-services-wait",@"file-sharing-busy"] containsObject:phase]?@"Finishing File Sharing setup…":
            [phase isEqual:@"update-restart-wait"]?@"Restart this Mac when its system update finishes…":
            [phase isEqual:@"account-busy"]?@"Creating your Mac account…":
            [phase isEqual:@"restarting"]?@"Restarting to finish setup…":[NSString stringWithFormat:@"Starting %@…",VIMOperatingSystemName(session.machine.guest)];
        [session.guestTimer invalidate];
        session.guestTimer=[NSTimer scheduledTimerWithTimeInterval:15 repeats:NO block:^(NSTimer *timer){[weakSelf refreshGuestStatus];}];
    }];
}
- (void)retireOpenGLSetupDiscWithReport:(NSDictionary *)report {
    if(!self.openGLSetupMediaAttached)return;
    // Only the dedicated configuration-media slot belongs to this flow.
    // The user-controlled CD/DVD drive (including an arbitrary repair disc)
    // is never force-ejected or changed merely because setup has completed.
    NSURL *receipt=[self.machine.URL URLByAppendingPathComponent:@"State/LegacySetupMedia.plist"];
    NSDictionary *owner=[NSDictionary dictionaryWithContentsOfURL:receipt];
    if(![owner[@"MachineIdentifier"] isEqual:self.machine.configuration[@"Identifier"]]||
       ![owner[@"VolumeUUID"] isEqual:report[@"VolumeUUID"]]||![report[@"PixelChecksPassed"] isEqual:@YES])return;
    // XML plist output is native to every supported period guest; Snow's
    // plutil predates JSON conversion. Parse on the host without weakening
    // the owned-media identity and successful-graphics checks below.
    [self.guestAgent runExecutable:@"/usr/bin/plutil" arguments:@[@"-convert",@"xml1",@"-o",@"-",@"/Volumes/VIM Setup/VirtualIntelMac.plist"] timeout:0 completion:^(NSDictionary *result,NSError *error){
        id policy=[NSPropertyListSerialization propertyListWithData:result[@"stdout"]?:NSData.data options:NSPropertyListImmutable format:NULL error:nil];
        if(error||![result[@"exitcode"] isEqual:@0]||![policy isKindOfClass:NSDictionary.class]||
           ![policy[@"MachineIdentifier"] isEqual:owner[@"MachineIdentifier"]]||![policy[@"VolumeUUID"] isEqual:owner[@"VolumeUUID"]])return;
        [self.guestAgent runExecutable:@"/usr/sbin/diskutil" arguments:@[@"eject",@"/Volumes/VIM Setup"] timeout:0 completion:^(NSDictionary *ejected,NSError *failure){
            if(failure||![ejected[@"exitcode"] isEqual:@0])return; // Never force a mounted image.
            [self.control execute:@"eject" arguments:@{@"id":@"opengl-setup-device"} completion:^(id ignored,NSError *unused){if(!unused)self.openGLSetupMediaAttached=NO;}];
        }];
    }];
}
- (BOOL)prepareMavericksSMBForBoot:(NSString *)boot {
    if(![self.machine.guest isEqual:@"mavericks"]||[self.mavericksSMBCheckedBoot isEqual:boot]||
       self.shutdownRequested)return NO;
    self.mavericksSMBCheckedBoot=boot;
    NSURL *tool=[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:
        @"Runtime/OpenGLGuests/mavericks/mavericks-smb-compat"];
    // Apple's signed allocator, which a fresh guest without developer tools
    // lacks; the tool uses it only for its own codesign children.
    NSURL *allocator=[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:@"Runtime/GuestTools/codesign_allocate"];
    if(![tool checkResourceIsReachableAndReturnError:nil]||![allocator checkResourceIsReachableAndReturnError:nil])return NO;
    self.guestPending=YES;
    VIMGuestAgent *agent=self.guestAgent;
    __weak VIMSession *weakSelf=self;
    void (^finished)(BOOL)=^(BOOL ok){
        VIMSession *session=weakSelf;
        if(session.guestAgent!=agent||!session.task.running)return;
        session.guestPending=NO;
        if(!ok)[session.log writeData:[@"\n[Virtual Intel Mac] Mavericks File Sharing compatibility could not be applied.\n"
            dataUsingEncoding:NSUTF8StringEncoding]];
        [session refreshGuestStatus];
    };
    [agent stageFiles:@{@"mavericks-smb-compat":tool,@"codesign_allocate":allocator} progress:nil completion:^(NSDictionary *receipt,NSError *issue){
        if(issue||!receipt){finished(NO);return;}
        NSString *program=[receipt[@"Directory"] stringByAppendingPathComponent:@"mavericks-smb-compat"];
        NSString *staged=[receipt[@"Directory"] stringByAppendingPathComponent:@"codesign_allocate"];
        [agent runExecutable:@"/bin/chmod" arguments:@[@"0700",program,staged] timeout:0 completion:^(NSDictionary *mode,NSError *failure){
            if(failure||![mode[@"exitcode"] isEqual:@0]){finished(NO);return;}
            [agent runExecutable:program arguments:@[] timeout:0 completion:^(NSDictionary *reply,NSError *failure){
                finished(!failure&&[reply[@"exitcode"] isEqual:@0]);
            }];
        }];
    }];
    return YES;
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
    if(self.machine.preMetalGuest){
        NSString *boot=[report[@"BootSessionUUID"] isKindOfClass:NSString.class]?report[@"BootSessionUUID"]:nil;
        BOOL desktop=!error&&[report[@"Guest"] isEqual:self.machine.guest]&&[report[@"DesktopRunning"] isEqual:@YES]&&self.clipboardUID;
        self.guestStatus.stringValue=desktop?[NSString stringWithFormat:@"%@ %@",VIMOperatingSystemName(self.machine.guest),report[@"ProductVersion"]?:@""]:@"Waiting for guest desktop services…";
        if(desktop&&boot.length){
            if([self prepareMavericksSMBForBoot:boot])return;
            NSString *username=report[@"ConsoleUsername"];
            if([username isKindOfClass:NSString.class]&&username.length){
                self.usernameBoot=boot;
                NSString *key=[@"VIMGuestUsername-" stringByAppendingString:self.machine.URL.path];
                BOOL changed=![[NSUserDefaults.standardUserDefaults stringForKey:key] isEqual:username];
                [NSUserDefaults.standardUserDefaults setObject:username forKey:key];
                if(changed)[NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
            }
            self.resizeArmed=YES;
            if(![self.displaySizedBoot isEqual:boot]){self.displaySizedBoot=boot;self.lastRequestedSize=NSZeroSize;[self scheduleResize];}
            [self enforceGuestSleepPolicyForBoot:boot nativeIdleGuard:[report[@"IdleSleepGuardRegistered"] isEqual:@YES]];
            [self updateClipboardForBoot:boot];
        }
        [self.guestTimer invalidate];__weak VIMSession *weakSelf=self;
        self.guestTimer=[NSTimer scheduledTimerWithTimeInterval:desktop?60:15 repeats:NO block:^(NSTimer *timer){[weakSelf refreshGuestStatus];}];
        return; // Desktop evidence never authorizes modern Metal patching.
    }
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
        self.guestStatus.stringValue=[NSString stringWithFormat:@"%@ %@%@",VIMOperatingSystemName(self.machine.guest),version,installed?@"":@" — setup in progress"];
        // Evidence only. Never modify InstallerMedia/boot routing here: a live
        // desktop is not proof that the required patch transaction succeeded.
        NSDictionary *record=@{@"ObservedAt":NSDate.date,@"Session":self.runtimeDirectory.lastPathComponent,
            @"Transport":self.guestAgent.transportName,@"PrivilegedAgentVerified":@YES,@"Report":report};
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
    if(!error&&!self.shutdownRequested&&!self.preparing&&!self.preparationRequested&&!self.relaunchForGraphics) {
        NSError *nativeError=nil;
        if([self.machine prepareNativeBigSurDisplayWithGuestReport:report error:&nativeError]) {
            [self.log writeData:[@"\n[Virtual Intel Mac] Installed Big Sur has native PVG; switching Setup Assistant to accelerated graphics. Guest patches and installer cleanup remain pending.\n" dataUsingEncoding:NSUTF8StringEncoding]];
            [self handoffToGraphics];
            self.guestStatus.stringValue=@"Starting native Big Sur graphics for Setup Assistant…";
            return;
        }
        if(nativeError){self.guestStatus.stringValue=nativeError.localizedDescription;return;}
    }
    if(!error&&self.modernSetup&&!self.modernSetupReportActive&&!self.shutdownRequested&&!self.relaunchForGraphics){
        self.guestPending=YES;NSString *runtime=self.runtimeDirectory;__weak VIMSession *weakSelf=self;
        [self.modernSetup advanceReport:report completion:^(NSDictionary *outcome,NSError *failure){
            VIMSession *session=weakSelf;
            if(!session.task.running||![session.runtimeDirectory isEqual:runtime])return;
            session.guestPending=NO;
            if(failure){[session dismissSetupNotice];session.guestStatus.stringValue=failure.localizedDescription;VIMPresentError(failure,session.window);return;}
            if([outcome[@"Handled"] isEqual:@YES]){
                session.guestStatus.stringValue=outcome[@"Message"]?:@"Installing macOS…";
                // Recovery's macOS Utilities stays on screen while startosinstall
                // stages the install behind it. Say so, as Automatic Setup does.
                if([outcome[@"RecoveryInstall"] isEqual:@YES])[session showRecoveryInstallNotice];
                if([outcome[@"Restart"] isEqual:@YES]){
                    [session handoffToGraphics];
                    // First-boot shutdown may wait for native cache/service
                    // work. Keep the one acknowledged handoff pending until
                    // actual exit; do not expire it and dispatch another one.
                    [session.handoffTimer invalidate];session.handoffTimer=nil;
                    session.guestStatus.stringValue=outcome[@"Message"]?:@"Restarting to sign in…";
                    return;
                }
                session.guestTimer=[NSTimer scheduledTimerWithTimeInterval:15 repeats:NO block:^(NSTimer *timer){[weakSelf refreshGuestStatus];}];
            }else{
                [session dismissSetupNotice];
                session.modernSetupReportActive=YES;[session finishGuestStatus:report error:nil];session.modernSetupReportActive=NO;
            }
        }];return;
    }
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
    if(!error)[self enforceGuestSleepPolicyForBoot:boot nativeIdleGuard:[report[@"IdleSleepGuardRegistered"] isEqual:@YES]];
    // Reserve initial graphics staging before ancillary display/clipboard
    // helpers can acquire the same agent's transfer owner. Big Sur already
    // has native PVG at this first desktop, unlike unaccelerated older guests.
    if(!error&&!self.preparationRequested&&!self.repairRequested) {
        NSError *claimError=nil;
        if([self.machine claimAutomaticInstallationWithGuestReport:report error:&claimError]) {
            self.preparationRequested=YES;self.installationRequested=YES;self.automaticInstallation=YES;
            [self.log writeData:[@"\n[Virtual Intel Mac] macOS setup completed; beginning initial graphics installation.\n" dataUsingEncoding:NSUTF8StringEncoding]];
        } else if(claimError)self.guestStatus.stringValue=claimError.localizedDescription;
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
                if([session.reconciledBoot isEqual:boot]){
                    [session updateClipboardForBoot:boot];
                    [session restoreDisplaySelectionForBoot:boot];
                }
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
                    session.window.subtitle=@"Installing Graphics Drivers…";
                    if(!session.verifiedUpdateRepair)
                        [session showSetupNoticeAutomatic:NO automaticLogin:NO restarting:NO];
                    void (^applyPrepared)(NSError *)=^(NSError *archiveError){
                    if(archiveError) {
                        [session dismissSetupNotice];
                        session.preparing=NO;session.window.subtitle=@"";session.guestStatus.stringValue=archiveError.localizedDescription;
                        [session.guestProgress updateMode:@"failed" fraction:0 message:@"The previous installation record could not be preserved. Nothing was installed." completion:nil];
                        VIMPresentError(archiveError,session.window);return;
                    }
                    [VIMGuestInstallation applyWithAgent:session.guestAgent preparation:receipt
                        journal:[session.machine.URL URLByAppendingPathComponent:@"State/GuestInstallation.plist"]
                        completion:^(NSDictionary *outcome,NSError *installError) {
                            VIMSession *current=weakSession;if(!current)return;current.preparing=NO;current.window.subtitle=@"";
                            [current dismissSetupNotice];
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
    if(!error&&[report[@"DesktopRunning"] isEqual:@YES]) {
        [self updateClipboardForBoot:boot];
        [self restoreDisplaySelectionForBoot:boot];
    }
    self.guestTimer=[NSTimer scheduledTimerWithTimeInterval:(error||self.preparationRequested||![report[@"DesktopRunning"] isEqual:@YES]||(!self.guestIPAddresses.count&&self.networkLinkConnected))?15:60 repeats:NO block:^(NSTimer *timer){
        [weakSelf refreshGuestStatus];
    }];
}
- (void)restoreDisplaySelectionForBoot:(NSString *)boot {
    // Updating the PVG panel's pixel size does not select macOS's logical
    //1x/2x mode. Reassert the saved selection once after login, using the same
    // console helper as the menu. Never poll or run a guest tool per resize.
    if(!boot.length||[boot isEqual:self.displaySelectedBoot]||
       self.displayPolicyRestartRequired||
       self.displaySelectionRetryAfter.timeIntervalSinceNow>0||
       !self.machine.hasPublishedGuestGraphics||!self.clipboardUID||
       self.preparing||self.preparationRequested||self.repairRequested||
       self.guestPending||self.clipboardPending||self.bootPolicyPending||
       self.displayModePending||self.shutdownRequested)return;
    NSString *runtime=self.runtimeDirectory;self.displaySelectedBoot=boot;
    [self applyDisplayMode:self.displayMode completion:^(NSDictionary *observed,NSError *failure){
        if(!self.task.running||![runtime isEqual:self.runtimeDirectory])return;
        if(failure) {
            self.displaySelectedBoot=nil;
            // Retry only through the ordinary cold readiness pass. A slow TCG
            // hot-plug must not become an immediate status/selection loop.
            self.displaySelectionRetryAfter=[NSDate dateWithTimeIntervalSinceNow:60];
            [self.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] Initial display selection pending: %@\n",failure.localizedDescription] dataUsingEncoding:NSUTF8StringEncoding]];
        } else self.displaySelectionRetryAfter=nil;
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
    if(self.shutdownRequested||self.closeAfterShutdown||self.relaunchForGraphics||!self.task.running)return;
    self.automaticInstallation=NO;self.relaunchForGraphics=YES;
    self.guestStatus.stringValue=@"Graphics support installed — restarting this Mac…";
    __weak VIMSession *weakSelf=self;
    // Changing emulated display hardware needs a new QEMU process, not an
    // in-guest reboot. Never force termination if macOS cannot shut down.
    self.handoffTimer=[NSTimer scheduledTimerWithTimeInterval:180 repeats:NO block:^(NSTimer *timer){
        VIMSession *session=weakSelf;
        // A TCG guest can still be finalizing its startup files. Elapsed
        // time is not a failed shutdown: keep waiting for the normal exit.
        if(session.task.running&&session.relaunchForGraphics)
            session.guestStatus.stringValue=@"macOS is still shutting down. This Mac will restart when it’s ready.";
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
    if(item.action==@selector(ejectOpticalMedia:))return !self.mediaChangePending&&[self.class imageRecordForMachine:self.machine slot:[item.representedObject unsignedIntegerValue]]!=nil;
    if(item.action==@selector(sendMenuShortcut:))return self.task.running&&!self.preparing;
    if(item.action==@selector(changeRetinaMode:)) {
        item.state=[self.displayMode isEqual:@[@"space",@"retina",@"scaled"][item.tag]]?NSControlStateValueOn:NSControlStateValueOff;
        return !self.displayModePending&&(item.tag!=1||self.supportsRetinaDisplay);
    }
    if(item.action==@selector(changeDisplayScalingFilter:)) {
        item.state=[self.displayScalingFilter isEqual:item.representedObject]?NSControlStateValueOn:NSControlStateValueOff;
        return YES;
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
        NSArray *images=[VIMSession connectedImagesForMachine:self.machine];
        if(!images.count)[self addDeviceItem:@"No Images Connected" action:nil toMenu:menu].enabled=NO;
        for(NSDictionary *image in images) {
            NSString *path=image[@"Path"];NSNumber *slot=image[@"Slot"];
            if(images.count==1) {
                [self addDeviceItem:path.lastPathComponent action:nil toMenu:menu].enabled=NO;
                [self addDeviceItem:@"Disconnect" action:@selector(ejectOpticalMedia:) toMenu:menu].representedObject=slot;
            } else {
                NSMenuItem *item=[self addDeviceItem:[NSString stringWithFormat:@"Disconnect “%@”",path.lastPathComponent] action:@selector(ejectOpticalMedia:) toMenu:menu];
                item.representedObject=slot;item.image=[NSImage imageWithSystemSymbolName:@"opticaldisc" accessibilityDescription:nil];
            }
        }
        if(images.count<VIMImageSlotCount){[menu addItem:NSMenuItem.separatorItem];[self addDeviceItem:@"Connect Image…" action:@selector(chooseOpticalMedia:) toMenu:menu];}
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
        // Replacing the group while AppKit is dismissing its pull-down can
        // leave the selected segment drawn as pressed. Update its menu in place.
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
- (void)ensureUSBClient {
    if(!self.usbClient) {
        self.usbClient=[VIMUSBClient new];
        self.usbClient.useUSB2Storage=[@[@"tiger",@"leopard",@"snowleopard"] containsObject:self.machine.guest];
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
        [session ensureUSBClient];
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
    void (^reset)(void)=^{
        if(!self.task.running||self.forceResetRequested)return;
        self.forceResetRequested=YES;self.shutdownRequested=NO;self.closeAfterShutdown=NO;
        self.relaunchForGraphics=NO;[self.displayView releasePressedKeys];
        self.displayView.displaySleeping=YES;
        NSTask *ownedTask=self.task;[ownedTask terminate];
        // Independent of QMP, QGA and any guest OS service. A wedged BQL must
        // not leave an explicitly requested force reset waiting forever.
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC),dispatch_get_main_queue(),^{
            if(self.task==ownedTask&&ownedTask.running)
                kill(ownedTask.processIdentifier,SIGKILL);
        });
    };
    if(VIMSkipsDevelopmentPowerConfirmation()){reset();return;}
    NSAlert *alert=[NSAlert new];alert.messageText=@"Reset this Mac?";alert.informativeText=@"This forcibly restarts the virtual Mac. Unsaved work will be lost, and an installation in progress may be interrupted.";
    [alert addButtonWithTitle:@"Cancel"];[alert addButtonWithTitle:@"Reset"];
    VIMPresentAlert(alert,self.window,^(NSModalResponse response){
        if(response==NSAlertSecondButtonReturn)reset();
    });
}
- (void)changeRetinaMode:(NSMenuItem *)sender {
    if(sender.tag<0||sender.tag>2||self.displayModePending)return;
    if(self.diagnosticPending){VIMPresentError(VIMSessionError(@"Wait for the current guest operation to finish, then change the resolution again."),self.window);return;}
    [self applyDisplayMode:@[@"space",@"retina",@"scaled"][sender.tag] completion:^(NSDictionary *observed,NSError *error){
        if(error)VIMPresentError(error,self.window);
    }];
}
- (BOOL)supportsRetinaDisplay {return VIMGuestSupportsRetinaDisplay(self.machine.editableConfiguration);}
- (NSString *)displayScalingFilter {return VIMDisplayScalingFilter(self.machine.editableConfiguration);}
+ (void)displayScalingPreferencesChangedForMachine:(VIMMachine *)machine {
    VIMSession *session=sessions[SessionKey(machine)];
    session.displayView.nearestNeighborScaling=[session.displayScalingFilter isEqual:@"nearest"];
    [NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
}
- (void)changeDisplayScalingFilter:(NSMenuItem *)sender {
    NSString *filter=sender.representedObject;
    if(![@[@"linear",@"nearest"] containsObject:filter])return;
    NSError *error=nil;
    if(![self.machine saveSettings:@{@"DisplayScalingFilter":filter} error:&error]){VIMPresentError(error,self.window);return;}
    [VIMSession displayScalingPreferencesChangedForMachine:self.machine];
}
- (void)applyDisplayMode:(NSString *)mode completion:(void (^)(NSDictionary *,NSError *))completion {
    if(![@[@"space",@"retina",@"scaled"] containsObject:mode]){completion(nil,VIMSessionError(@"Choose More Space, Retina or Scaled."));return;}
    if([mode isEqual:@"retina"]&&!self.supportsRetinaDisplay){completion(nil,VIMSessionError(@"This version of Mac OS X supports More Space and Scaled, but not Retina resolution."));return;}
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
    NSSize requested=VIMRequestedGuestPixelSize(self.displayView.bounds.size,factor,self.machine.preMetalGuest,
        [mode isEqual:@"retina"],self.openGLLargeResizeReady);
    NSUInteger width=requested.width,height=requested.height;
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
        [self.displayConnection requestWidth:width height:height
            density:self.machine.preMetalGuest?([mode isEqual:@"retina"]?2:1):0];
        [self.guestAgent runExecutable:@"/usr/bin/stat" arguments:@[@"-f",@"%u",@"/dev/console"] timeout:10 completion:^(NSDictionary *result,NSError *failure){
            NSString *uid=[[NSString alloc] initWithData:result[@"stdout"]?:NSData.data encoding:NSUTF8StringEncoding];
            uid=[uid stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
            if(failure||![result[@"exitcode"] isEqual:@0]||uid.integerValue<501||[uid rangeOfCharacterFromSet:NSCharacterSet.decimalDigitCharacterSet.invertedSet].location!=NSNotFound) {
                finished(nil,failure?:VIMSessionError(@"Sign in to this Mac before changing its display resolution."));return;
            }
            NSString *tool=self.machine.preMetalGuest?@"/Library/Application Support/Virtual Intel Mac/GuestTools/guest-display":[self.displayToolDirectory stringByAppendingPathComponent:@"guest-display"];
            NSArray *arguments=@[@(width).stringValue,@(height).stringValue,[mode isEqual:@"retina"]?@"2":@"1"];
            [self.guestAgent runConsoleExecutable:tool arguments:arguments timeout:15 completion:^(NSDictionary *reply,NSError *issue){
                NSString *detail=[[NSString alloc] initWithData:reply[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding];
                id observed=[NSJSONSerialization JSONObjectWithData:reply[@"stdout"]?:NSData.data options:0 error:nil];
                BOOL verified=[observed isKindOfClass:NSDictionary.class]&&[observed[@"PixelWidth"] isEqual:@(width)]&&
                    [observed[@"PixelHeight"] isEqual:@(height)]&&[observed[@"Scale"] isEqual:([mode isEqual:@"retina"]?@2:@1)];
                NSString *explanation=self.displayPolicyRestartRequired&&[mode isEqual:@"retina"]?
                    @"Restart this virtual Mac once to finish enabling Retina display modes.":
                    [@"macOS couldn’t apply and verify that display resolution. " stringByAppendingString:detail.length?detail:@"Try again after the desktop appears."];
                NSError *error=issue?:([reply[@"exitcode"] isEqual:@0]&&verified?nil:VIMSessionError(explanation));
                finished(verified?observed:nil,error);
            }];
        }];
    };
    if(self.machine.preMetalGuest||self.displayToolDirectory){selectMode();return;}
    NSURL *tools=[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:self.machine.preMetalGuest?[@"Runtime/OpenGLGuests/" stringByAppendingString:self.machine.guest]:@"Runtime/GuestTools"];
    [self.guestAgent stageFiles:@{@"guest-display":[tools URLByAppendingPathComponent:@"guest-display"]}
        progress:nil completion:^(NSDictionary *receipt,NSError *failure){
        if(failure){finished(nil,failure);return;}
        NSString *directory=receipt[@"Directory"];
        [self.guestAgent runExecutable:@"/bin/chmod" arguments:@[@"0755",[directory stringByAppendingPathComponent:@"guest-display"]] timeout:10 completion:^(NSDictionary *result,NSError *issue){
            if(issue||![result[@"exitcode"] isEqual:@0]){finished(nil,issue?:VIMSessionError(@"The display helper couldn’t be prepared."));return;}
            // The verified helper executes as the console user. Permit exact
            // path traversal, not directory listing or modification. This
            // staging directory contains only the two non-secret binaries.
            [self.guestAgent runExecutable:@"/bin/chmod" arguments:@[@"0711",directory] timeout:10 completion:^(NSDictionary *secured,NSError *problem){
                if(problem||![secured[@"exitcode"] isEqual:@0]){finished(nil,problem?:VIMSessionError(@"The display helper couldn’t be made available to the desktop."));return;}
                self.displayToolDirectory=directory;
                [self.guestAgent runExecutable:[directory stringByAppendingPathComponent:@"guest-display"]
                    arguments:@[@"--prepare-policy"] timeout:0 completion:^(NSDictionary *policy,NSError *policyError){
                    id observed=[NSJSONSerialization JSONObjectWithData:policy[@"stdout"]?:NSData.data options:0 error:nil];
                    if(policyError||![policy[@"exitcode"] isEqual:@0]||![observed isKindOfClass:NSDictionary.class]) {
                        finished(nil,policyError?:VIMSessionError(@"The native Retina display policy couldn’t be prepared. Try again after restarting this Mac."));return;
                    }
                    self.displayPolicyRestartRequired=[observed[@"RestartRequired"] isEqual:@YES];
                    selectMode();
                }];
            }];
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
    if(powerOnly&&(self.displayView.softwareCursor||self.machine.preMetalGuest))return;
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
    NSData *data=[NSJSONSerialization dataWithJSONObject:@{@"UID":self.clipboardUID?:@0,@"Transport":self.machine.preMetalGuest?@"isa-serial":@"virtio-serial",@"Token":token,@"DisplayPowerOnly":@(powerOnly)} options:0 error:NULL];
    NSURL *configuration=[NSURL fileURLWithPath:[self.runtimeDirectory stringByAppendingPathComponent:configName]];
    if(![data writeToURL:configuration options:NSDataWritingAtomic error:NULL])return;
    [NSFileManager.defaultManager setAttributes:@{NSFilePosixPermissions:@0600} ofItemAtPath:configuration.path error:NULL];
    NSURL *tools=[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:self.machine.preMetalGuest?[@"Runtime/OpenGLGuests/" stringByAppendingString:self.machine.guest]:@"Runtime/GuestTools"];
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
        if(!powerOnly&&ok)[session restoreDisplaySelectionForBoot:boot];
    };
    if(self.machine.preMetalGuest){
        step=@"installed GUI helper setup";
        [self.guestAgent runExecutable:@"/Library/Application Support/Virtual Intel Mac/GuestTools/guest-clipboard" arguments:@[@"--provision-stdin"] standardInput:data timeout:0 completion:^(NSDictionary *result,NSError *error){
            if(result[@"exitcode"])step=[NSString stringWithFormat:@"installed GUI helper setup (exit %@)",result[@"exitcode"]];
            finish(!error&&[result[@"exitcode"] isEqual:@0]);
        }];return;
    }
    [self.guestAgent stageFiles:@{@"guest-clipboard":[tools URLByAppendingPathComponent:@"guest-clipboard"],configName:configuration}
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
    NSString *key=[@"VIMExpandedToolbar-" stringByAppendingString:SessionKey(self.machine)];[NSUserDefaults.standardUserDefaults setBool:![self deviceToolbarExpanded] forKey:key];[self installWindowControls:self.task.running];
}
- (void)downloadApps:(id)sender {
    NSString *version=@"";
    for(NSDictionary *entry in [VIMInstallerCatalog currentCatalogWithError:nil].supportedInstallers)
        if([entry[@"Guest"] isEqual:self.machine.guest]){version=entry[@"Version"];break;}
    NSURLComponents *URL=[NSURLComponents componentsWithString:@"https://nfzerox.github.io/virtual-mac/intel/app/index.html"];
    URL.queryItems=@[[NSURLQueryItem queryItemWithName:@"os" value:version],
        [NSURLQueryItem queryItemWithName:@"vm" value:[NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"]?:@""]];
    [NSWorkspace.sharedWorkspace openURL:URL.URL];
}
- (void)showCPUSettings:(id)sender {[VIMMachineSettings showForMachine:self.machine section:@"Hardware" category:@"CPU & Memory" window:self.window];}
- (void)showDiskSettings:(id)sender {[VIMMachineSettings showForMachine:self.machine section:@"Hardware" category:@"Hard Disk" window:self.window];}
+ (void)chooseOpticalMediaForMachine:(VIMMachine *)machine window:(NSWindow *)window {
    [self chooseImageForMachine:machine slot:NSNotFound window:window];
}
+ (void)chooseImageForMachine:(VIMMachine *)machine slot:(NSUInteger)slot window:(NSWindow *)window {
    NSArray *connected=[self connectedImagesForMachine:machine];
    if(connected.count>=VIMImageSlotCount){VIMPresentError(VIMSessionError(@"Four images are already connected. Disconnect an image before adding another."),window);return;}
    if(slot!=NSNotFound&&[self imageRecordForMachine:machine slot:slot]) {
        NSAlert *alert=[NSAlert new];alert.messageText=@"Disconnect the current image first";
        alert.informativeText=@"Eject the image in the virtual Mac, then choose Disconnect before connecting a different image.";
        [alert addButtonWithTitle:@"OK"];VIMPresentAlert(alert,window,nil);return;
    }
    NSOpenPanel *panel=[NSOpenPanel openPanel];// QEMU reads these as raw 2048-byte CD/DVD sectors (ISO, CDR, Toast) or
    // through its read-only UDIF driver (DMG). The container is still
    // identified from its contents, not this extension, before connecting.
    panel.allowedContentTypes=@[[UTType typeWithFilenameExtension:@"iso"],[UTType typeWithFilenameExtension:@"cdr"],
        [UTType typeWithFilenameExtension:@"toast"],[UTType typeWithFilenameExtension:@"dmg"]];
    panel.allowsMultipleSelection=slot==NSNotFound;panel.prompt=@"Connect";
    panel.message=@"Choose ISO, CDR, Toast or unencrypted DMG images. Each image connects to its own read-only drive, without copying or conversion.";
    [panel beginSheetModalForWindow:window completionHandler:^(NSModalResponse response){
        if(response!=NSModalResponseOK)return;
        NSArray<NSURL *> *URLs=panel.URLs;
        if(URLs.count>VIMImageSlotCount-[self connectedImagesForMachine:machine].count) {
            VIMPresentError(VIMSessionError(@"There aren’t enough empty image drives for this selection. Choose fewer images or disconnect an existing image first."),window);return;
        }
        __block void (^next)(NSUInteger);
        next=^(NSUInteger index){
            if(index>=URLs.count){next=nil;return;}
            NSUInteger target=slot;
            if(target==NSNotFound)for(target=0;target<VIMImageSlotCount;target++)if(![self imageRecordForMachine:machine slot:target])break;
            [self insertImage:URLs[index].path slot:target machine:machine completion:^(NSError *error){
                if(error){next=nil;VIMPresentError(error,window);return;}next(index+1);
            }];
        };next(0);
    }];
}
+ (NSDictionary *)imageRecordForMachine:(VIMMachine *)machine slot:(NSUInteger)slot {
    if(slot>=VIMImageSlotCount)return nil;
    VIMSession *session=sessions[SessionKey(machine)];
    if(session.task.running) {
        NSDictionary *active=slot?session.additionalMedia[@(slot)]:
            (session.insertedMediaPath.length?@{@"Path":session.insertedMediaPath,@"Format":session.insertedMediaFormat?:@"raw"}:nil);
        if(active)return active;
        NSString *saved=VIMImageSlotPath(machine.editableConfiguration,slot);
        // Even if saving a disconnect failed, keep its saved selection visible
        // and removable. It is not a mounted guest medium and needs no eject.
        return saved.length?@{@"Path":saved,@"Connected":@NO}:nil;
    }
    NSString *path=VIMImageSlotPath(machine.editableConfiguration,slot);
    return path.length?@{@"Path":path}:nil;
}
+ (NSArray<NSDictionary *> *)connectedImagesForMachine:(VIMMachine *)machine {
    NSMutableArray *images=[NSMutableArray new];
    for(NSUInteger slot=0;slot<VIMImageSlotCount;slot++) {
        NSDictionary *record=[self imageRecordForMachine:machine slot:slot];
        if(record){NSMutableDictionary *entry=[record mutableCopy];entry[@"Slot"]=@(slot);[images addObject:entry];}
    }
    return images;
}
+ (NSString *)connectedOpticalMediaForMachine:(VIMMachine *)machine {
    return [self imageRecordForMachine:machine slot:0][@"Path"]?:@"";
}
+ (void)ejectOpticalMediaForMachine:(VIMMachine *)machine window:(NSWindow *)window {
    [self ejectImageForMachine:machine slot:0 window:window];
}
+ (void)ejectImageForMachine:(VIMMachine *)machine slot:(NSUInteger)slot window:(NSWindow *)window {
    [self insertImage:@"" slot:slot machine:machine completion:^(NSError *error){
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
    [self insertImage:path slot:0 machine:machine completion:completion];
}
+ (void)insertImage:(NSString *)path slot:(NSUInteger)slot machine:(VIMMachine *)machine completion:(void (^)(NSError *))completion {
    if(slot>=VIMImageSlotCount){completion(VIMSessionError(@"Choose an available image drive."));return;}
    NSError *validation=nil;
    NSString *format=path.length?VIMOpticalImageFormat(path,&validation):nil;
    if(path.length&&!format){completion(validation);return;}
    VIMSession *session=sessions[SessionKey(machine)];
    void (^save)(NSError *)=^(NSError *failure){NSError *error=failure;if(!error)[machine saveSettings:VIMImageSlotSettings(machine.editableConfiguration,slot,path) error:&error];if(!error)[NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];completion(error);};
    void (^apply)(NSError *)=^(NSError *error){
        if(error){completion(error);return;}
        NSString *current=[self imageRecordForMachine:machine slot:slot][@"Path"];
        if(path.length&&current.length) {
            completion([current isEqual:path]?nil:VIMSessionError(@"Disconnect the current image before choosing another."));return;
        }
        if(!session.task.running){save(nil);return;}
        [session changeRemovableMedia:path format:format slot:slot completion:save];
    };
    if(!path.length){apply(nil);return;}
    NSString *root=VIMDevelopmentSourceRoot();
    NSString *tool=root.length?[NSDictionary dictionaryWithContentsOfFile:[root stringByAppendingPathComponent:@"Build/RuntimeLocation.plist"]][@"Directory"]:
        [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"Runtime/bin"];
    if(!tool.length){completion(VIMSessionError(@"Open the complete Virtual Intel Mac app to connect disk images."));return;}
    VIMCheckOpticalImage(path,format,[tool stringByAppendingPathComponent:@"qemu-img"],apply);
}
- (void)ensureImageDevice:(NSUInteger)slot completion:(void (^)(NSError *))completion {
    if(!slot||[self.imageDevices containsObject:@(slot)]){completion(nil);return;}
    NSString *transport=[NSString stringWithFormat:@"image-usb-%lu",(unsigned long)slot];
    NSString *backend=[NSString stringWithFormat:@"image-backend-%lu",(unsigned long)slot];
    // QMP blockdev-add creates a media node, not an empty BlockBackend.
    // QEMU's fixed native drive_add command creates that removable backend;
    // no shell, user-supplied monitor text or filename is passed through HMP.
    NSString *add=[NSString stringWithFormat:@"drive_add 0 id=%@,if=none,media=disk,readonly=on",backend];
    [self.control execute:@"human-monitor-command" arguments:@{@"command-line":add} completion:^(id result,NSError *error){
        if(!error&&(![result isKindOfClass:NSString.class]||![[result stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet] isEqual:@"OK"]))
            error=VIMSessionError(@"The virtual image drive could not be created.");
        if(error){completion(error);return;}
        __block BOOL transportCreated=NO;
        void (^finish)(NSError *)=^(NSError *failure){
            if(!failure){[self.imageDevices addObject:@(slot)];completion(nil);return;}
            void (^releaseBackend)(void)=^{
                [self.control execute:@"human-monitor-command" arguments:@{@"command-line":[@"drive_del " stringByAppendingString:backend]} completion:^(id result,NSError *ignored){completion(failure);}];
            };
            if(transportCreated)[self.control execute:@"device_del" arguments:@{@"id":transport} completion:^(id result,NSError *ignored){releaseBackend();}];
            else releaseBackend();
        };
        __block void (^claimPort)(NSUInteger);
        claimPort=^(NSUInteger port){
          [self.control execute:@"device_add" arguments:@{@"driver":@"usb-bot",@"id":transport,@"bus":VIMImageUSBBus(self.machine.guest),@"port":@(port).stringValue} completion:^(id result,NSError *failure){
            // Explicit root ports avoid QEMU silently adding its USB1 hub on
            // the last free port. Only retry actual occupied-port errors.
            if(failure&&port<VIMImageUSBPort(self.machine.guest,VIMImageSlotCount-1)&&[failure.localizedDescription containsString:@"not found (in use?)"]){claimPort(port+1);return;}
            claimPort=nil;
            if(failure){finish(failure);return;}
            transportCreated=YES;
            [self.control execute:@"device_add" arguments:@{@"driver":@"scsi-hd",@"id":VIMImageSlotDevice(slot,@"raw"),
                @"bus":[transport stringByAppendingString:@".0"],@"drive":backend,
                @"removable":@YES,@"logical_block_size":@512,@"physical_block_size":@512} completion:^(id result,NSError *failure){
                if(failure){finish(failure);return;}
                // Hot-plugged usb-bot stays detached until its child is ready.
                [self.control execute:@"qom-set" arguments:@{@"path":[@"/machine/peripheral/" stringByAppendingString:transport],@"property":@"attached",@"value":@YES}
                    completion:^(id result,NSError *failure){finish(failure);}];
            }];
          }];
        };claimPort(VIMImageUSBPort(self.machine.guest,1));
    }];
}
- (void)changeRemovableMedia:(NSString *)path format:(NSString *)format slot:(NSUInteger)slot completion:(void (^)(NSError *))completion {
    if(self.mediaChangePending){completion(VIMSessionError(@"Wait for the current disc change to finish, then try again."));return;}
    self.mediaChangePending=YES;
    // The public/UI record may describe an unavailable saved choice. Only
    // actual live media can require a QMP eject or own a device to unplug.
    NSDictionary *previous=slot?self.additionalMedia[@(slot)]:
        (self.insertedMediaPath.length?@{@"Path":self.insertedMediaPath,@"Format":self.insertedMediaFormat?:@"raw"}:nil);
    NSString *oldPath=previous[@"Path"]?:@"",*oldFormat=previous[@"Format"];
    void (^record)(NSString *,NSString *)=^(NSString *file,NSString *kind){
        if(!slot){self.insertedMediaPath=file;self.insertedMediaFormat=kind;}
        else if(file.length)self.additionalMedia[@(slot)]=@{@"Path":file,@"Format":kind};
        else [self.additionalMedia removeObjectForKey:@(slot)];
    };
    void (^finish)(NSError *)=^(NSError *error){
        self.mediaChangePending=NO;
        if(error&&[error.localizedDescription.localizedLowercaseString containsString:@"locked"])
            error=[NSError errorWithDomain:@"VirtualIntelMac.Media" code:1 userInfo:@{NSLocalizedDescriptionKey:@"This disc is in use",
                NSLocalizedRecoverySuggestionErrorKey:@"In the virtual Mac, eject the disc in Finder, then try again. Close any apps using it first. Your image hasn’t been changed.",NSUnderlyingErrorKey:error}];
        if(error&&path.length)error=VIMFriendlyDiscImageError(path,error);
        completion(error);
    };
    void (^insert)(NSString *,NSString *,void (^)(NSError *))=^(NSString *file,NSString *kind,void (^done)(NSError *)){
        [self.control execute:@"blockdev-change-medium" arguments:@{@"id":VIMImageSlotDevice(slot,kind),@"filename":file,@"format":kind,@"read-only-mode":@"read-only"} completion:^(id result,NSError *error){done(error);}];
    };
    void (^disconnect)(void)=^{
        if(!slot||![self.imageDevices containsObject:@(slot)]){record(@"",nil);finish(nil);return;}
        [self.control execute:@"device_del" arguments:@{@"id":[NSString stringWithFormat:@"image-usb-%lu",(unsigned long)slot]} completion:^(id result,NSError *error){
            if(!error){[self.imageDevices removeObject:@(slot)];record(@"",nil);}finish(error);
        }];
    };
    if(!path.length) {
        if(!oldPath.length){disconnect();return;}
        [self.control execute:@"eject" arguments:@{@"id":VIMImageSlotDevice(slot,oldFormat)} completion:^(id result,NSError *error){
            if(error){finish(error);return;}disconnect();
        }];return;
    }
    if(oldPath.length){finish([oldPath isEqual:path]?nil:VIMSessionError(@"Disconnect this drive’s current image before choosing another."));return;}
    [self ensureImageDevice:slot completion:^(NSError *failure){
        if(failure){finish(failure);return;}
        insert(path,format,^(NSError *error){if(!error)record(path,format);finish(error);});
    }];
}
- (void)chooseOpticalMedia:(id)sender {[VIMSession chooseOpticalMediaForMachine:self.machine window:self.window];}
- (void)ejectOpticalMedia:(NSMenuItem *)sender {[VIMSession ejectImageForMachine:self.machine slot:[sender.representedObject unsignedIntegerValue] window:self.window];}
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
    NSAlert *alert=[NSAlert new];
    VIMSetInstallerAlertIcon(alert,self.machine.guest);
    NSString *os=VIMOperatingSystemName(self.machine.guest);
    alert.messageText=[@"Installing " stringByAppendingString:os];
    NSDictionary *setup=[NSDictionary dictionaryWithContentsOfURL:[self.machine.URL URLByAppendingPathComponent:@"State/AutomaticSetup.plist"]];
    BOOL automatic=[setup[@"Enabled"] boolValue];
    NSString *instructions=automatic?@"Your virtual Mac will install the operating system and create your account automatically. You don’t need to erase the disk or choose Reinstall.":
        [@[@"tiger",@"leopard",@"snowleopard"] containsObject:self.machine.guest]?@"Follow the installer’s onscreen instructions and select Macintosh HD as the destination.":
        [NSString stringWithFormat:@"In %@ Utilities, choose Reinstall %@, then select Macintosh HD as the destination. Don’t select the Base System; it contains the installer.",os,os];
    alert.informativeText=[instructions stringByAppendingString:@"\n\nYour Mac may restart several times. Graphics support is installed automatically after setup. Keep this Mac running while setup finishes. Installation may take up to 2 hours."];
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
            // The agent can disappear before QMP reports guest shutdown. A
            // transport error is not proof that tools are missing, and a
            // modal alert here can outlive an otherwise successful shutdown.
            self.guestStatus.stringValue=@"The shutdown request couldn’t be confirmed. This Mac may already be shutting down. If it stays on, choose Shut Down inside the virtual Mac, or use Force Stop.";
            self.guestStatus.toolTip=connectionError.localizedDescription;
            [self.log writeData:[[NSString stringWithFormat:@"\n[Virtual Intel Mac] Shutdown connection: %@\n",connectionError.localizedDescription] dataUsingEncoding:NSUTF8StringEncoding]];
            return;
        }
        [self.guestAgent requestShutdownWithCompletion:^(NSError *error){
            if(!self.task.running)return;
            // Do not send a second shutdown or an ACPI sleep request after an
            // ambiguous agent disconnect. The task's exit is the completion.
            self.guestStatus.stringValue=error?@"Shutdown has not been confirmed. Wait for your virtual Mac to finish, or use Force Stop if it is unresponsive.":[NSString stringWithFormat:@"%@ is shutting down…",VIMOperatingSystemName(self.machine.guest)];
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
    void (^stop)(void)=^{
        if(!self.task.running)return;
        self.forceResetRequested=NO;self.relaunchForGraphics=NO;self.relaunchForGuestReboot=NO;self.shutdownRequested=YES;[self.task terminate];
    };
    if(VIMSkipsDevelopmentPowerConfirmation()){stop();return;}
    NSAlert *alert=[NSAlert new];alert.messageText=@"Force stop this Mac?";
    VIMSetAppAlertIcon(alert);
    alert.informativeText=@"Unsaved work will be lost. Try Shut Down first.";
    [alert addButtonWithTitle:@"Cancel"];[alert addButtonWithTitle:@"Force Stop"];
    [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse result){
        if(result==NSAlertSecondButtonReturn)stop();
    }];
}
- (void)toggleRelativeMouse:(id)sender {
    self.displayView.relativeMouseMode=!self.displayView.relativeMouseMode;
}
- (void)cleanUp {
    self.previewSetupNotice=NO;
    [self dismissSetupNotice];
    void (^waitingCompletion)(NSError *)=self.pendingHostConnectionCompletion;
    self.pendingHostConnectionURL=nil;self.pendingHostConnectionCompletion=nil;
    if(waitingCompletion)waitingCompletion(VIMSessionError(@"The virtual Mac stopped before the connection could open. Start it and try again."));
    [self.controlRetryTimer invalidate];self.controlRetryTimer=nil;
    self.bootPolicyPending=NO;self.bootPolicyCheckedBoot=nil;
    self.networkStarting=NO;[self.networkClient stop];self.networkClient=nil;self.guestIPAddresses=nil;
    [self.usbClient stop];self.usbClient=nil;self.usbOperationPending=NO;self.usbAttached=nil;
    [self.smbService stop];self.smbService=nil;
    [self.clipboard stop];self.clipboard=nil;self.clipboardPending=NO;self.clipboardConfiguredBoot=nil;
    [self.powerBridge stop];self.powerBridge=nil;self.powerConfiguredBoot=nil;self.powerRetryAfter=nil;
    self.sleepActivity=nil;
    self.displaySelectedBoot=nil;self.displaySelectionRetryAfter=nil;self.displayPolicyRestartRequired=NO;
    [self.handoffTimer invalidate];self.handoffTimer=nil;
    [self.guestTimer invalidate];self.guestTimer=nil;[self.guestAgent close];
    [self.statusTimer invalidate];self.statusTimer=nil;
    [self.resizeTimer invalidate];self.resizeTimer=nil;
    self.displayView.inputHandler=nil;self.displayView.relativeMouseModeObserver=nil;
    [self.displayConnection stop];[self.displayView discardFrameStorage];
    [self.control close];[self.log closeFile];self.log=nil;
    if(self.runtimeDirectory) {
        unlink([self.runtimeDirectory stringByAppendingPathComponent:@"control"].fileSystemRepresentation);
        unlink([self.runtimeDirectory stringByAppendingPathComponent:@"guest-agent"].fileSystemRepresentation);
        unlink([self.runtimeDirectory stringByAppendingPathComponent:@"recovery-agent"].fileSystemRepresentation);
        rmdir(self.runtimeDirectory.fileSystemRepresentation);
    }
    // Only called before launch or after NSTask reports termination.
    if(self.machineLock>=0){close(self.machineLock);self.machineLock=-1;}
    if(self.completionGuestReport&&!self.task.running) {
        NSError *error=nil;
        BOOL finished=self.machine.preMetalGuest?[self.machine retryInstallerCleanup:&error]:
            [self.machine finishInstallationWithGuestReport:self.completionGuestReport error:&error];
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
        BOOL retina=[session.displayMode isEqual:@"retina"];
        CGFloat factor=retina?2:(session.retinaEnabled?session.window.backingScaleFactor:1);
        NSSize pixels=VIMRequestedGuestPixelSize(session.displayView.bounds.size,factor,
            session.machine.preMetalGuest,retina,session.openGLLargeResizeReady);
        if(NSEqualSizes(pixels,session.lastRequestedSize))return;
        session.lastRequestedSize=pixels;
        [session.displayConnection requestWidth:pixels.width height:pixels.height
            density:session.machine.preMetalGuest?(retina?2:1):0];
    }];
}
@end
