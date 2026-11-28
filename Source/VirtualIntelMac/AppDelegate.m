#import "VIMLocalization.h"
#import "AppDelegate.h"
#import "VIMUpdateChecker.h"
#import "VIMLibraryLocation.h"
#import "VIMLibraryViewController.h"
#import "VIMSession.h"
#import "VIMMachine.h"
#import "VIMInstallerWindow.h"
#import "VIMHostSupport.h"
#import "VIMNewMachineWindow.h"
#import "VIMAlerts.h"
#import "VIMMenuController.h"
#import "VIMMachineSettings.h"
#import "VIMDiagnostics.h"
#import "VIMDiskMaintenance.h"
#import "VIMUSBClient.h"
#import "VIMNetworkClient.h"
#include <sys/sysctl.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

@interface VIMSettingsPage : NSView
@end
@implementation VIMSettingsPage
- (void)drawRect:(NSRect)dirtyRect {[NSColor.windowBackgroundColor setFill];NSRectFill(dirtyRect);}
- (void)viewDidChangeEffectiveAppearance {[super viewDidChangeEffectiveAppearance];self.needsDisplay=YES;}
@end

// Preference pages have different content heights. Keep the toolbar and top
// edge stationary instead of leaving the shorter Advanced page in a large box.
@interface VIMSettingsTabs : NSTabViewController
- (void)fitSelectedPage;
@end
@implementation VIMSettingsTabs
- (void)fitSelectedPage {
    NSWindow *window=self.view.window;
    if(!window||self.selectedTabViewItemIndex<0)return;
    NSSize size=self.tabViewItems[self.selectedTabViewItemIndex].viewController.preferredContentSize;
    if(size.width<=0||size.height<=0)return;
    CGFloat top=NSMaxY(window.frame);
    [window setContentSize:size];
    NSRect frame=window.frame;frame.origin.y=top-NSHeight(frame);
    [window setFrame:frame display:YES];
}
- (void)tabView:(NSTabView *)tabView didSelectTabViewItem:(NSTabViewItem *)item {
    [super tabView:tabView didSelectTabViewItem:item];
    dispatch_async(dispatch_get_main_queue(),^{[self fitSelectedPage];});
}
@end

@interface AppDelegate ()

@property (strong) IBOutlet NSWindow *window;
@property (strong) VIMUpdateChecker *updateChecker;
@property (strong) NSTimer *updateTimer;
@property (strong) VIMLibraryViewController *libraryController;
@property (strong) NSWindow *settingsWindow;
@property (strong) NSPopUpButton *libraryLocationPopup;
@property (strong) NSTimer *quitTimer;
@property (strong) NSButton *updatesCheckbox;
@property NSTimeInterval activatedAt;
@property (strong) NSTextField *updateResult;
@property BOOL quitConfirmationPending;
@property BOOL quitApproved;
@property VIMMenuController *menuController;
@end

@implementation AppDelegate

