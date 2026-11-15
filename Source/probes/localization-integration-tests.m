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

@interface NSWindowController (DiskSettingsLocalizationProbe)
- (NSView *)hardware;
@end

@interface VIMLibraryViewController (LocalizationProbe)
- (void)applyFilter;
@end

@interface VIMSession (LocalizationProbe)
- (void)ensureImageDevice:(NSUInteger)slot completion:(void (^)(NSError *))completion;
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
            for (NSString *kind in @[@"Network", @"CD/DVD", @"Keyboard", @"Sound Output", @"Microphone"]) {
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
            NSError *invalidRequest = nil;
            BOOL started = [VIMDiskMaintenance startForPackage:alpha.URL identifier:@"invalid" qemuImage:[NSURL fileURLWithPath:@"/unused/qemu-img"] compressed:NO
                progress:^(NSString *phase, double fraction) {} completion:^(NSDictionary *result, NSError *error) {} error:&invalidRequest];
            Check(!started && [invalidRequest.localizedDescription isEqual:chinese ? @"此虚拟 Intel Mac 的磁盘维护请求无效。" : @"This Mac’s disk maintenance request is invalid."], @"Disk maintenance validation errors were not localized");
            (void)menus;
            printf("Production menu and sorting localization passed: %s\n", argv[argc - 1]);
        } @finally {
            [defaults removePersistentDomainForName:domain];
        }
    }
    return 0;
}
