#import "VIMLibraryViewController.h"
#import "VIMLibraryLocation.h"
#import "VIMMachine.h"
#import "VIMSession.h"
#import "VIMMachineSettings.h"
#import "VIMNewMachineWindow.h"
#import "VIMPlayButton.h"
#import "VIMAlerts.h"
#import <QuartzCore/QuartzCore.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

// A recovery row is deliberately not a partially valid runtime model.
@interface VIMUnavailableMac : VIMMachine
@property NSURL *missingURL;
@property NSString *savedName;
@property NSString *savedGuest;
@property NSString *reason;
@end
@implementation VIMUnavailableMac
- (NSURL *)URL {return self.missingURL;}
- (NSString *)name {return self.savedName;}
- (NSString *)guest {return self.savedGuest?:@"";}
@end

@interface VIMLibraryTable : NSTableView
@end
@implementation VIMLibraryTable
- (void)keyDown:(NSEvent *)event {
    if(event.keyCode==36||event.keyCode==76){[NSApp sendAction:self.doubleAction to:self.target from:self];return;}
    [super keyDown:event];
}
@end

static NSPasteboardType const VIMLibraryDragType=@"com.mac.virtualintelmac.library-row";
@interface VIMLibraryLabel : NSTextField
@end
@implementation VIMLibraryLabel
- (NSView *)hitTest:(NSPoint)point {return nil;}
@end

static NSColor *VIMReferenceColor(CGFloat r,CGFloat g,CGFloat b) {
    // Use explicit sRGB: calibrated RGB brightens the reference's encoded grays.
    return [NSColor colorWithSRGBRed:r green:g blue:b alpha:1];
}
static NSImage *VIMLibrarySymbol(NSString *name,CGFloat size) {
    NSImage *image=[NSImage imageWithSystemSymbolName:name accessibilityDescription:nil];
    return [image imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithPointSize:size weight:NSFontWeightRegular]];
}
static NSString *VIMLibraryResources(VIMMachine *machine) {
    NSDictionary *configuration=machine.configuration;
    NSNumberFormatter *number=[NSNumberFormatter new];number.numberStyle=NSNumberFormatterDecimalStyle;number.maximumFractionDigits=2;
    NSString *memory=[number stringFromNumber:@([configuration[@"MemoryMiB"] doubleValue]/1024)];
    uint64_t capacity=machine.diskCapacityBytes;
    NSString *storage=capacity?[NSString stringWithFormat:@"%@ GB",[number stringFromNumber:@(capacity/(double)(1ULL<<30))]]:@"Storage unavailable";
    return [NSString stringWithFormat:@"%@ CPU · %@ GB RAM · %@",configuration[@"CPUCount"],memory,storage];
}

@interface VIMLibraryRow : NSTableRowView
@property CGFloat thumbnailWidth;
@end
@implementation VIMLibraryRow
- (void)drawBackgroundInRect:(NSRect)dirtyRect {
    BOOL dark=[self.effectiveAppearance bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua,NSAppearanceNameDarkAqua]]==NSAppearanceNameDarkAqua;
    NSColor *top=dark?VIMReferenceColor(.19,.19,.20):VIMReferenceColor(.973,.973,.973);
    NSColor *bottom=dark?VIMReferenceColor(.185,.185,.195):VIMReferenceColor(.963,.963,.963);
    [[[NSGradient alloc] initWithStartingColor:top endingColor:bottom] drawInRect:self.bounds angle:90];
    [VIMReferenceColor(.165,.165,.165) setFill];NSRectFill(NSMakeRect(0,0,self.thumbnailWidth,NSHeight(self.bounds)));
    [(dark?VIMReferenceColor(.30,.30,.31):VIMReferenceColor(.89,.89,.89)) setFill];NSRectFill(NSMakeRect(self.thumbnailWidth,0,NSWidth(self.bounds)-self.thumbnailWidth,.5));
}
- (void)drawSelectionInRect:(NSRect)dirtyRect {
    NSRect detail=NSMakeRect(self.thumbnailWidth,0,NSWidth(self.bounds)-self.thumbnailWidth,NSHeight(self.bounds));
    NSColor *color=VIMReferenceColor(.88,.92,.98);
    if([self.effectiveAppearance bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua,NSAppearanceNameDarkAqua]]==NSAppearanceNameDarkAqua)color=VIMReferenceColor(.23,.30,.41);
    [color setFill];NSRectFill(detail);
}
@end