- (BOOL)applicationShouldSaveSecureApplicationState:(NSApplication *)application {return NO;}
- (BOOL)applicationShouldRestoreSecureApplicationState:(NSApplication *)application {return NO;}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    // Let AppKit resolve the compiled Icon Composer asset, including the
    // system's appearance-specific rendition, instead of overriding the Dock.
    NSArray *arguments=NSProcessInfo.processInfo.arguments;
    NSUInteger usbProbe=[arguments indexOfObject:@"--usb-interface-policy"];
    if(usbProbe!=NSNotFound&&usbProbe+4<arguments.count) {
        // Development harness uses the real signed app/XPC authorization path,
        // without starting a VM or opening a second emulator-control connection.
        NSMutableArray *numbers=[NSMutableArray new];BOOL valid=YES;
        for(NSUInteger index=usbProbe+1;index<usbProbe+4;index++) {
            const char *text=[arguments[index] UTF8String];char *end=NULL;errno=0;
            unsigned long long value=strtoull(text,&end,0);
            if(errno||!text[0]||text[0]=='-'||!end||*end||!value||value>(index==usbProbe+1?UINT32_MAX:UINT16_MAX))valid=NO;
            [numbers addObject:@(value)];
        }
        NSString *output=arguments[usbProbe+4];
        if(valid&&output.isAbsolutePath) {
            VIMUSBClient *client=[VIMUSBClient new];
            NSDictionary *identity=@{@"LocationID":numbers[0],@"VendorID":numbers[1],@"ProductID":numbers[2]};
            [client checkStorageInterface:identity inUserSession:![arguments containsObject:@"--usb-system-context"] completion:^(NSDictionary *result,NSError *error){
                // Retain the XPC owner until the one bounded request completes.
                (void)client;
                NSDictionary *response=error?@{@"Error":error.localizedDescription}:result;
                NSData *data=[NSJSONSerialization dataWithJSONObject:response options:NSJSONWritingPrettyPrinted error:NULL];
                if(data)[data writeToFile:output options:NSDataWritingAtomic error:NULL];
            }];
        }
    }
    if([arguments containsObject:@"--usb-helper-authorize-stdin"])
        [VIMUSBClient authorizeUsingTerminalWithCompletion:^(NSError *error){NSLog(@"USB helper authorization: %@",error.localizedDescription?:@"ready");}];
    if([arguments containsObject:@"--network-helper-authorize-stdin"])
        [VIMNetworkClient authorizeUsingTerminalWithCompletion:^(NSError *error){NSLog(@"Network helper authorization: %@",error.localizedDescription?:@"ready");}];
    NSUInteger helperCheck=[arguments indexOfObject:@"--check-privileged-helpers"];
    if(helperCheck!=NSNotFound&&helperCheck+1<arguments.count&&[arguments[helperCheck+1] isAbsolutePath]) {
        // Finite development check through the production clients: no USB
        // capture or guest required, and the temporary host-only network closes.
        NSString *output=arguments[helperCheck+1];NSMutableDictionary *results=[NSMutableDictionary new];
        VIMUSBClient *usb=[VIMUSBClient new];VIMNetworkClient *network=[VIMNetworkClient new];
        void (^record)(NSString *,NSError *)=^(NSString *kind,NSError *error){
            results[kind]=error?@{@"Ready":@NO,@"Error":error.localizedDescription}:@{@"Ready":@YES};
            if(results.count==2) {
                NSData *data=[NSJSONSerialization dataWithJSONObject:results options:NSJSONWritingPrettyPrinted error:NULL];
                [data writeToFile:output options:NSDataWritingAtomic error:NULL];
            }
        };
        [usb prepareWithCompletion:^(NSError *error){[usb stop];record(@"USB",error);}];
        [network startMode:@"host" bridge:@"" completion:^(NSError *error){[network stop];record(@"Network",error);}];
    }
    [NSUserDefaults.standardUserDefaults registerDefaults:@{@"VIMAutomaticUpdateChecks": @YES}];
    NSError *layoutError=nil;
    if(![VIMLibraryLocation prepareLayout:&layoutError]&&layoutError.code!=EBUSY)
        dispatch_async(dispatch_get_main_queue(),^{VIMPresentError(layoutError,self.window);});
    [VIMNewMachineWindow prewarmLibraryCapacity];
    self.updateChecker = [VIMUpdateChecker new];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(updateResultChanged:) name:@"VIMUpdateResultChanged" object:nil];
    if (!self.window) self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 920, 560)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.releasedWhenClosed = NO;
    self.libraryController = [VIMLibraryViewController new];
    self.window.title = VIMLocalized(@"app.virtual-intel-mac-library", @"Virtual Intel Mac Library");
    self.window.excludedFromWindowsMenu=YES;
    self.window.collectionBehavior=(self.window.collectionBehavior&~(NSWindowCollectionBehaviorFullScreenPrimary|NSWindowCollectionBehaviorFullScreenAuxiliary))|NSWindowCollectionBehaviorFullScreenNone;
    self.window.contentViewController = self.libraryController;
    [self.window setContentSize:NSMakeSize(540, 378)];
    [self.window center];
    NSMenuItem *format=VIMMenuItem(NSApp.mainMenu, @"vim.format");
    if(format)[NSApp.mainMenu removeItem:format];
    NSMenu *appMenu = NSApp.mainMenu.itemArray.firstObject.submenu;
    for(NSMenuItem *item in appMenu.itemArray)if(item.action==@selector(orderFrontStandardAboutPanel:)) {
        item.target=self;item.action=@selector(showAbout:);
    }
    for(NSMenuItem *item in appMenu.itemArray.copy)
        if([item.keyEquivalent isEqual:@","])[appMenu removeItem:item];
    NSMenuItem *settings=[[NSMenuItem alloc] initWithTitle:VIMLocalized(@"app.settings", @"Settings…") action:@selector(showSettings:) keyEquivalent:@","];
    settings.target=self;[appMenu insertItem:settings atIndex:MIN(2,appMenu.numberOfItems)];
    NSMenuItem *check = [[NSMenuItem alloc] initWithTitle:VIMLocalized(@"app.check-for-updates-2", @"Check for Updates…")
        action:@selector(checkForUpdates:) keyEquivalent:@""];
    check.target = self; [appMenu insertItem:check atIndex:MIN(2, appMenu.numberOfItems)];
    NSMenu *help = [[NSMenu alloc] initWithTitle:VIMLocalized(@"app.help", @"Help")];
    [help addItemWithTitle:VIMLocalized(@"app.virtual-intel-mac-project", @"Virtual Intel Mac Project") action:@selector(visitProject:) keyEquivalent:@""].target = self;
    [help addItemWithTitle:VIMLocalized(@"app.get-help-from-community", @"Get Help from Community") action:@selector(reportIssue:) keyEquivalent:@""].target = self;
    [help addItemWithTitle:VIMLocalized(@"app.report-an-issue", @"Report an Issue…") action:@selector(reportIssue:) keyEquivalent:@""].target = self;
    NSMenuItem *helpItem = nil;
    for (NSMenuItem *item in NSApp.mainMenu.itemArray) if (item.submenu == NSApp.helpMenu) helpItem = item;
    if (!helpItem) { helpItem = [NSMenuItem new]; [NSApp.mainMenu addItem:helpItem]; }
    helpItem.submenu = help; NSApp.helpMenu = help;
    self.menuController=[[VIMMenuController alloc] initWithLibrary:self.libraryController];
    // UI iteration can coexist with a separate app instance finishing media.
    // Relaunch opens the library, never an unsolicited creation assistant.
    // Saved downloads remain resumable when the user explicitly opens setup.
    if([arguments containsObject:@"--new-mac"])[VIMNewMachineWindow showWithInstaller:nil guest:nil];
    NSUInteger newGuestConfiguration=[arguments indexOfObject:@"--configure-new-guest"];
    if(newGuestConfiguration!=NSNotFound&&newGuestConfiguration+1<arguments.count)
        [VIMNewMachineWindow showConfigurationForGuest:arguments[newGuestConfiguration+1]];
    NSUInteger creationCapture=[arguments indexOfObject:@"--creation-capture"];
    if(creationCapture!=NSNotFound&&creationCapture+1<arguments.count&&[arguments[creationCapture+1] isAbsolutePath]) {
        NSString *path=arguments[creationCapture+1];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),dispatch_get_main_queue(),^{
            for(NSWindow *window in NSApp.windows)if([window.windowController isKindOfClass:VIMNewMachineWindow.class]) {
                NSView *view=window.contentView;[view layoutSubtreeIfNeeded];
                NSBitmapImageRep *image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];
                [view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
                [[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];break;
            }
        });
    }
    // Unattended acceptance: the same Create as the configuration step. The
    // password comes from the environment, never the argument list.
    NSUInteger newGuestCreation=[arguments indexOfObject:@"--create-new-guest"];
    if(newGuestCreation!=NSNotFound&&newGuestCreation+1<arguments.count)
        [VIMNewMachineWindow createGuest:arguments[newGuestCreation+1]
                  automaticSetupPassword:NSProcessInfo.processInfo.environment[@"VIM_DEVELOPMENT_GUEST_PASSWORD"]];
    NSUInteger installerDownload=[arguments indexOfObject:@"--download-installer"];
    if(installerDownload!=NSNotFound&&installerDownload+1<arguments.count)
        [VIMNewMachineWindow downloadInstallerForGuest:arguments[installerDownload+1]];
    self.updateTimer = [NSTimer scheduledTimerWithTimeInterval:3600 repeats:YES block:^(NSTimer *timer) {
        [self.updateChecker checkManually:NO];
    }];
    [self.updateChecker checkManually:NO];
    if([arguments containsObject:@"--show-settings"]) {
        [self showSettings:nil];
        NSUInteger tab=[arguments indexOfObject:@"--settings-tab"];
        if(tab!=NSNotFound&&tab+1<arguments.count&&[arguments[tab+1] isEqual:@"Advanced"])
            [(NSTabViewController *)self.settingsWindow.contentViewController setSelectedTabViewItemIndex:1];
        NSUInteger capture=[arguments indexOfObject:@"--settings-capture"];
        if(capture!=NSNotFound&&capture+1<arguments.count&&[arguments[capture+1] isAbsolutePath]) {
            NSString *path=arguments[capture+1];
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),dispatch_get_main_queue(),^{
                NSView *view=self.settingsWindow.contentView;[view layoutSubtreeIfNeeded];
                NSBitmapImageRep *image=[view bitmapImageRepForCachingDisplayInRect:view.bounds];
                [view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
                [[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];
            });
        }
    }
    if([arguments containsObject:@"--about"])[self showAbout:nil];
    for(NSUInteger openIndex=1;openIndex<arguments.count;openIndex++) {
        BOOL prepare=[arguments[openIndex] isEqual:@"--prepare-vm"];
        BOOL install=[arguments[openIndex] isEqual:@"--install-vm"];
        BOOL repair=[arguments[openIndex] isEqual:@"--repair-vm"];
        BOOL show=[arguments[openIndex] isEqual:@"--show-vm"];
        BOOL configure=[arguments[openIndex] isEqual:@"--configure-vm"];
        if(!show&&!configure&&!prepare&&!install&&!repair&&![arguments[openIndex] isEqual:@"--open-vm"])continue;
        NSError *error=nil;
        VIMMachine *machine=openIndex+1<arguments.count?[VIMMachine loadURL:[NSURL fileURLWithPath:arguments[openIndex+1]] error:&error]:nil;
        if(machine) {
            // Explicit harness option uses the same persistent settings path as
            // Configure. It is not a second emulator/network implementation.
            NSUInteger network=[arguments indexOfObject:@"--network-mode"];
            if(network!=NSNotFound) {
                NSString *mode=network+1<arguments.count?arguments[network+1]:@"";
                if(![@[@"shared",@"host",@"bridged"] containsObject:mode]) {
                    VIMPresentError([NSError errorWithDomain:@"VirtualIntelMac.Arguments" code:64 userInfo:@{NSLocalizedDescriptionKey:@"--network-mode requires shared, host, or bridged."}],nil);continue;
                }
                NSMutableDictionary *settings=[@{@"NetworkMode":mode,@"NetworkConnected":@YES} mutableCopy];
                NSUInteger bridge=[arguments indexOfObject:@"--bridge-interface"];
                if(bridge!=NSNotFound&&bridge+1<arguments.count)settings[@"BridgeInterface"]=arguments[bridge+1];
                if(![machine saveSettings:settings error:&error]){VIMPresentError(error,nil);continue;}
            }
            NSUInteger renderer=[arguments indexOfObject:@"--opengl-renderer"];
            if(renderer!=NSNotFound) {
                NSString *value=renderer+1<arguments.count?arguments[renderer+1]:@"";
                if(![machine.guest isEqual:@"mojave"]||![@[@"passthrough",@"apple"] containsObject:value]) {
                    VIMPresentError([NSError errorWithDomain:@"VirtualIntelMac.Arguments" code:64 userInfo:@{NSLocalizedDescriptionKey:@"--opengl-renderer requires a Mojave Mac and passthrough or apple."}],nil);continue;
                }
                if(![machine saveSettings:@{@"PassthroughOpenGLEnabled":@([value isEqual:@"passthrough"])} error:&error]) {
                    VIMPresentError(error,nil);continue;
                }
            }
            if(configure) {
                [VIMMachineSettings showForMachine:machine window:nil completion:nil];
                NSUInteger paneIndex=[arguments indexOfObject:@"--configuration-pane"];
                NSString *pane=paneIndex!=NSNotFound&&paneIndex+1<arguments.count?arguments[paneIndex+1]:nil;
                if(pane)[VIMMachineSettings showForMachine:machine section:[@[@"File Sharing",@"Remote Login",@"Clipboard"] containsObject:pane]?@"Sharing":([pane isEqual:@"General"]?@"General":@"Hardware") category:pane window:nil];
                if([arguments containsObject:@"--configuration-cycle-check"])
                    [VIMMachineSettings checkPaneCyclingForMachine:machine completion:^(BOOL passed){NSLog(@"Configuration pane cycling: %@ (21 transitions, outgoing table layout and sidebar elasticity)",passed?@"PASS":@"FAIL");}];
                NSUInteger captureIndex=[arguments indexOfObject:@"--configuration-capture"];
                if(captureIndex!=NSNotFound&&captureIndex+1<arguments.count&&[arguments[captureIndex+1] isAbsolutePath]) {
                    NSURL *destination=[NSURL fileURLWithPath:arguments[captureIndex+1]];
                    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),dispatch_get_main_queue(),^{
                        NSError *failure=nil;if(![VIMMachineSettings captureForMachine:machine toURL:destination error:&failure]&&failure)VIMPresentError(failure,NSApp.mainWindow);
                    });
                }
            } else if(show) {
                [VIMSession showMachineWindow:machine];
                NSUInteger toolbarCheck=[arguments indexOfObject:@"--toolbar-inspect"];
                if(toolbarCheck!=NSNotFound&&toolbarCheck+1<arguments.count&&[arguments[toolbarCheck+1] isAbsolutePath]) {
                    NSString *output=arguments[toolbarCheck+1];
                    dispatch_async(dispatch_get_main_queue(),^{
                        NSDictionary *result=[VIMSession inspectToolbarForMachine:machine];
                        [[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:NULL]
                            writeToFile:output options:NSDataWritingAtomic error:NULL];
                    });
                }
            }
            else if(!(repair?[VIMSession repairMachine:machine error:&error]:(install?[VIMSession installMachine:machine error:&error]:(prepare?[VIMSession prepareMachine:machine error:&error]:[VIMSession openMachine:machine error:&error]))))VIMPresentError(error,nil);
        }
        else if(error) VIMPresentError(error,nil);
        else VIMPresentError([NSError errorWithDomain:@"VirtualIntelMac.Arguments" code:64
            userInfo:@{NSLocalizedDescriptionKey:@"--configure-vm/--show-vm/--open-vm/--prepare-vm/--install-vm requires a Mac package path."}],nil);
        openIndex++;
    }
    int translated=0;size_t translatedSize=sizeof(translated);
    if(sysctlbyname("sysctl.proc_translated",&translated,&translatedSize,NULL,0)==0&&translated) {
        NSAlert *alert=[NSAlert new];alert.messageText=VIMLocalized(@"app.turn-off-rosetta-for-virtual-intel-mac", @"Turn off Rosetta for Virtual Intel Mac");
        alert.informativeText=VIMLocalized(@"app.virtual-intel-mac-needs-to-run-natively-on-this-apple", @"Virtual Intel Mac needs to run natively on this Apple silicon Mac. Quit the app, select it in Finder, and choose File → Get Info. Turn off “Open using Rosetta”, then open the app again.");
        [alert addButtonWithTitle:VIMLocalized(@"app.show-in-finder", @"Show in Finder")];[alert addButtonWithTitle:VIMLocalized(@"app.not-now", @"Not Now")];
        VIMPresentAlert(alert,NSApp.mainWindow,^(NSModalResponse response){if(response==NSAlertFirstButtonReturn)[NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[NSBundle.mainBundle.bundleURL]];});
    }
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {
    if([VIMDiskMaintenance hasActiveOperation])return NSTerminateCancel;
    if(self.quitConfirmationPending)return NSTerminateCancel;
    if(VIMNewMachineWindow.isCreating) {
        self.quitApproved=NO;
        NSAlert *alert=[NSAlert new];alert.messageText=VIMLocalized(@"app.a-mac-is-being-created", @"A Mac is being created");
        alert.informativeText=VIMLocalized(@"app.wait-for-the-new-disk-to-finish-being-created-before", @"Wait for the new disk to finish being created before quitting. Your existing Macs are unchanged.");
        [alert addButtonWithTitle:VIMLocalized(@"common.ok", @"OK")];VIMPresentAlert(alert,nil,nil);return NSTerminateCancel;
    }
    if(VIMSession.hasRunningMachines&&!self.quitApproved) {
        NSAlert *alert=[NSAlert new];alert.messageText=VIMLocalized(@"app.shut-down-your-macs-and-quit", @"Shut down your Macs and quit?");
        alert.informativeText=[NSString stringWithFormat:VIMLocalized(@"app.running-save-your-work-in-each-virtual-mac-before-shutting", @"Running: %@\n\nSave your work in each virtual Mac before shutting down. Force Stop stops immediately without letting macOS finish."),[[VIMSession runningMachineNames] componentsJoinedByString:@", "]];
        [alert addButtonWithTitle:VIMLocalized(@"app.shut-down-and-quit", @"Shut Down and Quit")];[alert addButtonWithTitle:VIMLocalized(@"common.cancel", @"Cancel")];[alert addButtonWithTitle:VIMLocalized(@"app.force-stop-and-quit", @"Force Stop and Quit")];
        self.quitConfirmationPending=YES;
        VIMPresentAlert(alert,nil,^(NSModalResponse response){
            self.quitConfirmationPending=NO;
            if(response==NSAlertFirstButtonReturn)[self waitForShutdown];
            else if(response==NSAlertThirdButtonReturn){self.quitApproved=YES;[NSApp terminate:nil];}
            else {[self.quitTimer invalidate];self.quitTimer=nil;}
        });return NSTerminateCancel;
    }
    self.quitApproved=NO;
    [self.quitTimer invalidate];self.quitTimer=nil;
    if([VIMNewMachineWindow stopForTermination:^{
        [VIMSession stopAll];[NSApp replyToApplicationShouldTerminate:YES];
    }])return NSTerminateLater;
    [VIMSession stopAll];return NSTerminateNow;
}
- (void)waitForShutdown {
    [self.quitTimer invalidate];[VIMSession requestShutdownAll];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:60];
    __weak typeof(self) weakSelf=self;
    self.quitTimer=[NSTimer scheduledTimerWithTimeInterval:1 repeats:YES block:^(NSTimer *timer){
        if(!VIMSession.hasRunningMachines){[timer invalidate];weakSelf.quitTimer=nil;[NSApp terminate:nil];return;}
        if(deadline.timeIntervalSinceNow>0)return;
        [timer invalidate];weakSelf.quitTimer=nil;
        NSAlert *alert=[NSAlert new];alert.messageText=VIMLocalized(@"app.your-virtual-mac-is-still-shutting-down", @"Your virtual Mac is still shutting down");
        alert.informativeText=VIMLocalized(@"app.the-app-will-stay-open-so-you-can-check-the", @"The app will stay open so you can check the virtual Mac’s window. If guest tools are unavailable, choose Shut Down from its Apple menu. Use Force Stop only if it is unresponsive.");
        [alert addButtonWithTitle:VIMLocalized(@"app.keep-app-open", @"Keep App Open")];VIMPresentAlert(alert,nil,nil);
    }];
    [NSRunLoop.mainRunLoop addTimer:self.quitTimer forMode:NSRunLoopCommonModes];
}

