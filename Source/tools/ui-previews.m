// Render the real AppKit library/configuration views with explicitly synthetic
// models. No VM is launched and no package is modified. Optional composited
// previews capture only this harness's windows, with existing capture consent.
#import <Cocoa/Cocoa.h>
#import "VIMLibraryViewController.h"
#import "VIMMachine.h"
#import "VIMMachineSettings.h"
#import "VIMSession.h"
#import "VIMNewMachineWindow.h"
#import "VIMMenuController.h"
#import "AppDelegate.h"
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

@interface VIMLibraryViewController (Preview)
- (void)applyFilter;
- (BOOL)moveMachineAtPath:(NSString *)path beforeVisibleRow:(NSInteger)row;
@end
@interface AppDelegate (Preview)
- (void)showSettings:(id)sender;
- (void)showAbout:(id)sender;
@end
@interface VIMMenuController (Preview)
- (NSWindow *)activeWindow;
- (void)refresh;
@end
@interface VIMSession (RunStatePreview)
- (void)refreshRunState;
@end
@interface VIMPreviewRunningTask : NSObject
@end
@implementation VIMPreviewRunningTask
- (BOOL)isRunning {return YES;}
@end
@interface VIMPreviewRunStateControl : NSObject
@property NSError *failure;
@end
@implementation VIMPreviewRunStateControl
- (void)execute:(NSString *)command arguments:(NSDictionary *)arguments completion:(void (^)(id,NSError *))completion {
    NSCAssert([command isEqual:@"query-status"],@"Read-only status fixture");
    completion(@{@"status":@"running"},self.failure);
}
@end
@interface VIMPreviewMenus : VIMMenuController
@property NSWindow *windowContext;
@end
@implementation VIMPreviewMenus
- (NSWindow *)activeWindow {return self.windowContext;}
@end

@interface VIMNewMachineWindow (Preview)
- (void)updateOverallProgress:(double)fraction;
- (double)downloadProgressForFileFraction:(double)fraction;
- (NSAlert *)cancellationAlert;
- (void)beginSetup:(id)sender;
- (void)refreshDownloadDetail;
- (BOOL)windowShouldClose:(NSWindow *)window;
- (NSAlert *)creationAlertWithTitle:(NSString *)title detail:(NSString *)detail;
@end
@interface VIMPreviewCreation : VIMNewMachineWindow
@property NSAlert *lastAlert;
@end
@implementation VIMPreviewCreation
- (NSAlert *)creationAlertWithTitle:(NSString *)title detail:(NSString *)detail {
    self.lastAlert=[super creationAlertWithTitle:title detail:detail];return self.lastAlert;
}
@end

static void DismissPreviewSheet(NSWindow *window,NSModalResponse response) {
    NSWindow *sheet=window.attachedSheet;NSCAssert(sheet!=nil,@"Expected a native alert sheet");
    [NSApp endSheet:sheet returnCode:response];[sheet orderOut:nil];
    [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.05]];
}

static void Render(NSWindow *window,NSString *path) {
    [window.contentView layoutSubtreeIfNeeded];
    NSView *view=window.contentView;
    NSBitmapImageRep *rep=[view bitmapImageRepForCachingDisplayInRect:view.bounds];
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:rep];
    NSData *png=[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    if(![png writeToFile:path atomically:YES])abort();
    // Bitmap caching omits WindowServer's native glass/backdrop filters. This
    // opt-in check captures only our synthetic window, never the desktop, and
    // never asks for new Screen Recording permission.
    NSString *name=path.lastPathComponent;
    if(getenv("VIM_COMPOSITED_PREVIEWS")&&CGPreflightScreenCaptureAccess()&&
       ([name hasPrefix:@"Sharing-"]||[name containsString:@"Guest-Stopped"]||[name containsString:@"Advanced"])) {
        [window orderFrontRegardless];[window displayIfNeeded];
        [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.3]];
        NSTask *capture=[NSTask new];capture.executableURL=[NSURL fileURLWithPath:@"/usr/sbin/screencapture"];
        capture.arguments=@[@"-x",@"-o",@"-l",[@(window.windowNumber) stringValue],
            [[path stringByDeletingPathExtension] stringByAppendingString:@"-Composited.png"]];
        NSError *error=nil;NSCAssert([capture launchAndReturnError:&error],@"Window-only capture launches: %@",error);
        [capture waitUntilExit];NSCAssert(capture.terminationStatus==0,@"Window-only composited capture succeeds");
    }
}
static NSView *FindView(NSView *root,Class kind) {
    if([root isKindOfClass:kind])return root;
    for(NSView *child in root.subviews){NSView *match=FindView(child,kind);if(match)return match;}
    return nil;
}
static NSButton *FindGuestSettings(NSView *root,NSString *pane) {
    if([root isKindOfClass:NSButton.class]&&[root.identifier isEqual:pane]&&
       ((NSButton *)root).action==@selector(openGuestSettings:))return (id)root;
    for(NSView *child in root.subviews){NSButton *match=FindGuestSettings(child,pane);if(match)return match;}
    return nil;
}
static void CheckDragThumbnail(NSTableCellView *cell,NSString *path) {
    [cell layoutSubtreeIfNeeded];NSView *viewport=cell.imageView.superview;BOOL found=NO;
    for(NSDraggingImageComponent *component in cell.draggingImageComponents)if([component.key isEqual:NSDraggingImageComponentIconKey]) {
        NSCAssert(NSEqualRects(component.frame,[cell convertRect:viewport.bounds fromView:viewport]),@"Drag uses the visible clipped thumbnail frame");
        NSImage *image=component.contents;NSCAssert(NSEqualSizes(image.size,viewport.bounds.size),@"Drag wallpaper has the displayed aspect ratio, not the image view’s overflow");
        NSBitmapImageRep *rep=[NSBitmapImageRep imageRepWithData:image.TIFFRepresentation];
        [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];found=YES;
    }
    NSCAssert(found,@"The real cell must provide its thumbnail drag component");
}