@interface VIMLibraryCell : NSTableCellView
@property CGFloat thumbnailWidth;
@property BOOL compact;
@property(nonatomic) BOOL hovered;
@property VIMPlayButton *primary;
@property(weak) NSTableView *table;
@property NSButton *configure;
@property NSTextField *specifications;
@property NSTrackingArea *hoverTracking;
@end
@implementation VIMLibraryCell
- (BOOL)isFlipped {return YES;}
- (NSArray<NSDraggingImageComponent *> *)draggingImageComponents {
    NSArray *components=[super draggingImageComponents];
    for(NSDraggingImageComponent *component in components)if([component.key isEqual:NSDraggingImageComponentIconKey]) {
        NSView *viewport=self.imageView.superview;
        NSImage *source=self.imageView.image;NSSize size=viewport.bounds.size;
        if(!source||size.width<=0||size.height<=0)continue;
        NSSize original=source.size;CGFloat scale=MAX(size.width/original.width,size.height/original.height);
        NSRect crop=NSMakeRect((original.width-size.width/scale)/2,(original.height-size.height/scale)/2,size.width/scale,size.height/scale);
        component.contents=[NSImage imageWithSize:size flipped:NO drawingHandler:^BOOL(NSRect rect){[source drawInRect:rect fromRect:crop operation:NSCompositingOperationSourceOver fraction:1];return YES;}];
        // The aspect-fill image view extends beyond its clipped viewport.
        // Drag exactly the visible thumbnail, not the larger underlying image.
        component.frame=[self convertRect:viewport.bounds fromView:viewport];
    }
    return components;
}
- (void)setBackgroundStyle:(NSBackgroundStyle)style {
    // AppKit normally swaps selected row labels to white/emphasized text.
    // Our selection is the pale reference tint, so keep the ordinary label.
    [super setBackgroundStyle:NSBackgroundStyleNormal];
    self.textField.textColor=NSColor.labelColor;
    self.textField.font=[NSFont systemFontOfSize:self.compact?17:22 weight:NSFontWeightLight];
    self.specifications.textColor=NSColor.secondaryLabelColor;
}
- (void)mouseDown:(NSEvent *)event {
    NSInteger row=[self.table rowForView:self];if(row<0)return;
    if(event.clickCount>=2) {
        [self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
        [NSApp sendAction:self.table.doubleAction to:self.table.target from:self.table];
    } else [self.table mouseDown:event];
}
- (BOOL)allowsVibrancy {return NO;}
- (void)refreshHover:(NSNotification *)notification {
    self.hovered=self.window.isKeyWindow&&NSPointInRect([self convertPoint:self.window.mouseLocationOutsideOfEventStream fromView:nil],self.visibleRect);
}
- (void)updateTrackingAreas {
    [super updateTrackingAreas];if(self.hoverTracking)[self removeTrackingArea:self.hoverTracking];
    self.hoverTracking=[[NSTrackingArea alloc] initWithRect:self.bounds options:NSTrackingMouseEnteredAndExited|NSTrackingActiveInKeyWindow|NSTrackingInVisibleRect owner:self userInfo:nil];[self addTrackingArea:self.hoverTracking];
    // Removing a tracking area while scrolling does not promise mouseExited.
    // Recompute from the current clipped geometry, not the reused row's state.
    [self refreshHover:nil];
}
- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    NSNotificationCenter *center=NSNotificationCenter.defaultCenter;
    for(NSString *name in @[NSViewBoundsDidChangeNotification,NSWindowDidBecomeKeyNotification,NSWindowDidResignKeyNotification])[center removeObserver:self name:name object:nil];
    if(self.window) {
        NSClipView *clip=self.enclosingScrollView.contentView;clip.postsBoundsChangedNotifications=YES;
        if(clip)[center addObserver:self selector:@selector(refreshHover:) name:NSViewBoundsDidChangeNotification object:clip];
        for(NSString *name in @[NSWindowDidBecomeKeyNotification,NSWindowDidResignKeyNotification])[center addObserver:self selector:@selector(refreshHover:) name:name object:self.window];
    }
    [self refreshHover:nil];
}
- (void)dealloc {[NSNotificationCenter.defaultCenter removeObserver:self];}
- (void)setHovered:(BOOL)hovered {_hovered=hovered;self.primary.showsPlayControl=hovered;self.configure.hidden=self.compact&&!hovered;}
- (void)mouseEntered:(NSEvent *)event {self.hovered=YES;}
- (void)mouseExited:(NSEvent *)event {self.hovered=NO;}
@end

@interface VIMLibraryViewController () <NSTableViewDataSource,NSTableViewDelegate,NSToolbarDelegate,NSSearchFieldDelegate,NSMenuDelegate,NSWindowDelegate,NSMenuItemValidation>
@property NSArray<VIMMachine *> *machines;
@property NSArray<VIMMachine *> *visibleMachines;
@property NSTableView *table;
@property NSStackView *emptyState;
@property NSTextField *emptyTitle;
@property NSTextField *emptyDetail;
@property NSTextField *notice;
@property NSButton *pendingSetup;
@property NSSearchField *search;
@property NSUInteger scanGeneration;
@property BOOL compact;
@property NSScrollView *scroll;
@property NSLayoutConstraint *footerHeight;
@property BOOL libraryLoaded;
@property BOOL presentedEmptyCreation;
@property BOOL wantsLibraryWindow;
@property NSSize automaticContentSize;
@end

