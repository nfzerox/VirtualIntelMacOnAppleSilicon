// Link the actual app objects. No app event loop, displayed windows or VM launch.
#import <Cocoa/Cocoa.h>
#import "VIMLocalization.h"
#import "VIMLibraryViewController.h"
#import "VIMLibraryLocation.h"
#import "VIMMachine.h"
#import "VIMMenuController.h"
#import "VIMSession.h"
#import "VIMQMPConnection.h"
#import "VIMDiskMaintenance.h"
#import "VIMGuestAgent.h"
#import "VIMGuestProgress.h"
#import "VIMNewMachineWindow.h"

@interface NSWindowController (DiskSettingsLocalizationProbe)
- (NSView *)hardware;
- (void)render;
@end

@interface VIMLibraryViewController (LocalizationProbe)
- (void)applyFilter;
- (BOOL)moveMachineAtPath:(NSString *)path beforeVisibleRow:(NSInteger)row;
@end

@interface VIMSession (LocalizationProbe)
- (void)ensureImageDevice:(NSUInteger)slot completion:(void (^)(NSError *))completion;
@end

@interface VIMNewMachineWindow (LocalizationProbe)
- (NSView *)configurationLabel:(NSString *)title symbol:(NSString *)symbol;
@end

// Exercise production parsing with real QEMU command responses, without a VM.
@interface LocalizationMonitor : VIMQMPConnection
@property NSUInteger calls;
@end
@implementation LocalizationMonitor
- (void)execute:(NSString *)command arguments:(NSDictionary *)arguments completion:(void (^)(id, NSError *))completion {
    self.calls++;
    completion([command isEqual:@"human-monitor-command"] ? @"OK\r\n" : @{}, nil);
}
@end

static void Check(BOOL condition, NSString *message) {
    if (!condition) { fprintf(stderr, "%s\n", message.UTF8String); exit(1); }
}

// Capture the production startup call, not a second guest connection or UI.
@interface LocalizationProgressAgent : VIMGuestAgent
@property NSDictionary *labels;
@end
@implementation LocalizationProgressAgent
- (void)runExecutable:(NSString *)path arguments:(NSArray *)arguments timeout:(NSTimeInterval)timeout
           completion:(void (^)(NSDictionary *,NSError *))completion {
    Check([path isEqual:@"/bin/chmod"],@"Unexpected guest setup bootstrap operation");
    completion(@{@"exitcode":@0},nil);
}
- (void)runExecutable:(NSString *)path arguments:(NSArray *)arguments standardInput:(NSData *)input
              timeout:(NSTimeInterval)timeout completion:(void (^)(NSDictionary *,NSError *))completion {
    Check([path hasSuffix:@"/guest-progress"]&&[arguments.firstObject isEqual:@"--start"]&&
        [arguments.lastObject isEqual:@"--labels"],@"Guest setup labels changed its executable or startup action");
    self.labels=[NSJSONSerialization JSONObjectWithData:input options:0 error:nil];
    NSData *receipt=[NSJSONSerialization dataWithJSONObject:@{@"Directory":@"/private/var/tmp/VirtualIntelMacUI.ABC123",
        @"Started":@YES,@"PID":@8800,@"ConsoleUID":@501} options:0 error:nil];
    completion(@{@"exitcode":@0,@"stdout":receipt},nil);
}
@end

static NSButton *MaintenanceButton(NSView *view, NSInteger tag) {
    if ([view isKindOfClass:NSButton.class]) {
        NSButton *button = (id)view;
        if (button.action == @selector(maintainDisk:) && button.tag == tag) return button;
    }
    if ([view isKindOfClass:NSGridView.class]) {
        NSGridView *grid = (id)view;
        for (NSInteger row = 0; row < grid.numberOfRows; row++) {
            for (NSInteger column = 0; column < grid.numberOfColumns; column++) {
                NSView *content = [grid cellAtColumnIndex:column rowIndex:row].contentView;
                NSButton *button = MaintenanceButton(content, tag);
                if (button) return button;
            }
        }
    }
    NSArray<NSView *> *children = [view isKindOfClass:NSStackView.class] ? [(NSStackView *)view views] : view.subviews;
    for (NSView *child in children) {
        NSButton *button = MaintenanceButton(child, tag);
        if (button) return button;
    }
    return nil;
}