int main(int argc,const char *argv[]) {
    @autoreleasepool {
        if(argc!=3)return 64;
        NSString *source=@(argv[1]),*output=@(argv[2]);
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        VIMSession *statusFixture=[[VIMSession alloc] initWithWindow:nil];
        VIMPreviewRunStateControl *statusControl=[VIMPreviewRunStateControl new];
        NSTextField *statusLabel=[NSTextField labelWithString:@"Running"];
        [statusFixture setValue:[VIMPreviewRunningTask new] forKey:@"task"];
        [statusFixture setValue:statusControl forKey:@"control"];
        [statusFixture setValue:statusLabel forKey:@"status"];
        [statusFixture setValue:@"running" forKey:@"lastRunState"];
        statusControl.failure=[NSError errorWithDomain:NSPOSIXErrorDomain code:ETIMEDOUT userInfo:nil];
        [statusFixture refreshRunState];NSCAssert([statusLabel.stringValue isEqual:statusControl.failure.localizedDescription],@"Control errors are shown");
        statusControl.failure=nil;[statusFixture refreshRunState];
        NSCAssert([statusLabel.stringValue isEqual:@"Running"],@"A recovered query clears stale error text even when VM state is unchanged");
        [NSFileManager.defaultManager createDirectoryAtPath:output withIntermediateDirectories:YES attributes:nil error:nil];
        // Lock contention can be checked without a VM or a real disk image.
        NSURL *fixture=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:[@"vim-ui-lock-" stringByAppendingString:NSUUID.UUID.UUIDString]]];
        [NSFileManager.defaultManager createDirectoryAtURL:[fixture URLByAppendingPathComponent:@"State"] withIntermediateDirectories:YES attributes:nil error:nil];
        int lock=open([fixture URLByAppendingPathComponent:@"State/Session.lock"].fileSystemRepresentation,O_RDWR|O_CREAT,0600);
        NSCAssert(lock>=0&&flock(lock,LOCK_EX|LOCK_NB)==0,@"Fixture lock acquired");
        VIMMachine *locked=[VIMMachine new];[locked setValue:fixture forKey:@"URL"];
        [locked setValue:@{@"CPUCount":@4,@"MemoryMiB":@4096} forKey:@"configuration"];
        NSError *lockError=nil;
        NSCAssert([locked saveSettings:@{@"CPUCount":@6} error:&lockError]&&[locked.configuration[@"CPUCount"] isEqual:@4]&&[locked.editableConfiguration[@"CPUCount"] isEqual:@6],@"Live edits must be staged without modifying the running snapshot");
        flock(lock,LOCK_UN);close(lock);
        // Legacy imports have no DiskGiB. A 32-byte header must report virtual
        // capacity, not this tiny file's length or its host allocation.
        NSMutableData *header=[NSMutableData dataWithLength:32];
        memcpy(header.mutableBytes,"QFI\xfb\0\0\0\3",8);
        uint64_t virtualSize=CFSwapInt64HostToBig(160ULL<<30);memcpy((char *)header.mutableBytes+24,&virtualSize,8);
        [header writeToURL:[fixture URLByAppendingPathComponent:@"System.qcow2"] atomically:YES];
        VIMMachine *imported=[VIMMachine new];[imported setValue:fixture forKey:@"URL"];
        [imported setValue:@{@"Disk":@"System.qcow2"} forKey:@"configuration"];
        NSCAssert(imported.diskCapacityBytes==(160ULL<<30),@"Imported capacity uses the QCOW2 virtual-size header");
        [NSFileManager.defaultManager removeItemAtURL:fixture error:nil];
        for(NSString *guest in @[@"mojave",@"catalina",@"bigsur"]) {
            NSBundle *app=[NSBundle bundleWithPath:[source stringByAppendingPathComponent:@"Build/App/Build/Products/Debug/Virtual Intel Mac.app"]];
            NSImage *image=[app imageForResource:[guest stringByAppendingString:@"Wallpaper"]]?:[[NSImage alloc] initWithContentsOfFile:[source stringByAppendingPathComponent:
                [NSString stringWithFormat:@"VirtualIntelMac/Assets.xcassets/%@Wallpaper.imageset/art.jpg",guest]]];
            [image setName:[guest stringByAppendingString:@"Wallpaper"]];
            NSImage *icon=[[NSImage alloc] initWithContentsOfFile:[source stringByAppendingPathComponent:[NSString stringWithFormat:@"VirtualIntelMac/Assets.xcassets/%@Icon.imageset/art.png",guest]]];
            [icon setName:[guest stringByAppendingString:@"Icon"]];
            NSImage *product=[app imageForResource:[guest stringByAppendingString:@"ProductIcon"]]?:[[NSImage alloc] initWithContentsOfFile:[source stringByAppendingPathComponent:[NSString stringWithFormat:@"VirtualIntelMac/Assets.xcassets/%@ProductIcon.imageset/art.png",guest]]];
            [product setName:[guest stringByAppendingString:@"ProductIcon"]];
        }
        VIMLibraryViewController *library=[VIMLibraryViewController new];
        NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,540,378)
            styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        window.contentViewController=library;
        [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
        // Isolate fixtures from real library-drive availability and pending scans.
        [NSNotificationCenter.defaultCenter removeObserver:library];
        [library setValue:@([[library valueForKey:@"scanGeneration"] unsignedIntegerValue]+1) forKey:@"scanGeneration"];
        [[library valueForKey:@"notice"] setStringValue:@""];
        NSMutableArray *models=[NSMutableArray array];
        for(NSString *guest in @[@"mojave",@"catalina",@"bigsur"]) {
            VIMMachine *machine=[VIMMachine new];
            NSString *name=@{@"mojave":@"macOS Mojave",@"catalina":@"macOS Catalina",@"bigsur":@"macOS Big Sur"}[guest];
            [machine setValue:@{@"Name":name,@"Guest":guest,@"CPUCount":@4,@"MemoryMiB":@8192,@"DiskGiB":@128,
                @"SSHPort":@2222,@"FileSharingPort":@1445,@"AudioEnabled":@YES} forKey:@"configuration"];
            [machine setValue:[NSURL fileURLWithPath:[@"/Preview Library" stringByAppendingPathComponent:[name stringByAppendingString:@".vintelmac"]]] forKey:@"URL"];
            [models addObject:machine];
        }
        [library setValue:models forKey:@"machines"];[library applyFilter];
        NSCAssert(window.contentMaxSize.height==378,@"A three-row library must not resize taller than its rows");
        // A bounded default size must not crush rows or prohibit user resizing.
        NSMutableArray *many=[NSMutableArray array];for(NSUInteger i=0;i<30;i++)[many addObject:models.firstObject];
        [library setValue:many forKey:@"machines"];[library setValue:[NSValue valueWithSize:NSZeroSize] forKey:@"automaticContentSize"];[library applyFilter];
        NSCAssert(NSHeight(window.contentView.bounds)<=641&&[[library valueForKey:@"table"] rowHeight]==126,@"Many VMs must scroll rather than produce an enormous window or shrink rows");
        NSScrollView *libraryScroll=[library valueForKey:@"scroll"];
        [window.contentView layoutSubtreeIfNeeded];[libraryScroll.contentView scrollToPoint:NSMakePoint(0,126)];[libraryScroll reflectScrolledClipView:libraryScroll.contentView];
        NSCAssert(libraryScroll.hasVerticalScroller&&libraryScroll.contentView.bounds.origin.y>0,@"Native library scrolling must work");
        [window setContentSize:NSMakeSize(680,330)];NSSize resized=window.contentView.bounds.size;[library applyFilter];
        NSCAssert(NSEqualSizes(window.contentView.bounds.size,resized)&&window.contentMaxSize.height>window.contentMinSize.height,@"Library refresh must preserve a manual two-way resize");
        [library setValue:models forKey:@"machines"];[library setValue:[NSValue valueWithSize:NSZeroSize] forKey:@"automaticContentSize"];
        [window setContentSize:NSMakeSize(540,378)];[library applyFilter];
        [libraryScroll.contentView scrollToPoint:NSZeroPoint];
        [[library valueForKey:@"table"] selectRowIndexes:[NSIndexSet indexSetWithIndex:2] byExtendingSelection:NO];
        NSTableCellView *selected=[(NSTableView *)[library valueForKey:@"table"] viewAtColumn:0 row:2 makeIfNecessary:YES];
        selected.backgroundStyle=NSBackgroundStyleEmphasized;
        NSCAssert([selected.textField.textColor isEqual:NSColor.labelColor],@"Selection must preserve the row label color");
        NSCAssert(!(selected.textField.font.fontDescriptor.symbolicTraits&NSFontDescriptorTraitBold),@"Library labels must remain regular weight");
        NSCAssert([[[selected valueForKey:@"specifications"] stringValue] isEqual:@"4 CPU · 8 GB RAM · 128 GB"],@"Expanded rows show configured resources");
        CheckDragThumbnail(selected,[output stringByAppendingPathComponent:@"Library-Drag-Expanded.png"]);
        // Deliver a real second-click event through the row's hit-test path.
        // This used to terminate at the label instead of opening the window.
        NSPoint titlePoint=NSMakePoint(NSMidX(selected.textField.bounds),NSMidY(selected.textField.bounds));
        NSPoint inRow=[selected.textField convertPoint:titlePoint toView:selected.superview];
        NSView *hit=[selected hitTest:inRow];
        NSCAssert(hit==selected,@"Title clicks must belong to the row");
        NSPoint inWindow=[selected.textField convertPoint:titlePoint toView:nil];
        NSEvent *doubleClick=[NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:inWindow modifierFlags:0 timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:window.windowNumber context:nil eventNumber:1 clickCount:2 pressure:1];
        [hit mouseDown:doubleClick];
        BOOL opened=NO;for(NSWindow *candidate in NSApp.windows)if([candidate.title isEqual:((VIMMachine *)models[2]).name]){opened=YES;[candidate orderOut:nil];}
        NSCAssert(opened&&!VIMSession.hasRunningMachines,@"Row double-click must open a stopped window without booting");
        NSCAssert([library moveMachineAtPath:((VIMMachine *)models[2]).URL.path beforeVisibleRow:0],@"Dragging to first position succeeds");
        NSCAssert([[library valueForKey:@"visibleMachines"] firstObject]==models[2],@"Reordering changes displayed order");
        [library setValue:models forKey:@"machines"];[library applyFilter];
        [[library valueForKey:@"table"] selectRowIndexes:[NSIndexSet indexSetWithIndex:2] byExtendingSelection:NO];
        for(NSString *appearance in @[NSAppearanceNameAqua,NSAppearanceNameDarkAqua]) {
            window.appearance=[NSAppearance appearanceNamed:appearance];
            Render(window,[output stringByAppendingPathComponent:[appearance stringByAppendingString:@"-library.png"]]);
        }
        [library setValue:@YES forKey:@"compact"];
        window.appearance=[NSAppearance appearanceNamed:NSAppearanceNameAqua];
        [[library valueForKey:@"table"] setRowHeight:63];[library applyFilter];
        NSTableView *table=[library valueForKey:@"table"];
        NSView *hover=[table viewAtColumn:0 row:0 makeIfNecessary:YES];[hover setValue:@YES forKey:@"hovered"];
        NSCAssert([[[hover valueForKey:@"specifications"] stringValue] isEqual:@"4 CPU · 8 GB RAM · 128 GB"]&&((NSTableCellView *)hover).textField.font.pointSize==17,@"Compact rows keep a smaller name and readable resources");
        CheckDragThumbnail((NSTableCellView *)hover,[output stringByAppendingPathComponent:@"Library-Drag-Compact.png"]);
        Render(window,[output stringByAppendingPathComponent:@"Library-Compact-Hover.png"]);
        [library setValue:@NO forKey:@"compact"];[library setValue:[NSValue valueWithSize:NSZeroSize] forKey:@"automaticContentSize"];[table setRowHeight:126];[library applyFilter];
        NSView *largeHover=[table viewAtColumn:0 row:0 makeIfNecessary:YES];[largeHover setValue:@YES forKey:@"hovered"];
        Render(window,[output stringByAppendingPathComponent:@"Library-Large-Hover.png"]);
        [largeHover setValue:@NO forKey:@"hovered"];
        id unavailable=[NSClassFromString(@"VIMUnavailableMac") new];
        [unavailable setValue:[NSURL fileURLWithPath:@"/Disconnected Drive/Work Mac.vintelmac"] forKey:@"missingURL"];
        [unavailable setValue:@"Work Mac" forKey:@"savedName"];[unavailable setValue:@"catalina" forKey:@"savedGuest"];
        [unavailable setValue:@"Reconnect the drive or locate the package." forKey:@"reason"];
        [library setValue:@[unavailable,models.firstObject] forKey:@"machines"];[library applyFilter];
        window.appearance=[NSAppearance appearanceNamed:NSAppearanceNameAqua];
        Render(window,[output stringByAppendingPathComponent:@"Library-Unavailable.png"]);
        [library setValue:models forKey:@"machines"];[library applyFilter];
        [VIMSession showMachineWindow:models.firstObject];
        NSWindow *stopped=nil;for(NSWindow *candidate in NSApp.windows)if([candidate.title isEqual:((VIMMachine *)models.firstObject).name])stopped=candidate;
        NSCAssert(stopped&&!VIMSession.hasRunningMachines,@"Opening a stopped window must not boot a VM");
        NSCAssert(stopped.toolbar&&stopped.toolbarStyle==NSWindowToolbarStyleUnifiedCompact&&stopped.titlebarAccessoryViewControllers.count==0,@"VM controls must use a real compact toolbar");
        NSCAssert([stopped.windowController valueForKey:@"task"]==nil,@"A stopped window must not allocate an emulator task");
        [VIMSession showMachineWindow:models.firstObject];
        NSMenu *main=[NSMenu new];
        for(NSString *title in @[@"Virtual Intel Mac",@"File",@"Edit",@"View",@"Window",@"Help"]){NSMenuItem *item=[[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];item.submenu=[[NSMenu alloc] initWithTitle:title];[main addItem:item];}
        NSApp.mainMenu=main;NSApp.windowsMenu=[main itemWithTitle:@"Window"].submenu;
        VIMPreviewMenus *menus=[[VIMPreviewMenus alloc] initWithLibrary:library];menus.windowContext=window;[menus refresh];
        NSMenu *file=[main itemWithTitle:@"File"].submenu,*view=[main itemWithTitle:@"View"].submenu;
        NSCAssert([file itemWithTitle:@"New…"]&&[file itemWithTitle:@"Open…"],@"Library file commands");
        NSCAssert([main itemWithTitle:@"Actions"].hidden&&[main itemWithTitle:@"Devices"].hidden,@"Guest commands hidden outside guest context");
        NSMenu *arrange=[view itemWithTitle:@"Arrange By"].submenu;
        NSCAssert(arrange.numberOfItems==8&&[arrange.itemArray.lastObject.keyEquivalent isEqual:@"0"]&&arrange.itemArray.firstObject.keyEquivalentModifierMask==(NSEventModifierFlagControl|NSEventModifierFlagCommand),@"All arrangement commands and shortcuts exist");
        NSCAssert([NSApp.windowsMenu itemWithTitle:@"Library"],@"Library can always be reopened");
        menus.windowContext=stopped;[menus refresh];
        NSCAssert(![main itemWithTitle:@"Actions"].hidden&&![main itemWithTitle:@"Devices"].hidden&&file.numberOfItems>0,@"Guest menu context");
        NSCAssert(![[main itemWithTitle:@"Actions"].submenu itemWithTitle:@"Graphics Support"],@"Automatic graphics setup needs no manual Check/Repair menus");
        NSMenuToolbarItem *controls=(id)[(id<NSToolbarDelegate>)stopped.windowController toolbar:stopped.toolbar itemForItemIdentifier:@"GuestControls" willBeInsertedIntoToolbar:NO];
        NSCAssert(controls.menu.numberOfItems==3&&[controls.menu itemWithTitle:@"Session Details…"]&&
            ![controls.menu itemWithTitle:@"Repair Graphics Support…"]&&![controls.menu itemWithTitle:@"Check Graphics Support"]&&
            ![controls.menu itemWithTitle:@"Installation Guide"],@"Toolbar keeps controls and diagnostics, not manual graphics setup");
        NSCAssert([view itemWithTitle:@"Retina Resolution"]&&[view itemWithTitle:@"Take Screenshot…"]&&![view itemWithTitle:@"Arrange By"],@"Guest view commands replace library commands");
        NSVisualEffectView *stoppedMaterial=(NSVisualEffectView *)FindView(stopped.contentView,NSVisualEffectView.class);
        NSCAssert(stoppedMaterial&&stoppedMaterial.blendingMode==NSVisualEffectBlendingModeWithinWindow,@"Stopped wallpaper uses real native within-window material");
        for(NSString *appearance in @[NSAppearanceNameAqua,NSAppearanceNameDarkAqua]) {
            stopped.appearance=[NSAppearance appearanceNamed:appearance];
            Render(stopped,[output stringByAppendingPathComponent:[appearance stringByAppendingString:@"-Guest-Stopped.png"]]);
        }
        Render(stopped,[output stringByAppendingPathComponent:@"Guest-Stopped.png"]);
        [stopped orderOut:nil];
        [VIMMachineSettings showForMachine:models.firstObject window:window completion:^{}];
        for(NSWindow *configuration in NSApp.windows)if([configuration.title containsString:@"Configuration"]) {
            id controller=configuration.windowController;
            NSCAssert([[controller valueForKey:@"section"] isEqual:@"Hardware"],@"Configuration opens on Hardware");
            for(NSString *section in @[@"General",@"Hardware",@"Sharing"]) {
                NSToolbarItem *item=[[NSToolbarItem alloc] initWithItemIdentifier:section];
                [controller performSelector:@selector(selectSection:) withObject:item];
                if(![section isEqual:@"General"]) {
                    NSTableView *categories=(id)FindView(configuration.contentView,NSTableView.class);
                    NSCAssert(categories&&!categories.allowsEmptySelection&&!categories.allowsMultipleSelection&&categories.selectedRow==[[controller valueForKey:@"hardwareIndex"] integerValue],
                        @"Hardware and Sharing must always keep exactly one category selected");
                }
                Render(configuration,[output stringByAppendingPathComponent:[section stringByAppendingString:@".png"]]);
            }
            [controller setValue:@YES forKey:@"hostFiles"];[controller performSelector:@selector(render)];
            Render(configuration,[output stringByAppendingPathComponent:@"Sharing-Host.png"]);
            [controller setValue:@NO forKey:@"hostFiles"];[controller performSelector:@selector(render)];
            Render(configuration,[output stringByAppendingPathComponent:@"Sharing-GuestFiles.png"]);
            NSButton *guestSharing=FindGuestSettings(configuration.contentView,@"sharing");
            NSCAssert(guestSharing&&!guestSharing.enabled&&guestSharing.target==controller,@"Guest Sharing action is wired, but requires a running VM");
            [controller setValue:@NO forKey:@"hostFiles"];[controller setValue:@1 forKey:@"hardwareIndex"];
            [controller setValue:@"mac" forKey:@"guestUsername"];[controller performSelector:@selector(render)];
            NSCAssert([[controller valueForKey:@"hardwareIndex"] integerValue]==1,@"Rebuilding the no-empty-selection table must not reset the requested category");
            Render(configuration,[output stringByAppendingPathComponent:@"Sharing-SSH.png"]);
            NSTabView *directions=(NSTabView *)FindView(configuration.contentView,NSTabView.class);
            NSCAssert(directions.numberOfTabViewItems==2&&[directions.tabViewItems[0].label isEqual:@"This Mac"]&&[directions.tabViewItems[1].label isEqual:@"Virtual Mac"],@"Sharing uses native direction tabs");
            NSTextField *account=[controller valueForKey:@"fields"][@"GuestUsername"];
            NSCAssert(NSWidth(account.frame)>=239,@"Guest account field must fit its placeholder and real short names");
            NSScrollView *instructions=(NSScrollView *)directions.selectedTabViewItem.view;
            NSView *document=instructions.documentView,*pane=document.subviews.firstObject;
            [instructions.contentView scrollToPoint:NSMakePoint(0,MAX(0,NSHeight(document.bounds)-NSHeight(instructions.contentView.bounds)))];
            [instructions reflectScrolledClipView:instructions.contentView];
            NSCAssert(NSHeight(document.bounds)-NSMaxY(pane.frame)>=19.5,@"Instruction document retains its bottom margin");
            NSCAssert(NSMaxY(instructions.contentView.bounds)>=NSMaxY(pane.frame)+19.5,@"Last text and bottom margin can scroll fully into view");
            Render(configuration,[output stringByAppendingPathComponent:@"Sharing-SSH-Scrolled.png"]);
            [directions selectTabViewItemAtIndex:0];
            NSCAssert([[controller valueForKey:@"hostFiles"] boolValue]&&FindView(configuration.contentView,NSTabView.class)==directions,@"Native tabs switch without rebuilding the preference window");
            configuration.appearance=[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
            Render(configuration,[output stringByAppendingPathComponent:@"Sharing-Dark.png"]);
            [controller setValue:@"Hardware" forKey:@"section"];
            [controller setValue:@4 forKey:@"hardwareIndex"];[controller performSelector:@selector(render)];
            for(NSString *appearance in @[NSAppearanceNameAqua,NSAppearanceNameDarkAqua]) {
                configuration.appearance=[NSAppearance appearanceNamed:appearance];
                Render(configuration,[output stringByAppendingPathComponent:[appearance stringByAppendingString:@"-Sound.png"]]);
                NSButton *guestPrivacy=FindGuestSettings(configuration.contentView,@"microphone");
                NSCAssert(guestPrivacy&&!guestPrivacy.enabled&&guestPrivacy.target==controller,@"Guest microphone privacy action is wired, but requires a running VM");
                NSScrollView *detail=nil;NSButton *restore=nil;
                for(NSView *child in configuration.contentView.subviews) {
                    if([child.identifier isEqual:@"ConfigurationDetail"])detail=(id)child;
                    if([child isKindOfClass:NSButton.class]&&[((NSButton *)child).title isEqual:@"Restore Defaults"])restore=(id)child;
                }
                NSCAssert(detail&&restore&&!NSIntersectsRect(detail.frame,restore.frame),@"Hardware scrolling must not overlap Restore Defaults");
            }
            configuration.appearance=[NSAppearance appearanceNamed:NSAppearanceNameAqua];
            for(NSNumber *index in @[@1,@2,@3,@6]) {
                [controller setValue:index forKey:@"hardwareIndex"];[controller performSelector:@selector(render)];
                Render(configuration,[output stringByAppendingPathComponent:[NSString stringWithFormat:@"Hardware-%@.png",index]]);
            }
            [controller setValue:@3 forKey:@"hardwareIndex"];[controller performSelector:@selector(render)];
            [controller performSelector:@selector(networkAdvanced:) withObject:nil];
            NSWindowController *sheet=[controller valueForKey:@"editorSheet"];
            NSCAssert(sheet.window.sheetParent==configuration,@"Network Advanced is a real sheet");
            Render(sheet.window,[output stringByAppendingPathComponent:@"Network-Advanced.png"]);
            [sheet performSelector:@selector(cancel:) withObject:nil];
            [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.05]];
            [controller setValue:@"Sharing" forKey:@"section"];[controller setValue:@0 forKey:@"hardwareIndex"];[controller setValue:@NO forKey:@"hostFiles"];[controller performSelector:@selector(render)];
            [controller performSelector:@selector(toggleAdvancedSharing:) withObject:nil];
            sheet=[controller valueForKey:@"editorSheet"];
            NSCAssert(sheet.window.sheetParent==configuration,@"Sharing port editing is a real sheet");
            Render(sheet.window,[output stringByAppendingPathComponent:@"Sharing-Advanced.png"]);
            [sheet performSelector:@selector(cancel:) withObject:nil];
            [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.05]];
            [configuration orderOut:nil];
        }
        VIMPreviewCreation *assistant=[VIMPreviewCreation new];
        menus.windowContext=assistant.window;[menus refresh];
        for(NSMenuItem *item in file.itemArray)NSCAssert(![item.title hasPrefix:@"Remove “"],@"Creation window must not expose the library’s selection as a removal target");
        NSCAssert([[assistant valueForKey:@"system"] indexOfSelectedItem]==0,@"New Macs default to Mojave");
        NSCAssert([[assistant valueForKey:@"nameField"] stringValue]&&[[[assistant valueForKey:@"nameField"] stringValue] isEqual:@"Mojave"],@"Default VM names omit the macOS prefix");
        NSCAssert((assistant.window.collectionBehavior&NSWindowCollectionBehaviorFullScreenNone)!=0,@"Creation must zoom rather than enter full screen");
        NSCAssert(!(assistant.window.styleMask&NSWindowStyleMaskResizable),@"Creation must be fixed-size");
        [assistant.window.contentView layoutSubtreeIfNeeded];
        for(NSButton *option in [assistant valueForKey:@"systemOptions"]) {
            NSCAssert(option.focusRingType==NSFocusRingTypeNone,@"Selection must not acquire a second system focus ring");
            NSString *benefit=[option valueForKey:@"benefit"];
            NSMutableParagraphStyle *caption=[NSMutableParagraphStyle new];caption.lineSpacing=2;
            NSRect bounds=[benefit boundingRectWithSize:NSMakeSize(NSWidth(option.bounds)-112,100)
                options:NSStringDrawingUsesLineFragmentOrigin attributes:@{NSFontAttributeName:[NSFont systemFontOfSize:13],NSParagraphStyleAttributeName:caption}];
            NSCAssert(NSHeight(bounds)>16&&NSHeight(bounds)<=35,@"%@ benefit must fit two spaced lines: %@, row %@",option.title,NSStringFromRect(bounds),NSStringFromRect(option.bounds));
        }
        NSCAssert(![[assistant valueForKey:@"mediaLabel"] isSelectable],@"Creation instructions are not selectable");
        Render(assistant.window,[output stringByAppendingPathComponent:@"NewMac-Choose.png"]);
        NSTimeInterval clicks=NSProcessInfo.processInfo.systemUptime;
        for(NSNumber *index in @[@2,@0,@1,@0,@2,@1]) {
            NSButton *option=[assistant valueForKey:@"systemOptions"][index.unsignedIntegerValue];
            NSPoint point=NSMakePoint(NSWidth(option.bounds)-4,NSMidY(option.bounds));
            NSView *hit=[option hitTest:[option convertPoint:point toView:option.superview]];
            NSCAssert(hit==option,@"The entire version row must be clickable");
            NSEvent *event=[NSEvent mouseEventWithType:NSEventTypeLeftMouseDown location:[option convertPoint:point toView:nil] modifierFlags:0 timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:assistant.window.windowNumber context:nil eventNumber:1 clickCount:1 pressure:1];
            [hit mouseDown:event];
            NSCAssert([[assistant valueForKey:@"system"] indexOfSelectedItem]==index.integerValue,@"Mouse-down must immediately select the row");
        }
        NSCAssert(NSProcessInfo.processInfo.systemUptime-clicks<.5,@"Row selection must not enter radio-cell tracking or animation waits");
        NSButton *selection=[NSButton new];selection.tag=1;
        [assistant performSelector:@selector(selectSystem:) withObject:selection];
        Render(assistant.window,[output stringByAppendingPathComponent:@"NewMac-Choose-Catalina.png"]);
        [assistant performSelector:@selector(continueToConfiguration:) withObject:nil];
        Render(assistant.window,[output stringByAppendingPathComponent:@"NewMac-Configure.png"]);
        NSView *cpuField=[assistant valueForKey:@"cpuField"],*memoryField=[assistant valueForKey:@"memoryField"];
        NSCAssert(fabs([cpuField convertPoint:NSZeroPoint toView:nil].x-[memoryField convertPoint:NSZeroPoint toView:nil].x)<1,@"Processor and memory numeric fields must align");
        NSButton *create=[assistant valueForKey:@"createButton"];
        NSCAssert(create.controlSize==NSControlSizeLarge&&create.font.pointSize==NSFont.systemFontSize,@"Large native controls must retain standard label typography");
        [(NSTextField *)cpuField setStringValue:@"0"];[assistant beginSetup:nil];
        NSCAssert([assistant.lastAlert.messageText isEqual:@"Enter a valid number of cores"]&&[assistant.lastAlert.informativeText containsString:@"cores to Catalina."]&&assistant.lastAlert.icon==[NSImage imageNamed:@"catalinaIcon"],@"Validation must describe the field and selected Mac with its installer icon");
        DismissPreviewSheet(assistant.window,NSAlertFirstButtonReturn);[(NSTextField *)cpuField setStringValue:@"4"];
        [assistant setValue:[@{@"Guest":@"catalina"} mutableCopy] forKey:@"job"];
        [assistant performSelector:@selector(showProgress)];
        [[assistant valueForKey:@"status"] setStringValue:@"Downloading macOS…"];
        [assistant setValue:@[@{@"Role":@"assistant",@"URL":@"fixture-assistant"},@{@"Role":@"recovery",@"URL":@"fixture-recovery"},@{@"Role":@"esd",@"URL":@"fixture-esd"}] forKey:@"files"];
        [assistant setValue:[@{@"fixture-assistant":@200000000,@"fixture-recovery":@800000000,@"fixture-esd":@5400000000} mutableCopy] forKey:@"downloadLengths"];
        [assistant setValue:[@{@"fixture-assistant":@200000000,@"fixture-recovery":@800000000,@"fixture-esd":@200000000} mutableCopy] forKey:@"downloadReceived"];
        [assistant setValue:@64000000 forKey:@"bytesPerSecond"];
        [assistant setValue:@.34 forKey:@"overallProgress"];
        [[assistant valueForKey:@"progress"] setDoubleValue:.34];
        [assistant refreshDownloadDetail];
        NSCAssert([[[assistant valueForKey:@"progressDetail"] stringValue] isEqual:@"1.20 GB of 6.40 GB (64.00 MB/s) - 2 minutes remaining"],@"Transfer text uses fixed decimal places, speed and ETA, without a percentage");
        Render(assistant.window,[output stringByAppendingPathComponent:@"NewMac-Progress.png"]);
        [assistant updateOverallProgress:.2];
        NSCAssert([[assistant valueForKey:@"progress"] doubleValue]==.34,@"Resume must not rewind preparation progress");
        [assistant setValue:@[@{@"Role":@"assistant"},@{@"Role":@"recovery"},@{@"Role":@"esd"}] forKey:@"files"];
        [assistant setValue:@1 forKey:@"fileIndex"];double endOfRecovery=[assistant downloadProgressForFileFraction:1];
        [assistant setValue:@2 forKey:@"fileIndex"];
        NSCAssert(fabs(endOfRecovery-[assistant downloadProgressForFileFraction:0])<.000001,@"Advancing to another package must not reset the bar");
        [assistant updateOverallProgress:.972];
        [[assistant valueForKey:@"status"] setStringValue:@"Creating the bootable installer image…"];
        [[assistant valueForKey:@"progressDetail"] setStringValue:@"Download complete. Preparing the installer."];
        Render(assistant.window,[output stringByAppendingPathComponent:@"NewMac-Preparing.png"]);
        assistant.window.appearance=[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
        Render(assistant.window,[output stringByAppendingPathComponent:@"NewMac-Progress-Dark.png"]);
        assistant.window.appearance=[NSAppearance appearanceNamed:NSAppearanceNameAqua];
        NSAlert *cancel=[assistant cancellationAlert];[cancel layout];
        // Window chrome/material is not part of cacheDisplay. Supply only its
        // background so this offscreen native alert remains legible in a PNG.
        NSBox *alertBackground=[[NSBox alloc] initWithFrame:cancel.window.contentView.bounds];
        alertBackground.boxType=NSBoxCustom;alertBackground.borderWidth=0;
        alertBackground.fillColor=NSColor.windowBackgroundColor;
        alertBackground.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
        [cancel.window.contentView addSubview:alertBackground positioned:NSWindowBelow relativeTo:nil];
        Render(cancel.window,[output stringByAppendingPathComponent:@"NewMac-Cancel.png"]);
        NSCAssert([cancel.buttons.lastObject.title isEqual:@"No"]&&[cancel.buttons.lastObject.keyEquivalent isEqual:@"\r"],@"Cancellation must default to No");
        // Exercise presentation-state transitions without starting downloads or
        // touching a VM. Defaults belong to this standalone preview executable.
        [assistant performSelector:@selector(setPhase:) withObject:@"creating"];
        NSCAssert([assistant valueForKey:@"sleepActivity"]!=nil,@"Active installer creation must prevent host idle sleep");
        NSCAssert(![[assistant valueForKey:@"pauseResume"] isEnabled],@"Cannot pause disk creation");
        [assistant performSelector:@selector(setPhase:) withObject:@"paused"];
        NSCAssert([assistant valueForKey:@"sleepActivity"]==nil,@"Pausing must restore normal host sleep");
        NSCAssert([[[assistant valueForKey:@"pauseResume"] title] isEqual:@"Resume"],@"Paused setup must offer Resume");
        NSCAssert(![assistant windowShouldClose:assistant.window],@"Closing a download must confirm cancellation");
        DismissPreviewSheet(assistant.window,NSAlertSecondButtonReturn);
        NSCAssert([assistant valueForKey:@"job"]!=nil,@"No must keep setup open and intact");
        NSCAssert(![assistant windowShouldClose:assistant.window],@"Closing still requires confirmation");
        DismissPreviewSheet(assistant.window,NSAlertFirstButtonReturn);
        NSCAssert([assistant valueForKey:@"job"]==nil,@"Cancellation retires the job record");
        NSCAssert(![NSUserDefaults.standardUserDefaults dictionaryForKey:@"VIMCreationJob"],@"Cancelled setup must not restart at launch");
        // The real global preference pane should fit its content, not retain a
        // fixed-height void beneath it. No settings actions are invoked here.
        AppDelegate *appDelegate=[AppDelegate new];[appDelegate showSettings:nil];
        NSWindow *settings=[appDelegate valueForKey:@"settingsWindow"];
        for(NSString *appearance in @[NSAppearanceNameAqua,NSAppearanceNameDarkAqua]) {
            settings.appearance=[NSAppearance appearanceNamed:appearance];
            Render(settings,[output stringByAppendingPathComponent:[appearance stringByAppendingString:@"-Settings.png"]]);
        }
        NSTabViewController *tabs=(NSTabViewController *)settings.contentViewController;
        NSView *general=tabs.tabViewItems.firstObject.viewController.view;
        NSGridView *settingsGrid=nil;for(NSView *child in general.subviews)if([child isKindOfClass:NSGridView.class])settingsGrid=(NSGridView *)child;
        NSCAssert(settingsGrid&&fabs(NSMinY(settingsGrid.frame)-22)<1,@"Settings ends 22 points below its content");
        NSCAssert(settingsGrid.numberOfRows==2,@"Downloads belongs to the library group, not a separate section");
        NSPopUpButton *location=[appDelegate valueForKey:@"libraryLocationPopup"];
        NSCAssert(location&&location.selectedItem.tag==0&&[location.lastItem.title isEqual:@"Other…"],@"Library location is a folder popup with a choose action");
        printf("Settings content %.0fx%.0f; bottom margin %.0f points\n",NSWidth(general.bounds),NSHeight(general.bounds),NSMinY(settingsGrid.frame));
        NSSet *beforeAbout=[NSSet setWithArray:NSApp.windows];
        [appDelegate showAbout:nil];
        [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.1]];
        NSWindow *about=nil;for(NSWindow *candidate in NSApp.windows)
            if(![beforeAbout containsObject:candidate]&&candidate.visible){about=candidate;break;}
        NSCAssert(about!=nil,@"Standard About panel must open");
        Render(about,[output stringByAppendingPathComponent:@"About.png"]);
    }
    return 0;
}