@implementation VIMLibraryViewController
- (void)loadView {
    self.view=[[NSView alloc] initWithFrame:NSMakeRect(0,0,540,378)];
    self.wantsLibraryWindow=YES;
    self.compact=[NSUserDefaults.standardUserDefaults boolForKey:@"VIMCompactLibrary"];
    NSScrollView *scroll=[NSScrollView new];scroll.translatesAutoresizingMaskIntoConstraints=NO;
    scroll.verticalScrollElasticity=NSScrollElasticityNone;scroll.horizontalScrollElasticity=NSScrollElasticityNone;
    self.scroll=scroll;scroll.hasVerticalScroller=YES;scroll.autohidesScrollers=YES;scroll.borderType=NSNoBorder;
    scroll.contentInsets=NSEdgeInsetsMake(0,0,0,0);scroll.automaticallyAdjustsContentInsets=NO;
    self.table=[VIMLibraryTable new];self.table.headerView=nil;self.table.style=NSTableViewStylePlain;
    self.table.accessibilityLabel=@"Virtual Macs";
    [self.table registerForDraggedTypes:@[VIMLibraryDragType,NSPasteboardTypeFileURL]];
    [self.table setDraggingSourceOperationMask:NSDragOperationMove forLocal:YES];
    self.table.rowHeight=self.compact?63:126;self.table.intercellSpacing=NSZeroSize;
    self.table.usesAlternatingRowBackgroundColors=NO;self.table.backgroundColor=NSColor.windowBackgroundColor;
    self.table.selectionHighlightStyle=NSTableViewSelectionHighlightStyleRegular;
    self.table.columnAutoresizingStyle=NSTableViewLastColumnOnlyAutoresizingStyle;
    NSTableColumn *column=[[NSTableColumn alloc] initWithIdentifier:@"machine"];column.width=540;
    [self.table addTableColumn:column];self.table.delegate=self;self.table.dataSource=self;
    self.table.target=self;self.table.doubleAction=@selector(openMachineWindow:);
    NSMenu *menu=[NSMenu new];menu.delegate=self;self.table.menu=menu;
    scroll.documentView=self.table;[self.view addSubview:scroll];
    self.notice=[NSTextField wrappingLabelWithString:@""];self.notice.font=[NSFont systemFontOfSize:12];
    self.notice.textColor=NSColor.secondaryLabelColor;self.notice.translatesAutoresizingMaskIntoConstraints=NO;
    self.notice.hidden=YES;
    [self.view addSubview:self.notice];
    self.pendingSetup=[NSButton buttonWithTitle:@"Show Setup" target:self action:@selector(newMac:)];
    self.pendingSetup.translatesAutoresizingMaskIntoConstraints=NO;self.pendingSetup.hidden=YES;[self.view addSubview:self.pendingSetup];
    self.emptyTitle=[NSTextField labelWithString:@"Create your first Mac"];
    self.emptyTitle.font=[NSFont systemFontOfSize:22 weight:NSFontWeightSemibold];
    self.emptyDetail=[NSTextField wrappingLabelWithString:@"Run macOS Mojave, Catalina, or Big Sur in its own window."];
    self.emptyDetail.alignment=NSTextAlignmentCenter;self.emptyDetail.textColor=NSColor.secondaryLabelColor;
    NSImageView *icon=[NSImageView imageViewWithImage:[NSImage imageWithSystemSymbolName:@"desktopcomputer" accessibilityDescription:nil]];
    [icon.heightAnchor constraintEqualToConstant:48].active=YES;
    self.emptyState=[NSStackView stackViewWithViews:@[icon,self.emptyTitle,self.emptyDetail,
        [NSButton buttonWithTitle:@"New Mac…" target:self action:@selector(newMac:)],
        [NSButton buttonWithTitle:@"Open Existing Mac…" target:self action:@selector(openExistingMac:)]]];
    self.emptyState.orientation=NSUserInterfaceLayoutOrientationVertical;
    self.emptyState.alignment=NSLayoutAttributeCenterX;self.emptyState.spacing=16;
    self.emptyState.translatesAutoresizingMaskIntoConstraints=NO;[self.view addSubview:self.emptyState];
    [NSLayoutConstraint activateConstraints:@[
        [scroll.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
        [scroll.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor constant:0],
        [self.notice.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:16],
        [self.notice.trailingAnchor constraintEqualToAnchor:self.pendingSetup.leadingAnchor constant:-12],
        [self.pendingSetup.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-16],
        [self.pendingSetup.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor constant:-8],
        [self.notice.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor constant:-8],
        [self.emptyState.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],
        [self.emptyState.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor],
        [self.emptyState.widthAnchor constraintLessThanOrEqualToAnchor:self.view.widthAnchor constant:-64]]];
    self.machines=@[];self.visibleMachines=@[];
    for(NSString *name in @[@"VIMLibraryChanged",@"VIMMachineRelocated",NSApplicationDidBecomeActiveNotification])
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(refresh:) name:name object:nil];
    [self reloadLibrary];
}
- (void)viewDidAppear {
    [super viewDidAppear];
    self.view.window.delegate=self;
    self.view.window.collectionBehavior=(self.view.window.collectionBehavior&~(NSWindowCollectionBehaviorFullScreenPrimary|NSWindowCollectionBehaviorFullScreenAuxiliary))|NSWindowCollectionBehaviorFullScreenNone;
    if(self.view.window.toolbar)return;
    NSToolbar *toolbar=[[NSToolbar alloc] initWithIdentifier:@"VIMLibraryToolbar"];
    toolbar.delegate=self;toolbar.displayMode=NSToolbarDisplayModeIconOnly;
    self.view.window.title=@"Virtual Intel Mac Library";self.view.window.titleVisibility=NSWindowTitleVisible;
    self.view.window.toolbar=toolbar;self.view.window.toolbarStyle=NSWindowToolbarStyleUnifiedCompact;
    self.view.window.tabbingMode=NSWindowTabbingModeDisallowed;
    [self sizeLibraryWindow];
    [self updateLibraryVisibility];
}
- (NSArray *)toolbarDefaultItemIdentifiers:(NSToolbar *)toolbar {
    return @[NSToolbarFlexibleSpaceItemIdentifier,@"density",@"new"];
}
- (NSArray *)toolbarAllowedItemIdentifiers:(NSToolbar *)toolbar {return [self toolbarDefaultItemIdentifiers:toolbar];}
- (NSToolbarItem *)toolbar:(NSToolbar *)toolbar itemForItemIdentifier:(NSString *)identifier willBeInsertedIntoToolbar:(BOOL)insert {
    NSToolbarItem *item=[[NSToolbarItem alloc] initWithItemIdentifier:identifier];
    if([identifier isEqual:@"new"]) {
        item.label=@"New Mac";item.toolTip=@"New Mac";
        item.image=[NSImage imageWithSystemSymbolName:@"plus" accessibilityDescription:@"New Mac"];
        item.target=self;item.action=@selector(newMac:);
    } else if([identifier isEqual:@"density"]) {
        NSSegmentedControl *density=[NSSegmentedControl segmentedControlWithImages:@[
            [NSImage imageWithSystemSymbolName:@"rectangle.grid.1x2" accessibilityDescription:@"Large thumbnails"],
            [NSImage imageWithSystemSymbolName:@"list.bullet" accessibilityDescription:@"Compact list"]]
            trackingMode:NSSegmentSwitchTrackingSelectOne target:self action:@selector(changeDensity:)];
        density.selectedSegment=self.compact?1:0;
        [density setToolTip:@"Large thumbnails" forSegment:0];[density setToolTip:@"Compact list" forSegment:1];
        item.label=@"View";item.view=density;
    } else {
        self.search=[[NSSearchField alloc] initWithFrame:NSMakeRect(0,0,150,24)];
        self.search.placeholderString=@"Search Macs";self.search.delegate=self;
        item.label=@"Search";item.view=self.search;
    }
    return item;
}
- (void)dealloc {[NSNotificationCenter.defaultCenter removeObserver:self];}
- (void)changeDensity:(NSSegmentedControl *)sender {
    self.compact=sender.selectedSegment==1;
    [NSUserDefaults.standardUserDefaults setBool:self.compact forKey:@"VIMCompactLibrary"];
    self.automaticContentSize=NSZeroSize;
    self.table.rowHeight=self.compact?63:126;[self.table reloadData];[self sizeLibraryWindow];
    for(NSToolbarItem *item in self.view.window.toolbar.items)if([item.itemIdentifier isEqual:@"density"]&&[item.view isKindOfClass:NSSegmentedControl.class])[(NSSegmentedControl *)item.view setSelectedSegment:self.compact?1:0];
}
- (VIMMachine *)selectedMachine {return [self selected];}
- (NSString *)arrangement {return [NSUserDefaults.standardUserDefaults stringForKey:[@"VIMLibraryArrange-" stringByAppendingString:VIMLibraryLocation.currentURL.path]]?:@"None";}
- (void)setDensityFromMenu:(NSMenuItem *)sender {
    NSSegmentedControl *control=[NSSegmentedControl segmentedControlWithLabels:@[@"Expanded",@"Compact"] trackingMode:NSSegmentSwitchTrackingSelectOne target:nil action:nil];control.selectedSegment=sender.tag;[self changeDensity:control];
}
- (void)arrangeFromMenu:(NSMenuItem *)sender {
    [NSUserDefaults.standardUserDefaults setObject:sender.representedObject forKey:[@"VIMLibraryArrange-" stringByAppendingString:VIMLibraryLocation.currentURL.path]];[self applyFilter];
}
- (void)sizeLibraryWindow {
    NSWindow *window=self.view.window;if(!window)return;
    // Fit a small library by default, cap growth, and scroll larger libraries.
    // Refreshes must not snap a manually resized window back to its default.
    CGFloat rows=MAX(1,self.visibleMachines.count)*(self.compact?63:126);
    window.contentMinSize=NSMakeSize(440,MIN(rows,126));
    window.contentMaxSize=NSMakeSize(10000,rows);
    NSSize current=window.contentView.bounds.size;
    if(current.height>rows){[window setContentSize:NSMakeSize(current.width,rows)];current=window.contentView.bounds.size;}
    if(!NSEqualSizes(self.automaticContentSize,NSZeroSize)&&
       (fabs(current.width-self.automaticContentSize.width)>1||fabs(current.height-self.automaticContentSize.height)>1))return;
    CGFloat chrome=NSHeight(window.frame)-NSHeight(window.contentView.bounds);
    CGFloat available=NSHeight((window.screen?:NSScreen.mainScreen).visibleFrame)-chrome-48;
    CGFloat height=MIN(rows,MAX(126,MIN(640,available*.8)));
    NSRect frame=window.frame;CGFloat top=NSMaxY(frame);
    NSSize content=NSMakeSize(MAX(440,NSWidth(window.contentView.bounds)),height);
    [window setContentSize:content];frame=window.frame;frame.origin.y=top-NSHeight(frame);
    self.automaticContentSize=window.contentView.bounds.size;
    NSRect visible=(window.screen?:NSScreen.mainScreen).visibleFrame;
    frame.origin.y=MAX(NSMinY(visible),MIN(frame.origin.y,NSMaxY(visible)-NSHeight(frame)));
    [window setFrame:frame display:YES];
}
- (NSRect)windowWillUseStandardFrame:(NSWindow *)window defaultFrame:(NSRect)frame {
    CGFloat titleWidth=0;
    for(VIMMachine *machine in self.visibleMachines)titleWidth=MAX(titleWidth,[machine.name sizeWithAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:22 weight:NSFontWeightLight]}].width);
    NSRect visible=(window.screen?:NSScreen.mainScreen).visibleFrame;
    CGFloat width=MIN(NSWidth(visible),MIN(800,MAX(540,titleWidth+(self.compact?180:290))));
    CGFloat chrome=NSHeight(window.frame)-NSHeight(window.contentView.bounds);
    CGFloat height=MIN(NSHeight(visible)-40,MAX(1,self.visibleMachines.count)*self.table.rowHeight+chrome);
    return NSMakeRect(MAX(NSMinX(visible),MIN(NSMinX(window.frame),NSMaxX(visible)-width)),MAX(NSMinY(visible),NSMaxY(window.frame)-height),width,height);
}
- (void)controlTextDidChange:(NSNotification *)notification {[self applyFilter];}
- (void)newMac:(id)sender {[VIMNewMachineWindow showWithInstaller:nil guest:nil];}
- (void)refresh:(id)sender {[self reloadLibrary];}
- (void)showLibrary {self.wantsLibraryWindow=YES;[self reloadLibrary];}
- (NSString *)catalogKey {return [@"VIMLibraryKnown-" stringByAppendingString:VIMLibraryLocation.currentURL.path];}
- (NSString *)orderKey {return [@"VIMLibraryOrder-" stringByAppendingString:VIMLibraryLocation.currentURL.path];}
- (void)registerPackageURL:(NSURL *)URL {
    if(!URL.isFileURL||![URL.pathExtension.lowercaseString isEqual:@"vintelmac"])return;
    NSMutableArray *paths=[[NSUserDefaults.standardUserDefaults arrayForKey:@"VIMRegisteredPackages"] mutableCopy]?:[NSMutableArray array];
    NSString *path=URL.URLByResolvingSymlinksInPath.path;if(![paths containsObject:path])[paths addObject:path];
    [NSUserDefaults.standardUserDefaults setObject:paths forKey:@"VIMRegisteredPackages"];[self reloadLibrary];
}
- (void)openExistingMac:(id)sender {
    NSOpenPanel *panel=[NSOpenPanel openPanel];panel.canChooseDirectories=YES;panel.canChooseFiles=YES;panel.allowsMultipleSelection=YES;
    panel.allowedContentTypes=@[[UTType typeWithFilenameExtension:@"vintelmac"]];panel.prompt=@"Add to Library";
    panel.message=@"Choose existing Mac packages. Their disks stay in their current locations.";
    [panel beginSheetModalForWindow:self.view.window completionHandler:^(NSModalResponse response){
        if(response==NSModalResponseOK)for(NSURL *URL in panel.URLs)[self registerPackageURL:URL];
    }];
}
- (NSDragOperation)tableView:(NSTableView *)table validateDrop:(id<NSDraggingInfo>)info proposedRow:(NSInteger)row proposedDropOperation:(NSTableViewDropOperation)operation {
    if(info.draggingSource==table&&[info.draggingPasteboard stringForType:VIMLibraryDragType]) {
        [table setDropRow:MAX(0,MIN(row,(NSInteger)self.visibleMachines.count)) dropOperation:NSTableViewDropAbove];return NSDragOperationMove;
    }
    [table setDropRow:-1 dropOperation:NSTableViewDropOn];
    for(NSURL *URL in [info.draggingPasteboard readObjectsForClasses:@[NSURL.class] options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}])
        if([URL.pathExtension.lowercaseString isEqual:@"vintelmac"])return NSDragOperationLink;
    return NSDragOperationNone;
}
- (BOOL)tableView:(NSTableView *)table acceptDrop:(id<NSDraggingInfo>)info row:(NSInteger)row dropOperation:(NSTableViewDropOperation)operation {
    if(info.draggingSource==table) {
        NSString *path=[info.draggingPasteboard stringForType:VIMLibraryDragType];
        return [self moveMachineAtPath:path beforeVisibleRow:row];
    }
    BOOL accepted=NO;
    for(NSURL *URL in [info.draggingPasteboard readObjectsForClasses:@[NSURL.class] options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}])
        if([URL.pathExtension.lowercaseString isEqual:@"vintelmac"]){[self registerPackageURL:URL];accepted=YES;}
    return accepted;
}
- (id<NSPasteboardWriting>)tableView:(NSTableView *)table pasteboardWriterForRow:(NSInteger)row {
    if(row<0||row>=self.visibleMachines.count)return nil;
    NSPasteboardItem *item=[NSPasteboardItem new];[item setString:self.visibleMachines[row].URL.path forType:VIMLibraryDragType];return item;
}
- (BOOL)moveMachineAtPath:(NSString *)path beforeVisibleRow:(NSInteger)row {
    if(!path||row<0||row>self.visibleMachines.count)return NO;
    NSUInteger source=[self.machines indexOfObjectPassingTest:^BOOL(VIMMachine *machine,NSUInteger index,BOOL *stop){return [machine.URL.path isEqual:path];}];
    if(source==NSNotFound)return NO;
    VIMMachine *before=row<self.visibleMachines.count?self.visibleMachines[row]:nil;
    VIMMachine *moving=self.machines[source];if(before==moving)return YES;
    NSMutableArray *ordered=[self.machines mutableCopy];[ordered removeObjectAtIndex:source];
    NSUInteger destination=before?[ordered indexOfObject:before]:ordered.count;if(destination==NSNotFound)destination=ordered.count;
    [ordered insertObject:moving atIndex:destination];self.machines=ordered;
    [NSUserDefaults.standardUserDefaults setObject:@"None" forKey:[@"VIMLibraryArrange-" stringByAppendingString:VIMLibraryLocation.currentURL.path]];
    // Discard scans that began before the user changed the order.
    self.scanGeneration++;[NSUserDefaults.standardUserDefaults setObject:[ordered valueForKeyPath:@"URL.path"] forKey:self.orderKey];
    [self applyFilter];NSUInteger selection=[self.visibleMachines indexOfObject:moving];
    if(selection!=NSNotFound)[self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:selection] byExtendingSelection:NO];
    return YES;
}
- (void)reloadLibrary {
    if(!self.isViewLoaded)return;
    NSUInteger generation=++self.scanGeneration;NSURL *library=VIMLibraryLocation.machinesURL;
    NSString *catalogKey=self.catalogKey;
    NSDictionary *known=[NSUserDefaults.standardUserDefaults dictionaryForKey:catalogKey]?:@{};
    NSArray *registered=[NSUserDefaults.standardUserDefaults arrayForKey:@"VIMRegisteredPackages"]?:@[];
    NSArray *savedOrder=[NSUserDefaults.standardUserDefaults arrayForKey:self.orderKey]?:@[];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        NSError *error=nil;NSArray *files=[NSFileManager.defaultManager contentsOfDirectoryAtURL:library includingPropertiesForKeys:nil
            options:NSDirectoryEnumerationSkipsHiddenFiles error:&error];
        NSMutableSet *paths=[NSMutableSet setWithArray:known.allKeys];[paths addObjectsFromArray:registered];
        for(NSURL *URL in files)if([URL.pathExtension.lowercaseString isEqual:@"vintelmac"])[paths addObject:URL.URLByResolvingSymlinksInPath.path];
        NSMutableArray *machines=[NSMutableArray array];NSMutableDictionary *updated=[NSMutableDictionary dictionary];NSUInteger unavailable=0;
        NSMutableSet *identifiers=[NSMutableSet set];NSMutableArray *missing=[NSMutableArray array];
        for(NSString *path in paths) {
            NSURL *URL=[NSURL fileURLWithPath:path];NSError *failure=nil;VIMMachine *machine=[VIMMachine loadURL:URL error:&failure];
            if(machine) {
                [machine applyPendingChanges:nil];
                [machines addObject:machine];NSString *identifier=machine.configuration[@"Identifier"]?:path;[identifiers addObject:identifier];
                updated[machine.URL.path]=@{@"Name":machine.name,@"Guest":machine.guest,@"Identifier":identifier};
            } else {
                VIMUnavailableMac *row=[VIMUnavailableMac new];row.missingURL=URL;row.savedName=known[path][@"Name"]?:URL.lastPathComponent.stringByDeletingPathExtension;
                row.savedGuest=known[path][@"Guest"]?:@"";row.reason=@"Mac Unavailable";[missing addObject:row];
            }
        }
        for(VIMUnavailableMac *row in missing) {
            NSDictionary *record=known[row.URL.path];
            if(record[@"Identifier"]&&[identifiers containsObject:record[@"Identifier"]])continue; // Renamed, not lost.
            [machines addObject:row];unavailable++;updated[row.URL.path]=record?:@{@"Name":row.name,@"Guest":row.guest};
        }
        [machines sortUsingComparator:^NSComparisonResult(VIMMachine *a,VIMMachine *b){
            NSUInteger x=[savedOrder indexOfObject:a.URL.path],y=[savedOrder indexOfObject:b.URL.path];
            if(x!=y)return x<y?NSOrderedAscending:NSOrderedDescending;
            return [a.name localizedStandardCompare:b.name];
        }];
        dispatch_async(dispatch_get_main_queue(),^{
            if(generation!=self.scanGeneration)return;
            // Retain known rows while the external library is disconnected.
            self.libraryLoaded=YES;self.machines=machines;[NSUserDefaults.standardUserDefaults setObject:updated forKey:catalogKey];[self applyFilter];
            self.notice.stringValue=error?@"Library unavailable. Connect its drive, or choose a library in Settings.":
                (unavailable?[NSString stringWithFormat:@"%lu Mac packages need attention. Check their files in Finder.",(unsigned long)unavailable]:@"");
            NSDictionary *job=[NSUserDefaults.standardUserDefaults dictionaryForKey:@"VIMCreationJob"];
            self.pendingSetup.hidden=YES;
            if(job&&!error)self.notice.stringValue=[NSString stringWithFormat:@"Setup in progress: %@",job[@"Name"]?:@"New Mac"];
            if(error&&!self.machines.count) {
                self.emptyTitle.stringValue=@"Your library is unavailable";
                self.emptyDetail.stringValue=@"Reconnect its drive to see your Macs. Your library location is saved in Settings.";
            }
        });
    });
}
- (void)applyFilter {
    CGFloat previousTop=NSMinY(self.scroll.contentView.bounds);
    NSURL *selection=[self selected].URL;NSString *query=self.search.stringValue?:@"";
    self.visibleMachines=query.length?[self.machines filteredArrayUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(VIMMachine *machine,NSDictionary *bindings){
        return [machine.name localizedCaseInsensitiveContainsString:query];
    }]]:self.machines;
    NSString *arrange=self.arrangement;
    if(![arrange isEqual:@"None"]) {
        NSMutableDictionary *values=[NSMutableDictionary dictionary];
        NSDictionary *lastOpened=[NSUserDefaults.standardUserDefaults dictionaryForKey:@"VIMLastOpened"]?:@{};
        for(VIMMachine *machine in self.visibleMachines) {
            id value=machine.name;
            if([arrange isEqual:@"Type"])value=machine.guest;
            else if([arrange isEqual:@"Date Last Opened"])value=lastOpened[machine.configuration[@"Identifier"]?:machine.URL.path]?:NSDate.distantPast;
            else if([arrange isEqual:@"Date Created"]){[machine.URL getResourceValue:&value forKey:NSURLCreationDateKey error:nil];if(![value isKindOfClass:NSDate.class])value=NSDate.distantPast;}
            else if([arrange isEqual:@"Size"]){value=@0;NSString *disk=machine.configuration[@"Disk"];if(disk.length)[[machine.URL URLByAppendingPathComponent:disk] getResourceValue:&value forKey:NSURLTotalFileAllocatedSizeKey error:nil];if(![value isKindOfClass:NSNumber.class])value=@0;}
            else if([arrange isEqual:@"Tags"]){NSArray *tags=nil;[machine.URL getResourceValue:&tags forKey:NSURLTagNamesKey error:nil];value=[tags isKindOfClass:NSArray.class]?[[tags sortedArrayUsingSelector:@selector(localizedStandardCompare:)] componentsJoinedByString:@", "]:@"";}
            values[machine.URL.path]=value?:@"";
        }
        self.visibleMachines=[self.visibleMachines sortedArrayUsingComparator:^NSComparisonResult(VIMMachine *a,VIMMachine *b){
            id x=values[a.URL.path],y=values[b.URL.path];NSComparisonResult order=[x isKindOfClass:NSString.class]?[x localizedStandardCompare:y]:[x compare:y];
            if([arrange hasPrefix:@"Date "]||[arrange isEqual:@"Size"])order=-order;
            return order==NSOrderedSame?[a.name localizedStandardCompare:b.name]:order;
        }];
    }
    [self.table reloadData];
    NSUInteger index=[self.visibleMachines indexOfObjectPassingTest:^BOOL(VIMMachine *machine,NSUInteger idx,BOOL *stop){return [machine.URL isEqual:selection];}];
    if(index!=NSNotFound)[self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:index] byExtendingSelection:NO];
    self.emptyState.hidden=self.visibleMachines.count!=0;
    self.emptyTitle.stringValue=query.length?@"No matching Macs":@"Create your first Mac";
    self.emptyDetail.stringValue=query.length?@"Try a different name.":@"Run macOS Mojave, Catalina, or Big Sur in its own window.";
    self.emptyState.hidden=YES;[self sizeLibraryWindow];
    [self.view layoutSubtreeIfNeeded];
    CGFloat maxTop=MAX(0,self.visibleMachines.count*self.table.rowHeight-NSHeight(self.scroll.contentView.bounds));
    [self.scroll.contentView scrollToPoint:NSMakePoint(0,MAX(0,MIN(previousTop,maxTop)))];
    [self.scroll reflectScrolledClipView:self.scroll.contentView];
    [self updateLibraryVisibility];
}
- (void)updateLibraryVisibility {
    if(!self.libraryLoaded||!self.view.window)return;
    if(self.machines.count) {
        self.presentedEmptyCreation=NO;
        if(self.wantsLibraryWindow){self.wantsLibraryWindow=NO;[self.view.window makeKeyAndOrderFront:nil];}
    } else {
        [self.view.window orderOut:nil];
        if(!self.presentedEmptyCreation){self.presentedEmptyCreation=YES;[VIMNewMachineWindow showWithInstaller:nil guest:nil];}
    }
}
- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {return self.visibleMachines.count;}
- (NSTableRowView *)tableView:(NSTableView *)tableView rowViewForRow:(NSInteger)row {
    VIMLibraryRow *view=[VIMLibraryRow new];view.thumbnailWidth=self.compact?101:202;return view;
}
- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)column row:(NSInteger)row {
    VIMMachine *machine=self.visibleMachines[row];VIMLibraryCell *cell=[VIMLibraryCell new];
    BOOL unavailable=[machine isKindOfClass:VIMUnavailableMac.class];
    BOOL running=!unavailable&&[VIMSession isRunningMachine:machine];
    cell.compact=self.compact;cell.thumbnailWidth=self.compact?101:202;cell.table=tableView;
    // A standard image view, centered and aspect-filled inside a clipped pane.
    // AppKit has aspect-fit but no aspect-fill enum: size the image view to
    // cover the pane and let the container crop, without custom image drawing.
    NSView *thumbnail=[NSView new];thumbnail.wantsLayer=YES;thumbnail.layer.masksToBounds=YES;
    NSImage *wallpaper=[NSImage imageNamed:[machine.guest stringByAppendingString:@"Wallpaper"]]?:[NSImage imageNamed:@"tigerWallpaper"];
    // A disconnected/previously registered package may have no saved OS type.
    // imageViewWithImage: asserts on nil; the recovery row must remain usable
    // even when its artwork (or the package itself) is unavailable.
    NSImageView *image=[NSImageView new];
    image.image=wallpaper;
    image.imageScaling=NSImageScaleProportionallyUpOrDown;image.accessibilityElement=NO;
    image.translatesAutoresizingMaskIntoConstraints=NO;thumbnail.translatesAutoresizingMaskIntoConstraints=NO;
    [thumbnail addSubview:image];[cell addSubview:thumbnail];cell.imageView=image;
    CGFloat aspect=wallpaper.size.height>0?wallpaper.size.width/wallpaper.size.height:1.6;
    NSLayoutConstraint *fit=[image.widthAnchor constraintEqualToAnchor:thumbnail.widthAnchor];fit.priority=999;
    [NSLayoutConstraint activateConstraints:@[[thumbnail.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor],[thumbnail.topAnchor constraintEqualToAnchor:cell.topAnchor],[thumbnail.bottomAnchor constraintEqualToAnchor:cell.bottomAnchor],[thumbnail.widthAnchor constraintEqualToConstant:cell.thumbnailWidth],
        [image.centerXAnchor constraintEqualToAnchor:thumbnail.centerXAnchor],[image.centerYAnchor constraintEqualToAnchor:thumbnail.centerYAnchor],[image.widthAnchor constraintGreaterThanOrEqualToAnchor:thumbnail.widthAnchor],[image.heightAnchor constraintGreaterThanOrEqualToAnchor:thumbnail.heightAnchor],[image.widthAnchor constraintEqualToAnchor:image.heightAnchor multiplier:aspect],fit]];
    NSTextField *title=[VIMLibraryLabel labelWithString:machine.name];title.font=[NSFont systemFontOfSize:self.compact?17:22 weight:NSFontWeightLight];
    title.lineBreakMode=NSLineBreakByTruncatingTail;
    NSString *resources=unavailable?((VIMUnavailableMac *)machine).reason:VIMLibraryResources(machine);
    NSTextField *specifications=[VIMLibraryLabel labelWithString:resources?:@"Unavailable"];
    specifications.font=[NSFont systemFontOfSize:self.compact?11:12];specifications.textColor=NSColor.secondaryLabelColor;
    specifications.lineBreakMode=NSLineBreakByTruncatingTail;specifications.toolTip=resources;cell.specifications=specifications;
    NSString *action=unavailable?@"Locate":(running?@"Show":(machine.requiresInstallerDisplay?@"Continue Setup":@"Start"));
    VIMPlayButton *start=[VIMPlayButton new];start.target=self;start.action=@selector(rowStart:);start.controlDiameter=self.compact?40:58;start.glyphScale=1.35;start.tag=row;start.contentTintColor=NSColor.labelColor;
    start.accessibilityLabel=[NSString stringWithFormat:@"%@ %@",action,machine.name];start.toolTip=start.accessibilityLabel;cell.primary=start;
    NSButton *configure=[NSButton buttonWithImage:VIMLibrarySymbol(@"gearshape",20) target:self action:@selector(rowConfigure:)];
    configure.bordered=NO;configure.contentTintColor=NSColor.secondaryLabelColor;configure.toolTip=[NSString stringWithFormat:@"Configure %@",machine.name];configure.tag=row;cell.configure=configure;
    configure.enabled=!unavailable;configure.accessibilityLabel=configure.toolTip;
    NSButton *power=[NSButton buttonWithImage:VIMLibrarySymbol(running?@"arrow.up.forward.square":(unavailable?@"folder.badge.questionmark":@"power"),20) target:self action:@selector(rowStart:)];power.bordered=NO;power.contentTintColor=NSColor.secondaryLabelColor;power.tag=row;power.accessibilityLabel=start.accessibilityLabel;power.toolTip=power.accessibilityLabel;
    NSBox *separator=[NSBox new];separator.boxType=NSBoxSeparator;separator.translatesAutoresizingMaskIntoConstraints=NO;
    for(NSView *view in @[title,specifications,start,configure]){view.translatesAutoresizingMaskIntoConstraints=NO;[cell addSubview:view];}
    [NSLayoutConstraint activateConstraints:@[
        [title.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:cell.thumbnailWidth+(self.compact?22:32)],
        [title.trailingAnchor constraintLessThanOrEqualToAnchor:cell.trailingAnchor constant:self.compact?-58:-24],
        [specifications.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
        [specifications.trailingAnchor constraintLessThanOrEqualToAnchor:cell.trailingAnchor constant:self.compact?-58:-24],
        [specifications.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:self.compact?3:4],
        [start.centerXAnchor constraintEqualToAnchor:cell.leadingAnchor constant:cell.thumbnailWidth/2],
        [start.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],
        [start.widthAnchor constraintEqualToConstant:cell.thumbnailWidth],[start.heightAnchor constraintEqualToAnchor:cell.heightAnchor]]];
    if(self.compact) {
        [NSLayoutConstraint activateConstraints:@[[title.topAnchor constraintEqualToAnchor:cell.topAnchor constant:10],
            [configure.trailingAnchor constraintEqualToAnchor:cell.trailingAnchor constant:-18],[configure.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor]]];
    } else {
        power.translatesAutoresizingMaskIntoConstraints=NO;[cell addSubview:power];[cell addSubview:separator];
        [NSLayoutConstraint activateConstraints:@[[title.topAnchor constraintEqualToAnchor:cell.topAnchor constant:25],
            [power.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:cell.thumbnailWidth+30],
            [power.centerYAnchor constraintEqualToAnchor:cell.topAnchor constant:self.table.rowHeight*(101.0/126)],
            [power.widthAnchor constraintEqualToConstant:22],[power.heightAnchor constraintEqualToConstant:24],
            [separator.leadingAnchor constraintEqualToAnchor:power.trailingAnchor constant:14],
            [separator.centerYAnchor constraintEqualToAnchor:power.centerYAnchor],[separator.heightAnchor constraintEqualToConstant:15],[separator.widthAnchor constraintEqualToConstant:1],
            [configure.leadingAnchor constraintEqualToAnchor:separator.trailingAnchor constant:16],[configure.centerYAnchor constraintEqualToAnchor:power.centerYAnchor]]];
    }
    cell.hovered=NO;cell.toolTip=[NSString stringWithFormat:@"%@\n%@\n%@",machine.name,resources,unavailable?@"Unavailable. Locate this Mac’s package.":[VIMSession statusForMachine:machine]];
    cell.textField=title;return cell;
}
- (VIMMachine *)selected {
    NSInteger row=self.table.selectedRow;return row>=0&&row<self.visibleMachines.count?self.visibleMachines[row]:nil;
}
- (void)rowStart:(NSButton *)sender {
    [self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:sender.tag] byExtendingSelection:NO];[self startMachine:nil];
}
- (void)rowConfigure:(NSButton *)sender {
    [self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:sender.tag] byExtendingSelection:NO];[self machineSettings:nil];
}
- (void)menuNeedsUpdate:(NSMenu *)menu {
    [menu removeAllItems];NSInteger row=self.table.clickedRow;
    if(row>=0)[self.table selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
    if(!self.selected)return;
    if([self.selected isKindOfClass:VIMUnavailableMac.class]) {
        [menu addItemWithTitle:@"Locate Mac…" action:@selector(startMachine:) keyEquivalent:@""].target=self;
        [menu addItemWithTitle:@"Show in Finder" action:@selector(revealMachine:) keyEquivalent:@""].target=self;
        [menu addItem:NSMenuItem.separatorItem];
        [menu addItemWithTitle:@"Remove from Library…" action:@selector(removeSelectedMachine:) keyEquivalent:@""].target=self;return;
    }
    [menu addItemWithTitle:[VIMSession isRunningMachine:self.selected]?@"Show":@"Start" action:@selector(startMachine:) keyEquivalent:@""].target=self;
    [menu addItemWithTitle:@"Open Window" action:@selector(openMachineWindow:) keyEquivalent:@""].target=self;
    [menu addItemWithTitle:@"Configure…" action:@selector(machineSettings:) keyEquivalent:@""].target=self;
    [menu addItem:NSMenuItem.separatorItem];
    [menu addItemWithTitle:@"Rename…" action:@selector(renameMachine:) keyEquivalent:@""].target=self;
    [menu addItemWithTitle:@"Show in Finder" action:@selector(revealMachine:) keyEquivalent:@""].target=self;
    [menu addItem:NSMenuItem.separatorItem];
    [menu addItemWithTitle:@"Delete…" action:@selector(removeSelectedMachine:) keyEquivalent:@""].target=self;
}
- (void)revealMachine:(id)sender {if(self.selected)[NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[self.selected.URL]];}
- (void)openMachineWindow:(id)sender {
    if(!self.selected)return;
    if([self.selected isKindOfClass:VIMUnavailableMac.class]){[self locateUnavailable:(VIMUnavailableMac *)self.selected];return;}
    [VIMSession showMachineWindow:self.selected];
}
- (void)startMachine:(id)sender {
    if(!self.selected)return;NSError *error=nil;
    if([self.selected isKindOfClass:VIMUnavailableMac.class]){[self locateUnavailable:(VIMUnavailableMac *)self.selected];return;}
    if(![VIMSession openMachine:self.selected error:&error])VIMPresentError(error,self.view.window);[self.table reloadData];
}
- (void)machineSettings:(id)sender {
    if(self.selected&&![self.selected isKindOfClass:VIMUnavailableMac.class])[VIMMachineSettings showForMachine:self.selected window:self.view.window completion:^{[self reloadLibrary];}];
}
- (void)locateUnavailable:(VIMUnavailableMac *)machine {
    NSAlert *alert=[NSAlert new];alert.messageText=[NSString stringWithFormat:@"Locate %@",machine.name];alert.informativeText=[NSString stringWithFormat:@"%@\n\n%@\n\nReconnect its drive, or choose its current package location. No disks will be copied.",machine.reason,machine.URL.path];
    VIMSetAppAlertIcon(alert);
    [alert addButtonWithTitle:@"Locate…"];[alert addButtonWithTitle:@"Try Again"];[alert addButtonWithTitle:@"Cancel"];
    [alert beginSheetModalForWindow:self.view.window completionHandler:^(NSModalResponse response){
        if(response==NSAlertSecondButtonReturn){[self reloadLibrary];return;}if(response!=NSAlertFirstButtonReturn)return;
        NSOpenPanel *panel=[NSOpenPanel openPanel];panel.canChooseDirectories=YES;panel.allowedContentTypes=@[[UTType typeWithFilenameExtension:@"vintelmac"]];
        [panel beginSheetModalForWindow:self.view.window completionHandler:^(NSModalResponse choice){
            if(choice!=NSModalResponseOK)return;NSError *failure=nil;
            if(![VIMMachine loadURL:panel.URL error:&failure]){VIMPresentError(failure,self.view.window);return;}
            NSMutableDictionary *known=[[NSUserDefaults.standardUserDefaults dictionaryForKey:self.catalogKey] mutableCopy]?:[NSMutableDictionary dictionary];
            [known removeObjectForKey:machine.URL.path];[NSUserDefaults.standardUserDefaults setObject:known forKey:self.catalogKey];
            NSMutableArray *registered=[[NSUserDefaults.standardUserDefaults arrayForKey:@"VIMRegisteredPackages"] mutableCopy]?:[NSMutableArray array];
            [registered removeObject:machine.URL.path];[NSUserDefaults.standardUserDefaults setObject:registered forKey:@"VIMRegisteredPackages"];
            [self registerPackageURL:panel.URL];
        }];
    }];
}
- (void)renameMachine:(id)sender {
    VIMMachine *machine=self.selected;if(!machine)return;
    NSAlert *alert=[NSAlert new];alert.messageText=@"Rename Mac";
    VIMSetAppAlertIcon(alert);
    NSTextField *name=[[NSTextField alloc] initWithFrame:NSMakeRect(0,0,300,24)];name.stringValue=machine.name;
    alert.accessoryView=name;[alert addButtonWithTitle:@"Rename"];[alert addButtonWithTitle:@"Cancel"];
    [alert beginSheetModalForWindow:self.view.window completionHandler:^(NSModalResponse response){
        if(response!=NSAlertFirstButtonReturn)return;NSError *error=nil;
        if(![machine rename:name.stringValue error:&error])VIMPresentError(error,self.view.window);[self reloadLibrary];
    }];
}
- (void)removeSelectedMachine:(id)sender {
    VIMMachine *machine=self.selected;if(!machine)return;
    if([VIMSession isRunningMachine:machine])return;
    BOOL missing=[machine isKindOfClass:VIMUnavailableMac.class];
    NSAlert *alert=[NSAlert new];alert.messageText=[NSString stringWithFormat:missing?@"Remove “%@” from the library?":@"Move “%@” to Trash?",machine.name];
    alert.informativeText=missing?@"The missing Mac will be removed from this library. No files will be deleted.":@"This virtual Mac and its disk will be moved to Trash. You can restore the package from Trash later.";
    [alert addButtonWithTitle:@"Cancel"];[alert addButtonWithTitle:missing?@"Remove":@"Move to Trash"];
    VIMPresentAlert(alert,self.view.window,^(NSModalResponse response){
        if(response!=NSAlertSecondButtonReturn)return;
        void (^forget)(void)=^{
            NSUserDefaults *defaults=NSUserDefaults.standardUserDefaults;
            NSMutableDictionary *known=[[defaults dictionaryForKey:self.catalogKey] mutableCopy];[known removeObjectForKey:machine.URL.path];[defaults setObject:known?:@{} forKey:self.catalogKey];
            for(NSString *key in @[@"VIMRegisteredPackages",self.orderKey]){NSMutableArray *paths=[[defaults arrayForKey:key] mutableCopy];[paths removeObject:machine.URL.path];if(paths)[defaults setObject:paths forKey:key];}
            [self reloadLibrary];
        };
        if(missing){forget();return;}
        // Recheck after the confirmation sheet: another window/process could
        // have started or renamed the VM while the decision was pending.
        NSURL *lockURL=[machine.URL URLByAppendingPathComponent:@"State/Session.lock"];
        int lock=open(lockURL.fileSystemRepresentation,O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);struct stat info;
        if(lock<0||fstat(lock,&info)||!S_ISREG(info.st_mode)||info.st_nlink!=1||flock(lock,LOCK_EX|LOCK_NB)) {
            if(lock>=0)close(lock);
            NSAlert *busy=[NSAlert new];busy.messageText=@"This Mac can’t be deleted right now";
            busy.informativeText=@"Shut down the virtual Mac and close any other Virtual Intel Mac session using it, then try again.";
            [busy addButtonWithTitle:@"OK"];VIMPresentAlert(busy,self.view.window,nil);return;
        }
        VIMMachine *current=[VIMMachine loadURL:machine.URL error:nil];
        if(![current.configuration[@"Identifier"] isEqual:machine.configuration[@"Identifier"]]){close(lock);[self reloadLibrary];return;}
        [NSWorkspace.sharedWorkspace recycleURLs:@[machine.URL] completionHandler:^(NSDictionary *newURLs,NSError *error){close(lock);dispatch_async(dispatch_get_main_queue(),^{if(error)VIMPresentError(error,self.view.window);else forget();});}];
    });
}
- (BOOL)validateMenuItem:(NSMenuItem *)item {
    if(item.action==@selector(removeSelectedMachine:))return self.selected!=nil&&![VIMSession isRunningMachine:self.selected];
    return YES;
}
@end
