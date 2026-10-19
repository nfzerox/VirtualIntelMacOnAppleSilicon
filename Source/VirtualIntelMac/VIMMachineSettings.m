#import "VIMMachineSettings.h"
#import "VIMSettingsIcon.h"
#import "VIMMachine.h"
#import "VIMSession.h"
#import "VIMNetworkDefaults.h"
#import "VIMSMBService.h"
#import "VIMHostAudio.h"
#import "VIMAlerts.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
@import AVFoundation;

@interface VIMConfigurationDocument : NSView
@property BOOL drawsWindowBackground;
@end
@implementation VIMConfigurationDocument
- (BOOL)isFlipped {return YES;}
- (void)drawRect:(NSRect)rect {if(self.drawsWindowBackground){[NSColor.windowBackgroundColor setFill];NSRectFill(rect);}}
@end

// Retained by the configuration controller until the native sheet is dismissed.
// Validation keeps the editor open rather than losing the user's input.
@interface VIMConfigurationSheet : NSWindowController
@property(copy) BOOL (^save)(NSWindow *);
- (instancetype)initWithTitle:(NSString *)title content:(NSView *)content;
@end
@implementation VIMConfigurationSheet
- (instancetype)initWithTitle:(NSString *)title content:(NSView *)content {
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,420,180) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    if((self=[super initWithWindow:window])) {
        window.title=title;window.releasedWhenClosed=NO;
        NSButton *cancel=[NSButton buttonWithTitle:@"Cancel" target:self action:@selector(cancel:)];cancel.keyEquivalent=@"\e";
        NSButton *save=[NSButton buttonWithTitle:@"OK" target:self action:@selector(save:)];save.keyEquivalent=@"\r";
        NSStackView *buttons=[NSStackView stackViewWithViews:@[cancel,save]];buttons.spacing=12;
        for(NSButton *button in @[cancel,save])[button.widthAnchor constraintGreaterThanOrEqualToConstant:80].active=YES;
        NSView *root=window.contentView;
        for(NSView *view in @[content,buttons]){view.translatesAutoresizingMaskIntoConstraints=NO;[root addSubview:view];}
        [NSLayoutConstraint activateConstraints:@[[content.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:24],
            [content.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
            [content.topAnchor constraintEqualToAnchor:root.topAnchor constant:24],
            [buttons.topAnchor constraintEqualToAnchor:content.bottomAnchor constant:24],
            [buttons.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-24],
            [buttons.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-20]]];
        [window setContentSize:NSMakeSize(MAX(420,content.fittingSize.width+48),content.fittingSize.height+84)];
    }return self;
}
- (void)cancel:(id)sender {[self.window.sheetParent endSheet:self.window];}
- (void)save:(id)sender {if(!self.save||self.save(self.window))[self.window.sheetParent endSheet:self.window];}
@end

@interface VIMConfigurationSidebar : NSTableView
@property(copy) NSArray<NSString *> *categories;
@end
@implementation VIMConfigurationSidebar
@end
@interface VIMFixedSidebarClip : NSClipView
@end
@implementation VIMFixedSidebarClip
- (NSRect)constrainBoundsRect:(NSRect)proposed {
    NSRect bounds=[super constrainBoundsRect:proposed];bounds.origin=NSZeroPoint;return bounds;
}
@end
@interface VIMFixedSidebarScroll : NSScrollView
@end
@implementation VIMFixedSidebarScroll
- (void)scrollWheel:(NSEvent *)event {} // All categories fit; this is not scrollable content.
@end

@interface VIMConfigurationWindow : NSWindowController <NSToolbarDelegate,NSTableViewDelegate,NSTableViewDataSource,NSTextFieldDelegate,NSTextViewDelegate,NSWindowDelegate,NSTabViewDelegate>
@property VIMMachine *machine;
@property(copy) void (^completion)(void);
@property NSString *section;
@property NSInteger hardwareIndex;
@property NSView *body;
@property NSTextField *feedback;
@property NSMutableDictionary<NSString *,NSControl *> *fields;
@property BOOL invalidField;
@property BOOL hostFiles;
@property VIMConfigurationSheet *editorSheet;
@property NSString *guestUsername;
@property NSSlider *memorySlider;
@property NSTextField *memoryValue;
@property NSTextView *descriptionEditor;
@property uint64_t diskBytes;
@property uint64_t otherBytes;
@property BOOL sizesReady;
@property NSTimer *descriptionTimer;
@property VIMConfigurationSidebar *sidebar;
@end

static NSTextField *VIMText(NSString *text,BOOL heading) {
    NSTextField *label=[NSTextField wrappingLabelWithString:text?:@""];label.selectable=NO;
    label.font=[NSFont systemFontOfSize:13 weight:heading?NSFontWeightSemibold:NSFontWeightRegular];
    label.textColor=heading?NSColor.labelColor:NSColor.secondaryLabelColor;return label;
}
static NSStackView *VIMColumn(NSArray<NSView *> *views) {
    NSStackView *stack=[NSStackView stackViewWithViews:views];stack.orientation=NSUserInterfaceLayoutOrientationVertical;
    stack.alignment=NSLayoutAttributeLeading;stack.spacing=16;return stack;
}
static NSScrollView *VIMSharingScrollPane(NSView *pane) {
    NSScrollView *scroll=[NSScrollView new];scroll.hasVerticalScroller=YES;scroll.autohidesScrollers=YES;
    scroll.drawsBackground=NO;scroll.borderType=NSNoBorder;
    scroll.verticalScrollElasticity=NSScrollElasticityNone;scroll.horizontalScrollElasticity=NSScrollElasticityNone;
    VIMConfigurationDocument *document=[VIMConfigurationDocument new];document.translatesAutoresizingMaskIntoConstraints=NO;
    scroll.documentView=document;pane.translatesAutoresizingMaskIntoConstraints=NO;[document addSubview:pane];
    // Keep padding in the document so the last line and its bottom margin can
    // scroll fully into view. The scroll view itself fills the native tab pane.
    NSLayoutConstraint *fit=[document.heightAnchor constraintEqualToAnchor:pane.heightAnchor constant:40];fit.priority=750;
    [NSLayoutConstraint activateConstraints:@[[document.leadingAnchor constraintEqualToAnchor:scroll.contentView.leadingAnchor],
        [document.topAnchor constraintEqualToAnchor:scroll.contentView.topAnchor],
        [document.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor],
        [document.heightAnchor constraintGreaterThanOrEqualToAnchor:scroll.contentView.heightAnchor],fit,
        [pane.leadingAnchor constraintEqualToAnchor:document.leadingAnchor constant:20],
        [pane.trailingAnchor constraintEqualToAnchor:document.trailingAnchor constant:-20],
        [pane.topAnchor constraintEqualToAnchor:document.topAnchor constant:20],
        [pane.bottomAnchor constraintLessThanOrEqualToAnchor:document.bottomAnchor constant:-20]]];
    for(NSView *view in pane.subviews)if([view isKindOfClass:NSTextField.class]&&![(NSTextField *)view isEditable])
        [view.widthAnchor constraintEqualToAnchor:pane.widthAnchor].active=YES;
    return scroll;
}
static void VIMAlignFormBaselines(NSView *view) {
    if([view isKindOfClass:NSGridView.class]) {
        NSGridView *grid=(NSGridView *)view;grid.rowAlignment=NSGridRowAlignmentFirstBaseline;
        for(NSInteger row=0;row<grid.numberOfRows;row++) {
            NSView *value=[grid cellAtColumnIndex:1 rowIndex:row].contentView;
            if([value isKindOfClass:NSStackView.class]||[value isKindOfClass:NSScrollView.class])
                [grid rowAtIndex:row].yPlacement=NSGridCellPlacementTop;
        }
    }
    for(NSView *child in view.subviews)VIMAlignFormBaselines(child);
}