static NSButton *ButtonForAction(NSView *view,SEL action) {
    if([view isKindOfClass:NSButton.class]&&[(NSButton *)view action]==action)return (id)view;
    NSArray *children=[view isKindOfClass:NSStackView.class]?[(NSStackView *)view views]:view.subviews;
    for(NSView *child in children){NSButton *found=ButtonForAction(child,action);if(found)return found;}
    return nil;
}

static NSView *StorageBar(NSView *view) {
    if([view isKindOfClass:NSClassFromString(@"VIMStorageDistributionBar")])return view;
    for(NSView *child in view.subviews){NSView *bar=StorageBar(child);if(bar)return bar;}
    return nil;
}

static VIMMachine *Machine(NSString *root, NSString *name) {
    VIMMachine *machine = [VIMMachine new];
    [machine setValue:[NSURL fileURLWithPath:[root stringByAppendingPathComponent:[name stringByAppendingString:@".vintelmac"]]] forKey:@"URL"];
    [machine setValue:@{@"Identifier": NSUUID.UUID.UUIDString, @"Name": name, @"Guest": @"mojave",
                        @"CPUCount": @4, @"MemoryMiB": @4096, @"Disk": @"Disks/System.qcow2", @"InstallState": @"installed"} forKey:@"configuration"];
    return machine;
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        BOOL chinese = [@(argv[argc - 1]) isEqual:@"zh-Hans"];
        NSString *root = @(argv[argc - 2]);
        [NSApplication sharedApplication];
        // Isolate preferences from both user settings and other test runs.
        NSString *domain = NSBundle.mainBundle.bundleIdentifier;
        NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
        [defaults removePersistentDomainForName:domain];
        [defaults setObject:root forKey:@"VIMLibraryPath"];
        @try {
            LocalizationProgressAgent *progressAgent=[LocalizationProgressAgent new];
            __block BOOL progressStarted=NO;
            [VIMGuestProgress startWithAgent:progressAgent staging:@"/private/var/tmp/VirtualIntelMac.ABC123"
                completion:^(VIMGuestProgress *progress,NSError *error){
                    Check(progress!=nil&&!error,@"Localized guest progress startup failed");progressStarted=YES;
                }];
            Check(progressStarted&&[progressAgent.labels[@"Finishing Setup"] isEqual:chinese?@"正在完成设置":@"Finishing Setup"],
                @"The production guest startup did not send labels in the app language");
            VIMNewMachineWindow *creation=[VIMNewMachineWindow new];
            NSButton *continueButton=ButtonForAction(creation.window.contentView,NSSelectorFromString(@"continueToConfiguration:"));
            Check([continueButton.title isEqual:chinese?@"继续":@"Continue"],
                @"The actual version picker retained an English Continue button");
            BOOL experimental=[defaults boolForKey:@"VIMEnableExperimentalModernGuests"];
            NSArray<NSButton *> *choices=[creation valueForKey:@"systemOptions"];
            NSPopUpButton *selection=[creation valueForKey:@"system"];
            Check(choices.count==(experimental?18:14)&&selection.numberOfItems==(experimental?18:13),
                @"The shipped/experimental picker does not match its actual selectable guest list");
            BOOL introPlacement=NO;
            for(NSLayoutConstraint *constraint in creation.window.contentView.constraints) {
                id item=constraint.firstItem;
                if(constraint.firstAttribute==NSLayoutAttributeTop&&constraint.secondAttribute==NSLayoutAttributeBottom&&
                   [item isKindOfClass:NSTextField.class]&&[[(NSTextField *)item stringValue] isEqual:
                       VIMLocalized(@"creation.choose-a-version-to-run-the-intel-or-powerpc-apps", @"Choose a version to run the Intel or PowerPC apps you love and revisit classic Mac designs. Every version supports graphics acceleration.")]) {
                    introPlacement=constraint.constant==(experimental?9:4);
                }
            }
            Check(introPlacement,@"The two-column introduction/grid did not move up5points independently of the heading");
            for(NSButton *choice in choices) {
                Check([[choice valueForKey:@"columns"] unsignedIntegerValue]==(experimental?3:2),
                    @"Keyboard navigation uses the wrong column count");
                Check(choice.bounds.size.height==46||choice.fittingSize.height==46||
                    [choice.constraints filteredArrayUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(NSLayoutConstraint *constraint,NSDictionary *bindings){
                        return constraint.firstAttribute==NSLayoutAttributeHeight&&constraint.constant==46;
                    }]].count>0,@"Creation rows lost shipping2.1's46-point height");
            }
            if(!experimental) {
                NSInteger selected=selection.indexOfSelectedItem;
                NSButton *recommendation=choices.lastObject;
                Check(recommendation.action==NSSelectorFromString(@"showNewerSystems:")&&
                    [[recommendation valueForKey:@"recommendation"] boolValue],@"The fake Monterey row could start a creation");
                [creation.window orderFront:nil];[recommendation performClick:nil];
                NSPopover *popover=[creation valueForKey:@"otherSystemsPopover"];
                NSTextView *body=(id)popover.contentViewController.view.subviews.firstObject;
                Check([body.string containsString:@"Monterey"]&&selection.indexOfSelectedItem==selected,
                    @"The Monterey recommendation changed the selected guest or lost its content");
                popover.animates=NO;[popover close];
            }
            NSButton *otherSystems=ButtonForAction(creation.window.contentView,NSSelectorFromString(@"showOtherSystems:"));
            Check(otherSystems!=nil,@"The blue alternatives link is missing");
            [creation.window orderFront:nil];[otherSystems performClick:nil];
            NSPopover *other=[creation valueForKey:@"otherSystemsPopover"];
            NSTextView *alternatives=(id)other.contentViewController.view.subviews.firstObject;
            Check([alternatives.string containsString:@"Ptolemy"]&&[alternatives.string containsString:@"Light Touch"],
                @"The alternatives popover lost its app links");
            other.animates=NO;[other close];
            Check([alternatives.string containsString:chinese?@"iOS和iPadOS":@"iOS and iPadOS"],
                @"The iOS/iPadOS category is not localized");
            NSStackView *updateLabel=(id)[creation configurationLabel:@"Install the latest security updates" symbol:@"arrow.down.circle"];
            NSTextField *updateField=(id)updateLabel.views.lastObject;
            NSTextAttachment *updateIcon=[updateField.attributedStringValue attribute:NSAttachmentAttributeName atIndex:0 effectiveRange:NULL];
            Check(updateIcon&&NSEqualRects(updateIcon.bounds,NSMakeRect(-2,round((updateField.font.ascender+updateField.font.descender-18)/2),26,18)),
                @"The security-update icon lost the shared icon column/baseline");
            NSDictionary *fieldLabels=@{@"Name":@"名称",@"Username":@"用户名",@"Password":@"密码",
                @"Processors":@"处理器",@"Memory":@"内存",@"Storage":@"存储"};
            for(NSString *token in fieldLabels) {
                NSStackView *fieldLabel=(id)[creation configurationLabel:token symbol:@"person"];
                Check([[(NSTextField *)fieldLabel.views.lastObject stringValue] isEqual:chinese?fieldLabels[token]:token],
                    @"Creation label lookup still displays its English semantic token");
            }
            [creation close];
            NSNib *nib = [[NSNib alloc] initWithNibNamed:@"MainMenu" bundle:NSBundle.mainBundle];
            NSArray *topLevel = nil;
            Check([nib instantiateWithOwner:NSApp topLevelObjects:&topLevel], @"Cannot load packaged Base menu and translations");
            NSMenu *main = nil;
            for (id object in topLevel) if ([object isKindOfClass:NSMenu.class]) main = object;
            Check(main != nil, @"The packaged NIB has no main menu");
            NSApp.mainMenu = main;
            Check([VIMMenuItem(main, @"vim.file").title isEqual:chinese ? @"文件" : @"File"], @"Compiled XIB title or identifier was lost");
            Check(VIMMenuItem(main, @"vim.edit") && VIMMenuItem(main, @"vim.view"), @"Translated main menu roots cannot be found");

            VIMLibraryViewController *library = [VIMLibraryViewController new];
            library.view = [NSView new];
            [library setValue:@[] forKey:@"machines"];
            Check([library.arrangement isEqual:@"macOS Release"],@"A new library must default to chronological releases");
            __attribute__((objc_precise_lifetime)) VIMMenuController *menus = [[VIMMenuController alloc] initWithLibrary:library];
            NSMenu *file = VIMMenuItem(main, @"vim.file").submenu;
            Check(file.numberOfItems > 0 && file.itemArray.firstObject.action == @selector(newMac:), @"File menu was not rebuilt after localization");
            NSMenu *view = VIMMenuItem(main, @"vim.view").submenu;
            NSMenuItem *sort = nil;
            for (NSMenuItem *item in view.itemArray) {
                for (NSMenuItem *child in item.submenu.itemArray) {
                    if ([child.representedObject isEqual:@"Date Created"]) sort = child;
                }
            }
            Check(sort != nil && [sort.title isEqual:chinese ? @"创建日期" : @"Date Created"], @"Sort menu did not separate labels from persisted values");
            [library arrangeFromMenu:sort];
            Check([library.arrangement isEqual:@"Date Created"], @"Sorting saved a translated label");
            Check([[VIMLibraryViewController new].arrangement isEqual:@"Date Created"], @"Reopening the library lost its sort preference");
            NSMenuItem *nameSort = [NSMenuItem new]; nameSort.representedObject = @"Name";
            VIMMachine *beta = Machine(root, @"Beta"), *alpha = Machine(root, @"Alpha");
            [library setValue:@[beta, alpha] forKey:@"machines"];
            [library arrangeFromMenu:nameSort];
            Check([[(NSArray *)[library valueForKey:@"visibleMachines"] valueForKey:@"name"] isEqual:@[@"Alpha", @"Beta"]], @"Production sorting stopped working under the selected language");
            [library arrangeFromMenu:nameSort];
            Check([[(NSArray *)[library valueForKey:@"visibleMachines"] valueForKey:@"name"] isEqual:@[@"Beta", @"Alpha"]],@"Repeating a sort must reverse direction");
            VIMMachine *tiger=Machine(root,@"Tiger"),*newer=Machine(root,@"Newer");
            NSMutableDictionary *configuration=[tiger.configuration mutableCopy];configuration[@"Guest"]=@"tiger";[tiger setValue:configuration forKey:@"configuration"];
            configuration=[newer.configuration mutableCopy];configuration[@"Guest"]=@"tahoe";[newer setValue:configuration forKey:@"configuration"];
            [library setValue:@[newer,beta,tiger,alpha] forKey:@"machines"];
            NSMenuItem *releaseSort=[NSMenuItem new];releaseSort.representedObject=@"macOS Release";
            [library arrangeFromMenu:releaseSort];
            Check([[(NSArray *)[library valueForKey:@"visibleMachines"] valueForKey:@"name"] isEqual:@[@"Tiger",@"Alpha",@"Beta",@"Newer"]],@"Release sorting must be chronological with a name tiebreak");
            [library arrangeFromMenu:releaseSort];
            Check([[(NSArray *)[library valueForKey:@"visibleMachines"] valueForKey:@"name"] isEqual:@[@"Newer",@"Beta",@"Alpha",@"Tiger"]],@"Release sorting must also reverse");
            Check([library moveMachineAtPath:tiger.URL.path beforeVisibleRow:0],@"Sorted rows must remain draggable");
            Check([library.arrangement isEqual:@"None"]&&[[(NSArray *)[library valueForKey:@"visibleMachines"] valueForKey:@"name"] isEqual:@[@"Tiger",@"Newer",@"Beta",@"Alpha"]],@"Manual arrangement must retain the other displayed rows");

            VIMSession *session = [[VIMSession alloc] initWithWindow:nil];
            [session setValue:alpha forKey:@"machine"];
            NSDictionary *network = [VIMSession networkDetailsForMachine:alpha];
            Check(network[@"Running"] != nil && network[@"Mode"] != nil, @"Network metadata keys were translated");
            LocalizationMonitor *monitor = [LocalizationMonitor new];
            [session setValue:monitor forKey:@"control"];
            [session setValue:[NSMutableSet new] forKey:@"imageDevices"];
            __block BOOL completed = NO;
            [session ensureImageDevice:1 completion:^(NSError *error) {
                Check(!error, @"QEMU's literal OK response was treated as a translated label");
                completed = YES;
            }];
            Check(completed && monitor.calls == 4, @"Image hot-plug did not complete its production command sequence");
            for (NSString *kind in @[@"Network", @"CD/DVD", @"Keyboard", @"Sound Output", @"Microphone", @"Camera"]) {
                NSMenu *menu = [session deviceMenuForKind:kind];
                Check(menu.numberOfItems > 0, [@"Empty translated device menu: " stringByAppendingString:kind]);
                NSMenu *copy = [menu copy];
                copy.title = @"Toolbar title";
                [copy.delegate menuNeedsUpdate:copy];
                Check(copy.numberOfItems == menu.numberOfItems, @"Copied menu lost semantic routing when its title changed");
                if ([kind isEqual:@"Network"]) {
                    NSMenuItem *shared = copy.itemArray[2];
                    Check(shared.action == @selector(changeNetworkConnection:) && shared.tag == 1, @"Translated network menu lost its mode action");
                }
                if ([kind isEqual:@"CD/DVD"]) {
                    BOOL connect = NO;
                    for (NSMenuItem *item in copy.itemArray) if (item.action == @selector(chooseOpticalMedia:)) connect = YES;
                    Check(connect, @"Translated optical menu lost Connect Image");
                }
            }
            NSWindowController *settings = [[NSClassFromString(@"VIMConfigurationWindow") alloc] initWithWindow:nil];
            [settings setValue:alpha forKey:@"machine"];
            [settings setValue:@5 forKey:@"hardwareIndex"];
            NSView *diskPane = [settings hardware];
            NSButton *reclaim = MaintenanceButton(diskPane, -1), *compress = MaintenanceButton(diskPane, 1);
            Check([reclaim.title isEqual:chinese ? @"释放空间…" : @"Reclaim Space…"], @"Disk space reclamation action was not localized");
            Check([compress.title isEqual:chinese ? @"压缩…" : @"Compress…"], @"Disk compression action was not localized");
            Check(reclaim.enabled && compress.enabled, @"Localization changed disk maintenance availability");
            settings.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,640,400)
                styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
            [settings setValue:@"General" forKey:@"section"];
            [settings render];[settings.window.contentView layoutSubtreeIfNeeded];
            NSView *storageBar=StorageBar(settings.window.contentView);
            Check(storageBar&&storageBar.clipsToBounds&&storageBar.layer.masksToBounds&&storageBar.layer.cornerRadius==4,
                @"Form focus-ring accommodation removed the storage bar's rounded clipping");
            NSError *invalidRequest = nil;
            BOOL started = [VIMDiskMaintenance startForPackage:alpha.URL identifier:@"invalid" qemuImage:[NSURL fileURLWithPath:@"/unused/qemu-img"] compressed:NO
                progress:^(NSString *phase, double fraction) {} completion:^(NSDictionary *result, NSError *error) {} error:&invalidRequest];
            Check(!started && [invalidRequest.localizedDescription isEqual:chinese ? @"此虚拟Intel Mac的磁盘维护请求无效。" : @"This Mac’s disk maintenance request is invalid."], @"Disk maintenance validation errors were not localized");
            (void)menus;
            printf("Production picker (%s), menus and sorting localization passed: %s\n",experimental?"experimental":"shipping",argv[argc - 1]);
        } @finally {
            [defaults removePersistentDomainForName:domain];
        }
    }
    return 0;
}