- (void)checkForUpdates:(id)sender { [self.updateChecker checkManually:YES]; }
- (void)showInstallers:(id)sender { [VIMNewMachineWindow showWithInstaller:nil guest:nil]; }
- (void)newMac:(id)sender { [VIMNewMachineWindow showWithInstaller:nil guest:nil]; }
- (void)showLibrary:(id)sender { [self.libraryController showLibrary]; }
- (void)application:(NSApplication *)application openURLs:(NSArray<NSURL *> *)URLs {
    for(NSURL *URL in URLs) {
        NSError *error=nil;
        if(!URL.isFileURL||![URL.pathExtension.lowercaseString isEqual:@"vintelmac"]) {
            error=[NSError errorWithDomain:@"VirtualIntelMac.Documents" code:1 userInfo:@{NSLocalizedDescriptionKey:VIMLocalized(@"app.choose-a-virtual-intel-mac-package-vintelmac", @"Choose a Virtual Intel Mac package (.vintelmac).")}];
        } else {
            [self.libraryController registerPackageURL:URL];
            VIMMachine *machine=[VIMMachine loadURL:URL error:&error];
            if(machine)[VIMSession showMachineWindow:machine];
        }
        if(error)VIMPresentError(error,nil);
    }
    [self.libraryController reloadLibrary];
}
- (void)applicationDidBecomeActive:(NSNotification *)notification {
    self.activatedAt=NSProcessInfo.processInfo.systemUptime;
}
- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender hasVisibleWindows:(BOOL)visible {
    // A Dock click that only brings the app forward keeps the user's existing
    // windows; a click while already frontmost (or with no window) opens the
    // library. The reopen event can arrive just before or just after
    // activation; either way the app was in the background when clicked.
    BOOL background=!NSApp.isActive||NSProcessInfo.processInfo.systemUptime-self.activatedAt<0.5;
    if(!visible||!background)[self.libraryController showLibrary];
    return NO;
}
- (void)chooseLibraryFolder:(id)sender {
    [VIMLibraryLocation chooseFromWindow:self.settingsWindow?:self.window completion:^(BOOL changed) {
        if (changed) {
            self.window.representedURL = VIMLibraryLocation.currentURL;
            [self.libraryController reloadLibrary];
        }
        [self refreshLibraryLocationPopup];
    }];
}
- (void)refreshLibraryLocationPopup {
    NSPopUpButton *popup=self.libraryLocationPopup;[popup removeAllItems];
    [popup addItemWithTitle:VIMLocalized(@"app.folders", @"Folders:")];popup.lastItem.enabled=NO;
    NSString *folderPath=VIMLibraryLocation.rootURL.path;
    [popup addItemWithTitle:folderPath];popup.lastItem.tag=0;
    NSImage *folderIcon=VIMLibraryLocation.folderIcon;
    folderIcon.size=NSMakeSize(16,16);popup.lastItem.image=folderIcon;
    [popup.menu addItem:NSMenuItem.separatorItem];
    if(![VIMLibraryLocation.rootURL.URLByResolvingSymlinksInPath.path isEqual:VIMLibraryLocation.defaultURL.URLByResolvingSymlinksInPath.path]) {
        [popup addItemWithTitle:VIMLocalized(@"app.default", @"Default")];popup.lastItem.tag=2;
        NSImage *defaultIcon=[NSWorkspace.sharedWorkspace iconForFile:VIMLibraryLocation.defaultURL.path].copy;
        defaultIcon.size=NSMakeSize(16,16);popup.lastItem.image=defaultIcon;
    }
    [popup addItemWithTitle:VIMLocalized(@"app.other", @"Other…")];popup.lastItem.tag=1;[popup selectItemAtIndex:1];
}
- (void)libraryLocationChanged:(NSPopUpButton *)sender {
    if(sender.selectedItem.tag==2) {
        NSError *error=nil;NSURL *URL=VIMLibraryLocation.defaultURL;
        if([NSFileManager.defaultManager createDirectoryAtURL:URL withIntermediateDirectories:YES attributes:nil error:&error]&&[VIMLibraryLocation setURL:URL error:&error]) {
            self.window.representedURL=URL;[self.libraryController reloadLibrary];
        }else VIMPresentError(error,self.settingsWindow);
        [self refreshLibraryLocationPopup];return;
    }
    BOOL choose=sender.selectedItem.tag==1;[sender selectItemAtIndex:1];
    if(choose)[self chooseLibraryFolder:sender];
}
- (void)showSettings:(id)sender {
    if(!self.settingsWindow) {
        self.settingsWindow=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,640,330)
            styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
        self.settingsWindow.releasedWhenClosed=NO;self.settingsWindow.title=VIMLocalized(@"app.settings-2", @"Settings");
        self.settingsWindow.collectionBehavior=NSWindowCollectionBehaviorFullScreenNone;
        VIMSettingsTabs *tabs=[VIMSettingsTabs new];tabs.tabStyle=NSTabViewControllerTabStyleToolbar;
        NSViewController *general=[NSViewController new];general.view=[[VIMSettingsPage alloc] initWithFrame:NSMakeRect(0,0,640,330)];
        self.libraryLocationPopup=[NSPopUpButton new];self.libraryLocationPopup.target=self;
        self.libraryLocationPopup.action=@selector(libraryLocationChanged:);self.libraryLocationPopup.autoenablesItems=NO;
        self.libraryLocationPopup.accessibilityLabel=VIMLocalized(@"app.virtual-macs-folder", @"Virtual Intel Mac folder");
        [self.libraryLocationPopup.widthAnchor constraintEqualToConstant:400].active=YES;
        self.libraryLocationPopup.cell.lineBreakMode=NSLineBreakByTruncatingMiddle;[self refreshLibraryLocationPopup];
        NSButton *updates=[NSButton checkboxWithTitle:VIMLocalized(@"app.automatically-check-for-updates", @"Automatically check for updates") target:self action:@selector(toggleAutomaticUpdates:)];
        self.updatesCheckbox=updates;
        updates.state=[NSUserDefaults.standardUserDefaults boolForKey:@"VIMAutomaticUpdateChecks"];
        NSButton *showLibrary=[NSButton buttonWithTitle:VIMLocalized(@"app.show-in-finder", @"Show in Finder") target:self action:@selector(revealLibrary:)];
        NSTextField *explanation=[NSTextField wrappingLabelWithString:VIMLocalized(@"app.changing-libraries-does-not-move-existing-macs-setups-already-in", @"Changing libraries does not move existing Macs. Setups already in progress keep their original location.")];explanation.textColor=NSColor.secondaryLabelColor;
        NSButton *downloads=[NSButton buttonWithTitle:VIMLocalized(@"app.show-downloads-in-finder", @"Show Downloads in Finder") target:self action:@selector(revealDownloads:)];
        NSStackView *libraryActions=[NSStackView stackViewWithViews:@[showLibrary,downloads]];libraryActions.spacing=12;
        NSButton *check=[NSButton buttonWithTitle:VIMLocalized(@"app.check-now", @"Check Now") target:self action:@selector(checkForUpdates:)];
        NSButton *diagnostics=[NSButton buttonWithTitle:VIMLocalized(@"app.export-diagnostics", @"Export Diagnostics…") target:self action:@selector(exportDiagnostics:)];
        NSStackView *library=[NSStackView stackViewWithViews:@[self.libraryLocationPopup,libraryActions,explanation]];library.orientation=NSUserInterfaceLayoutOrientationVertical;library.alignment=NSLayoutAttributeLeading;library.spacing=8;
        NSStackView *updateGroup=[NSStackView stackViewWithViews:@[updates,check]];updateGroup.orientation=NSUserInterfaceLayoutOrientationVertical;updateGroup.alignment=NSLayoutAttributeLeading;updateGroup.spacing=10;
        NSGridView *grid=[NSGridView gridViewWithViews:@[@[[NSTextField labelWithString:VIMLocalized(@"app.virtual-macs-folder-2", @"Virtual Intel Mac folder:")],library],@[[NSTextField labelWithString:VIMLocalized(@"app.updates", @"Updates:")],updateGroup],@[[NSTextField labelWithString:VIMLocalized(@"app.support", @"Support:")],diagnostics]]];
        grid.columnSpacing=14;grid.rowSpacing=24;[grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;[grid columnAtIndex:0].width=150;[grid columnAtIndex:1].xPlacement=NSGridCellPlacementLeading;
        for(NSInteger row=0;row<3;row++)[grid rowAtIndex:row].yPlacement=NSGridCellPlacementTop;
        [grid rowAtIndex:2].yPlacement=NSGridCellPlacementCenter;
        grid.translatesAutoresizingMaskIntoConstraints=NO;[general.view addSubview:grid];
        [NSLayoutConstraint activateConstraints:@[[grid.leadingAnchor constraintEqualToAnchor:general.view.leadingAnchor constant:24],[grid.trailingAnchor constraintEqualToAnchor:general.view.trailingAnchor constant:-24],[grid.topAnchor constraintEqualToAnchor:general.view.topAnchor constant:28],
            [grid.bottomAnchor constraintEqualToAnchor:general.view.bottomAnchor constant:-22],
            [general.view.widthAnchor constraintEqualToConstant:640],[library.widthAnchor constraintEqualToConstant:400]]];
        NSTabViewItem *item=[NSTabViewItem tabViewItemWithViewController:general];item.label=VIMLocalized(@"app.general", @"General");
        item.image=[NSImage imageWithSystemSymbolName:@"gearshape" accessibilityDescription:VIMLocalized(@"app.general", @"General")];
        [tabs addTabViewItem:item];
        NSViewController *advanced=[NSViewController new];advanced.view=[[VIMSettingsPage alloc] initWithFrame:NSMakeRect(0,0,640,330)];
        NSButton *keep=[NSButton checkboxWithTitle:VIMLocalized(@"app.keep-downloaded-files-and-installers", @"Keep downloaded files and installers") target:self action:@selector(changeInstallerRetention:)];
        keep.state=[NSUserDefaults.standardUserDefaults boolForKey:@"VIMKeepInstallers"]?NSControlStateValueOn:NSControlStateValueOff;
        NSButton *guestInstaller=[NSButton checkboxWithTitle:VIMLocalized(@"appdelegate.use-slower-guest-side-installation-for-troubleshooting", @"Use slower guest-side installation for troubleshooting") target:self action:@selector(changeGuestInstaller:)];
        guestInstaller.state=[NSUserDefaults.standardUserDefaults boolForKey:@"VIMUseGuestInstaller"]?NSControlStateValueOn:NSControlStateValueOff;
        NSButton *testPattern=[NSButton checkboxWithTitle:VIMLocalized(@"appdelegate.show-camera-test-pattern", @"Show the camera Test Pattern mode") target:self action:@selector(changeCameraTestPattern:)];
        testPattern.state=[NSUserDefaults.standardUserDefaults boolForKey:@"VIMShowCameraTestPattern"]?NSControlStateValueOn:NSControlStateValueOff;
        NSStackView *options=[NSStackView stackViewWithViews:@[keep,guestInstaller,testPattern]];options.orientation=NSUserInterfaceLayoutOrientationVertical;options.alignment=NSLayoutAttributeLeading;options.spacing=16;options.translatesAutoresizingMaskIntoConstraints=NO;[advanced.view addSubview:options];
        [NSLayoutConstraint activateConstraints:@[[options.leadingAnchor constraintEqualToAnchor:advanced.view.leadingAnchor constant:32],[options.trailingAnchor constraintEqualToAnchor:advanced.view.trailingAnchor constant:-32],[options.topAnchor constraintEqualToAnchor:advanced.view.topAnchor constant:28],[options.bottomAnchor constraintEqualToAnchor:advanced.view.bottomAnchor constant:-24],[advanced.view.widthAnchor constraintEqualToConstant:640]]];
        NSTabViewItem *advancedItem=[NSTabViewItem tabViewItemWithViewController:advanced];advancedItem.label=VIMLocalized(@"app.advanced", @"Advanced");advancedItem.image=[NSImage imageWithSystemSymbolName:@"gearshape.2" accessibilityDescription:VIMLocalized(@"app.advanced", @"Advanced")];[tabs addTabViewItem:advancedItem];
        self.settingsWindow.contentViewController=tabs;
        [general.view layoutSubtreeIfNeeded];
        NSSize fitting=NSMakeSize(640,ceil(general.view.fittingSize.height));
        general.preferredContentSize=fitting;tabs.preferredContentSize=fitting;
        advanced.preferredContentSize=NSMakeSize(640,ceil(options.fittingSize.height)+52);
        [self.settingsWindow setContentSize:fitting];[self.settingsWindow center];
    }
    [self refreshLibraryLocationPopup];
    [self.settingsWindow makeKeyAndOrderFront:nil];
}
- (void)changeInstallerRetention:(NSButton *)sender {
    [NSUserDefaults.standardUserDefaults setBool:sender.state==NSControlStateValueOn forKey:@"VIMKeepInstallers"];
}
- (void)changeGuestInstaller:(NSButton *)sender {
    [NSUserDefaults.standardUserDefaults setBool:sender.state==NSControlStateValueOn forKey:@"VIMUseGuestInstaller"];
}
- (void)changeCameraTestPattern:(NSButton *)sender {
    [NSUserDefaults.standardUserDefaults setBool:sender.state==NSControlStateValueOn forKey:@"VIMShowCameraTestPattern"];
    [NSNotificationCenter.defaultCenter postNotificationName:@"VIMCameraTestPatternChanged" object:nil];
}
- (void)exportDiagnostics:(id)sender {[VIMDiagnostics exportFromWindow:self.settingsWindow];}
- (void)showAbout:(id)sender {
    NSMutableParagraphStyle *paragraph=[NSMutableParagraphStyle new];paragraph.alignment=NSTextAlignmentCenter;paragraph.paragraphSpacing=5;
    NSArray *developers=@[@[@"Damon Lu",@"https://github.com/WhatDamon"],
        @[@"Ely Wright",@"https://github.com/KiwiOnGit"],
        @[@"Nihaal Sharma",@"https://github.com/neon443"],
        @[@"UrkiMimi",@"https://github.com/UrkiMimi"],
        @[@"Zach Bennoui",@"https://github.com/ZachB100"],
        @[@"いえなが",@"https://github.com/ienaga045"]];
    NSString *developerLine=[NSString stringWithFormat:VIMLocalized(@"appdelegate.developers", @"Developers: %@, %@, %@, %@, %@, and %@"),
        developers[0][0],developers[1][0],developers[2][0],developers[3][0],developers[4][0],developers[5][0]];
    NSString *project=[developerLine stringByAppendingFormat:@"\n%@",VIMLocalized(@"app.project-on-github", @"Project on GitHub")];
    NSMutableAttributedString *credits=[[NSMutableAttributedString alloc] initWithString:[NSString stringWithFormat:VIMLocalized(@"appdelegate.gpu-acceleration-and-app-ui-by-nfzerox-hybrid-hypervisor", @"GPU Acceleration and App UI by nfzerox\nHybrid Hypervisor by Sam Henri Gold\n%@"),project]
        attributes:@{NSFontAttributeName:[NSFont systemFontOfSize:12],NSForegroundColorAttributeName:NSColor.labelColor,NSParagraphStyleAttributeName:paragraph,NSUnderlineStyleAttributeName:@0}];
    [credits addAttribute:NSLinkAttributeName value:[NSURL URLWithString:@"https://github.com/nfzerox"] range:[credits.string rangeOfString:@"nfzerox"]];
    [credits addAttribute:NSLinkAttributeName value:[NSURL URLWithString:@"https://github.com/samhenrigold"] range:[credits.string rangeOfString:@"Sam Henri Gold"]];
    for(NSArray *developer in developers)[credits addAttribute:NSLinkAttributeName value:[NSURL URLWithString:developer[1]] range:[credits.string rangeOfString:developer[0]]];
    [credits addAttribute:NSLinkAttributeName value:[NSURL URLWithString:@"https://github.com/nfzerox/VirtualIntelMacOnAppleSilicon"] range:[credits.string rangeOfString:VIMLocalized(@"app.project-on-github", @"Project on GitHub")]];
    [NSApp orderFrontStandardAboutPanelWithOptions:@{NSAboutPanelOptionApplicationName:VIMLocalized(@"app.name", @"Virtual Intel Mac"),NSAboutPanelOptionCredits:credits}];
}
- (void)updateResultChanged:(NSNotification *)notification {
    self.updateResult.stringValue=[NSUserDefaults.standardUserDefaults stringForKey:@"VIMLastUpdateResult"]?:@"";
}
- (void)revealLibrary:(id)sender {[NSWorkspace.sharedWorkspace openURL:VIMLibraryLocation.rootURL];}
- (void)revealDownloads:(id)sender {
    NSURL *URL=VIMLibraryLocation.downloadsURL;
    if(![NSFileManager.defaultManager fileExistsAtPath:URL.path]) {
        NSAlert *alert=[NSAlert new];alert.messageText=VIMLocalized(@"app.no-downloads-yet", @"No downloads yet");alert.informativeText=VIMLocalized(@"app.downloads-will-appear-here-when-you-create-a-mac", @"Downloads will appear here when you create a Mac.");VIMPresentAlert(alert,self.settingsWindow,nil);return;
    }
    [NSWorkspace.sharedWorkspace openURL:URL];
}
- (void)toggleAutomaticUpdates:(NSMenuItem *)sender {
    BOOL enabled = ![NSUserDefaults.standardUserDefaults boolForKey:@"VIMAutomaticUpdateChecks"];
    [NSUserDefaults.standardUserDefaults setBool:enabled forKey:@"VIMAutomaticUpdateChecks"];
    sender.state = enabled ? NSControlStateValueOn : NSControlStateValueOff;
    self.updatesCheckbox.state=enabled;
    for(NSMenuItem *item in NSApp.mainMenu.itemArray.firstObject.submenu.itemArray)
        if(item.action==@selector(toggleAutomaticUpdates:))item.state=enabled;
    if (enabled) [self.updateChecker checkManually:NO];
}
- (void)visitProject:(id)sender {
    [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"https://github.com/nfzerox/VirtualIntelMacOnAppleSilicon"]];
}
- (void)reportIssue:(id)sender {
    [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"https://github.com/nfzerox/VirtualIntelMacOnAppleSilicon/issues?q=is%3Aissue"]];
}


- (void)applicationWillTerminate:(NSNotification *)aNotification {
    [self.updateTimer invalidate];
    [self.quitTimer invalidate];[NSNotificationCenter.defaultCenter removeObserver:self];
}


- (BOOL)applicationSupportsSecureRestorableState:(NSApplication *)app {
    return YES;
}


@end