@implementation VIMConfigurationWindow
- (instancetype)initWithMachine:(VIMMachine *)machine {
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,640,300)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable
        backing:NSBackingStoreBuffered defer:NO];
    if((self=[super initWithWindow:window])) {
        self.machine=machine;self.section=@"Hardware";self.hostFiles=YES;window.releasedWhenClosed=NO;window.delegate=self;window.windowController=self;
        window.backgroundColor=NSColor.windowBackgroundColor;
        window.collectionBehavior=NSWindowCollectionBehaviorFullScreenNone;
        self.guestUsername=[NSUserDefaults.standardUserDefaults stringForKey:[@"VIMGuestUsername-" stringByAppendingString:machine.URL.path]]?:@"";
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(sessionStateChanged:) name:@"VIMLibraryChanged" object:nil];
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(sessionStateChanged:) name:@"VIMNetworkChanged" object:nil];
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(packageRelocated:) name:@"VIMMachineRelocated" object:nil];
        window.title=[NSString stringWithFormat:@"“%@” Configuration",machine.name];
        NSToolbar *toolbar=[[NSToolbar alloc] initWithIdentifier:@"VIMConfigurationToolbar"];
        toolbar.delegate=self;toolbar.displayMode=NSToolbarDisplayModeIconAndLabel;
        toolbar.allowsUserCustomization=NO;toolbar.autosavesConfiguration=NO;
        window.toolbar=toolbar;window.toolbarStyle=NSWindowToolbarStylePreference;
        window.tabbingMode=NSWindowTabbingModeDisallowed;
        toolbar.selectedItemIdentifier=self.section;[self render];[window center];[self readStorageSizes];
    }return self;
}
- (void)dealloc {[NSNotificationCenter.defaultCenter removeObserver:self];}
- (void)sessionStateChanged:(NSNotification *)notification {
    VIMMachine *fresh=[VIMMachine loadURL:self.machine.URL error:nil];if(fresh)self.machine=fresh;
    if(!self.window.firstResponder||![self.window.firstResponder isKindOfClass:NSTextView.class])[self render];
}
- (void)packageRelocated:(NSNotification *)note {
    if(![note.userInfo[@"Identifier"] isEqual:self.machine.configuration[@"Identifier"]])return;
    VIMMachine *fresh=[VIMMachine loadURL:note.userInfo[@"URL"] error:nil];if(fresh)self.machine=fresh;
    self.window.title=[NSString stringWithFormat:@"“%@” Configuration",self.machine.name];[self render];
}
- (void)readStorageSizes {
    NSURL *package=self.machine.URL;NSString *disk=self.machine.configuration[@"Disk"];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
        uint64_t diskSize=0,other=0;
        struct stat diskInfo={0};if(disk.length)stat([package URLByAppendingPathComponent:disk].fileSystemRepresentation,&diskInfo);
        NSDirectoryEnumerator *files=[NSFileManager.defaultManager enumeratorAtURL:package includingPropertiesForKeys:@[NSURLIsRegularFileKey,NSURLIsSymbolicLinkKey,NSURLFileAllocatedSizeKey,NSURLFileSizeKey] options:NSDirectoryEnumerationSkipsHiddenFiles errorHandler:nil];
        for(NSURL *file in files) {
            NSDictionary *v=[file resourceValuesForKeys:@[NSURLIsRegularFileKey,NSURLIsSymbolicLinkKey,NSURLFileAllocatedSizeKey,NSURLFileSizeKey] error:nil];
            if([v[NSURLIsSymbolicLinkKey] boolValue]){[files skipDescendants];continue;}
            if(![v[NSURLIsRegularFileKey] boolValue])continue;
            uint64_t size=[v[NSURLFileAllocatedSizeKey] unsignedLongLongValue]?:[v[NSURLFileSizeKey] unsignedLongLongValue];
            struct stat info={0};BOOL isDisk=disk.length&&!stat(file.fileSystemRepresentation,&info)&&info.st_dev==diskInfo.st_dev&&info.st_ino==diskInfo.st_ino;
            if(isDisk)diskSize+=size;else other+=size;
        }
        dispatch_async(dispatch_get_main_queue(),^{
            self.diskBytes=diskSize;self.otherBytes=other;self.sizesReady=YES;
            // Only General displays these values. Replacing Hardware as this
            // scan finishes needlessly recreates its controls just after opening.
            if(self.window.visible&&[self.section isEqual:@"General"]&&![self.window.firstResponder isKindOfClass:NSTextView.class])[self render];
        });
    });
}
- (NSString *)storageSummary {
    uint64_t capacity=self.machine.diskCapacityBytes;
    NSString *disk=self.machine.configuration[@"Disk"];
    NSNumber *allocated=nil;
    if(disk.length)[[self.machine.URL URLByAppendingPathComponent:disk] getResourceValue:&allocated forKey:NSURLTotalFileAllocatedSizeKey error:nil];
    NSString *size=allocated?[NSByteCountFormatter stringFromByteCount:allocated.longLongValue countStyle:NSByteCountFormatterCountStyleFile]:@"Unavailable";
    return [NSString stringWithFormat:@"Disk capacity: %@\nDisk file space on this Mac: %@",capacity?[NSString stringWithFormat:@"%g GB",capacity/(double)(1ULL<<30)]:@"Not recorded",size];
}
- (NSArray *)toolbarDefaultItemIdentifiers:(NSToolbar *)toolbar {return @[@"General",@"Hardware",@"Sharing",NSToolbarFlexibleSpaceItemIdentifier];}
- (NSArray *)toolbarAllowedItemIdentifiers:(NSToolbar *)toolbar {return [self toolbarDefaultItemIdentifiers:toolbar];}
- (NSArray *)toolbarSelectableItemIdentifiers:(NSToolbar *)toolbar {return @[@"General",@"Hardware",@"Sharing"];}
- (NSToolbarItem *)toolbar:(NSToolbar *)toolbar itemForItemIdentifier:(NSString *)identifier willBeInsertedIntoToolbar:(BOOL)insert {
    NSToolbarItem *item=[[NSToolbarItem alloc] initWithItemIdentifier:identifier];item.label=identifier;
    NSString *symbol=@{@"General":@"gearshape",@"Hardware":@"cpu",@"Sharing":@"folder"}[identifier];
    item.image=[[NSImage imageWithSystemSymbolName:symbol accessibilityDescription:identifier] imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithPointSize:24 weight:NSFontWeightRegular]];item.target=self;item.action=@selector(selectSection:);return item;
}
- (void)selectSection:(NSToolbarItem *)sender {
    [self saveDescription];
    [self.window makeFirstResponder:nil];if(self.invalidField){self.window.toolbar.selectedItemIdentifier=self.section;return;}
    self.section=[sender.itemIdentifier isEqual:@"Options"]?@"Sharing":sender.itemIdentifier;self.window.toolbar.selectedItemIdentifier=self.section;self.hardwareIndex=0;[self render];
}
- (NSTextField *)field:(NSString *)key value:(NSString *)value {
    NSTextField *field=[NSTextField textFieldWithString:value];field.identifier=key;field.delegate=self;
    [field.widthAnchor constraintEqualToConstant:[key isEqual:@"GuestUsername"]?240:([key isEqual:@"Name"]?220:90)].active=YES;
    self.fields[key]=field;return field;
}
- (NSButton *)toggle:(NSString *)key title:(NSString *)title {
    NSButton *button=[NSButton checkboxWithTitle:title target:self action:@selector(saveToggle:)];
    button.identifier=key;id value=self.machine.editableConfiguration[key];
    button.state=value?[value boolValue]:([@[@"SSHEnabled",@"FileSharingEnabled",@"ClipboardEnabled"] containsObject:key]?NSControlStateValueOn:NSControlStateValueOff);
    self.fields[key]=button;return button;
}
- (NSButton *)button:(NSString *)title action:(SEL)action {
    return [NSButton buttonWithTitle:title target:self action:action];
}
- (NSView *)general {
    NSString *system=@{@"mojave":@"macOS Mojave",@"catalina":@"macOS Catalina",@"bigsur":@"macOS Big Sur"}[self.machine.guest]?:@"macOS";
    NSTextField *name=[self field:@"Name" value:self.machine.pendingName?:self.machine.name];
    NSImageView *mark=[NSImageView imageViewWithImage:[NSImage imageNamed:[self.machine.guest stringByAppendingString:@"ProductIcon"]]?:NSApp.applicationIconImage];
    mark.imageScaling=NSImageScaleProportionallyUpOrDown;
    [mark.widthAnchor constraintEqualToConstant:24].active=YES;[mark.heightAnchor constraintEqualToConstant:24].active=YES;
    NSTextField *os=[NSTextField labelWithString:system];
    NSStackView *identity=[NSStackView stackViewWithViews:@[mark,os]];identity.spacing=8;
    NSScrollView *description=[NSScrollView new];description.borderType=NSBezelBorder;description.hasVerticalScroller=YES;description.autohidesScrollers=YES;
    self.descriptionEditor=[[NSTextView alloc] initWithFrame:NSMakeRect(0,0,450,54)];self.descriptionEditor.font=[NSFont systemFontOfSize:13];self.descriptionEditor.richText=NO;
    self.descriptionEditor.textContainerInset=NSMakeSize(4,4);self.descriptionEditor.delegate=self;self.descriptionEditor.string=self.machine.editableConfiguration[@"Description"]?:@"";
    description.documentView=self.descriptionEditor;self.descriptionEditor.autoresizingMask=NSViewWidthSizable;
    [description.widthAnchor constraintEqualToConstant:472].active=YES;[description.heightAnchor constraintEqualToConstant:58].active=YES;
    NSTextField *descriptionLabel=[NSTextField labelWithString:@"Description:"];
    NSGridView *grid=[NSGridView gridViewWithViews:@[@[NSGridCell.emptyContentView,identity],@[[NSTextField labelWithString:@"Name:"],name],@[descriptionLabel,description]]];
    grid.columnSpacing=8;grid.rowSpacing=12;[grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;[grid columnAtIndex:0].width=80;
    [grid columnAtIndex:1].xPlacement=NSGridCellPlacementLeading;[grid rowAtIndex:2].yPlacement=NSGridCellPlacementTop;
    NSGridCell *caption=[grid cellAtColumnIndex:0 rowIndex:2];caption.yPlacement=NSGridCellPlacementNone;
    caption.customPlacementConstraints=@[[descriptionLabel.topAnchor constraintEqualToAnchor:description.topAnchor]];
    NSBox *divider=[NSBox new];divider.boxType=NSBoxSeparator;
    NSView *storage=[self storageOverview];
    NSButton *reveal=[self button:@"Show in Finder" action:@selector(reveal:)];
    NSStackView *location=[NSStackView stackViewWithViews:@[VIMText(@"Location:",NO),reveal]];location.spacing=12;
    NSStackView *column=VIMColumn(@[grid,divider,storage,location]);column.spacing=16;
    [divider.widthAnchor constraintEqualToAnchor:column.widthAnchor].active=YES;
    [storage.widthAnchor constraintEqualToAnchor:column.widthAnchor].active=YES;
    return column;
}
- (NSView *)storageOverview {
    NSString *(^bytes)(uint64_t)=^NSString *(uint64_t value){return self.sizesReady?[NSByteCountFormatter stringFromByteCount:value countStyle:NSByteCountFormatterCountStyleFile]:@"Calculating…";};
    NSView *bar=[NSView new];bar.wantsLayer=YES;bar.layer.cornerRadius=4;bar.layer.masksToBounds=YES;
    NSView *disk=[NSView new],*other=[NSView new];
    NSColor *diskColor=[NSColor colorWithSRGBRed:.29 green:.67 blue:.91 alpha:1],*otherColor=[NSColor colorWithSRGBRed:.73 green:.48 blue:.83 alpha:1];
    disk.wantsLayer=other.wantsLayer=YES;disk.layer.backgroundColor=diskColor.CGColor;other.layer.backgroundColor=otherColor.CGColor;
    for(NSView *part in @[disk,other]){part.translatesAutoresizingMaskIntoConstraints=NO;[bar addSubview:part];}
    CGFloat share=self.diskBytes+self.otherBytes?MAX(.01,MIN(.99,(double)self.diskBytes/(self.diskBytes+self.otherBytes))):.98;
    [NSLayoutConstraint activateConstraints:@[[bar.heightAnchor constraintEqualToConstant:20],[disk.leadingAnchor constraintEqualToAnchor:bar.leadingAnchor],[disk.widthAnchor constraintEqualToAnchor:bar.widthAnchor multiplier:share],
        [disk.topAnchor constraintEqualToAnchor:bar.topAnchor],[disk.bottomAnchor constraintEqualToAnchor:bar.bottomAnchor],[other.leadingAnchor constraintEqualToAnchor:disk.trailingAnchor],
        [other.trailingAnchor constraintEqualToAnchor:bar.trailingAnchor],[other.topAnchor constraintEqualToAnchor:bar.topAnchor],[other.bottomAnchor constraintEqualToAnchor:bar.bottomAnchor]]];
    NSMutableArray *legends=[NSMutableArray new];
    NSArray *colors=@[diskColor,otherColor],*titles=@[@"Hard Disks",@"Miscellaneous"],*values=@[bytes(self.diskBytes),bytes(self.otherBytes)];
    for(NSUInteger i=0;i<2;i++) {
        NSView *swatch=[NSView new];swatch.wantsLayer=YES;swatch.layer.backgroundColor=[colors[i] CGColor];[swatch.widthAnchor constraintEqualToConstant:11].active=YES;[swatch.heightAnchor constraintEqualToConstant:11].active=YES;
        NSStackView *text=VIMColumn(@[VIMText(titles[i],YES),VIMText(values[i],NO)]);text.spacing=1;
        NSStackView *legend=[NSStackView stackViewWithViews:@[swatch,text]];legend.alignment=NSLayoutAttributeTop;legend.spacing=8;[legends addObject:legend];
    }
    NSStackView *legend=[NSStackView stackViewWithViews:legends];legend.spacing=20;legend.distribution=NSStackViewDistributionFillEqually;
    NSStackView *right=VIMColumn(@[bar,legend]);right.spacing=12;[bar.widthAnchor constraintEqualToAnchor:right.widthAnchor].active=YES;
    [legend.widthAnchor constraintEqualToAnchor:right.widthAnchor].active=YES;
    NSTextField *total=VIMText([NSString stringWithFormat:@"Total size\n%@",bytes(self.diskBytes+self.otherBytes)],NO);total.alignment=NSTextAlignmentRight;[total.widthAnchor constraintEqualToConstant:80].active=YES;
    NSStackView *row=[NSStackView stackViewWithViews:@[total,right]];row.alignment=NSLayoutAttributeTop;row.spacing=8;[right.widthAnchor constraintEqualToAnchor:row.widthAnchor constant:-88].active=YES;return row;
}
- (NSView *)hardware {
    NSDictionary *c=self.machine.editableConfiguration;
    if(self.hardwareIndex==0) {
        NSPopUpButton *cpu=[NSPopUpButton new];cpu.identifier=@"CPUCount";cpu.target=self;cpu.action=@selector(processorsChanged:);
        for(NSInteger i=1;i<=NSProcessInfo.processInfo.processorCount;i++)[cpu addItemWithTitle:[NSString stringWithFormat:@"%ld%@",(long)i,i==MIN(4,NSProcessInfo.processInfo.processorCount)?@" (Recommended)":@""]];
        [cpu selectItemAtIndex:MAX(0,[c[@"CPUCount"] integerValue]-1)];[cpu.widthAnchor constraintEqualToConstant:170].active=YES;self.fields[@"CPUCount"]=cpu;
        NSTextField *memory=[self field:@"MemoryMiB" value:[c[@"MemoryMiB"] stringValue]];self.memoryValue=memory;
        NSStepper *stepper=[NSStepper new];stepper.minValue=1024;stepper.maxValue=NSProcessInfo.processInfo.physicalMemory/(1024*1024);stepper.increment=1024;stepper.doubleValue=memory.doubleValue;stepper.target=self;stepper.action=@selector(memoryStepped:);
        NSStackView *memoryRow=[NSStackView stackViewWithViews:@[memory,[NSTextField labelWithString:@"MB"],stepper]];memoryRow.spacing=4;
        double maxGiB=MAX(1,NSProcessInfo.processInfo.physicalMemory/(1024*1024*1024));
        self.memorySlider=[NSSlider sliderWithValue:memory.doubleValue/1024 minValue:1 maxValue:maxGiB target:self action:@selector(memorySlid:)];
        self.memorySlider.continuous=NO;self.memorySlider.numberOfTickMarks=(NSInteger)maxGiB;self.memorySlider.allowsTickMarkValuesOnly=YES;self.memorySlider.accessibilityLabel=@"Memory allocation in gigabytes";
        [self.memorySlider.widthAnchor constraintEqualToConstant:280].active=YES;
        NSTextField *low=VIMText(@"1 GB",NO),*mid=VIMText([NSString stringWithFormat:@"%.0f GB",round((maxGiB+1)/2)],NO),*high=VIMText([NSString stringWithFormat:@"%.0f GB",maxGiB],NO);mid.alignment=NSTextAlignmentCenter;high.alignment=NSTextAlignmentRight;
        NSStackView *ticks=[NSStackView stackViewWithViews:@[low,mid,high]];ticks.distribution=NSStackViewDistributionFillEqually;[ticks.widthAnchor constraintEqualToConstant:280].active=YES;
        NSStackView *memoryColumn=VIMColumn(@[memoryRow,self.memorySlider,ticks]);memoryColumn.spacing=10;
        NSGridView *grid=[NSGridView gridViewWithViews:@[
            @[[NSTextField labelWithString:@"Processors:"],cpu],@[[NSTextField labelWithString:@"Memory:"],memoryColumn]]];
        grid.columnSpacing=12;grid.rowSpacing=22;[grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;[grid columnAtIndex:1].xPlacement=NSGridCellPlacementLeading;[grid rowAtIndex:1].yPlacement=NSGridCellPlacementTop;
        return VIMColumn(@[grid,
            VIMText([NSString stringWithFormat:@"This Mac has %lu logical processors and %llu GB of memory. Leave resources available for other apps.",(unsigned long)NSProcessInfo.processInfo.processorCount,NSProcessInfo.processInfo.physicalMemory/(1024ULL*1024*1024)],NO),
            VIMText(@"Changes take effect the next time this virtual Mac starts.",NO)]);
    }
    if(self.hardwareIndex==1) {
        NSMutableArray *views=[NSMutableArray new];
        NSArray *modes=@[@"space",@"retina",@"scaled"],*titles=@[@"More Space",@"Best for Retina display",@"Scaled"];
        NSArray *descriptions=@[@"Graphics at full Retina resolution. Text and interface are small.",@"",@"Graphics scaled to full size."];
        NSButton *firstOption=nil;
        for(NSUInteger i=0;i<modes.count;i++) {
            NSButton *option=[NSButton radioButtonWithTitle:titles[i] target:self action:@selector(retinaChanged:)];option.tag=i;
            if(!firstOption)firstOption=option;
            option.state=[VIMDisplayMode(c) isEqual:modes[i]];
            if(![descriptions[i] length]){[views addObject:option];continue;}
            NSTextField *description=VIMText(descriptions[i],NO);
            NSView *pair=[NSView new];
            for(NSView *view in @[option,description]){view.translatesAutoresizingMaskIntoConstraints=NO;[pair addSubview:view];}
            [NSLayoutConstraint activateConstraints:@[[option.topAnchor constraintEqualToAnchor:pair.topAnchor],
                [option.leadingAnchor constraintEqualToAnchor:pair.leadingAnchor],
                [description.leadingAnchor constraintEqualToAnchor:pair.leadingAnchor constant:20],
                [description.topAnchor constraintEqualToAnchor:option.bottomAnchor constant:4],
                [description.widthAnchor constraintEqualToConstant:285],
                [description.trailingAnchor constraintEqualToAnchor:pair.trailingAnchor],
                [description.bottomAnchor constraintEqualToAnchor:pair.bottomAnchor]]];[views addObject:pair];
        }
        NSStackView *options=VIMColumn(views);options.spacing=18;
        NSView *form=[NSView new];NSTextField *label=[NSTextField labelWithString:@"Resolution:"];
        for(NSView *view in @[label,options]){view.translatesAutoresizingMaskIntoConstraints=NO;[form addSubview:view];}
        [NSLayoutConstraint activateConstraints:@[[label.leadingAnchor constraintEqualToAnchor:form.leadingAnchor],
            [label.firstBaselineAnchor constraintEqualToAnchor:firstOption.firstBaselineAnchor],
            [options.leadingAnchor constraintEqualToAnchor:label.trailingAnchor constant:12],
            [options.trailingAnchor constraintEqualToAnchor:form.trailingAnchor],
            [options.topAnchor constraintEqualToAnchor:form.topAnchor],
            [options.bottomAnchor constraintEqualToAnchor:form.bottomAnchor]]];
        return VIMColumn(@[form,VIMText(@"Changes take effect the next time this Mac starts.",NO)]);
    }
    if(self.hardwareIndex==4) {
        NSPopUpButton *output=[self audioPopup:NO],*input=[self audioPopup:YES];
        NSGridView *grid=[NSGridView gridViewWithViews:@[@[[NSTextField labelWithString:@"Sound Output:"],output],@[[NSTextField labelWithString:@"Microphone:"],input]]];grid.rowSpacing=20;grid.columnSpacing=12;[grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;
        NSButton *hostAccess=[self button:@"This Mac’s Access…" action:@selector(microphone:)];
        hostAccess.toolTip=@"Allow Virtual Intel Mac to use this Mac’s microphone.";
        NSStackView *access=[NSStackView stackViewWithViews:@[hostAccess,
            [self guestSettingsButton:@"Virtual Mac’s Access…" pane:@"microphone"]]];access.spacing=8;
        return VIMColumn(@[grid,access,
            VIMText(@"Inside the virtual Mac, also allow access in System Preferences → Security & Privacy → Privacy → Microphone.",NO),
            VIMText(@"Changes take effect the next time the virtual Mac starts.",NO)]);
    }
    if(self.hardwareIndex==3) {
        NSPopUpButton *source=[NSPopUpButton new];source.target=self;source.action=@selector(networkChanged:);
        [source addItemsWithTitles:@[@"Disconnected",@"Shared Network (Recommended)",@"Host-Only Network",@"Bridged Network"]];
        NSArray *modes=@[@"shared",@"host",@"bridged"];NSString *mode=VIMNetworkMode(c);
        [source selectItemAtIndex:c[@"NetworkConnected"]&&![c[@"NetworkConnected"] boolValue]?0:1+[modes indexOfObject:mode]];
        [source.widthAnchor constraintEqualToConstant:270].active=YES;
        NSMutableArray *rows=[NSMutableArray arrayWithObject:@[[NSTextField labelWithString:@"Source:"],source]];
        if([mode isEqual:@"bridged"]) {
            NSPopUpButton *bridge=[NSPopUpButton new];bridge.target=self;bridge.action=@selector(bridgeChanged:);
            [bridge addItemWithTitle:@"Automatic"];bridge.lastItem.representedObject=@"";
            for(NSDictionary *interface in VIMNetworkBridgeInterfaces()){[bridge addItemWithTitle:interface[@"Name"]];bridge.lastItem.representedObject=interface[@"Identifier"];}
            for(NSMenuItem *item in bridge.itemArray)if([item.representedObject isEqual:c[@"BridgeInterface"]?:@""])[bridge selectItem:item];
            [rows addObject:@[[NSTextField labelWithString:@"Interface:"],bridge]];
        }
        [rows addObject:@[NSGridCell.emptyContentView,[self button:@"Advanced…" action:@selector(networkAdvanced:)]]];
        NSGridView *grid=[NSGridView gridViewWithViews:rows];grid.columnSpacing=12;grid.rowSpacing=20;
        [grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;[grid columnAtIndex:1].xPlacement=NSGridCellPlacementLeading;
        NSString *explanation=[mode isEqual:@"host"]?@"Connects to this Mac and other host-only virtual Macs, without access to the internet.":([mode isEqual:@"bridged"]?@"Connects directly to your local network. Other devices can reach services you enable inside the virtual Mac.":@"The virtual Mac shares this Mac’s internet connection.");
        return VIMColumn(@[grid,VIMText(explanation,NO)]);
    }
    if(self.hardwareIndex==2) {
        NSPopUpButton *mode=[NSPopUpButton new];[mode addItemWithTitle:@"Automatic"];
        [mode.widthAnchor constraintEqualToConstant:220].active=YES;
        NSGridView *grid=[NSGridView gridViewWithViews:@[@[[NSTextField labelWithString:@"Mouse & Keyboard:"],mode]]];grid.columnSpacing=12;
        return VIMColumn(@[grid,VIMText(@"Click inside this Mac to use its mouse and Mac keyboard. Press Control–Option to release them.",NO)]);
    }
    if(self.hardwareIndex==6) {
        NSString *path=[VIMSession connectedOpticalMediaForMachine:self.machine];
        NSPopUpButton *source=[NSPopUpButton new];source.target=self;source.action=@selector(opticalChanged:);
        [source addItemWithTitle:path.length?path.lastPathComponent:@"No Disc"];source.selectedItem.tag=0;
        source.selectedItem.image=[NSImage imageWithSystemSymbolName:@"opticaldisc" accessibilityDescription:nil];
        [source.menu addItem:NSMenuItem.separatorItem];
        if(path.length){[source addItemWithTitle:@"Disconnect"];source.lastItem.tag=2;}
        else {[source addItemWithTitle:@"Choose Image…"];source.lastItem.tag=1;}
        [source.widthAnchor constraintEqualToConstant:280].active=YES;
        NSGridView *grid=[NSGridView gridViewWithViews:@[@[[NSTextField labelWithString:@"Source:"],source]]];grid.columnSpacing=12;[grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;
        return VIMColumn(@[grid,VIMText(@"Choose an ISO, CDR or supported unencrypted DMG. Images are read-only. Disconnect the current image before choosing another.",NO)]);
    }
    NSString *file=[self.machine.configuration[@"Disk"] lastPathComponent]?:@"System.qcow2";
    NSPopUpButton *source=[NSPopUpButton new];[source addItemWithTitle:file];[source.widthAnchor constraintEqualToConstant:250].active=YES;
    NSGridView *grid=[NSGridView gridViewWithViews:@[@[[NSTextField labelWithString:@"Source:"],source]]];grid.columnSpacing=12;
    return VIMColumn(@[grid,VIMText([self storageSummary],NO),[self button:@"Show in Finder" action:@selector(reveal:)],VIMText(@"Disk capacity is set when the Mac is created. The disk grows on demand up to that capacity.",NO)]);
}
- (NSView *)sharingContentForHost:(BOOL)host {
    BOOL files=self.hardwareIndex==0;
    NSDictionary *network=[VIMSession networkDetailsForMachine:self.machine];BOOL shared=[network[@"Mode"] isEqual:@"shared"];
    NSString *hostSettings=NSProcessInfo.processInfo.operatingSystemVersion.majorVersion>=13?@"System Settings → General → Sharing":@"System Preferences → Sharing";
    if(host) {
        NSString *username=[NSUserName() stringByReplacingOccurrencesOfString:@"'" withString:@"'\\''"];
        NSString *hostIP=network[@"HostAddress"];
        NSString *address=hostIP.length?(files?[@"smb://" stringByAppendingString:hostIP]:[NSString stringWithFormat:@"ssh -l '%@' %@",username,hostIP]):@"";
        NSButton *copy=[self button:files?@"Copy Address":@"Copy Command" action:@selector(copyConnection:)];copy.identifier=address;
        copy.enabled=address.length>0;
        NSURLComponents *connection=[NSURLComponents new];connection.scheme=files?@"smb":@"ssh";connection.host=hostIP;if(!files)connection.user=NSUserName();
        NSButton *open=[self button:files?@"Open in Finder":@"Open in Terminal" action:@selector(openGuestConnection:)];open.identifier=address.length?connection.URL.absoluteString:@"";open.enabled=[network[@"Connected"] boolValue]&&address.length;
        NSStackView *actions=[NSStackView stackViewWithViews:@[open,copy]];actions.spacing=12;
        return VIMColumn(@[VIMText(files?@"Open this Mac’s files inside the virtual Mac":@"Connect to this Mac from the virtual Mac",YES),
            VIMText([NSString stringWithFormat:@"1. On this Mac, open %@ and enable %@.",hostSettings,files?@"File Sharing":@"Remote Login"],NO),
            [self button:@"Open Sharing Settings" action:@selector(openSharing:)],
            VIMText(files?@"2. In the virtual Mac’s Finder, choose Go → Connect to Server and enter:":@"2. In the virtual Mac’s Terminal, run:",NO),
            VIMText(address.length?address:@"Start the virtual Mac and connect its network to discover this Mac’s address.",NO),actions,VIMText(@"Use this Mac’s account and password, not the virtual Mac’s.",NO)]);
    }
    NSString *key=files?@"FileSharingEnabled":@"SSHEnabled";
    NSString *portKey=files?@"FileSharingPort":@"SSHPort";
    NSDictionary *ports=VIMSuggestedForwardPorts(self.machine.URL.URLByDeletingLastPathComponent,self.machine.URL);
    NSNumber *port=self.machine.editableConfiguration[portKey]?:ports[portKey];
    NSButton *enabled=[self toggle:key title:files?@"Allow connections to this virtual Mac’s shared files":@"Allow SSH connections to this virtual Mac"];
    NSString *address=files?[VIMSMBService URLForMachine:self.machine].absoluteString:
        [NSString stringWithFormat:@"ssh -p %@ -l '%@' 127.0.0.1",port,self.guestUsername];
    NSString *guestIP=[network[@"GuestAddresses"] firstObject];
    if(!shared)address=guestIP.length?(files?[@"smb://" stringByAppendingString:guestIP]:[NSString stringWithFormat:@"ssh -l '%@' %@",self.guestUsername,guestIP]):@"";
    NSButton *copy=[self button:files?@"Copy Address":@"Copy Command" action:@selector(copyConnection:)];copy.identifier=address;
    copy.enabled=address.length&&(files||self.guestUsername.length>0);
    NSURLComponents *connection=[NSURLComponents new];connection.scheme=files?@"smb":@"ssh";connection.host=@"127.0.0.1";connection.port=port;if(!files)connection.user=self.guestUsername;
    if(!shared){connection.host=guestIP;connection.port=nil;}
    NSButton *open=[self button:files?@"Open in Finder":@"Open in Terminal" action:@selector(openConnection:)];open.identifier=files?address:(address.length?connection.URL.absoluteString:@"");open.enabled=[network[@"Connected"] boolValue]&&(!shared||enabled.state==NSControlStateValueOn)&&address.length&&(files||self.guestUsername.length>0);
    NSStackView *actions=[NSStackView stackViewWithViews:@[open,copy]];actions.spacing=12;
    NSMutableArray *views=[NSMutableArray arrayWithArray:@[VIMText(files?@"Open the virtual Mac’s files on this Mac":@"Connect to the virtual Mac with SSH",YES),
        VIMText(files?@"1. In the virtual Mac, open System Preferences → Sharing and turn on File Sharing. In Options, enable SMB for your account.":@"1. In the virtual Mac, open System Preferences → Sharing and turn on Remote Login.",NO)]];
    if(files) {
        [views addObject:[self guestSettingsButton:@"Open Sharing in Virtual Mac…" pane:@"sharing"]];
    }
    if(!files) {
        NSButton *automatic=[self button:@"Set Up Remote Login…" action:@selector(enableGuestRemoteLogin:)];
        automatic.enabled=[VIMSession isRunningMachine:self.machine];
        automatic.toolTip=@"Uses the built-in guest tools. Start the virtual Mac first.";
        [views addObject:automatic];
        NSTextField *username=[self field:@"GuestUsername" value:self.guestUsername];username.placeholderString=@"Guest account short name";
        username.accessibilityLabel=@"Guest account short name";[views addObject:username];
    }
    if(shared)[actions addArrangedSubview:[self button:@"Advanced…" action:@selector(toggleAdvancedSharing:)]];
    [views addObjectsFromArray:@[VIMText(files?@"2. On this Mac, connect using the virtual Mac’s account and password:":@"2. Run this command in Terminal on this Mac:",NO),
        VIMText(!files&&!self.guestUsername.length?@"Enter your guest account’s short name above.":(address.length?address:@"Start the virtual Mac and wait for its network address."),NO),actions]];
    if(shared&&enabled.state!=NSControlStateValueOn)[views addObject:VIMText(@"Local connections are turned off. Enable them in Advanced, then restart this Mac.",NO)];
    if(!shared)[views addObject:VIMText(@"Connect directly using the address above. Service access is controlled by Sharing and Firewall settings inside the virtual Mac; local port forwarding is not used.",NO)];
    return VIMColumn(views);
}
- (NSView *)sharing {
    if(self.hardwareIndex==2) {
        NSView *pane=[NSView new];NSButton *enabled=[self toggle:@"ClipboardEnabled" title:@"Share copied text with this Mac"];
        enabled.translatesAutoresizingMaskIntoConstraints=NO;[pane addSubview:enabled];
        [NSLayoutConstraint activateConstraints:@[[enabled.leadingAnchor constraintEqualToAnchor:pane.leadingAnchor constant:24],
            [enabled.centerYAnchor constraintEqualToAnchor:pane.centerYAnchor],
            [enabled.trailingAnchor constraintLessThanOrEqualToAnchor:pane.trailingAnchor constant:-20]]];
        NSTabView *tabs=[NSTabView new];tabs.tabViewType=NSTopTabsBezelBorder;
        NSTabViewItem *item=[[NSTabViewItem alloc] initWithIdentifier:@"guest"];
        item.label=@"Virtual Mac";item.view=pane;[tabs addTabViewItem:item];return tabs;
    }
    NSTabView *tabs=[NSTabView new];tabs.identifier=@"SharingDirections";tabs.tabViewType=NSTopTabsBezelBorder;
    tabs.accessibilityLabel=@"Which Mac to connect to";
    for(NSNumber *host in @[@YES,@NO]) {
        NSTabViewItem *item=[[NSTabViewItem alloc] initWithIdentifier:host.boolValue?@"host":@"guest"];
        item.label=host.boolValue?@"This Mac":@"Virtual Mac";
        NSView *pane=[self sharingContentForHost:host.boolValue];VIMAlignFormBaselines(pane);
        item.view=VIMSharingScrollPane(pane);[tabs addTabViewItem:item];
    }
    [tabs selectTabViewItemAtIndex:self.hostFiles?0:1];tabs.delegate=self;return tabs;
}
- (BOOL)tabView:(NSTabView *)tabView shouldSelectTabViewItem:(NSTabViewItem *)item {
    [self.window makeFirstResponder:nil];return !self.invalidField;
}
- (void)tabView:(NSTabView *)tabView didSelectTabViewItem:(NSTabViewItem *)item {
    self.hostFiles=[item.identifier isEqual:@"host"];
}
- (void)presentEditor:(VIMConfigurationSheet *)sheet {
    if(self.window.attachedSheet)return;
    self.editorSheet=sheet;__weak typeof(self) weakSelf=self;
    [self.window beginSheet:sheet.window completionHandler:^(NSModalResponse response){weakSelf.editorSheet=nil;}];
}
- (void)toggleAdvancedSharing:(id)sender {
    [self.window makeFirstResponder:nil];if(self.invalidField)return;
    BOOL files=self.hardwareIndex==0;NSString *key=files?@"FileSharingEnabled":@"SSHEnabled",*portKey=files?@"FileSharingPort":@"SSHPort";
    NSDictionary *c=self.machine.editableConfiguration;
    NSButton *allowed=[NSButton checkboxWithTitle:files?@"Allow connections to shared files":@"Allow SSH connections" target:nil action:nil];allowed.state=c[key]?[c[key] boolValue]:YES;
    NSNumber *port=c[portKey]?:VIMSuggestedForwardPorts(self.machine.URL.URLByDeletingLastPathComponent,self.machine.URL)[portKey];
    NSTextField *field=[NSTextField textFieldWithString:port.stringValue];[field.widthAnchor constraintEqualToConstant:100].active=YES;
    NSGridView *grid=[NSGridView gridViewWithViews:@[@[[NSTextField labelWithString:@"Local port:"],field]]];grid.columnSpacing=12;
    grid.rowAlignment=NSGridRowAlignmentFirstBaseline;
    [grid columnAtIndex:0].width=80;[grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;[grid columnAtIndex:1].xPlacement=NSGridCellPlacementLeading;
    [grid.widthAnchor constraintEqualToConstant:240].active=YES;
    NSTextField *explanation=VIMText(@"Accessible only from this Mac. Changes take effect at the next start.",NO);
    [explanation.widthAnchor constraintEqualToConstant:360].active=YES;
    VIMConfigurationSheet *sheet=[[VIMConfigurationSheet alloc] initWithTitle:files?@"File Sharing — Advanced":@"Remote Login — Advanced"
        content:VIMColumn(@[allowed,grid,explanation])];
    __weak typeof(self) weakSelf=self;
    sheet.save=^BOOL(NSWindow *window){
        NSScanner *scanner=[NSScanner scannerWithString:field.stringValue];long long value=0;NSError *error=nil;
        if(![scanner scanLongLong:&value]||!scanner.isAtEnd||value<1024||value>65535)
            error=[NSError errorWithDomain:@"VirtualIntelMac.Settings" code:1 userInfo:@{NSLocalizedDescriptionKey:@"Enter a local port from 1024 to 65535."}];
        if(!error)[weakSelf.machine saveSettings:@{key:@(allowed.state==NSControlStateValueOn),portKey:@(value)} error:&error];
        if(error){VIMPresentError(error,window);return NO;}
        if(weakSelf.completion)weakSelf.completion();[weakSelf render];return YES;
    };[self presentEditor:sheet];
}
- (NSPopUpButton *)audioPopup:(BOOL)input {
    NSPopUpButton *popup=[NSPopUpButton new];popup.identifier=input?@"AudioInputDevice":@"AudioOutputDevice";
    popup.target=self;popup.action=@selector(audioChanged:);[popup.widthAnchor constraintEqualToConstant:240].active=YES;
    [popup addItemWithTitle:@"Default"];popup.lastItem.representedObject=@"default";
    NSString *selected=VIMAudioSelection(self.machine.editableConfiguration,input);
    for(NSDictionary *device in VIMHostAudioDevices(input)) {
        [popup addItemWithTitle:device[@"Name"]];popup.lastItem.representedObject=device[@"UID"];
    }
    NSMenuItem *choice=nil;for(NSMenuItem *item in popup.itemArray)if([item.representedObject isEqual:selected])choice=item;
    if(!choice&&![selected isEqual:@"disabled"]){[popup addItemWithTitle:@"Selected Device (Disconnected)"];choice=popup.lastItem;choice.representedObject=selected;}
    [popup.menu addItem:NSMenuItem.separatorItem];
    [popup addItemWithTitle:@"Disconnected"];popup.lastItem.representedObject=@"disabled";
    if([selected isEqual:@"disabled"])choice=popup.lastItem;
    [popup selectItem:choice];return popup;
}
- (void)audioChanged:(NSPopUpButton *)sender {
    if([self commitSettings:@{@"AudioEnabled":@YES,sender.identifier:sender.selectedItem.representedObject}])[self render];
}
- (void)networkChanged:(NSPopUpButton *)sender {
    BOOL connected=sender.indexOfSelectedItem!=0;
    NSString *mode=connected?@[@"shared",@"host",@"bridged"][sender.indexOfSelectedItem-1]:VIMNetworkMode(self.machine.editableConfiguration);
    [VIMSession changeNetworkMode:mode connected:connected machine:self.machine window:self.window completion:^(NSError *error){
        if(error)VIMPresentError(error,self.window);[self render];
    }];
}
- (void)bridgeChanged:(NSPopUpButton *)sender {
    if([self commitSettings:@{@"BridgeInterface":sender.selectedItem.representedObject?:@""}]) {
        if([VIMSession isRunningMachine:self.machine]) {
            NSAlert *alert=[NSAlert new];alert.messageText=@"Restart to change the bridge interface";alert.informativeText=@"Your selection is saved for the next start.";[alert addButtonWithTitle:@"OK"];VIMPresentAlert(alert,self.window,nil);
        }[self render];
    }
}
- (void)generateMAC:(NSButton *)sender {
    uint8_t bytes[6];arc4random_buf(bytes,sizeof(bytes));bytes[0]=(bytes[0]|2)&0xfe;
    NSTextField *field=(id)sender.cell.representedObject;
    field.stringValue=[NSString stringWithFormat:@"%02X:%02X:%02X:%02X:%02X:%02X",bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5]];
}
- (void)networkAdvanced:(id)sender {
    NSTextField *mac=[NSTextField textFieldWithString:VIMNetworkMACAddress(self.machine.editableConfiguration)];[mac.widthAnchor constraintEqualToConstant:175].active=YES;
    NSButton *generate=[self button:@"Generate" action:@selector(generateMAC:)];generate.cell.representedObject=mac;
    NSStackView *macRow=[NSStackView stackViewWithViews:@[mac,generate]];macRow.spacing=12;
    macRow.alignment=NSLayoutAttributeFirstBaseline;
    NSPopUpButton *type=[NSPopUpButton new];[type addItemWithTitle:@"Intel 82545EM Gigabit Ethernet"];type.toolTip=@"QEMU e1000-82545em · PCI 8086:100F";
    NSGridView *grid=[NSGridView gridViewWithViews:@[@[[NSTextField labelWithString:@"MAC:"],macRow],@[[NSTextField labelWithString:@"Type:"],type]]];grid.rowSpacing=16;grid.columnSpacing=12;[grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;
    // A stack's bounds and a popup's bezel are not text baselines. Align the
    // captions with the actual editable/control text, not their outer frames.
    for(NSInteger row=0;row<2;row++) {
        NSGridCell *caption=[grid cellAtColumnIndex:0 rowIndex:row];
        caption.yPlacement=NSGridCellPlacementNone;
        caption.customPlacementConstraints=@[[caption.contentView.firstBaselineAnchor constraintEqualToAnchor:(row==0?mac:type).firstBaselineAnchor]];
    }
    VIMConfigurationSheet *sheet=[[VIMConfigurationSheet alloc] initWithTitle:@"Network — Advanced" content:VIMColumn(@[grid,VIMText(@"Changes take effect the next time this Mac starts.",NO)])];
    __weak typeof(self) weakSelf=self;
    sheet.save=^BOOL(NSWindow *window){
        NSError *error=nil;
        if(!VIMNetworkMACAddressIsValid(mac.stringValue))error=[NSError errorWithDomain:@"VirtualIntelMac.Settings" code:1 userInfo:@{NSLocalizedDescriptionKey:@"Enter a valid MAC address, such as 52:54:00:12:34:56. Use a unicast address unique to this network."}];
        if(!error)[weakSelf.machine saveSettings:@{@"MACAddress":mac.stringValue.uppercaseString} error:&error];
        if(error){VIMPresentError(error,window);return NO;}if(weakSelf.completion)weakSelf.completion();[weakSelf render];return YES;
    };[self presentEditor:sheet];
}
- (void)opticalChanged:(NSPopUpButton *)sender {
    NSInteger action=sender.selectedItem.tag;[sender selectItemAtIndex:0];
    if(action==1)[self chooseDisc:sender];else if(action==2)[self ejectDisc:sender];
}
- (NSButton *)guestSettingsButton:(NSString *)title pane:(NSString *)pane {
    NSButton *button=[self button:title action:@selector(openGuestSettings:)];button.identifier=pane;
    button.enabled=[VIMSession isRunningMachine:self.machine];
    button.toolTip=@"Uses the built-in guest tools. Start the virtual Mac and sign in first.";return button;
}
- (void)openGuestSettings:(NSButton *)sender {
    sender.enabled=NO;
    [VIMSession openGuestPreferences:sender.identifier machine:self.machine completion:^(NSError *error){
        sender.enabled=[VIMSession isRunningMachine:self.machine];
        if(error){VIMPresentError(error,self.window);return;}
        [VIMSession showMachineWindow:self.machine];
    }];
}
- (void)enableGuestRemoteLogin:(NSButton *)sender {
    sender.enabled=NO;
    [VIMSession enableGuestRemoteLogin:self.machine completion:^(NSError *error){
        sender.enabled=YES;
        if(error){VIMPresentError(error,self.window);return;}
        NSError *saveError=nil;
        if(![self.machine saveSettings:@{@"SSHEnabled":@YES} error:&saveError]){VIMPresentError(saveError,self.window);return;}
        [self render];
        NSAlert *alert=[NSAlert new];alert.messageText=@"Remote Login is on";
        BOOL restart=![self.machine.configuration[@"SSHEnabled"] boolValue]||
            ![self.machine.configuration[@"SSHPort"] isEqual:self.machine.editableConfiguration[@"SSHPort"]];
        alert.informativeText=restart?@"Remote Login is enabled in the virtual Mac. You previously turned off or changed its local connection port; restart the virtual Mac to apply that change, then use the command shown here.":
            @"Remote Login is enabled. Use the command shown here with your virtual Mac’s account and password—there’s no need to restart.";
        [alert addButtonWithTitle:@"OK"];VIMPresentAlert(alert,self.window,nil);
    }];
}
- (void)render {
    // AppKit can finish laying out an outgoing table during a section switch.
    // Retire its callbacks before replacing the body; each table also owns
    // its immutable row model instead of consulting the new section's list.
    self.sidebar.delegate=nil;self.sidebar.dataSource=nil;self.sidebar=nil;
    BOOL hasRestore=[self.section isEqual:@"Hardware"]&&[@[@0,@1,@3,@4] containsObject:@(self.hardwareIndex)];
    CGFloat height=[self.section isEqual:@"General"]?380:([self.section isEqual:@"Hardware"]?390:490);
    CGFloat width=660;
    NSRect frame=self.window.frame;CGFloat top=NSMaxY(frame);[self.window setContentSize:NSMakeSize(width,height)];frame=self.window.frame;frame.origin.y=top-NSHeight(frame);[self.window setFrame:frame display:YES];
    self.fields=[NSMutableDictionary dictionary];self.descriptionEditor=nil;VIMConfigurationDocument *content=[VIMConfigurationDocument new];content.drawsWindowBackground=YES;self.window.contentView=content;
    self.feedback=VIMText(@"",NO);self.feedback.textColor=NSColor.systemRedColor;
    self.feedback.translatesAutoresizingMaskIntoConstraints=NO;[content addSubview:self.feedback];
    CGFloat left=24;
    if(![self.section isEqual:@"General"]) {
        NSScrollView *sidebar=[VIMFixedSidebarScroll new];sidebar.translatesAutoresizingMaskIntoConstraints=NO;
        sidebar.contentView=[VIMFixedSidebarClip new];
        sidebar.borderType=NSLineBorder;sidebar.hasVerticalScroller=NO;sidebar.hasHorizontalScroller=NO;
        sidebar.verticalScrollElasticity=NSScrollElasticityNone;
        sidebar.horizontalScrollElasticity=NSScrollElasticityNone;
        VIMConfigurationSidebar *table=[VIMConfigurationSidebar new];table.categories=self.categories;self.sidebar=table;
        table.headerView=nil;table.style=NSTableViewStylePlain;table.rowHeight=40;table.intercellSpacing=NSZeroSize;table.selectionHighlightStyle=NSTableViewSelectionHighlightStyleRegular;table.backgroundColor=NSColor.textBackgroundColor;
        table.allowsEmptySelection=NO;table.allowsMultipleSelection=NO;
        NSTableColumn *column=[[NSTableColumn alloc] initWithIdentifier:@"category"];column.width=187;[table addTableColumn:column];
        table.columnAutoresizingStyle=NSTableViewLastColumnOnlyAutoresizingStyle;table.accessibilityLabel=[self.section isEqual:@"Hardware"]?@"Hardware categories":@"Sharing categories";
        table.dataSource=self;sidebar.documentView=table;[content addSubview:sidebar];
        [NSLayoutConstraint activateConstraints:@[[sidebar.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:12],
            [sidebar.topAnchor constraintEqualToAnchor:content.topAnchor constant:12],[sidebar.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-12],
            [sidebar.widthAnchor constraintEqualToConstant:189]]];
        // With empty selection disallowed, AppKit may select row zero while
        // attaching the data source. Establish the intended category before
        // observing changes, rather than reentering render during setup.
        [table selectRowIndexes:[NSIndexSet indexSetWithIndex:self.hardwareIndex] byExtendingSelection:NO];table.delegate=self;left=213;
        // Rebuilding the pane must not leave first responder on the discarded
        // table, which makes the newly selected row immediately turn inactive.
        [self.window makeFirstResponder:table];
        if([self.section isEqual:@"Sharing"]) {
            self.feedback.hidden=YES;
            NSView *tabs=[self sharing];tabs.translatesAutoresizingMaskIntoConstraints=NO;[content addSubview:tabs];
            [NSLayoutConstraint activateConstraints:@[[tabs.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:left],
                [tabs.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-12],
                [tabs.topAnchor constraintEqualToAnchor:content.topAnchor constant:16],
                [tabs.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-12]]];
            [content layoutSubtreeIfNeeded];
            return;
        }
        NSBox *box=[NSBox new];box.boxType=NSBoxCustom;box.borderWidth=.5;box.borderColor=NSColor.separatorColor;box.cornerRadius=4;
        box.fillColor=NSColor.controlBackgroundColor;
        box.translatesAutoresizingMaskIntoConstraints=NO;[content addSubview:box positioned:NSWindowBelow relativeTo:nil];
        [NSLayoutConstraint activateConstraints:@[[box.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:left],[box.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-12],[box.topAnchor constraintEqualToAnchor:content.topAnchor constant:12],[box.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-12]]];
        left+=20;
    }
    NSScrollView *scroll=[NSScrollView new];scroll.hasVerticalScroller=![self.section isEqual:@"General"];scroll.autohidesScrollers=YES;
    scroll.identifier=@"ConfigurationDetail";
    scroll.verticalScrollElasticity=NSScrollElasticityNone;scroll.horizontalScrollElasticity=NSScrollElasticityNone;
    scroll.drawsBackground=NO;scroll.translatesAutoresizingMaskIntoConstraints=NO;[content addSubview:scroll];
    NSView *document=[VIMConfigurationDocument new];document.translatesAutoresizingMaskIntoConstraints=NO;scroll.documentView=document;
    NSView *pane=[self.section isEqual:@"General"]?[self general]:([self.section isEqual:@"Hardware"]?[self hardware]:[self sharing]);
    VIMAlignFormBaselines(pane);
    pane.translatesAutoresizingMaskIntoConstraints=NO;[document addSubview:pane];
    [NSLayoutConstraint activateConstraints:@[[scroll.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:left],
        [scroll.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-24],
        [scroll.topAnchor constraintEqualToAnchor:content.topAnchor constant:24],
        [scroll.bottomAnchor constraintEqualToAnchor:self.feedback.topAnchor constant:hasRestore?-46:-12],
        [self.feedback.leadingAnchor constraintEqualToAnchor:scroll.leadingAnchor],
        [self.feedback.trailingAnchor constraintEqualToAnchor:scroll.trailingAnchor],
        [self.feedback.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-16],
        [document.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor],
        [pane.leadingAnchor constraintEqualToAnchor:document.leadingAnchor],
        [pane.trailingAnchor constraintEqualToAnchor:document.trailingAnchor],
        [pane.topAnchor constraintEqualToAnchor:document.topAnchor],
        [pane.bottomAnchor constraintEqualToAnchor:document.bottomAnchor]]];
    if(hasRestore) {
        NSButton *restore=[self button:@"Restore Defaults" action:@selector(restoreDefaults:)];restore.translatesAutoresizingMaskIntoConstraints=NO;[content addSubview:restore];
        [NSLayoutConstraint activateConstraints:@[[restore.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-32],[restore.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-30]]];
    }
    if([self.section isEqual:@"General"]) {
        [content layoutSubtreeIfNeeded];
        CGFloat fitting=ceil(pane.fittingSize.height)+24+12+ceil(self.feedback.fittingSize.height)+16;
        CGFloat top=NSMaxY(self.window.frame);
        [self.window setContentSize:NSMakeSize(width,fitting)];
        NSRect fitted=self.window.frame;fitted.origin.y=top-NSHeight(fitted);
        [self.window setFrame:fitted display:YES];
    }
    // Resolve the scroll view, grid and fixed-width controls before this pane
    // can be presented. Otherwise the processor popup is first drawn at its
    // intrinsic menu width and shrinks on the next AppKit layout pass.
    [content layoutSubtreeIfNeeded];
}
- (NSArray *)categories {return [self.section isEqual:@"Hardware"]?@[@"CPU & Memory",@"Graphics",@"Mouse & Keyboard",@"Network",@"Sound",@"Hard Disk",@"CD/DVD"]:@[@"File Sharing",@"Remote Login",@"Clipboard"];}
- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {return [(VIMConfigurationSidebar *)tableView categories].count;}
- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)column row:(NSInteger)row {
    NSArray *categories=[(VIMConfigurationSidebar *)tableView categories];
    if(row<0||row>=(NSInteger)categories.count)return nil;
    NSTableCellView *cell=[NSTableCellView new];NSString *title=categories[row];
    NSString *symbol=@{@"CPU & Memory":@"cpu",@"Graphics":@"display",@"Sound":@"speaker.wave.2",@"Network":@"network",@"Hard Disk":@"internaldrive",@"Mouse & Keyboard":@"keyboard",@"File Sharing":@"folder",@"Remote Login":@"terminal",@"CD/DVD":@"opticaldisc",@"Clipboard":@"doc.on.clipboard"}[title];
    NSColor *color=@{@"CPU & Memory":NSColor.systemBlueColor,@"Graphics":NSColor.systemIndigoColor,
        @"Sound":NSColor.systemPinkColor,@"Network":NSColor.systemBlueColor,
        @"Hard Disk":NSColor.systemGrayColor,@"Mouse & Keyboard":NSColor.darkGrayColor,
        @"File Sharing":NSColor.systemBlueColor,@"Remote Login":NSColor.darkGrayColor,@"Clipboard":NSColor.systemOrangeColor,
        @"CD/DVD":NSColor.systemTealColor}[title]?:NSColor.systemBlueColor;
    NSView *icon=[[VIMSettingsIcon alloc] initWithSymbol:symbol color:color glyphSize:[title isEqual:@"Clipboard"]?16:20];
    NSTextField *label=[NSTextField labelWithString:title];label.font=[NSFont systemFontOfSize:13];
    icon.translatesAutoresizingMaskIntoConstraints=NO;label.translatesAutoresizingMaskIntoConstraints=NO;[cell addSubview:icon];[cell addSubview:label];
    [NSLayoutConstraint activateConstraints:@[[icon.leadingAnchor constraintEqualToAnchor:cell.leadingAnchor constant:8],[icon.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],[icon.widthAnchor constraintEqualToConstant:32],[icon.heightAnchor constraintEqualToConstant:32],
        [label.leadingAnchor constraintEqualToAnchor:icon.trailingAnchor constant:8],[label.centerYAnchor constraintEqualToAnchor:cell.centerYAnchor],[label.trailingAnchor constraintLessThanOrEqualToAnchor:cell.trailingAnchor constant:-8]]];
    cell.textField=label;return cell;
}
- (void)tableViewSelectionDidChange:(NSNotification *)notification {
    NSTableView *table=notification.object;if(table!=self.sidebar||table.selectedRow<0||table.selectedRow==self.hardwareIndex)return;
    [self.window makeFirstResponder:nil];if(self.invalidField){[table selectRowIndexes:[NSIndexSet indexSetWithIndex:self.hardwareIndex] byExtendingSelection:NO];return;}
    self.hardwareIndex=table.selectedRow;[self render];
}
- (void)controlTextDidEndEditing:(NSNotification *)notification {
    NSTextField *field=notification.object;NSError *error=nil;NSString *key=field.identifier;
    self.invalidField=NO;
    if([key isEqual:@"GuestUsername"]) {
        NSString *value=field.stringValue;
        NSCharacterSet *allowed=[NSCharacterSet characterSetWithCharactersInString:@"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-"];
        if([value rangeOfCharacterFromSet:allowed.invertedSet].location!=NSNotFound){self.feedback.stringValue=@"Use the guest account’s short name (letters, numbers, dots, hyphens or underscores).";self.invalidField=YES;return;}
        if([value isEqual:self.guestUsername]){self.feedback.stringValue=@"";return;}
        self.guestUsername=value;[NSUserDefaults.standardUserDefaults setObject:value forKey:[@"VIMGuestUsername-" stringByAppendingString:self.machine.URL.path]];
        dispatch_async(dispatch_get_main_queue(),^{[self render];});return;
    }
    if([key isEqual:@"Name"]) {
        if([field.stringValue isEqual:self.machine.pendingName?:self.machine.name]){self.feedback.stringValue=@"";return;}
        if(![self.machine rename:field.stringValue error:&error]){self.feedback.stringValue=error.localizedDescription;self.invalidField=YES;}
        else {self.feedback.stringValue=@"";self.window.title=[NSString stringWithFormat:@"“%@” Configuration",self.machine.name];if(self.completion)self.completion();}
        return;
    }
    NSScanner *scanner=[NSScanner scannerWithString:field.stringValue];long long value=0;
    BOOL port=[key hasSuffix:@"Port"];
    BOOL megabytes=[key isEqual:@"MemoryMiB"];
    if(![scanner scanLongLong:&value]||!scanner.isAtEnd||value<(port||megabytes?1024:1)||value>(port?65535:(megabytes?NSProcessInfo.processInfo.physicalMemory/(1024*1024):([key isEqual:@"CPUCount"]?NSProcessInfo.processInfo.processorCount:1024)))) {
        self.feedback.stringValue=port?@"Enter a local port from 1024–65535.":[NSString stringWithFormat:@"Use up to %lu processors or %llu MB of memory.",(unsigned long)NSProcessInfo.processInfo.processorCount,NSProcessInfo.processInfo.physicalMemory/(1024ULL*1024)];self.invalidField=YES;return;
    }
    if([key isEqual:@"MemoryGiB"]){key=@"MemoryMiB";value*=1024;}
    if([self.machine.editableConfiguration[key] longLongValue]==value){self.feedback.stringValue=@"";return;}
    if(![self.machine saveSettings:@{key:@(value)} error:&error]){self.feedback.stringValue=error.localizedDescription;self.invalidField=YES;}
    else {self.feedback.stringValue=@"";if(self.completion)self.completion();if(port)dispatch_async(dispatch_get_main_queue(),^{[self render];});}
}
- (void)saveToggle:(NSButton *)sender {
    BOOL prior=self.machine.editableConfiguration[sender.identifier]?[self.machine.editableConfiguration[sender.identifier] boolValue]:[@[@"AudioEnabled",@"SSHEnabled",@"FileSharingEnabled",@"ClipboardEnabled"] containsObject:sender.identifier];
    NSMutableDictionary *settings=[NSMutableDictionary dictionaryWithObject:@(sender.state==NSControlStateValueOn) forKey:sender.identifier];
    NSDictionary *ports=VIMSuggestedForwardPorts(self.machine.URL.URLByDeletingLastPathComponent,self.machine.URL);
    for(NSString *key in @[@"SSHPort",@"FileSharingPort"])if(!self.machine.editableConfiguration[key])settings[key]=ports[key];
    NSError *error=nil;if(![self.machine saveSettings:settings error:&error]){sender.state=prior;self.feedback.stringValue=error.localizedDescription;return;}
    if([sender.identifier isEqual:@"ClipboardEnabled"])[VIMSession clipboardPreferencesChangedForMachine:self.machine];
    if(self.completion)self.completion();[self render];
}
- (BOOL)commitSettings:(NSDictionary *)settings {
    NSError *error=nil;if(![self.machine saveSettings:settings error:&error]){VIMPresentError(error,self.window);return NO;}
    if(self.completion)self.completion();return YES;
}
- (void)processorsChanged:(NSPopUpButton *)sender {[self commitSettings:@{@"CPUCount":@(sender.indexOfSelectedItem+1)}];}
- (void)memoryStepped:(NSStepper *)sender {self.memoryValue.integerValue=sender.integerValue;self.memorySlider.doubleValue=sender.doubleValue/1024;[self commitSettings:@{@"MemoryMiB":@(sender.integerValue)}];}
- (void)memorySlid:(NSSlider *)sender {
    NSInteger mib=MIN(NSProcessInfo.processInfo.physicalMemory/(1024*1024),1024*(NSInteger)llround(sender.doubleValue));self.memoryValue.integerValue=mib;
    [self commitSettings:@{@"MemoryMiB":@(mib)}];
}
- (void)retinaChanged:(NSButton *)sender {if([self commitSettings:@{@"DisplayMode":@[@"space",@"retina",@"scaled"][sender.tag],@"RetinaEnabled":@(sender.tag!=2)}])[self render];}
- (void)restoreDefaults:(id)sender {
    if(self.hardwareIndex==3) {
        if(![self commitSettings:@{@"MACAddress":@"",@"BridgeInterface":@""}])return;
        [VIMSession changeNetworkMode:@"shared" connected:YES machine:self.machine window:self.window completion:^(NSError *error){if(error)VIMPresentError(error,self.window);[self render];}];return;
    }
    NSDictionary *settings=self.hardwareIndex==0?@{@"CPUCount":@(MIN(4,NSProcessInfo.processInfo.processorCount)),@"MemoryMiB":@4096}:(self.hardwareIndex==1?@{@"DisplayMode":@"retina",@"RetinaEnabled":@YES}:(self.hardwareIndex==3?@{@"NetworkConnected":@YES,@"MACAddress":@""}:@{@"AudioEnabled":@YES,@"AudioInputDevice":@"default",@"AudioOutputDevice":@"default"}));
    if([self commitSettings:settings]) {
        if(self.hardwareIndex==3)[VIMSession setNetworkConnected:YES machine:self.machine completion:^(NSError *error){if(error)VIMPresentError(error,self.window);}];
        [self render];
    }
}
- (void)showSharingSection:(id)sender {NSToolbarItem *item=[[NSToolbarItem alloc] initWithItemIdentifier:@"Options"];[self selectSection:item];}
- (void)textDidChange:(NSNotification *)notification {
    if(notification.object!=self.descriptionEditor)return;
    [self.descriptionTimer invalidate];__weak typeof(self) weakSelf=self;
    self.descriptionTimer=[NSTimer scheduledTimerWithTimeInterval:.5 repeats:NO block:^(NSTimer *timer){[weakSelf saveDescription];}];
}
- (void)saveDescription {
    [self.descriptionTimer invalidate];self.descriptionTimer=nil;
    if(self.descriptionEditor&&![self.descriptionEditor.string isEqual:self.machine.editableConfiguration[@"Description"]?:@""])[self commitSettings:@{@"Description":self.descriptionEditor.string}];
}
- (BOOL)windowShouldClose:(NSWindow *)sender {[sender makeFirstResponder:nil];[self saveDescription];return !self.invalidField;}
- (void)reveal:(id)sender {[NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[self.machine.URL]];}
- (void)chooseDisc:(id)sender {[VIMSession chooseOpticalMediaForMachine:self.machine window:self.window];}
- (void)ejectDisc:(id)sender {[VIMSession ejectOpticalMediaForMachine:self.machine window:self.window];}
- (void)copyConnection:(NSButton *)sender {[NSPasteboard.generalPasteboard clearContents];[NSPasteboard.generalPasteboard setString:sender.identifier forType:NSPasteboardTypeString];}
- (void)openConnection:(NSButton *)sender {
    NSURL *url=[NSURL URLWithString:sender.identifier];
    if(![url.scheme isEqual:@"smb"]){[NSWorkspace.sharedWorkspace openURL:url];return;}
    sender.enabled=NO;
    [VIMSession checkGuestFileSharing:self.machine completion:^(NSError *error){
        sender.enabled=YES;
        if(!error){[NSWorkspace.sharedWorkspace openURL:url];return;}
        if(![error.domain isEqual:@"VirtualIntelMac.FileSharingDisabled"]){VIMPresentError(error,self.window);return;}
        NSAlert *alert=[NSAlert alertWithError:error];
        [alert.buttons.firstObject setTitle:@"Open Sharing Settings"];
        [alert addButtonWithTitle:@"Cancel"];
        VIMPresentAlert(alert,self.window,^(NSModalResponse answer){
            if(answer==NSAlertFirstButtonReturn)[VIMSession openGuestPreferences:@"sharing" machine:self.machine completion:^(NSError *failure){if(failure)VIMPresentError(failure,self.window);}];
        });
    }];
}
- (void)openGuestConnection:(NSButton *)sender {
    [VIMSession openHostConnection:[NSURL URLWithString:sender.identifier] inMachine:self.machine completion:^(NSError *error){if(error)VIMPresentError(error,self.window);}];
}
- (void)openSharing:(id)sender {[NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preferences.sharing"]];}
- (void)microphone:(id)sender {
    [VIMMachineSettings requestMicrophoneAccessFromWindow:self.window];
}
@end

@implementation VIMMachineSettings
+ (void)checkPaneCyclingForMachine:(VIMMachine *)machine completion:(void (^)(BOOL))completion {
    [self showForMachine:machine window:nil completion:nil];
    VIMConfigurationWindow *controller=nil;
    for(NSWindow *window in NSApp.windows)if([window.windowController isKindOfClass:VIMConfigurationWindow.class]&&
       [[(VIMConfigurationWindow *)window.windowController machine].configuration[@"Identifier"] isEqual:machine.configuration[@"Identifier"]])controller=(id)window.windowController;
    if(!controller){completion(NO);return;}
    __block NSUInteger cycle=0;__block void (^step)(void);
    step=^{
        if(cycle==21){step=nil;completion(YES);return;}
        VIMConfigurationSidebar *outgoing=controller.sidebar;
        NSToolbarItem *item=[[NSToolbarItem alloc] initWithItemIdentifier:cycle%2?@"Sharing":@"Hardware"];
        [controller selectSection:item];[outgoing layoutSubtreeIfNeeded];
        [controller.window.contentView layoutSubtreeIfNeeded];
        NSScrollView *scroll=controller.sidebar.enclosingScrollView;
        if(scroll.verticalScrollElasticity!=NSScrollElasticityNone||scroll.horizontalScrollElasticity!=NSScrollElasticityNone||
           scroll.hasVerticalScroller||scroll.hasHorizontalScroller||
           controller.sidebar.categories.count!=controller.categories.count){step=nil;completion(NO);return;}
        cycle++;dispatch_after(dispatch_time(DISPATCH_TIME_NOW,20*NSEC_PER_MSEC),dispatch_get_main_queue(),step);
    };
    step();
}
+ (BOOL)captureForMachine:(VIMMachine *)machine toURL:(NSURL *)URL error:(NSError **)error {
    for(NSWindow *window in NSApp.windows)if([window.windowController isKindOfClass:VIMConfigurationWindow.class]&&
        [[(VIMConfigurationWindow *)window.windowController machine].configuration[@"Identifier"] isEqual:machine.configuration[@"Identifier"]]) {
        NSView *view=window.contentView;[view layoutSubtreeIfNeeded];
        NSBitmapImageRep *bitmap=[view bitmapImageRepForCachingDisplayInRect:view.bounds];
        [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
        return [[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToURL:URL options:NSDataWritingAtomic error:error];
    }
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.Configuration" code:1 userInfo:@{NSLocalizedDescriptionKey:@"Open this Mac’s configuration before capturing its preview."}];
    return NO;
}
+ (void)requestMicrophoneAccessFromWindow:(NSWindow *)window {
    if([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]==AVAuthorizationStatusNotDetermined) {
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted){
            if(!granted)dispatch_async(dispatch_get_main_queue(),^{NSAlert *alert=[NSAlert new];alert.messageText=@"Microphone access is turned off";alert.informativeText=@"You can allow Virtual Intel Mac to use this Mac’s microphone in Privacy settings.";[alert addButtonWithTitle:@"OK"];VIMPresentAlert(alert,window,nil);});
        }];
    } else [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone"]];
}
+ (void)showForMachine:(VIMMachine *)machine section:(NSString *)section category:(NSString *)category window:(NSWindow *)window {
    if([category isEqual:@"Sharing"])category=@"File Sharing";
    [self showForMachine:machine window:window completion:nil];
    for(NSWindow *candidate in NSApp.windows)if([candidate.windowController isKindOfClass:VIMConfigurationWindow.class]) {
        VIMConfigurationWindow *controller=(id)candidate.windowController;
        if(![controller.machine.configuration[@"Identifier"] isEqual:machine.configuration[@"Identifier"]])continue;
        NSToolbarItem *item=[[NSToolbarItem alloc] initWithItemIdentifier:section];[controller selectSection:item];
        NSUInteger index=[controller.categories indexOfObject:category];if(index!=NSNotFound){controller.hardwareIndex=index;[controller render];}
    }
}
+ (void)showForMachine:(VIMMachine *)machine window:(NSWindow *)window completion:(void (^)(void))completion {
    static NSMutableDictionary<NSString *,VIMConfigurationWindow *> *windows;
    if(!windows)windows=[NSMutableDictionary dictionary];NSString *key=machine.configuration[@"Identifier"]?:machine.URL.URLByResolvingSymlinksInPath.path;
    VIMConfigurationWindow *controller=windows[key];
    if(!controller){controller=[[VIMConfigurationWindow alloc] initWithMachine:machine];windows[key]=controller;}
    else if(!controller.window.visible){controller.machine=machine;controller.invalidField=NO;[controller render];}
    controller.completion=completion;
    [controller showWindow:nil];[controller.window makeKeyAndOrderFront:nil];
}
@end
