#import "VIMMenuController.h"
#import "VIMLibraryViewController.h"
#import "VIMMachine.h"
#import "VIMSession.h"
#import "AppDelegate.h"

@interface VIMMenuController ()
@property VIMLibraryViewController *library;
@property NSMenu *fileMenu;
@property NSMenu *viewMenu;
@property NSMenuItem *editItem;
@property NSMenu *hostEditMenu;
@property NSMenu *guestEditMenu;
@property NSMenuItem *actionsItem;
@property NSMenuItem *devicesItem;
@property BOOL guestContext;
@end
@implementation VIMMenuController
- (instancetype)initWithLibrary:(VIMLibraryViewController *)library {
    if((self=[super init])) {
        self.library=library;
        NSMenu *main=NSApp.mainMenu;
        NSMenuItem *legacy=[main itemWithTitle:@"Mac"];if(legacy)[main removeItem:legacy];
        self.fileMenu=[main itemWithTitle:@"File"].submenu;
        self.fileMenu.autoenablesItems=YES;
        // Do not inherit the document-template menu's Print/Recent sections.
        self.fileMenu.minimumWidth=0;
        self.editItem=[main itemWithTitle:@"Edit"];self.hostEditMenu=self.editItem.submenu;
        self.guestEditMenu=[[NSMenu alloc] initWithTitle:@"Edit"];
        NSArray *editTitles=@[@"Undo",@"Cut",@"Copy",@"Paste",@"Select All"],*editActions=@[@"undo:",@"cut:",@"copy:",@"paste:",@"selectAll:"],*editKeys=@[@"z",@"x",@"c",@"v",@"a"];
        for(NSUInteger i=0;i<editTitles.count;i++){if(i==1)[self.guestEditMenu addItem:NSMenuItem.separatorItem];[self.guestEditMenu addItemWithTitle:editTitles[i] action:NSSelectorFromString(editActions[i]) keyEquivalent:editKeys[i]];}
        self.guestEditMenu.delegate=self;self.hostEditMenu.delegate=self;
        NSMenuItem *view=[main itemWithTitle:@"View"];
        if(!view){view=[[NSMenuItem alloc] initWithTitle:@"View" action:nil keyEquivalent:@""];[main insertItem:view atIndex:MIN(3,main.numberOfItems)];}
        self.viewMenu=[[NSMenu alloc] initWithTitle:@"View"];view.submenu=self.viewMenu;
        self.fileMenu.delegate=self;self.viewMenu.delegate=self;
        self.actionsItem=[[NSMenuItem alloc] initWithTitle:@"Actions" action:nil keyEquivalent:@""];self.actionsItem.submenu=[[NSMenu alloc] initWithTitle:@"Actions"];self.actionsItem.submenu.delegate=self;
        self.devicesItem=[[NSMenuItem alloc] initWithTitle:@"Devices" action:nil keyEquivalent:@""];self.devicesItem.submenu=[[NSMenu alloc] initWithTitle:@"Devices"];self.devicesItem.submenu.delegate=self;
        NSInteger index=[main indexOfItem:view]+1;[main insertItem:self.actionsItem atIndex:index];[main insertItem:self.devicesItem atIndex:index+1];
        NSMenu *windows=NSApp.windowsMenu?:[main itemWithTitle:@"Window"].submenu;
        if(windows){[windows addItem:NSMenuItem.separatorItem];[windows addItemWithTitle:@"Library" action:@selector(showLibrary:) keyEquivalent:@""].target=NSApp.delegate;}
        for(NSString *name in @[NSWindowDidBecomeKeyNotification,NSWindowDidResignKeyNotification,NSWindowWillCloseNotification,@"VIMLibraryChanged"])
            [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(contextChanged:) name:name object:nil];
        [self refresh];
    }return self;
}
- (void)dealloc {[NSNotificationCenter.defaultCenter removeObserver:self];}
- (NSWindow *)activeWindow {
    NSWindow *window=NSApp.keyWindow;while(window.isSheet)window=window.sheetParent;
    // A real key panel (About, Help search, etc.) is not a guest context. Only
    // use the main window when menu tracking temporarily leaves no key window.
    // Rebuilding remains deferred out of tracking in contextChanged: below.
    return window?:NSApp.mainWindow;
}
- (VIMSession *)session {id controller=self.activeWindow.windowController;return [controller isKindOfClass:VIMSession.class]?controller:nil;}
- (void)contextChanged:(NSNotification *)note {
    [NSObject cancelPreviousPerformRequestsWithTarget:self selector:@selector(refresh) object:nil];
    // Menu tracking runs a nested run loop. A main-queue block can run inside
    // it and replace the very submenu AppKit is laying out. Wait for normal UI
    // dispatch before rebuilding any menu hierarchy.
    [self performSelector:@selector(refresh) withObject:nil afterDelay:0 inModes:@[NSDefaultRunLoopMode]];
}
- (NSMenuItem *)add:(NSString *)title action:(SEL)action key:(NSString *)key to:(NSMenu *)menu target:(id)target {
    NSMenuItem *item=[menu addItemWithTitle:title action:action keyEquivalent:key];item.target=target;return item;
}
- (void)refresh {
    VIMSession *session=self.session;BOOL guest=session!=nil;self.guestContext=guest;
    NSMenu *edit=guest?self.guestEditMenu:self.hostEditMenu;
    if(self.editItem.submenu!=edit)self.editItem.submenu=edit;
    // Keep the menu bar stable across library, creation and guest windows.
    // A non-guest context has no retained session targets or actionable items.
    self.actionsItem.hidden=self.devicesItem.hidden=NO;
    [self buildGuestMenus:session];
    [self.fileMenu removeAllItems];
    if(!guest) {
        [self add:@"New Virtual Intel Mac…" action:@selector(newMac:) key:@"n" to:self.fileMenu target:NSApp.delegate];
        [self add:@"Open Virtual Intel Mac…" action:@selector(openExistingMac:) key:@"o" to:self.fileMenu target:self.library];
        VIMMachine *selected=self.library.selectedMachine;
        if(selected&&self.activeWindow==self.library.view.window) {
            [self.fileMenu addItem:NSMenuItem.separatorItem];
            NSMenuItem *remove=[self add:[NSString stringWithFormat:@"Remove “%@”",selected.name] action:@selector(removeSelectedMachine:) key:@"" to:self.fileMenu target:self.library];remove.enabled=![VIMSession isRunningMachine:selected];
        }
        [self.fileMenu addItem:NSMenuItem.separatorItem];
    }
    [self add:@"Close Window" action:@selector(performClose:) key:@"w" to:self.fileMenu target:nil];
    [self.viewMenu removeAllItems];
    if(guest) {
        NSMenuItem *full=[self add:(self.activeWindow.styleMask&NSWindowStyleMaskFullScreen)?@"Exit Full Screen":@"Enter Full Screen" action:@selector(toggleFullScreen:) key:@"f" to:self.viewMenu target:self.activeWindow];full.keyEquivalentModifierMask=NSEventModifierFlagControl|NSEventModifierFlagCommand;
        [self.viewMenu addItem:NSMenuItem.separatorItem];
        NSMenuItem *retina=[self.viewMenu addItemWithTitle:@"Retina Resolution" action:nil keyEquivalent:@""];retina.submenu=[NSMenu new];
        for(NSUInteger i=0;i<3;i++) {NSMenuItem *item=[self add:@[@"More Space",@"Best for Retina display",@"Scaled"][i] action:@selector(changeRetinaMode:) key:@"" to:retina.submenu target:session];item.tag=i;item.state=[session.displayMode isEqual:@[@"space",@"retina",@"scaled"][i]]?NSControlStateValueOn:NSControlStateValueOff;}
        [self.viewMenu addItem:NSMenuItem.separatorItem];[self add:@"Take Screenshot…" action:@selector(takeScreenshot:) key:@"" to:self.viewMenu target:session];
    } else {
        for(NSUInteger i=0;i<2;i++){NSMenuItem *item=[self add:i?@"Compact View":@"Expanded View" action:@selector(setDensity:) key:@"" to:self.viewMenu target:self];item.tag=i;item.state=self.library.compact==(i==1)?NSControlStateValueOn:NSControlStateValueOff;}
        [self.viewMenu addItem:NSMenuItem.separatorItem];
        NSMenuItem *arrange=[self.viewMenu addItemWithTitle:@"Arrange By" action:nil keyEquivalent:@""];arrange.submenu=[[NSMenu alloc] initWithTitle:@"Arrange By"];
        NSArray *names=@[@"Name",@"Type",@"Date Last Opened",@"Date Created",@"Size",@"Tags",@"None"];
        for(NSUInteger i=0;i<names.count;i++) {
            if(i==6)[arrange.submenu addItem:NSMenuItem.separatorItem];
            NSMenuItem *item=[self add:names[i] action:@selector(arrange:) key:i==6?@"0":[NSString stringWithFormat:@"%lu",(unsigned long)i+1] to:arrange.submenu target:self];
            item.keyEquivalentModifierMask=NSEventModifierFlagControl|NSEventModifierFlagCommand;item.representedObject=names[i];item.state=[self.library.arrangement isEqual:names[i]]?NSControlStateValueOn:NSControlStateValueOff;
        }
    }
}
- (void)buildGuestMenus:(VIMSession *)session {
    NSMenu *actions=self.actionsItem.submenu,*devices=self.devicesItem.submenu;[actions removeAllItems];[devices removeAllItems];
    actions.autoenablesItems=devices.autoenablesItems=session!=nil;
    NSArray *titles=@[@"Start",@"Shut Down",@"Stop…",@"Reset…"],*selectors=@[@"startAgain:",@"shutdown:",@"forceStop:",@"resetMachine:"];
    NSArray *symbols=@[@"power",@"power.circle",@"stop.fill",@"arrow.counterclockwise"];
    for(NSUInteger i=0;i<titles.count;i++) {
        if(i==1||i==2)[actions addItem:NSMenuItem.separatorItem];
        NSMenuItem *item=[self add:titles[i] action:NSSelectorFromString(selectors[i]) key:@"" to:actions target:session];item.image=[NSImage imageWithSystemSymbolName:symbols[i] accessibilityDescription:nil];item.enabled=session!=nil;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 270000
        if(@available(macOS 27.0,*))item.preferredImageVisibility=NSMenuItemImageVisibilityVisible;
#endif
    }
    [actions addItem:NSMenuItem.separatorItem];NSMenuItem *configure=[self add:@"Configure…" action:@selector(configureMachine:) key:@"" to:actions target:session];configure.image=[NSImage imageWithSystemSymbolName:@"gearshape" accessibilityDescription:nil];configure.enabled=session!=nil;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 270000
    if(@available(macOS 27.0,*))configure.preferredImageVisibility=NSMenuItemImageVisibilityVisible;
#endif
    for(NSString *name in @[@"Network",@"Sound Output",@"Microphone",@"CD/DVD",@"USB",@"Sharing",@"Keyboard"]) {
        NSMenuItem *item=[devices addItemWithTitle:name action:nil keyEquivalent:@""];
        if(!session) {
            item.enabled=NO;item.submenu=[NSMenu new];item.submenu.autoenablesItems=NO;
            NSMenuItem *placeholder=[item.submenu addItemWithTitle:@"No Virtual Mac Selected" action:nil keyEquivalent:@""];placeholder.enabled=NO;
            continue;
        }
        if(![name isEqual:@"Sharing"]){item.submenu=[session deviceMenuForKind:name];continue;}
        item.submenu=[NSMenu new];[self add:@"File Sharing…" action:@selector(showSharingSettings:) key:@"" to:item.submenu target:session];[self add:@"Remote Login…" action:@selector(showSSHSettings:) key:@"" to:item.submenu target:session];
    }
}
- (void)menuNeedsUpdate:(NSMenu *)menu {
    // Context changes rebuild the menus before tracking. Replacing every menu
    // while AppKit sizes a tracked submenu leaves stale blank section space.
    VIMSession *session=self.session;
    for(NSMenuItem *item in menu.itemArray)if(session&&item.target==session)
        item.enabled=[session validateMenuItem:item];
}
- (void)menuWillOpen:(NSMenu *)menu {[self.session releaseGuestKeys];}
- (void)setDensity:(NSMenuItem *)sender {[self.library setDensityFromMenu:sender];[self contextChanged:nil];}
- (void)arrange:(NSMenuItem *)sender {[self.library arrangeFromMenu:sender];[self contextChanged:nil];}
- (BOOL)validateMenuItem:(NSMenuItem *)item {
    if(item.action==@selector(setDensity:)||item.action==@selector(arrange:))return self.activeWindow==self.library.view.window;
    return YES;
}
@end
