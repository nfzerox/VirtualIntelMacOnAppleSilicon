#import "VIMLocalization.h"
#import "VIMGuestFamilies.h"
#import "VIMMachineSettings.h"
#import "VIMSettingsIcon.h"
#import "VIMMachine.h"
#import "VIMCPUBackend.h"
#import "VIMGuestProductImage.h"
#import "VIMSession.h"
#import "VIMImageSlots.h"
#import "VIMNetworkDefaults.h"
#import "VIMSMBService.h"
#import "VIMHostAudio.h"
#import "VIMCameraCapture.h"
#import "VIMAlerts.h"
#import "VIMConfigurationTabs.h"
#import "VIMDiskMaintenance.h"
#import "VIMHostSupport.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <math.h>
@import AVFoundation;

@interface VIMConfigurationDocument : NSView
@property BOOL drawsWindowBackground;
@end
@implementation VIMConfigurationDocument
- (BOOL)isFlipped {return YES;}
- (void)drawRect:(NSRect)rect {if(self.drawsWindowBackground){[NSColor.windowBackgroundColor setFill];NSRectFill(rect);}}
@end

@interface VIMStorageDistributionBar : NSView @end
@implementation VIMStorageDistributionBar
- (instancetype)initWithFrame:(NSRect)frame {
    if((self=[super initWithFrame:frame])) {
        self.wantsLayer=YES;self.clipsToBounds=YES;
        self.layer.cornerRadius=4;self.layer.masksToBounds=YES;
    }return self;
}
@end

static NSView *VIMStyledConfigurationTabs(NSTabView *tabs) {
    if(@available(macOS 26.0,*))return tabs;
    return [[VIMConfigurationTabs alloc] initWithTabs:tabs];
}

// Native popup rendering indents short and truncated image titles differently.
// Use ordinary image/label views for the closed title; the native popup keeps
// its bezel, arrow, complete menu, keyboard handling and selection/action.
@interface VIMOpticalImageLabel : NSStackView @end
@implementation VIMOpticalImageLabel
- (NSView *)hitTest:(NSPoint)point {return nil;}
@end
static void VIMSetOpticalImageTitle(NSPopUpButton *popup,NSImage *image,NSString *title) {
    NSPopUpButtonCell *cell=popup.cell;cell.usesItemFromMenu=NO;
    cell.menuItem=[[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    NSImageView *icon=[NSImageView imageViewWithImage:image];icon.imageScaling=NSImageScaleProportionallyDown;
    NSTextField *label=[NSTextField labelWithString:title];
    label.font=[NSFont systemFontOfSize:popup.font.pointSize weight:NSFontWeightRegular];label.textColor=NSColor.controlTextColor;
    label.lineBreakMode=NSLineBreakByTruncatingTail;label.usesSingleLineMode=YES;
    [label setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    VIMOpticalImageLabel *content=[VIMOpticalImageLabel stackViewWithViews:@[icon,label]];
    content.spacing=6;content.alignment=NSLayoutAttributeCenterY;content.translatesAutoresizingMaskIntoConstraints=NO;
    for(NSView *view in @[icon,label,content])view.accessibilityElement=NO;
    popup.accessibilityLabel=title;popup.toolTip=title;[popup addSubview:content];
    [NSLayoutConstraint activateConstraints:@[[icon.widthAnchor constraintEqualToConstant:16],[icon.heightAnchor constraintEqualToConstant:16],
        [content.leadingAnchor constraintEqualToAnchor:popup.leadingAnchor constant:5],
        [content.trailingAnchor constraintEqualToAnchor:popup.trailingAnchor constant:-25],
        [content.centerYAnchor constraintEqualToAnchor:popup.centerYAnchor]]];
}

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
        NSButton *cancel=[NSButton buttonWithTitle:VIMLocalized(@"common.cancel", @"Cancel") target:self action:@selector(cancel:)];cancel.keyEquivalent=@"\e";
        NSButton *save=[NSButton buttonWithTitle:VIMLocalized(@"common.ok", @"OK") target:self action:@selector(save:)];save.keyEquivalent=@"\r";
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

@interface VIMAutosenseOnboardingSheet : NSWindowController
@end
@implementation VIMAutosenseOnboardingSheet
- (instancetype)init {
    const CGFloat width=440;
    NSImage *banner=[NSImage imageNamed:@"AutosenseOnboarding"];
    CGFloat bannerHeight=banner.size.width>0?(width*(banner.size.height/banner.size.width)):275;
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,width,bannerHeight+200) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    if((self=[super initWithWindow:window])) {
        window.title=@"";window.titleVisibility=NSWindowTitleHidden;window.releasedWhenClosed=NO;
        NSImageView *image=[NSImageView imageViewWithImage:banner];
        image.imageScaling=NSImageScaleAxesIndependently;image.imageAlignment=NSImageAlignCenter;
        [image.widthAnchor constraintEqualToConstant:width].active=YES;
        [image.heightAnchor constraintEqualToConstant:bannerHeight].active=YES;
        NSTextField *title=[NSTextField labelWithString:VIMLocalized(@"settings.autosense", @"Autosense")];
        title.font=[NSFont systemFontOfSize:20 weight:NSFontWeightSemibold];title.alignment=NSTextAlignmentCenter;
        title.textColor=NSColor.labelColor;
        NSTextField *body=[NSTextField wrappingLabelWithString:VIMLocalized(@"settings.autosense-onboarding", @"Through a custom cross-architecture driver, Virtual Intel Mac will automatically detect when the virtual machine is in need of camera output, and hint at the host accordingly. This helps better preserve your privacy, but may not work in all edge cases.")];
        body.font=[NSFont systemFontOfSize:13 weight:NSFontWeightRegular];body.alignment=NSTextAlignmentCenter;
        body.textColor=NSColor.secondaryLabelColor;body.preferredMaxLayoutWidth=width-48;
        NSButton *continueButton=[NSButton buttonWithTitle:VIMLocalized(@"common.continue", @"Continue") target:self action:@selector(continue:)];
        continueButton.keyEquivalent=@"\r";[continueButton.widthAnchor constraintGreaterThanOrEqualToConstant:90].active=YES;
        NSStackView *copy=[NSStackView stackViewWithViews:@[title,body]];
        copy.orientation=NSUserInterfaceLayoutOrientationVertical;copy.alignment=NSLayoutAttributeCenterX;copy.spacing=8;
        NSStackView *content=[NSStackView stackViewWithViews:@[copy,continueButton]];
        content.orientation=NSUserInterfaceLayoutOrientationVertical;content.alignment=NSLayoutAttributeCenterX;content.spacing=20;
        content.edgeInsets=NSEdgeInsetsMake(20,24,24,24);
        NSStackView *root=[NSStackView stackViewWithViews:@[image,content]];
        root.orientation=NSUserInterfaceLayoutOrientationVertical;root.alignment=NSLayoutAttributeLeading;root.spacing=0;
        window.contentView=root;
        [window setContentSize:NSMakeSize(width,bannerHeight+content.fittingSize.height)];
    }return self;
}
- (void)continue:(id)sender {[self.window.sheetParent endSheet:self.window];}
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
@property NSWindow *maintenanceWindow;
@property VIMInstallationActivity *diskActivity;
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
static NSImageView *VIMDiskSymbol(CGFloat size) {
    NSImageView *icon=[NSImageView imageViewWithImage:[NSImage imageWithSystemSymbolName:@"internaldrive" accessibilityDescription:nil]];
    icon.symbolConfiguration=[NSImageSymbolConfiguration configurationWithPointSize:size weight:NSFontWeightRegular];
    icon.contentTintColor=NSColor.secondaryLabelColor;
    [icon.widthAnchor constraintEqualToConstant:size+8].active=YES;
    [icon.heightAnchor constraintEqualToConstant:size+8].active=YES;return icon;
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
    // Pop-up bezels extend beyond their alignment rects. Older AppKit
    // container defaults can clip that outset; keep clipping only on the
    // actual scrolling viewport, not on the grid/stack/control hierarchy.
    if(![view isKindOfClass:NSScrollView.class]&&![view isKindOfClass:NSClipView.class]&&
       ![view isKindOfClass:VIMStorageDistributionBar.class])view.clipsToBounds=NO;
    if([view isKindOfClass:NSGridView.class]) {
        NSGridView *grid=(NSGridView *)view;grid.rowAlignment=NSGridRowAlignmentFirstBaseline;
        if(grid.numberOfColumns==2) {
            CGFloat captionWidth=0;
            for(NSInteger row=0;row<grid.numberOfRows;row++) {
                NSView *caption=[grid cellAtColumnIndex:0 rowIndex:row].contentView;
                if([caption isKindOfClass:NSTextField.class]) {
                    ((NSTextField *)caption).alignment=NSTextAlignmentRight;
                    captionWidth=MAX(captionWidth,caption.intrinsicContentSize.width);
                }
            }
            if(captionWidth>0)[grid columnAtIndex:0].width=ceil(captionWidth);
            [grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;
            [grid columnAtIndex:1].xPlacement=NSGridCellPlacementLeading;
        }
        for(NSInteger row=0;row<grid.numberOfRows;row++) {
            NSView *value=[grid cellAtColumnIndex:1 rowIndex:row].contentView;
            if([value isKindOfClass:NSStackView.class]||[value isKindOfClass:NSScrollView.class])
                [grid rowAtIndex:row].yPlacement=NSGridCellPlacementTop;
        }
    }
    for(NSView *child in view.subviews)VIMAlignFormBaselines(child);
}
static BOOL VIMConfigurationFormLayoutIsValid(NSView *view) {
    if([view isKindOfClass:NSGridView.class]) {
        NSGridView *grid=(NSGridView *)view;
        if(grid.numberOfColumns==2)for(NSInteger row=0;row<grid.numberOfRows;row++) {
            NSView *label=[grid cellAtColumnIndex:0 rowIndex:row].contentView;
            if([label isKindOfClass:NSTextField.class]&&
               (((NSTextField *)label).alignment!=NSTextAlignmentRight||[grid columnAtIndex:0].xPlacement!=NSGridCellPlacementTrailing))return NO;
        }
    }
    if([view isKindOfClass:NSPopUpButton.class]&&[view.identifier isEqual:@"CPUBackend"]) {
        NSPopUpButton *backend=(NSPopUpButton *)view;
        if(!backend.enabled)return NO;
#if !defined(__arm64__)
        if(backend.numberOfItems!=1||![backend.titleOfSelectedItem isEqual:@"Hypervisor"])return NO;
#endif
    }
    for(NSView *child in view.subviews)if(!VIMConfigurationFormLayoutIsValid(child))return NO;
    return YES;
}

@implementation VIMConfigurationWindow
- (instancetype)initWithMachine:(VIMMachine *)machine {
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,640,300)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable
        backing:NSBackingStoreBuffered defer:NO];
    if((self=[super initWithWindow:window])) {
        self.machine=machine;self.section=@"Hardware";self.hostFiles=YES;window.releasedWhenClosed=NO;window.delegate=self;window.windowController=self;
        window.backgroundColor=NSColor.windowBackgroundColor;
        window.autorecalculatesKeyViewLoop=YES;
        window.collectionBehavior=NSWindowCollectionBehaviorFullScreenNone;
        self.guestUsername=[NSUserDefaults.standardUserDefaults stringForKey:[@"VIMGuestUsername-" stringByAppendingString:machine.URL.path]]?:@"";
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(sessionStateChanged:) name:@"VIMLibraryChanged" object:nil];
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(sessionStateChanged:) name:@"VIMNetworkChanged" object:nil];
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(packageRelocated:) name:@"VIMMachineRelocated" object:nil];
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(cameraDriverChanged:) name:@"VIMCameraDriverChanged" object:nil];
        [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(cameraDriverChanged:) name:@"VIMCameraTestPatternChanged" object:nil];
        window.title=[NSString stringWithFormat:VIMLocalized(@"ui.configuration", @"“%@” Configuration"),machine.name];
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
    if(!self.window.firstResponder||![self.window.firstResponder isKindOfClass:NSTextView.class]){
        self.guestUsername=[NSUserDefaults.standardUserDefaults stringForKey:[@"VIMGuestUsername-" stringByAppendingString:self.machine.URL.path]]?:@"";
        [self render];
    }
}
- (void)cameraDriverChanged:(NSNotification *)note {
    if(note.object&&![note.object isEqual:self.machine.configuration[@"Identifier"]])return;
    if([self.section isEqual:@"Hardware"]&&self.hardwareIndex==5)[self render];
}
- (void)packageRelocated:(NSNotification *)note {
    if(![note.userInfo[@"Identifier"] isEqual:self.machine.configuration[@"Identifier"]])return;
    VIMMachine *fresh=[VIMMachine loadURL:note.userInfo[@"URL"] error:nil];if(fresh)self.machine=fresh;
    self.window.title=[NSString stringWithFormat:VIMLocalized(@"ui.configuration", @"“%@” Configuration"),self.machine.name];[self render];
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
- (NSArray *)toolbarDefaultItemIdentifiers:(NSToolbar *)toolbar {return @[@"General",@"Hardware",@"Sharing",NSToolbarFlexibleSpaceItemIdentifier];}
- (NSArray *)toolbarAllowedItemIdentifiers:(NSToolbar *)toolbar {return [self toolbarDefaultItemIdentifiers:toolbar];}
- (NSArray *)toolbarSelectableItemIdentifiers:(NSToolbar *)toolbar {return @[@"General",@"Hardware",@"Sharing"];}
- (NSToolbarItem *)toolbar:(NSToolbar *)toolbar itemForItemIdentifier:(NSString *)identifier willBeInsertedIntoToolbar:(BOOL)insert {
    NSToolbarItem *item=[[NSToolbarItem alloc] initWithItemIdentifier:identifier];item.label=VIMLocalizedLabel(identifier);
    NSString *symbol=@{@"General":@"gearshape",@"Hardware":@"cpu",@"Sharing":@"folder"}[identifier];
    item.image=[[NSImage imageWithSystemSymbolName:symbol accessibilityDescription:VIMLocalizedLabel(identifier)] imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithPointSize:24 weight:NSFontWeightRegular]];item.target=self;item.action=@selector(selectSection:);return item;
}
- (void)selectSection:(NSToolbarItem *)sender {
    [self saveDescription];
    [self.window makeFirstResponder:nil];if(self.invalidField){self.window.toolbar.selectedItemIdentifier=self.section;return;}
    self.section=[sender.itemIdentifier isEqual:@"Options"]?@"Sharing":sender.itemIdentifier;self.window.toolbar.selectedItemIdentifier=self.section;self.hardwareIndex=0;[self render];
}
- (NSTextField *)field:(NSString *)key value:(NSString *)value {
    NSTextField *field=[NSTextField textFieldWithString:value];field.identifier=key;field.delegate=self;
    if(![key isEqual:@"Name"])
        [field.widthAnchor constraintEqualToConstant:[key isEqual:@"GuestUsername"]?240:90].active=YES;
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
    NSString *system=VIMSystemTitleForGuest(self.machine.guest)?:@"Mac OS";
    NSTextField *name=[self field:@"Name" value:self.machine.pendingName?:self.machine.name];
    NSImage *identityImage=VIMGuestProductImage(self.machine.guest)?:
        [NSImage imageNamed:[self.machine.guest stringByAppendingString:@"Roundel"]]?:
        [NSImage imageNamed:[self.machine.guest stringByAppendingString:@"Icon"]]?:NSApp.applicationIconImage;
    NSImageView *mark=[NSImageView imageViewWithImage:identityImage];
    mark.imageScaling=NSImageScaleProportionallyUpOrDown;
    [mark.widthAnchor constraintEqualToConstant:24].active=YES;[mark.heightAnchor constraintEqualToConstant:24].active=YES;
    NSTextField *os=[NSTextField labelWithString:system];
    NSStackView *identity=[NSStackView stackViewWithViews:@[mark,os]];identity.spacing=8;
    NSScrollView *description=[NSScrollView new];description.borderType=NSBezelBorder;description.hasVerticalScroller=YES;description.autohidesScrollers=YES;
    self.descriptionEditor=[[NSTextView alloc] initWithFrame:NSMakeRect(0,0,450,54)];self.descriptionEditor.font=[NSFont systemFontOfSize:13];self.descriptionEditor.richText=NO;
    self.descriptionEditor.textContainerInset=NSMakeSize(4,4);self.descriptionEditor.delegate=self;self.descriptionEditor.string=self.machine.editableConfiguration[@"Description"]?:@"";
    description.documentView=self.descriptionEditor;self.descriptionEditor.autoresizingMask=NSViewWidthSizable;
    [description.heightAnchor constraintEqualToConstant:58].active=YES;
    NSTextField *descriptionLabel=[NSTextField labelWithString:VIMLocalized(@"settings.description", @"Description:")];
    NSGridView *grid=[NSGridView gridViewWithViews:@[@[NSGridCell.emptyContentView,identity],@[[NSTextField labelWithString:VIMLocalized(@"settings.name", @"Name:")],name],@[descriptionLabel,description]]];
    grid.columnSpacing=8;grid.rowSpacing=12;[grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;[grid columnAtIndex:0].width=80;
    [grid columnAtIndex:1].xPlacement=NSGridCellPlacementFill;[grid rowAtIndex:2].yPlacement=NSGridCellPlacementTop;
    NSGridCell *caption=[grid cellAtColumnIndex:0 rowIndex:2];caption.yPlacement=NSGridCellPlacementNone;
    caption.customPlacementConstraints=@[[descriptionLabel.topAnchor constraintEqualToAnchor:description.topAnchor]];
    NSBox *divider=[NSBox new];divider.boxType=NSBoxSeparator;
    NSView *storage=[self storageOverview];
    NSButton *reveal=[self button:VIMLocalized(@"app.show-in-finder", @"Show in Finder") action:@selector(reveal:)];
    NSTextField *locationLabel=VIMText(VIMLocalized(@"settings.location", @"Location:"),NO);locationLabel.alignment=NSTextAlignmentRight;
    [locationLabel.widthAnchor constraintEqualToConstant:80].active=YES;
    NSStackView *location=[NSStackView stackViewWithViews:@[locationLabel,reveal]];location.spacing=8;
    NSStackView *column=VIMColumn(@[grid,divider,storage,location]);column.spacing=16;
    [grid.widthAnchor constraintEqualToAnchor:column.widthAnchor].active=YES;
    [divider.widthAnchor constraintEqualToAnchor:column.widthAnchor].active=YES;
    [storage.widthAnchor constraintEqualToAnchor:column.widthAnchor].active=YES;
    return column;
}
- (NSView *)storageOverview {
    NSString *(^bytes)(uint64_t)=^NSString *(uint64_t value){return self.sizesReady?[NSByteCountFormatter stringFromByteCount:value countStyle:NSByteCountFormatterCountStyleFile]:VIMLocalized(@"settings.calculating", @"Calculating…");};
    NSView *bar=[[VIMStorageDistributionBar alloc] initWithFrame:NSZeroRect];
    NSView *disk=[NSView new],*other=[NSView new];
    NSColor *diskColor=[NSColor colorWithSRGBRed:.29 green:.67 blue:.91 alpha:1],*otherColor=[NSColor colorWithSRGBRed:.73 green:.48 blue:.83 alpha:1];
    disk.wantsLayer=other.wantsLayer=YES;disk.layer.backgroundColor=diskColor.CGColor;other.layer.backgroundColor=otherColor.CGColor;
    for(NSView *part in @[disk,other]){part.translatesAutoresizingMaskIntoConstraints=NO;[bar addSubview:part];}
    CGFloat share=self.diskBytes+self.otherBytes?MAX(.01,MIN(.99,(double)self.diskBytes/(self.diskBytes+self.otherBytes))):.98;
    [NSLayoutConstraint activateConstraints:@[[bar.heightAnchor constraintEqualToConstant:20],[disk.leadingAnchor constraintEqualToAnchor:bar.leadingAnchor],[disk.widthAnchor constraintEqualToAnchor:bar.widthAnchor multiplier:share],
        [disk.topAnchor constraintEqualToAnchor:bar.topAnchor],[disk.bottomAnchor constraintEqualToAnchor:bar.bottomAnchor],[other.leadingAnchor constraintEqualToAnchor:disk.trailingAnchor],
        [other.trailingAnchor constraintEqualToAnchor:bar.trailingAnchor],[other.topAnchor constraintEqualToAnchor:bar.topAnchor],[other.bottomAnchor constraintEqualToAnchor:bar.bottomAnchor]]];
    NSMutableArray *legends=[NSMutableArray new];
    NSArray *colors=@[diskColor,otherColor],*titles=@[VIMLocalized(@"settings.hard-disks", @"Hard Disks"),VIMLocalized(@"ui.miscellaneous", @"Miscellaneous")],*values=@[bytes(self.diskBytes),bytes(self.otherBytes)];
    for(NSUInteger i=0;i<2;i++) {
        NSView *swatch=[NSView new];swatch.wantsLayer=YES;swatch.layer.backgroundColor=[colors[i] CGColor];[swatch.widthAnchor constraintEqualToConstant:11].active=YES;[swatch.heightAnchor constraintEqualToConstant:11].active=YES;
        NSStackView *text=VIMColumn(@[VIMText(titles[i],YES),VIMText(values[i],NO)]);text.spacing=1;
        NSStackView *legend=[NSStackView stackViewWithViews:@[swatch,text]];legend.alignment=NSLayoutAttributeTop;legend.spacing=8;[legends addObject:legend];
    }
    NSStackView *legend=[NSStackView stackViewWithViews:legends];legend.spacing=20;legend.distribution=NSStackViewDistributionFillEqually;
    NSStackView *right=VIMColumn(@[bar,legend]);right.spacing=12;[bar.widthAnchor constraintEqualToAnchor:right.widthAnchor].active=YES;
    [legend.widthAnchor constraintEqualToAnchor:right.widthAnchor].active=YES;
    NSTextField *total=VIMText([NSString stringWithFormat:VIMLocalized(@"settings.total-size", @"Total size\n%@"),bytes(self.diskBytes+self.otherBytes)],NO);total.alignment=NSTextAlignmentRight;[total.widthAnchor constraintEqualToConstant:80].active=YES;
    NSStackView *row=[NSStackView stackViewWithViews:@[total,right]];row.alignment=NSLayoutAttributeTop;row.spacing=8;[right.widthAnchor constraintEqualToAnchor:row.widthAnchor constant:-88].active=YES;return row;
}
- (NSView *)hardware {
    NSDictionary *c=self.machine.editableConfiguration;
    if(self.hardwareIndex==0) {
        NSPopUpButton *cpu=[NSPopUpButton new];cpu.identifier=@"CPUCount";cpu.target=self;cpu.action=@selector(processorsChanged:);
        for(NSInteger i=1;i<=NSProcessInfo.processInfo.processorCount;i++)[cpu addItemWithTitle:[NSString stringWithFormat:@"%ld%@",(long)i,i==MIN(4,NSProcessInfo.processInfo.processorCount)?VIMLocalized(@"ui.recommended", @" (Recommended)"):@""]];
        [cpu selectItemAtIndex:MAX(0,[c[@"CPUCount"] integerValue]-1)];[cpu.widthAnchor constraintEqualToConstant:170].active=YES;self.fields[@"CPUCount"]=cpu;
        NSTextField *memory=[self field:@"MemoryMiB" value:[c[@"MemoryMiB"] stringValue]];self.memoryValue=memory;
        NSStepper *stepper=[NSStepper new];stepper.minValue=1024;stepper.maxValue=NSProcessInfo.processInfo.physicalMemory/(1024*1024);stepper.increment=1024;stepper.doubleValue=memory.doubleValue;stepper.target=self;stepper.action=@selector(memoryStepped:);
        NSStackView *memoryRow=[NSStackView stackViewWithViews:@[memory,[NSTextField labelWithString:@"MB"],stepper]];memoryRow.spacing=4;
        double maxGiB=MAX(1,NSProcessInfo.processInfo.physicalMemory/(1024*1024*1024));
        self.memorySlider=[NSSlider sliderWithValue:memory.doubleValue/1024 minValue:1 maxValue:maxGiB target:self action:@selector(memorySlid:)];
        self.memorySlider.continuous=NO;self.memorySlider.numberOfTickMarks=(NSInteger)maxGiB;self.memorySlider.allowsTickMarkValuesOnly=YES;self.memorySlider.accessibilityLabel=VIMLocalized(@"settings.memory-allocation-in-gigabytes", @"Memory allocation in gigabytes");
        [self.memorySlider.widthAnchor constraintEqualToConstant:280].active=YES;
        NSTextField *low=VIMText(@"1 GB",NO),*mid=VIMText([NSString stringWithFormat:@"%.0f GB",round((maxGiB+1)/2)],NO),*high=VIMText([NSString stringWithFormat:@"%.0f GB",maxGiB],NO);mid.alignment=NSTextAlignmentCenter;high.alignment=NSTextAlignmentRight;
        NSStackView *ticks=[NSStackView stackViewWithViews:@[low,mid,high]];ticks.distribution=NSStackViewDistributionFillEqually;[ticks.widthAnchor constraintEqualToConstant:280].active=YES;
        NSStackView *memoryColumn=VIMColumn(@[memoryRow,self.memorySlider,ticks]);memoryColumn.spacing=10;
        NSPopUpButton *backend=[NSPopUpButton new];
        backend.autoenablesItems=NO;backend.identifier=@"CPUBackend";backend.accessibilityLabel=VIMLocalized(@"settings.cpu.backend.label", @"CPU backend");
        backend.target=self;backend.action=@selector(cpuBackendChanged:);
#if defined(__arm64__)
        BOOL appleSilicon=YES;
#else
        BOOL appleSilicon=NO;
#endif
        [backend addItemsWithTitles:appleSilicon?@[
            VIMLocalized(@"settings.cpu.backend.hybrid", @"Hybrid Hypervisor"),
            VIMLocalized(@"settings.cpu.backend.tcg", @"TCG Emulation")]:@[
            VIMLocalized(@"settings.cpu.backend.hvf", @"Hypervisor")]];
        [backend selectItemAtIndex:VIMCPUUsesEmulation(c,appleSilicon)?1:0];
        backend.enabled=YES;
        backend.toolTip=!appleSilicon?nil:VIMLocalized(@"vimmachinesettings.uses-emulation-for-unsupported-code-changes-apply-after-restart", @"Uses emulation for unsupported code. Changes apply after restart.");
        [backend.widthAnchor constraintEqualToConstant:MAX(170,backend.intrinsicContentSize.width)].active=YES;
        NSGridView *grid=[NSGridView gridViewWithViews:@[
            @[[NSTextField labelWithString:VIMLocalized(@"settings.processors", @"Processors:")],cpu],@[[NSTextField labelWithString:VIMLocalized(@"settings.memory", @"Memory:")],memoryColumn],
            @[[NSTextField labelWithString:VIMLocalized(@"vimmachinesettings.cpu-backend", @"CPU Backend:")],backend]]];
        grid.columnSpacing=12;grid.rowSpacing=22;[grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;[grid columnAtIndex:1].xPlacement=NSGridCellPlacementLeading;[grid rowAtIndex:1].yPlacement=NSGridCellPlacementTop;
        NSMutableArray *views=[NSMutableArray arrayWithObject:grid];
        NSTextField *credit=nil;
        if(appleSilicon&&!VIMCPUUsesEmulation(c,appleSilicon)) {
            NSString *copy=VIMLocalized(@"vimmachinesettings.hybrid-hypervisor-is-invented-by-sam-henri-gold-the", @"Hybrid Hypervisor is developed by Sam Henri Gold.");
            credit=VIMText(copy,NO);credit.selectable=YES;credit.allowsEditingTextAttributes=YES;
            NSMutableAttributedString *text=[[NSMutableAttributedString alloc] initWithString:copy
                attributes:@{NSFontAttributeName:credit.font,NSForegroundColorAttributeName:credit.textColor,NSUnderlineStyleAttributeName:@0}];
            [text addAttribute:NSLinkAttributeName value:[NSURL URLWithString:@"https://samhenri.gold"] range:[copy rangeOfString:@"Sam Henri Gold"]];
            credit.attributedStringValue=text;[views addObject:credit];
        }
        [views addObject:VIMText(VIMLocalized(@"settings.changes-take-effect-the-next-time-this-virtual-mac-starts", @"Changes take effect the next time this virtual Mac starts."),NO)];
        NSStackView *column=VIMColumn(views);
        if(credit)[credit.widthAnchor constraintEqualToAnchor:column.widthAnchor].active=YES;
        return column;
    }
    if(self.hardwareIndex==1) {
        NSMutableArray *views=[NSMutableArray new];
        NSArray *modes=@[@"space",@"retina",@"scaled"],*titles=@[VIMLocalized(@"settings.more-space", @"More Space"),VIMLocalized(@"settings.best-for-retina-display", @"Best for Retina display"),VIMLocalized(@"ui.scaled", @"Scaled")];
        NSArray *descriptions=@[VIMLocalized(@"settings.graphics-at-full-retina-resolution-text-and-interface-are-small", @"Graphics at full Retina resolution. Text and interface are small."),@"",VIMLocalized(@"settings.graphics-scaled-to-full-size", @"Graphics scaled to full size.")];
        NSButton *firstOption=nil;
        for(NSUInteger i=0;i<modes.count;i++) {
            if(i==1&&!VIMGuestSupportsRetinaDisplay(c))continue;
            NSButton *option=[NSButton radioButtonWithTitle:titles[i] target:self action:@selector(retinaChanged:)];option.tag=i;
            if(!firstOption)firstOption=option;
            option.state=[VIMDisplayMode(c) isEqual:modes[i]];
            if(![descriptions[i] length]){[views addObject:option];continue;}
            NSString *copy=i==0&&!VIMGuestSupportsRetinaDisplay(c)?VIMLocalized(@"settings.graphics-at-full-display-resolution-text-and-interface-are-small", @"Graphics at full display resolution. Text and interface are small."):descriptions[i];
            NSTextField *description=VIMText(copy,NO);
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
        NSView *form=[NSView new];NSTextField *label=[NSTextField labelWithString:VIMLocalized(@"settings.resolution", @"Resolution:")];
        for(NSView *view in @[label,options]){view.translatesAutoresizingMaskIntoConstraints=NO;[form addSubview:view];}
        [NSLayoutConstraint activateConstraints:@[[label.leadingAnchor constraintEqualToAnchor:form.leadingAnchor],
            [label.firstBaselineAnchor constraintEqualToAnchor:firstOption.firstBaselineAnchor],
            [options.leadingAnchor constraintEqualToAnchor:label.trailingAnchor constant:12],
            [options.trailingAnchor constraintEqualToAnchor:form.trailingAnchor],
            [options.topAnchor constraintEqualToAnchor:form.topAnchor],
            [options.bottomAnchor constraintEqualToAnchor:form.bottomAnchor]]];
        NSPopUpButton *filter=[NSPopUpButton new];
        [filter addItemsWithTitles:@[VIMLocalized(@"settings.nearest-neighbor", @"Nearest Neighbor"),VIMLocalized(@"ui.linear", @"Linear")]];
        [filter selectItemAtIndex:[VIMDisplayScalingFilter(c) isEqual:@"nearest"]?0:1];
        filter.target=self;filter.action=@selector(scalingFilterChanged:);
        NSTextField *samplingLabel=[NSTextField labelWithString:VIMLocalized(@"settings.scaling-filter", @"Scaling Filter:")];
        CGFloat labelWidth=MAX(label.intrinsicContentSize.width,samplingLabel.intrinsicContentSize.width);
        label.alignment=NSTextAlignmentRight;
        [label.widthAnchor constraintEqualToConstant:labelWidth].active=YES;
        NSMutableArray *rows=[@[@[samplingLabel,filter]] mutableCopy];
        NSPopUpButton *renderer=nil;
        if([self.machine.guest isEqual:@"mojave"]) {
            renderer=[NSPopUpButton new];
            [renderer addItemsWithTitles:@[VIMLocalized(@"settings.opengl.passthrough", @"Passthrough OpenGL"),VIMLocalized(@"vimmachinesettings.apple-64-bit-only", @"Apple (64-bit only)")]];
            [renderer selectItemAtIndex:[c[@"PassthroughOpenGLEnabled"]?:@YES boolValue]?0:1];
            renderer.target=self;renderer.action=@selector(openGLRendererChanged:);renderer.accessibilityLabel=@"OpenGL renderer";
            [rows addObject:@[[NSTextField labelWithString:@"OpenGL:"],renderer]];
        }
        NSGridView *sampling=[NSGridView gridViewWithViews:rows];
        // The grid must own both controls before their cross-view constraint.
        if(renderer)[renderer.widthAnchor constraintEqualToAnchor:filter.widthAnchor].active=YES;
        sampling.columnSpacing=12;sampling.rowSpacing=16;
        [sampling columnAtIndex:0].width=labelWidth;
        [sampling columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;
        [sampling columnAtIndex:1].xPlacement=NSGridCellPlacementLeading;
        return VIMColumn(@[form,sampling,VIMText(VIMLocalized(@"vimmachinesettings.changes-apply-after-restart-the-scaling-filter-applies-immediately", @"Changes apply after restart. The scaling filter applies immediately."),NO)]);
    }
    if(self.hardwareIndex==4) {
        NSPopUpButton *output=[self audioPopup:NO],*input=[self audioPopup:YES];
        NSGridView *grid=[NSGridView gridViewWithViews:@[@[[NSTextField labelWithString:VIMLocalized(@"settings.sound-output", @"Sound Output:")],output],@[[NSTextField labelWithString:VIMLocalized(@"settings.microphone", @"Microphone:")],input]]];grid.rowSpacing=20;grid.columnSpacing=12;[grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;
        NSButton *hostAccess=[self button:VIMLocalized(@"vimmachinesettings.open-system-settings", @"Open System Settings") action:@selector(microphone:)];
        hostAccess.toolTip=VIMLocalized(@"settings.allow-virtual-intel-mac-to-use-this-mac-s-microphone", @"Allow Virtual Intel Mac to use this Mac’s microphone.");
        return VIMColumn(@[grid,hostAccess,
            VIMText(VIMLocalized(@"settings.changes-take-effect-the-next-time-the-virtual-mac-starts", @"Changes take effect the next time the virtual Mac starts."),NO)]);
    }
    if(self.hardwareIndex==5) {
        BOOL connected=VIMCameraDeviceAdvertised(c);NSString *modeValue=VIMCameraMode(c);
        BOOL driverLoaded=[VIMSession cameraDriverLoadedForMachine:self.machine];
        NSSwitch *connection=[NSSwitch new];connection.identifier=@"CameraConnected";
        connection.state=connected?NSControlStateValueOn:NSControlStateValueOff;
        connection.target=self;connection.action=@selector(cameraConnectionChanged:);
        NSString *driverTitle=VIMLocalized(@"settings.enable-virtual-intel-mac-driver", @"Enable Camera");
        connection.accessibilityLabel=driverTitle;
        NSTextField *driverLabel=[NSTextField labelWithString:driverTitle];
        NSImageView *dot=[NSImageView imageViewWithImage:[NSImage imageWithSystemSymbolName:@"circle.fill" accessibilityDescription:nil]];
        dot.symbolConfiguration=[NSImageSymbolConfiguration configurationWithPointSize:9 weight:NSFontWeightRegular];
        dot.contentTintColor=driverLoaded?NSColor.systemGreenColor:NSColor.tertiaryLabelColor;
        NSTextField *status=[NSTextField labelWithString:driverLoaded?VIMLocalized(@"settings.connected", @"Connected"):VIMLocalized(@"settings.disconnected", @"Disconnected")];
        status.textColor=driverLoaded?NSColor.labelColor:NSColor.secondaryLabelColor;
        status.toolTip=VIMLocalized(@"settings.camera-driver-light-tooltip", @"Green while the virtual Mac’s camera driver is loaded.");
        NSStackView *connectionRow=[NSStackView stackViewWithViews:@[connection,driverLabel,dot,status]];connectionRow.spacing=8;
        [connectionRow setCustomSpacing:20 afterView:driverLabel];connectionRow.alignment=NSLayoutAttributeCenterY;
        NSPopUpButton *camera=[self cameraPopup],*mode=[self cameraModePopup];camera.enabled=connected&&![modeValue isEqual:@"testpattern"];mode.enabled=connected;
        NSButton *chimes=[NSButton checkboxWithTitle:VIMLocalized(@"settings.play-chimes-when-autosense-acknowledges-camera-requests", @"Play chimes when Autosense acknowledges camera requests") target:self action:@selector(cameraChimesChanged:)];
        chimes.state=VIMCameraChimesEnabled(c)?NSControlStateValueOn:NSControlStateValueOff;
        chimes.enabled=connected&&[modeValue isEqual:@"autosense"];
        NSTextField *cameraLabel=[NSTextField labelWithString:VIMLocalized(@"settings.camera", @"Camera:")],*modeLabel=[NSTextField labelWithString:VIMLocalized(@"settings.camera-mode", @"Mode:")];
        if(!connected){cameraLabel.textColor=NSColor.disabledControlTextColor;modeLabel.textColor=NSColor.disabledControlTextColor;}
        NSGridView *grid=[NSGridView gridViewWithViews:@[
            @[NSGridCell.emptyContentView,connectionRow],
            @[cameraLabel,camera],@[modeLabel,mode],@[NSGridCell.emptyContentView,chimes]]];
        grid.rowSpacing=20;grid.columnSpacing=12;[grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;[grid columnAtIndex:1].xPlacement=NSGridCellPlacementLeading;
        NSString *explanation=!connected?VIMLocalized(@"settings.camera-explanation.disconnected", @"The virtual Mac has no camera. Turn on Enable Camera to add one; only Autosense and Constant Output ask for camera access."):
            ([modeValue isEqual:@"constant"]?VIMLocalized(@"settings.camera-explanation.constant", @"This Mac’s camera stays on while the virtual Mac is running."):
            ([modeValue isEqual:@"autosense"]?VIMLocalized(@"settings.camera-explanation.autosense", @"This Mac’s camera turns on only while an app in the virtual Mac uses it."):
            ([modeValue isEqual:@"testpattern"]?VIMLocalized(@"settings.camera-explanation.testpattern", @"The virtual Mac receives a test pattern. This Mac’s camera stays off and is never asked for access."):
            VIMLocalized(@"settings.camera-explanation.off", @"No camera data is sent, but the camera stays connected, so apps in the virtual Mac still see it and you can switch to another mode without restarting. Turn the camera off to remove it entirely."))));
        return VIMColumn(@[grid,VIMText(explanation,NO)]);
    }
    if(self.hardwareIndex==3) {
        NSPopUpButton *source=[NSPopUpButton new];source.target=self;source.action=@selector(networkChanged:);
        [source addItemsWithTitles:@[VIMLocalized(@"settings.disconnected", @"Disconnected"),VIMLocalized(@"settings.shared-network-recommended", @"Shared Network (Recommended)"),VIMLocalized(@"settings.host-only-network", @"Host-Only Network"),VIMLocalized(@"settings.bridged-network", @"Bridged Network")]];
        NSArray *modes=@[@"shared",@"host",@"bridged"];NSString *mode=VIMNetworkMode(c);
        [source selectItemAtIndex:c[@"NetworkConnected"]&&![c[@"NetworkConnected"] boolValue]?0:1+[modes indexOfObject:mode]];
        [source.widthAnchor constraintEqualToConstant:270].active=YES;
        NSMutableArray *rows=[NSMutableArray arrayWithObject:@[[NSTextField labelWithString:VIMLocalized(@"settings.source", @"Source:")],source]];
        if([mode isEqual:@"bridged"]) {
            NSPopUpButton *bridge=[NSPopUpButton new];bridge.target=self;bridge.action=@selector(bridgeChanged:);
            [bridge addItemWithTitle:VIMLocalized(@"settings.automatic", @"Automatic")];bridge.lastItem.representedObject=@"";
            for(NSDictionary *interface in VIMNetworkBridgeInterfaces()){[bridge addItemWithTitle:interface[@"Name"]];bridge.lastItem.representedObject=interface[@"Identifier"];}
            for(NSMenuItem *item in bridge.itemArray)if([item.representedObject isEqual:c[@"BridgeInterface"]?:@""])[bridge selectItem:item];
            [rows addObject:@[[NSTextField labelWithString:VIMLocalized(@"settings.interface", @"Interface:")],bridge]];
        }
        [rows addObject:@[NSGridCell.emptyContentView,[self button:VIMLocalized(@"settings.advanced", @"Advanced…") action:@selector(networkAdvanced:)]]];
        NSGridView *grid=[NSGridView gridViewWithViews:rows];grid.columnSpacing=12;grid.rowSpacing=20;
        [grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;[grid columnAtIndex:1].xPlacement=NSGridCellPlacementLeading;
        NSString *explanation=[mode isEqual:@"host"]?VIMLocalized(@"settings.connects-to-this-mac-and-other-host-only-virtual-macs", @"Connects to this Mac and other host-only virtual Macs, without access to the internet."):([mode isEqual:@"bridged"]?VIMLocalized(@"settings.connects-directly-to-your-local-network-other-devices-can-reach", @"Connects directly to your local network. Other devices can reach services you enable inside the virtual Mac."):VIMLocalized(@"settings.the-virtual-mac-shares-this-mac-s-internet-connection", @"The virtual Mac shares this Mac’s internet connection."));
        return VIMColumn(@[grid,VIMText(explanation,NO)]);
    }
    if(self.hardwareIndex==2) {
        NSPopUpButton *region=[NSPopUpButton new];[region addItemsWithTitles:@[VIMLocalized(@"vimmachinesettings.automatic-default", @"Automatic (Default)"),@"On",@"Off"]];
        region.identifier=@"MouseRegionAccommodation";region.target=self;region.action=@selector(inputPreferenceChanged:);
        NSUInteger regionIndex=[@[@"automatic",@"on",@"off"] indexOfObject:c[region.identifier]?:@"automatic"];
        [region selectItemAtIndex:regionIndex==NSNotFound?0:regionIndex];
        NSPopUpButton *release=[NSPopUpButton new];[release addItemsWithTitles:@[@"Control–Option",@"Command–Option"]];
        release.identifier=@"InputReleaseShortcut";release.target=self;release.action=@selector(inputPreferenceChanged:);
        [release selectItemAtIndex:[c[release.identifier] isEqual:@"command-option"]?1:0];
        for(NSPopUpButton *popup in @[region,release])[popup.widthAnchor constraintEqualToConstant:170].active=YES;
        NSGridView *grid=[NSGridView gridViewWithViews:@[
            @[[NSTextField labelWithString:VIMLocalized(@"vimmachinesettings.mouse-region-accommodation", @"Mouse Region Accommodation:")],region],
            @[[NSTextField labelWithString:VIMLocalized(@"vimmachinesettings.release-mouse-keyboard", @"Release Mouse & Keyboard:")],release]]];
        grid.columnSpacing=12;grid.rowSpacing=18;[grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;
        return VIMColumn(@[grid]);
    }
    if(self.hardwareIndex==7) {
        NSArray *images=[VIMSession connectedImagesForMachine:self.machine];NSMutableArray *rows=[NSMutableArray new];
        NSUInteger driveNumber=0;
        for(NSDictionary *image in images) {
            NSUInteger slot=[image[@"Slot"] unsignedIntegerValue];NSString *path=image[@"Path"];
            NSPopUpButton *source=[NSPopUpButton new];
            source.target=self;source.action=@selector(opticalChanged:);source.tag=(NSInteger)slot;
            source.alignment=NSTextAlignmentLeft;
            [source addItemWithTitle:path.lastPathComponent];source.selectedItem.tag=0;
            NSImage *disc=[[NSImage alloc] initWithContentsOfFile:@"/System/Library/CoreServices/CoreTypes.bundle/Contents/Resources/CDAudioVolumeIcon.icns"]?:[NSImage imageWithSystemSymbolName:@"opticaldisc" accessibilityDescription:nil];
            disc.size=NSMakeSize(16,16);
            source.selectedItem.image=disc;
            [source.menu addItem:NSMenuItem.separatorItem];[source addItemWithTitle:VIMLocalized(@"ui.disconnect", @"Disconnect")];source.lastItem.tag=2;
            VIMSetOpticalImageTitle(source,disc,path.lastPathComponent);
            [source.widthAnchor constraintEqualToConstant:280].active=YES;
            [rows addObject:@[[NSTextField labelWithString:[NSString stringWithFormat:VIMLocalized(@"settings.drive-lu", @"Drive %lu:"),(unsigned long)++driveNumber]],source]];
        }
        if(!images.count) {
            NSPopUpButton *source=[NSPopUpButton new];source.target=self;source.action=@selector(opticalChanged:);source.tag=0;
            [source addItemWithTitle:VIMLocalized(@"settings.no-disc", @"No Disc")];[source.menu addItem:NSMenuItem.separatorItem];
            [source addItemWithTitle:VIMLocalized(@"settings.choose-image", @"Choose Image…")];source.lastItem.tag=1;
            [source.widthAnchor constraintEqualToConstant:280].active=YES;
            [rows addObject:@[[NSTextField labelWithString:VIMLocalized(@"settings.source", @"Source:")],source]];
        } else if(images.count<VIMImageSlotCount)[rows addObject:@[NSGridCell.emptyContentView,[self button:VIMLocalized(@"settings.connect-image", @"Connect Image…") action:@selector(chooseDisc:)]]];
        NSGridView *grid=[NSGridView gridViewWithViews:rows];grid.columnSpacing=12;grid.rowSpacing=14;[grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;
        return VIMColumn(@[grid,VIMText(VIMLocalized(@"vimmachinesettings.connect-iso-cdr-toast-or-unencrypted-dmg-images", @"Connect ISO, CDR, Toast or unencrypted DMG images."),NO)]);
    }
    NSString *file=[self.machine.configuration[@"Disk"] lastPathComponent]?:@"System.qcow2";
    NSPopUpButton *source=[NSPopUpButton new];[source addItemWithTitle:file];source.toolTip=file;
    [source.widthAnchor constraintEqualToConstant:250].active=YES;
    uint64_t capacity=self.machine.diskCapacityBytes;NSNumber *allocated=nil;
    [[self.machine.URL URLByAppendingPathComponent:self.machine.configuration[@"Disk"]] getResourceValue:&allocated forKey:NSURLTotalFileAllocatedSizeKey error:nil];
    NSString *size=allocated?[NSByteCountFormatter stringFromByteCount:allocated.longLongValue countStyle:NSByteCountFormatterCountStyleFile]:VIMLocalized(@"library.unavailable", @"Unavailable");
    NSButton *reclaim=[self button:VIMLocalized(@"diskmaintenance.reclaim-space", @"Reclaim Space…") action:@selector(maintainDisk:)],*compress=[self button:VIMLocalized(@"diskmaintenance.compress", @"Compress…") action:@selector(maintainDisk:)];compress.tag=1;
    BOOL running=[VIMSession isRunningMachine:self.machine];reclaim.enabled=compress.enabled=!running&&![VIMDiskMaintenance hasActiveOperation];
    reclaim.toolTip=VIMLocalized(@"diskmaintenance.rebuild-the-disk-image-to-recover-unused-storage", @"Rebuild the disk image to recover unused storage.");
    compress.toolTip=VIMLocalized(@"diskmaintenance.reclaim-unused-storage-and-compress-existing-data", @"Reclaim unused storage and compress existing data.");
    NSStackView *tools=[NSStackView stackViewWithViews:@[reclaim,compress]];tools.spacing=8;
    NSGridView *grid=[NSGridView gridViewWithViews:@[
        @[[NSTextField labelWithString:VIMLocalized(@"settings.source", @"Source:")],source],
        @[NSGridCell.emptyContentView,[self button:VIMLocalized(@"app.show-in-finder", @"Show in Finder") action:@selector(reveal:)]],
        @[[NSTextField labelWithString:VIMLocalized(@"diskmaintenance.capacity", @"Capacity:")],[NSTextField labelWithString:capacity?[NSString stringWithFormat:@"%g GB",capacity/(double)(1ULL<<30)]:VIMLocalized(@"settings.not-recorded", @"Not recorded")]],
        @[[NSTextField labelWithString:VIMLocalized(@"diskmaintenance.space-on-this-mac", @"Space on this Mac:")],[NSTextField labelWithString:size]],
        @[[NSTextField labelWithString:VIMLocalized(@"diskmaintenance.maintenance", @"Maintenance:")],tools]]];
    grid.columnSpacing=12;grid.rowSpacing=16;
    [grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;[grid columnAtIndex:1].xPlacement=NSGridCellPlacementLeading;
    return VIMColumn(@[grid]);
}
- (void)maintainDisk:(NSButton *)sender {
    if(self.maintenanceWindow||[VIMDiskMaintenance hasActiveOperation])return;
    if([VIMSession isRunningMachine:self.machine]){self.feedback.stringValue=VIMLocalized(@"diskmaintenance.shut-down-this-mac-before-maintaining-its-disk", @"Shut down this Mac before maintaining its disk.");return;}
    BOOL compressed=sender.tag==1;
    NSAlert *alert=[NSAlert new];alert.messageText=compressed?VIMLocalized(@"diskmaintenance.compress-this-mac-s-disk", @"Compress this Mac’s disk?"):VIMLocalized(@"diskmaintenance.reclaim-space-from-this-mac-s-disk", @"Reclaim space from this Mac’s disk?");
    alert.informativeText=[NSString stringWithFormat:compressed?
        VIMLocalized(@"diskmaintenance.reclaim-unused-image-storage-and-compress-existing-data-for-new-writes-stay", @"Reclaim unused image storage and compress existing data for “%@”. New writes stay uncompressed.\n\nThis needs temporary free space. Wait for the operation to finish and keep the drive connected."):
        VIMLocalized(@"diskmaintenance.recover-unused-image-storage-for-deleted-files-may-still-occupy-space-inside", @"Recover unused image storage for “%@”. Deleted files may still occupy space inside the image.\n\nThis needs temporary free space. Wait for the operation to finish and keep the drive connected."),self.machine.name];
    [alert addButtonWithTitle:compressed?VIMLocalized(@"diskmaintenance.compress-2", @"Compress"):VIMLocalized(@"diskmaintenance.reclaim-space-2", @"Reclaim Space")];[alert addButtonWithTitle:VIMLocalized(@"common.cancel", @"Cancel")];
    VIMPresentAlert(alert,self.window,^(NSModalResponse response){
        if(response==NSAlertFirstButtonReturn)[self runDiskMaintenance:compressed];
    });
}
- (void)runDiskMaintenance:(BOOL)compressed {
    if(self.maintenanceWindow||[VIMDiskMaintenance hasActiveOperation])return;
    NSURL *image=[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:@"Runtime/bin/qemu-img"];
    NSString *root=VIMDevelopmentSourceRoot();
    if(root.length) {
        NSString *runtime=[NSDictionary dictionaryWithContentsOfFile:[root stringByAppendingPathComponent:@"Build/RuntimeLocation.plist"]][@"Directory"];
        if(!runtime.length){VIMPresentError([NSError errorWithDomain:@"VirtualIntelMac.Runtime" code:1 userInfo:@{NSLocalizedDescriptionKey:VIMLocalized(@"vimmachinesettings.open-the-complete-virtual-intel-mac-app-to-maintain", @"Open the complete Virtual Intel Mac app to maintain its disks.")}],self.window);return;}
        image=[[NSURL fileURLWithPath:runtime] URLByAppendingPathComponent:@"qemu-img"];
    }
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,440,208) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    window.title=self.machine.name;window.releasedWhenClosed=NO;
    window.tabbingMode=NSWindowTabbingModeDisallowed;
    NSTextField *title=VIMText(compressed?VIMLocalized(@"diskmaintenance.compressing-disk-2", @"Compressing Disk"):VIMLocalized(@"diskmaintenance.reclaiming-disk-space-2", @"Reclaiming Disk Space"),YES);title.font=[NSFont systemFontOfSize:15 weight:NSFontWeightSemibold];
    NSTextField *machine=VIMText(self.machine.name,NO);machine.lineBreakMode=NSLineBreakByTruncatingMiddle;machine.maximumNumberOfLines=1;machine.toolTip=self.machine.name;
    NSStackView *identity=VIMColumn(@[title,machine]);identity.spacing=4;
    NSStackView *header=[NSStackView stackViewWithViews:@[VIMDiskSymbol(28),identity]];header.spacing=14;header.alignment=NSLayoutAttributeCenterY;
    NSTextField *status=VIMText(VIMLocalized(@"diskmaintenance.checking-disk", @"Checking disk…"),NO);status.textColor=NSColor.labelColor;
    NSTextField *percentage=[NSTextField labelWithString:@""];percentage.font=[NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular];
    percentage.textColor=NSColor.secondaryLabelColor;percentage.alignment=NSTextAlignmentRight;
    [percentage.widthAnchor constraintEqualToConstant:44].active=YES;
    NSStackView *statusRow=[NSStackView stackViewWithViews:@[status,percentage]];statusRow.spacing=12;statusRow.alignment=NSLayoutAttributeFirstBaseline;
    [status.widthAnchor constraintEqualToAnchor:statusRow.widthAnchor constant:-56].active=YES;
    NSTextField *explanation=VIMText(VIMLocalized(@"vimmachinesettings.the-original-disk-is-kept-until-the-operation-finishes", @"The original disk is kept until the operation finishes."),NO);explanation.font=[NSFont systemFontOfSize:11];
    NSProgressIndicator *progress=[NSProgressIndicator new];progress.style=NSProgressIndicatorStyleBar;progress.indeterminate=YES;
    progress.minValue=0;progress.maxValue=1;[progress startAnimation:nil];
    NSStackView *activity=VIMColumn(@[statusRow,progress]);activity.spacing=8;
    NSStackView *content=VIMColumn(@[header,activity,explanation]);content.spacing=20;content.translatesAutoresizingMaskIntoConstraints=NO;
    [window.contentView addSubview:content];
    [NSLayoutConstraint activateConstraints:@[[content.leadingAnchor constraintEqualToAnchor:window.contentView.leadingAnchor constant:24],
        [content.trailingAnchor constraintEqualToAnchor:window.contentView.trailingAnchor constant:-24],
        [content.topAnchor constraintEqualToAnchor:window.contentView.topAnchor constant:24],
        [content.bottomAnchor constraintEqualToAnchor:window.contentView.bottomAnchor constant:-24],
        [header.widthAnchor constraintEqualToAnchor:content.widthAnchor],
        [identity.widthAnchor constraintEqualToAnchor:header.widthAnchor constant:-50],
        [activity.widthAnchor constraintEqualToAnchor:content.widthAnchor],
        [statusRow.widthAnchor constraintEqualToAnchor:activity.widthAnchor],
        [progress.widthAnchor constraintEqualToAnchor:activity.widthAnchor],
        [explanation.widthAnchor constraintEqualToAnchor:content.widthAnchor]]];
    self.maintenanceWindow=window;
    NSError *error=nil;
    BOOL started=[VIMDiskMaintenance startForPackage:self.machine.URL identifier:self.machine.configuration[@"Identifier"] qemuImage:image compressed:compressed
        progress:^(NSString *phase,double fraction){
            status.stringValue=phase;percentage.stringValue=fraction<0?@"":[NSString stringWithFormat:@"%.0f%%",floor(fraction*100)];
            progress.accessibilityLabel=phase;
            progress.indeterminate=fraction<0;
            if(fraction<0)[progress startAnimation:nil];else {[progress stopAnimation:nil];progress.doubleValue=fraction;}
        } completion:^(NSDictionary *value,NSError *issue){
            [self.window endSheet:window];[window orderOut:nil];
            self.maintenanceWindow=nil;self.diskActivity=nil;
            [self diskMaintenanceFinished:value error:issue compressed:compressed];
        } error:&error];
    if(!started){self.maintenanceWindow=nil;VIMPresentError(error,self.window);return;}
    self.diskActivity=[VIMInstallationActivity beginWithReason:compressed?VIMLocalized(@"diskmaintenance.compressing-a-virtual-mac-disk", @"Compressing a virtual Mac disk"):VIMLocalized(@"diskmaintenance.reclaiming-virtual-mac-disk-space", @"Reclaiming virtual Mac disk space")];
    [self.window beginSheet:window completionHandler:nil];
}
- (void)diskMaintenanceFinished:(NSDictionary *)result error:(NSError *)failure compressed:(BOOL)compressed {
    [self readStorageSizes];[self render];
    [NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
    if(self.completion)self.completion();
    if(failure){VIMPresentError(failure,self.window);return;}
    NSAlert *finished=[NSAlert new];finished.messageText=[result[@"Changed"] boolValue]?(compressed?VIMLocalized(@"diskmaintenance.disk-compressed", @"Disk compressed"):VIMLocalized(@"diskmaintenance.space-reclaimed", @"Space reclaimed")):VIMLocalized(@"diskmaintenance.no-additional-space-to-reclaim", @"No additional space to reclaim");
    if([result[@"Changed"] boolValue]) {
        NSString *(^size)(NSNumber *)=^NSString *(NSNumber *bytes){return [NSByteCountFormatter stringFromByteCount:bytes.longLongValue countStyle:NSByteCountFormatterCountStyleFile];};
        NSNumber *saved=@([result[@"BeforeBytes"] unsignedLongLongValue]-[result[@"AfterBytes"] unsignedLongLongValue]);
        finished.informativeText=[NSString stringWithFormat:VIMLocalized(@"diskmaintenance.saved-for-the-virtual-disk-s-capacity-is-unchanged", @"Saved %@ for “%@”. The virtual disk’s capacity is unchanged."),size(saved),self.machine.name];
        NSGridView *summary=[NSGridView gridViewWithViews:@[@[VIMText(VIMLocalized(@"diskmaintenance.before", @"Before"),NO),VIMText(size(result[@"BeforeBytes"]),NO)],
            @[VIMText(VIMLocalized(@"diskmaintenance.after", @"After"),NO),VIMText(size(result[@"AfterBytes"]),YES)]]];summary.columnSpacing=24;summary.rowSpacing=8;
        [summary columnAtIndex:1].xPlacement=NSGridCellPlacementTrailing;finished.accessoryView=summary;
    } else finished.informativeText=[NSString stringWithFormat:VIMLocalized(@"diskmaintenance.already-uses-the-same-or-less-storage-its-original-disk-was-kept", @"“%@” already uses the same or less storage. Its original disk was kept."),self.machine.name];
    [finished addButtonWithTitle:VIMLocalized(@"common.ok", @"OK")];VIMPresentAlert(finished,self.window,nil);
}
- (NSView *)sharingContentForHost:(BOOL)host {
    BOOL files=self.hardwareIndex==0;
    NSDictionary *network=[VIMSession networkDetailsForMachine:self.machine];BOOL shared=[network[@"Mode"] isEqual:@"shared"];
    NSInteger hostVersion=NSProcessInfo.processInfo.operatingSystemVersion.majorVersion;
    NSString *hostSettings=hostVersion>=13?VIMLocalized(@"settings.system-settings-general-sharing", @"System Settings → General → Sharing"):VIMLocalized(@"settings.system-preferences-sharing", @"System Preferences → Sharing");
    if(host) {
        BOOL windowsFileSharing=files&&
            [@[@"tiger",@"leopard",@"snowleopard"] containsObject:self.machine.guest];
        NSString *sharingInstructions=windowsFileSharing?
            VIMLocalized(@"settings.1-on-this-mac-go-to-system-settings-general-sharing", @"1. On this Mac, go to System Settings → General → Sharing. Click the info button next to File Sharing and turn it on. Click Options, turn on SMB sharing, then select your account under Windows File Sharing and enter its password."):
            [NSString stringWithFormat:VIMLocalized(@"settings.1-on-this-mac-open-and-enable", @"1. On this Mac, open %@ and enable %@."),hostSettings,files?VIMLocalized(@"settings.file-sharing", @"File Sharing"):VIMLocalized(@"settings.remote-login", @"Remote Login")];
        NSString *username=[NSUserName() stringByReplacingOccurrencesOfString:@"'" withString:@"'\\''"];
        NSString *hostIP=network[@"HostAddress"];
        NSString *address=hostIP.length?(files?[@"smb://" stringByAppendingString:hostIP]:[NSString stringWithFormat:@"ssh -l '%@' %@",username,hostIP]):@"";
        if(!files&&self.machine.preMetalGuest&&hostIP.length)
            address=[NSString stringWithFormat:@"'/Library/Application Support/Virtual Intel Mac/GuestTools/guest-ssh' -l '%@' %@",username,hostIP];
        NSButton *copy=[self button:files?VIMLocalized(@"settings.copy-address", @"Copy Address"):VIMLocalized(@"settings.copy-command", @"Copy Command") action:@selector(copyConnection:)];copy.identifier=address;
        copy.enabled=address.length>0;
        NSURLComponents *connection=[NSURLComponents new];connection.scheme=files?@"smb":@"ssh";connection.host=hostIP;if(!files)connection.user=NSUserName();
        NSButton *open=[self button:files?VIMLocalized(@"settings.open-in-finder", @"Open in Finder"):VIMLocalized(@"settings.open-in-terminal", @"Open in Terminal") action:@selector(openGuestConnection:)];open.identifier=address.length?connection.URL.absoluteString:@"";open.enabled=[network[@"Connected"] boolValue]&&address.length;
        NSStackView *actions=[NSStackView stackViewWithViews:@[open,copy]];actions.spacing=12;
        return VIMColumn(@[VIMText(files?VIMLocalized(@"settings.open-this-mac-s-files-inside-the-virtual-mac", @"Open this Mac’s files inside the virtual Mac"):VIMLocalized(@"settings.connect-to-this-mac-from-the-virtual-mac", @"Connect to this Mac from the virtual Mac"),YES),
            VIMText(sharingInstructions,NO),
            [self button:VIMLocalized(@"settings.open-sharing-settings", @"Open Sharing Settings") action:@selector(openSharing:)],
            VIMText(files?VIMLocalized(@"settings.2-in-the-virtual-mac-s-finder-choose-go-connect", @"2. In the virtual Mac’s Finder, choose Go → Connect to Server and enter:"):VIMLocalized(@"settings.2-in-the-virtual-mac-s-terminal-run", @"2. In the virtual Mac’s Terminal, run:"),NO),
            VIMText(address.length?address:VIMLocalized(@"settings.start-the-virtual-mac-and-connect-its-network-to-discover", @"Start the virtual Mac and connect its network to discover this Mac’s address."),NO),actions,VIMText(VIMLocalized(@"settings.use-this-mac-s-account-and-password-not-the-virtual", @"Use this Mac’s account and password, not the virtual Mac’s."),NO)]);
    }
    NSString *key=files?@"FileSharingEnabled":@"SSHEnabled";
    NSString *portKey=files?@"FileSharingPort":@"SSHPort";
    NSDictionary *ports=VIMSuggestedForwardPorts(self.machine.URL.URLByDeletingLastPathComponent,self.machine.URL);
    NSNumber *port=self.machine.editableConfiguration[portKey]?:ports[portKey];
    NSButton *enabled=[self toggle:key title:files?VIMLocalized(@"settings.allow-connections-to-this-virtual-mac-s-shared-files", @"Allow connections to this virtual Mac’s shared files"):VIMLocalized(@"settings.allow-ssh-connections-to-this-virtual-mac", @"Allow SSH connections to this virtual Mac")];
    NSString *address=files?[VIMSMBService URLForMachine:self.machine].absoluteString:
        [NSString stringWithFormat:@"ssh -p %@ -l '%@' 127.0.0.1",port,self.guestUsername];
    NSString *guestIP=[network[@"GuestAddresses"] firstObject];
    if(!shared)address=guestIP.length?(files?[@"smb://" stringByAppendingString:guestIP]:[NSString stringWithFormat:@"ssh -l '%@' %@",self.guestUsername,guestIP]):@"";
    BOOL tigerSharing=files&&[self.machine.guest isEqual:@"tiger"];
    if(tigerSharing&&self.guestUsername.length&&(shared||guestIP.length)) {
        // A current host can mount Tiger's home share and transfer files,
        // but its server-browser request to Tiger's native Samba can fail.
        // Connect to the actual share rather than enumerating the server.
        NSURLComponents *home=[NSURLComponents new];home.scheme=@"smb";
        home.host=shared?@"127.0.0.1":guestIP;home.port=shared?port:nil;
        home.user=self.guestUsername;home.path=[@"/" stringByAppendingString:self.guestUsername];
        address=home.URL.absoluteString;
    }
    if(!files&&self.machine.preMetalGuest&&address.length){
        NSString *user=[self.guestUsername stringByReplacingOccurrencesOfString:@"'" withString:@"'\\''"];
        // Stock 13F34 advertises AES-GCM but its sshd aborts when using it.
        // Keep period crypto options and the known-host identity VM-scoped;
        // never weaken the user's global SSH configuration.
        address=[NSString stringWithFormat:@"ssh -o HostKeyAlgorithms=+ssh-rsa -c aes128-ctr -o HostKeyAlias=vim-%@ -p %@ -l '%@' '%@'",
            [self.machine.configuration[@"Identifier"] lowercaseString],shared?port:@22,user,shared?@"127.0.0.1":guestIP];
    }
    NSButton *copy=[self button:files?VIMLocalized(@"settings.copy-address", @"Copy Address"):VIMLocalized(@"settings.copy-command", @"Copy Command") action:@selector(copyConnection:)];copy.identifier=address;
    copy.enabled=address.length&&(files||self.guestUsername.length>0);
    NSURLComponents *connection=[NSURLComponents new];connection.scheme=files?@"smb":@"ssh";connection.host=@"127.0.0.1";connection.port=port;if(!files)connection.user=self.guestUsername;
    if(!shared){connection.host=guestIP;connection.port=nil;}
    NSButton *open=[self button:files?VIMLocalized(@"settings.open-in-finder", @"Open in Finder"):VIMLocalized(@"settings.open-in-terminal", @"Open in Terminal") action:@selector(openConnection:)];open.identifier=files||self.machine.preMetalGuest?address:(address.length?connection.URL.absoluteString:@"");open.enabled=[network[@"Connected"] boolValue]&&(!shared||enabled.state==NSControlStateValueOn)&&address.length&&(files||self.guestUsername.length>0);
    NSStackView *actions=[NSStackView stackViewWithViews:@[open,copy]];actions.spacing=12;
    NSString *enableSharing=tigerSharing?VIMLocalized(@"vimmachinesettings.1-in-the-virtual-mac-open-system-preferences-sharing", @"1. In the virtual Mac, open System Preferences → Sharing and turn on Windows Sharing for your account."):
        VIMLocalized(@"settings.1-in-the-virtual-mac-open-system-preferences-sharing-and", @"1. In the virtual Mac, open System Preferences → Sharing and turn on File Sharing. In Options, enable SMB for your account.");
    NSMutableArray *views=[NSMutableArray arrayWithArray:@[VIMText(files?VIMLocalized(@"settings.open-the-virtual-mac-s-files-on-this-mac", @"Open the virtual Mac’s files on this Mac"):VIMLocalized(@"settings.connect-to-the-virtual-mac-with-ssh", @"Connect to the virtual Mac with SSH"),YES),
        VIMText(files?enableSharing:VIMLocalized(@"settings.1-in-the-virtual-mac-open-system-preferences-sharing-and-2", @"1. In the virtual Mac, open System Preferences → Sharing and turn on Remote Login."),NO)]];
    if(files) {
        [views addObject:[self guestSettingsButton:VIMLocalized(@"settings.open-sharing-in-virtual-mac", @"Open Sharing in Virtual Mac…") pane:@"sharing"]];
    }
    if(!files) {
        NSButton *automatic=[self button:VIMLocalized(@"settings.set-up-remote-login", @"Set Up Remote Login…") action:@selector(enableGuestRemoteLogin:)];
        automatic.enabled=[VIMSession isRunningMachine:self.machine];
        automatic.toolTip=VIMLocalized(@"settings.uses-the-built-in-guest-tools-start-the-virtual-mac", @"Uses the built-in guest tools. Start the virtual Mac first.");
        [views addObject:automatic];
        NSTextField *username=[self field:@"GuestUsername" value:self.guestUsername];username.placeholderString=VIMLocalized(@"settings.guest-account-short-name", @"Guest account short name");
        username.accessibilityLabel=VIMLocalized(@"settings.guest-account-short-name", @"Guest account short name");[views addObject:username];
    }
    if(shared)[actions addArrangedSubview:[self button:VIMLocalized(@"settings.advanced", @"Advanced…") action:@selector(toggleAdvancedSharing:)]];
    [views addObjectsFromArray:@[VIMText(files?VIMLocalized(@"settings.2-on-this-mac-connect-using-the-virtual-mac-s", @"2. On this Mac, connect using the virtual Mac’s account and password:"):VIMLocalized(@"settings.2-run-this-command-in-terminal-on-this-mac", @"2. Run this command in Terminal on this Mac:"),NO),
        VIMText(!files&&!self.guestUsername.length?VIMLocalized(@"settings.enter-your-guest-account-s-short-name-above", @"Enter your guest account’s short name above."):(address.length?address:VIMLocalized(@"settings.start-the-virtual-mac-and-wait-for-its-network-address", @"Start the virtual Mac and wait for its network address.")),NO),actions]];
    if(shared&&enabled.state!=NSControlStateValueOn)[views addObject:VIMText(VIMLocalized(@"settings.local-connections-are-turned-off-enable-them-in-advanced-then", @"Local connections are turned off. Enable them in Advanced, then restart this Mac."),NO)];
    if(!shared)[views addObject:VIMText(VIMLocalized(@"settings.connect-directly-using-the-address-above-service-access-is-controlled", @"Connect directly using the address above. Service access is controlled by Sharing and Firewall settings inside the virtual Mac; local port forwarding is not used."),NO)];
    return VIMColumn(views);
}
- (NSView *)sharing {
    if(self.hardwareIndex==2) {
        NSView *pane=[NSView new];NSButton *enabled=[self toggle:@"ClipboardEnabled" title:VIMLocalized(@"settings.share-copied-text-with-this-mac", @"Share copied text with this Mac")];
        enabled.translatesAutoresizingMaskIntoConstraints=NO;[pane addSubview:enabled];
        [NSLayoutConstraint activateConstraints:@[[enabled.leadingAnchor constraintEqualToAnchor:pane.leadingAnchor constant:24],
            [enabled.centerYAnchor constraintEqualToAnchor:pane.centerYAnchor],
            [enabled.trailingAnchor constraintLessThanOrEqualToAnchor:pane.trailingAnchor constant:-20]]];
        NSTabView *tabs=[NSTabView new];tabs.tabViewType=NSTopTabsBezelBorder;
        NSTabViewItem *item=[[NSTabViewItem alloc] initWithIdentifier:@"guest"];
        item.label=VIMLocalized(@"settings.virtual-mac", @"Virtual Mac");item.view=pane;[tabs addTabViewItem:item];return VIMStyledConfigurationTabs(tabs);
    }
    NSTabView *tabs=[NSTabView new];tabs.identifier=@"SharingDirections";tabs.tabViewType=NSTopTabsBezelBorder;
    tabs.accessibilityLabel=VIMLocalized(@"settings.which-mac-to-connect-to", @"Which Mac to connect to");
    for(NSNumber *host in @[@YES,@NO]) {
        NSTabViewItem *item=[[NSTabViewItem alloc] initWithIdentifier:host.boolValue?@"host":@"guest"];
        item.label=host.boolValue?VIMLocalized(@"settings.this-mac", @"This Mac"):VIMLocalized(@"settings.virtual-mac", @"Virtual Mac");
        NSView *pane=[self sharingContentForHost:host.boolValue];VIMAlignFormBaselines(pane);
        item.view=VIMSharingScrollPane(pane);[tabs addTabViewItem:item];
    }
    [tabs selectTabViewItemAtIndex:self.hostFiles?0:1];tabs.delegate=self;return VIMStyledConfigurationTabs(tabs);
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
    NSButton *allowed=[NSButton checkboxWithTitle:files?VIMLocalized(@"settings.allow-connections-to-shared-files", @"Allow connections to shared files"):VIMLocalized(@"settings.allow-ssh-connections", @"Allow SSH connections") target:nil action:nil];allowed.state=c[key]?[c[key] boolValue]:YES;
    NSNumber *port=c[portKey]?:VIMSuggestedForwardPorts(self.machine.URL.URLByDeletingLastPathComponent,self.machine.URL)[portKey];
    NSTextField *field=[NSTextField textFieldWithString:port.stringValue];[field.widthAnchor constraintEqualToConstant:100].active=YES;
    NSGridView *grid=[NSGridView gridViewWithViews:@[@[[NSTextField labelWithString:VIMLocalized(@"settings.local-port", @"Local port:")],field]]];grid.columnSpacing=12;
    grid.rowAlignment=NSGridRowAlignmentFirstBaseline;
    [grid columnAtIndex:0].width=80;[grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;[grid columnAtIndex:1].xPlacement=NSGridCellPlacementLeading;
    [grid.widthAnchor constraintEqualToConstant:240].active=YES;
    NSTextField *explanation=VIMText(VIMLocalized(@"settings.accessible-only-from-this-mac-changes-take-effect-at-the", @"Accessible only from this Mac. Changes take effect at the next start."),NO);
    [explanation.widthAnchor constraintEqualToConstant:360].active=YES;
    VIMConfigurationSheet *sheet=[[VIMConfigurationSheet alloc] initWithTitle:files?VIMLocalized(@"settings.file-sharing-advanced", @"File Sharing — Advanced"):VIMLocalized(@"settings.remote-login-advanced", @"Remote Login — Advanced")
        content:VIMColumn(@[allowed,grid,explanation])];
    __weak typeof(self) weakSelf=self;
    sheet.save=^BOOL(NSWindow *window){
        NSScanner *scanner=[NSScanner scannerWithString:field.stringValue];long long value=0;NSError *error=nil;
        if(![scanner scanLongLong:&value]||!scanner.isAtEnd||value<1024||value>65535)
            error=[NSError errorWithDomain:@"VirtualIntelMac.Settings" code:1 userInfo:@{NSLocalizedDescriptionKey:VIMLocalized(@"settings.enter-a-local-port-from-1024-to-65535", @"Enter a local port from 1024 to 65535.")}];
        if(!error)[weakSelf.machine saveSettings:@{key:@(allowed.state==NSControlStateValueOn),portKey:@(value)} error:&error];
        if(error){VIMPresentError(error,window);return NO;}
        if(weakSelf.completion)weakSelf.completion();[weakSelf render];return YES;
    };[self presentEditor:sheet];
}
- (NSPopUpButton *)audioPopup:(BOOL)input {
    NSPopUpButton *popup=[NSPopUpButton new];popup.identifier=input?@"AudioInputDevice":@"AudioOutputDevice";
    popup.target=self;popup.action=@selector(audioChanged:);[popup.widthAnchor constraintEqualToConstant:240].active=YES;
    [popup addItemWithTitle:VIMLocalized(@"app.default", @"Default")];popup.lastItem.representedObject=@"default";
    NSString *selected=VIMAudioSelection(self.machine.editableConfiguration,input);
    for(NSDictionary *device in VIMHostAudioDevices(input)) {
        [popup addItemWithTitle:device[@"Name"]];popup.lastItem.representedObject=device[@"UID"];
    }
    NSMenuItem *choice=nil;for(NSMenuItem *item in popup.itemArray)if([item.representedObject isEqual:selected])choice=item;
    if(!choice&&![selected isEqual:@"disabled"]){[popup addItemWithTitle:VIMLocalized(@"settings.selected-device-disconnected", @"Selected Device (Disconnected)")];choice=popup.lastItem;choice.representedObject=selected;}
    [popup.menu addItem:NSMenuItem.separatorItem];
    [popup addItemWithTitle:VIMLocalized(@"settings.disconnected", @"Disconnected")];popup.lastItem.representedObject=@"disabled";
    if([selected isEqual:@"disabled"])choice=popup.lastItem;
    [popup selectItem:choice];return popup;
}
- (NSPopUpButton *)cameraPopup {
    NSPopUpButton *popup=[NSPopUpButton new];popup.identifier=@"CameraDevice";
    [popup.widthAnchor constraintEqualToConstant:240].active=YES;
    [popup addItemWithTitle:VIMLocalized(@"app.default", @"Default")];popup.lastItem.representedObject=@"default";
    NSString *selected=VIMCameraSelection(self.machine.editableConfiguration);
    for(NSDictionary *device in VIMHostCameraDevices()) {
        [popup addItemWithTitle:device[@"Name"]];popup.lastItem.representedObject=device[@"UID"];
    }
    NSMenuItem *choice=popup.itemArray.firstObject;for(NSMenuItem *item in popup.itemArray)if([item.representedObject isEqual:selected])choice=item;
    if(![selected isEqual:@"disabled"]&&![choice.representedObject isEqual:selected]){[popup addItemWithTitle:VIMLocalized(@"settings.selected-device-unavailable", @"Selected Camera (Unavailable)")];choice=popup.lastItem;choice.representedObject=selected;}
    [popup selectItem:choice];
    popup.target=self;popup.action=@selector(cameraChanged:);
    return popup;
}
- (NSPopUpButton *)cameraModePopup {
    NSPopUpButton *popup=[NSPopUpButton new];popup.identifier=@"CameraMode";
    [popup.widthAnchor constraintEqualToConstant:240].active=YES;
    [popup addItemWithTitle:VIMLocalized(@"settings.camera-mode.off", @"No Picture")];popup.lastItem.representedObject=@"off";
    [popup addItemWithTitle:VIMLocalized(@"settings.autosense", @"Autosense")];popup.lastItem.representedObject=@"autosense";
    [popup addItemWithTitle:VIMLocalized(@"settings.constant-output", @"Constant Output")];popup.lastItem.representedObject=@"constant";
    NSString *mode=VIMCameraMode(self.machine.editableConfiguration);
    if(VIMCameraTestPatternOffered(mode)){
        [popup.menu addItem:NSMenuItem.separatorItem];
        [popup addItemWithTitle:VIMLocalized(@"settings.test-pattern", @"Test Pattern")];popup.lastItem.representedObject=@"testpattern";
    }
    for(NSMenuItem *item in popup.itemArray)if([item.representedObject isEqual:mode])[popup selectItem:item];
    popup.target=self;popup.action=@selector(cameraChanged:);
    return popup;
}
- (void)cameraChanged:(NSPopUpButton *)sender {
    NSString *value=sender.selectedItem.representedObject;
    NSDictionary *config=self.machine.editableConfiguration;
    if(!VIMCameraDeviceAdvertised(config))return;
    if([sender.identifier isEqual:@"CameraMode"]&&[VIMCameraMode(config) isEqual:value])return;
    if([sender.identifier isEqual:@"CameraDevice"]&&[VIMCameraSelection(config) isEqual:value])return;
    if([self commitSettings:@{sender.identifier:value}]) {
        if([sender.identifier isEqual:@"CameraMode"])[VIMMachineSettings handleCameraModeSelection:value fromWindow:self.window];
        [VIMSession cameraPreferencesChangedForMachine:self.machine];
        [self render];
    }
}
- (void)cameraConnectionChanged:(NSSwitch *)sender {
    BOOL connect=sender.state==NSControlStateValueOn;
    if(connect==VIMCameraDeviceAdvertised(self.machine.editableConfiguration))return;
    if(![self commitSettings:@{@"CameraDevice":connect?@"default":@"disabled"}]){[self render];return;}
    if(connect&&VIMCameraModeUsesHostCamera(VIMCameraMode(self.machine.editableConfiguration)))[VIMMachineSettings requestCameraAccessFromWindow:self.window];
    [VIMSession cameraPreferencesChangedForMachine:self.machine];
    [NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
    [self render];
    [VIMSession offerCameraRestartForMachine:self.machine window:self.window];
}
- (void)cameraChimesChanged:(NSButton *)sender {
    [self commitSettings:@{@"CameraChimesEnabled":@(sender.state==NSControlStateValueOn)}];
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
            NSAlert *alert=[NSAlert new];alert.messageText=VIMLocalized(@"settings.restart-to-change-the-bridge-interface", @"Restart to change the bridge interface");alert.informativeText=VIMLocalized(@"settings.your-selection-is-saved-for-the-next-start", @"Your selection is saved for the next start.");[alert addButtonWithTitle:VIMLocalized(@"common.ok", @"OK")];VIMPresentAlert(alert,self.window,nil);
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
    NSButton *generate=[self button:VIMLocalized(@"ui.generate", @"Generate") action:@selector(generateMAC:)];generate.cell.representedObject=mac;
    NSStackView *macRow=[NSStackView stackViewWithViews:@[mac,generate]];macRow.spacing=12;
    macRow.alignment=NSLayoutAttributeFirstBaseline;
    NSString *model=VIMNetworkDeviceModel(self.machine.guest,self.machine.editableConfiguration);
    BOOL virtio=[model isEqual:@"virtio-net-pci"],realtek=[model isEqual:@"rtl8139"];
    NSPopUpButton *type=[NSPopUpButton new];[type addItemWithTitle:virtio?@"VirtIO Network":realtek?@"Realtek RTL8139 Fast Ethernet":@"Intel 82545EM Gigabit Ethernet"];
    type.toolTip=virtio?@"QEMU virtio-net-pci · PCI 1AF4:1041":realtek?@"QEMU rtl8139 · PCI 10EC:8139":@"QEMU e1000-82545em · PCI 8086:100F";
    NSGridView *grid=[NSGridView gridViewWithViews:@[@[[NSTextField labelWithString:@"MAC:"],macRow],@[[NSTextField labelWithString:VIMLocalized(@"settings.type", @"Type:")],type]]];grid.rowSpacing=16;grid.columnSpacing=12;[grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;
    // A stack's bounds and a popup's bezel are not text baselines. Align the
    // captions with the actual editable/control text, not their outer frames.
    for(NSInteger row=0;row<2;row++) {
        NSGridCell *caption=[grid cellAtColumnIndex:0 rowIndex:row];
        caption.yPlacement=NSGridCellPlacementNone;
        caption.customPlacementConstraints=@[[caption.contentView.firstBaselineAnchor constraintEqualToAnchor:(row==0?mac:type).firstBaselineAnchor]];
    }
    VIMConfigurationSheet *sheet=[[VIMConfigurationSheet alloc] initWithTitle:VIMLocalized(@"ui.network-advanced", @"Network — Advanced") content:VIMColumn(@[grid,VIMText(VIMLocalized(@"settings.changes-take-effect-the-next-time-this-mac-starts", @"Changes take effect the next time this Mac starts."),NO)])];
    __weak typeof(self) weakSelf=self;
    sheet.save=^BOOL(NSWindow *window){
        NSError *error=nil;
        if(!VIMNetworkMACAddressIsValid(mac.stringValue))error=[NSError errorWithDomain:@"VirtualIntelMac.Settings" code:1 userInfo:@{NSLocalizedDescriptionKey:VIMLocalized(@"settings.enter-a-valid-mac-address-such-as-52-54-00", @"Enter a valid MAC address, such as 52:54:00:12:34:56. Use a unicast address unique to this network.")}];
        if(!error)[weakSelf.machine saveSettings:@{@"MACAddress":mac.stringValue.uppercaseString} error:&error];
        if(error){VIMPresentError(error,window);return NO;}if(weakSelf.completion)weakSelf.completion();[weakSelf render];return YES;
    };[self presentEditor:sheet];
}
- (void)opticalChanged:(NSPopUpButton *)sender {
    NSInteger action=sender.selectedItem.tag;[sender selectItemAtIndex:0];
    if(action==1)[VIMSession chooseImageForMachine:self.machine slot:(NSUInteger)sender.tag window:self.window];
    else if(action==2)[VIMSession ejectImageForMachine:self.machine slot:(NSUInteger)sender.tag window:self.window];
}
- (NSButton *)guestSettingsButton:(NSString *)title pane:(NSString *)pane {
    NSButton *button=[self button:title action:@selector(openGuestSettings:)];button.identifier=pane;
    button.enabled=[VIMSession isRunningMachine:self.machine];
    button.toolTip=VIMLocalized(@"settings.uses-the-built-in-guest-tools-start-the-virtual-mac-2", @"Uses the built-in guest tools. Start the virtual Mac and sign in first.");return button;
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
        NSAlert *alert=[NSAlert new];alert.messageText=VIMLocalized(@"settings.remote-login-is-on", @"Remote Login is on");
        BOOL restart=![self.machine.configuration[@"SSHEnabled"] boolValue]||
            ![self.machine.configuration[@"SSHPort"] isEqual:self.machine.editableConfiguration[@"SSHPort"]];
        alert.informativeText=restart?VIMLocalized(@"settings.remote-login-is-enabled-in-the-virtual-mac-you-previously", @"Remote Login is enabled in the virtual Mac. You previously turned off or changed its local connection port; restart the virtual Mac to apply that change, then use the command shown here."):
            VIMLocalized(@"settings.remote-login-is-enabled-use-the-command-shown-here-with", @"Remote Login is enabled. Use the command shown here with your virtual Mac’s account and password—there’s no need to restart.");
        [alert addButtonWithTitle:VIMLocalized(@"common.ok", @"OK")];VIMPresentAlert(alert,self.window,nil);
    }];
}
- (void)render {
    if(self.maintenanceWindow)return;
    // AppKit can finish laying out an outgoing table during a section switch.
    // Retire its callbacks before replacing the body; each table also owns
    // its immutable row model instead of consulting the new section's list.
    self.sidebar.delegate=nil;self.sidebar.dataSource=nil;self.sidebar=nil;
    BOOL hasRestore=[self.section isEqual:@"Hardware"]&&[@[@0,@1,@3,@4,@5] containsObject:@(self.hardwareIndex)];
    CGFloat height=[self.section isEqual:@"General"]?380:([self.section isEqual:@"Hardware"]?390:490);
    // All panes share one width; long popup titles must not resize the window.
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
        table.columnAutoresizingStyle=NSTableViewLastColumnOnlyAutoresizingStyle;table.accessibilityLabel=[self.section isEqual:@"Hardware"]?VIMLocalized(@"settings.hardware-categories", @"Hardware categories"):VIMLocalized(@"settings.sharing-categories", @"Sharing categories");
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
        NSBox *box=[NSBox new];box.identifier=@"ConfigurationDetailFrame";box.boxType=NSBoxCustom;box.borderWidth=.5;box.borderColor=NSColor.separatorColor;box.cornerRadius=4;
        if(@available(macOS 26.0,*))box.fillColor=NSColor.controlBackgroundColor;
        else box.fillColor=NSColor.clearColor;
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
    // A wide footer must not alter a form's leading edge or distribute spare
    // width into its caption column when the CPU backend changes.
    if([pane isKindOfClass:NSStackView.class])for(NSView *view in ((NSStackView *)pane).arrangedSubviews)
        if([view isKindOfClass:NSGridView.class])[view.widthAnchor constraintEqualToAnchor:pane.widthAnchor].active=YES;
    VIMAlignFormBaselines(pane);
    pane.translatesAutoresizingMaskIntoConstraints=NO;[document addSubview:pane];
    // Leave the native text-field focus ring inside the clip view. General's
    // fields fill their form; clipping at that exact edge cuts off the ring.
    CGFloat focusMargin=[self.section isEqual:@"General"]?4:0;
    [NSLayoutConstraint activateConstraints:@[[scroll.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:left],
        [scroll.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-24],
        [scroll.topAnchor constraintEqualToAnchor:content.topAnchor constant:24],
        [scroll.bottomAnchor constraintEqualToAnchor:self.feedback.topAnchor constant:hasRestore?-46:-12],
        [self.feedback.leadingAnchor constraintEqualToAnchor:scroll.leadingAnchor],
        [self.feedback.trailingAnchor constraintEqualToAnchor:scroll.trailingAnchor],
        [self.feedback.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-16],
        [document.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor],
        [pane.leadingAnchor constraintEqualToAnchor:document.leadingAnchor constant:focusMargin],
        [pane.trailingAnchor constraintEqualToAnchor:document.trailingAnchor constant:-focusMargin],
        [pane.topAnchor constraintEqualToAnchor:document.topAnchor constant:6],
        [pane.bottomAnchor constraintEqualToAnchor:document.bottomAnchor constant:-6]]];
    if(hasRestore) {
        NSButton *restore=[self button:VIMLocalized(@"settings.restore-defaults", @"Restore Defaults") action:@selector(restoreDefaults:)];restore.translatesAutoresizingMaskIntoConstraints=NO;[content addSubview:restore];
        [NSLayoutConstraint activateConstraints:@[[restore.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-32],[restore.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-30]]];
    }
    if([self.section isEqual:@"General"]) {
        [content layoutSubtreeIfNeeded];
        CGFloat fitting=ceil(pane.fittingSize.height)+12+24+12+ceil(self.feedback.fittingSize.height)+16;
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
- (NSArray *)categories {return [self.section isEqual:@"Hardware"]?@[@"CPU & Memory",@"Graphics",@"Mouse & Keyboard",@"Network",@"Sound",@"Camera",@"Hard Disk",@"CD/DVD"]:@[@"File Sharing",@"Remote Login",@"Clipboard"];}
- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView {return [(VIMConfigurationSidebar *)tableView categories].count;}
- (NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)column row:(NSInteger)row {
    NSArray *categories=[(VIMConfigurationSidebar *)tableView categories];
    if(row<0||row>=(NSInteger)categories.count)return nil;
    NSTableCellView *cell=[NSTableCellView new];NSString *title=categories[row];
    NSString *symbol=@{@"CPU & Memory":@"cpu",@"Graphics":@"display",@"Sound":@"speaker.wave.2",@"Camera":@"camera",@"Network":@"network",@"Hard Disk":@"internaldrive",@"Mouse & Keyboard":@"keyboard",@"File Sharing":@"folder",@"Remote Login":@"terminal",@"CD/DVD":@"opticaldisc",@"Clipboard":@"doc.on.clipboard"}[title];
    NSColor *color=@{@"CPU & Memory":NSColor.systemBlueColor,@"Graphics":NSColor.systemIndigoColor,
        @"Sound":NSColor.systemPinkColor,@"Camera":NSColor.systemOrangeColor,@"Network":NSColor.systemBlueColor,
        @"Hard Disk":NSColor.systemGrayColor,@"Mouse & Keyboard":NSColor.darkGrayColor,
        @"File Sharing":NSColor.systemBlueColor,@"Remote Login":NSColor.darkGrayColor,@"Clipboard":NSColor.systemOrangeColor,
        @"CD/DVD":NSColor.systemTealColor}[title]?:NSColor.systemBlueColor;
    NSView *icon=[[VIMSettingsIcon alloc] initWithSymbol:symbol color:color glyphSize:[title isEqual:@"Clipboard"]?16:20];
    NSTextField *label=[NSTextField labelWithString:VIMLocalizedLabel(title)];label.font=[NSFont systemFontOfSize:13];
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
        if([value rangeOfCharacterFromSet:allowed.invertedSet].location!=NSNotFound){self.feedback.stringValue=VIMLocalized(@"settings.use-the-guest-account-s-short-name-letters-numbers-dots", @"Use the guest account’s short name (letters, numbers, dots, hyphens or underscores).");self.invalidField=YES;return;}
        if([value isEqual:self.guestUsername]){self.feedback.stringValue=@"";return;}
        self.guestUsername=value;[NSUserDefaults.standardUserDefaults setObject:value forKey:[@"VIMGuestUsername-" stringByAppendingString:self.machine.URL.path]];
        dispatch_async(dispatch_get_main_queue(),^{[self render];});return;
    }
    if([key isEqual:@"Name"]) {
        if([field.stringValue isEqual:self.machine.pendingName?:self.machine.name]){self.feedback.stringValue=@"";return;}
        if(![self.machine rename:field.stringValue error:&error]){self.feedback.stringValue=error.localizedDescription;self.invalidField=YES;}
        else {self.feedback.stringValue=@"";self.window.title=[NSString stringWithFormat:@"“%@” Configuration",self.machine.pendingName?:self.machine.name];if(self.completion)self.completion();}
        return;
    }
    NSScanner *scanner=[NSScanner scannerWithString:field.stringValue];long long value=0;
    BOOL port=[key hasSuffix:@"Port"];
    BOOL megabytes=[key isEqual:@"MemoryMiB"];
    if(![scanner scanLongLong:&value]||!scanner.isAtEnd||value<(port||megabytes?1024:1)||value>(port?65535:(megabytes?NSProcessInfo.processInfo.physicalMemory/(1024*1024):([key isEqual:@"CPUCount"]?NSProcessInfo.processInfo.processorCount:1024)))) {
        self.feedback.stringValue=port?VIMLocalized(@"settings.enter-a-local-port-from-1024-65535", @"Enter a local port from 1024–65535."):[NSString stringWithFormat:VIMLocalized(@"settings.use-up-to-lu-processors-or-llu-mb-of-memory", @"Use up to %lu processors or %llu MB of memory."),(unsigned long)NSProcessInfo.processInfo.processorCount,NSProcessInfo.processInfo.physicalMemory/(1024ULL*1024)];self.invalidField=YES;return;
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
- (void)cpuBackendChanged:(NSPopUpButton *)sender {
#if defined(__arm64__)
    if([self commitSettings:@{@"CPUBackend":sender.indexOfSelectedItem==0?@"hybrid":@"tcg"}])[self render];
#else
    // Intel has one backend; choosing it must not replace an ARM-side preference.
    (void)sender;
#endif
}
- (void)inputPreferenceChanged:(NSPopUpButton *)sender {
    NSArray *values=[sender.identifier isEqual:@"MouseRegionAccommodation"]?@[@"automatic",@"on",@"off"]:@[@"control-option",@"command-option"];
    if([self commitSettings:@{sender.identifier:values[sender.indexOfSelectedItem]}])
        [VIMSession inputPreferencesChangedForMachine:self.machine];
}
- (void)memoryStepped:(NSStepper *)sender {self.memoryValue.integerValue=sender.integerValue;self.memorySlider.doubleValue=sender.doubleValue/1024;[self commitSettings:@{@"MemoryMiB":@(sender.integerValue)}];}
- (void)memorySlid:(NSSlider *)sender {
    NSInteger mib=MIN(NSProcessInfo.processInfo.physicalMemory/(1024*1024),1024*(NSInteger)llround(sender.doubleValue));self.memoryValue.integerValue=mib;
    [self commitSettings:@{@"MemoryMiB":@(mib)}];
}
- (void)retinaChanged:(NSButton *)sender {if([self commitSettings:@{@"DisplayMode":@[@"space",@"retina",@"scaled"][sender.tag],@"RetinaEnabled":@(sender.tag!=2)}])[self render];}
- (void)openGLRendererChanged:(NSPopUpButton *)sender {
    if([self commitSettings:@{@"PassthroughOpenGLEnabled":@(sender.indexOfSelectedItem==0)}])[self render];
}
- (void)scalingFilterChanged:(NSPopUpButton *)sender {
    if([self commitSettings:@{@"DisplayScalingFilter":sender.indexOfSelectedItem==0?@"nearest":@"linear"}])
        [VIMSession displayScalingPreferencesChangedForMachine:self.machine];
}
- (void)restoreDefaults:(id)sender {
    if(self.hardwareIndex==3) {
        if(![self commitSettings:@{@"MACAddress":@"",@"BridgeInterface":@""}])return;
        [VIMSession changeNetworkMode:@"shared" connected:YES machine:self.machine window:self.window completion:^(NSError *error){if(error)VIMPresentError(error,self.window);[self render];}];return;
    }
    BOOL retina=VIMGuestSupportsRetinaDisplay(self.machine.editableConfiguration);
    NSDictionary *settings=self.hardwareIndex==0?@{@"CPUCount":@(MIN(4,NSProcessInfo.processInfo.processorCount)),@"MemoryMiB":@4096}:(self.hardwareIndex==1?@{@"DisplayMode":retina?@"retina":@"scaled",@"RetinaEnabled":@(retina),@"DisplayScalingFilter":retina?@"linear":@"nearest"}:(self.hardwareIndex==5?@{@"CameraDevice":@"disabled",@"CameraMode":@"off",@"CameraChimesEnabled":@YES}:@{@"AudioEnabled":@YES,@"AudioInputDevice":@"default",@"AudioOutputDevice":@"default"}));
    if([self commitSettings:settings]) {
        if(self.hardwareIndex==1)[VIMSession displayScalingPreferencesChangedForMachine:self.machine];
        if(self.hardwareIndex==3)[VIMSession setNetworkConnected:YES machine:self.machine completion:^(NSError *error){if(error)VIMPresentError(error,self.window);}];
        if(self.hardwareIndex==5)[VIMSession cameraPreferencesChangedForMachine:self.machine];
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
- (BOOL)windowShouldClose:(NSWindow *)sender {if(self.maintenanceWindow)return NO;[sender makeFirstResponder:nil];[self saveDescription];return !self.invalidField;}
- (void)reveal:(id)sender {[NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[self.machine.URL]];}
- (void)chooseDisc:(id)sender {[VIMSession chooseOpticalMediaForMachine:self.machine window:self.window];}
- (void)ejectDisc:(id)sender {[VIMSession ejectOpticalMediaForMachine:self.machine window:self.window];}
- (void)copyConnection:(NSButton *)sender {[NSPasteboard.generalPasteboard clearContents];[NSPasteboard.generalPasteboard setString:sender.identifier forType:NSPasteboardTypeString];}
- (void)openConnection:(NSButton *)sender {
    if(self.hardwareIndex==1&&self.machine.preMetalGuest){
        // Terminal's native profile document opens the interactive connection
        // without AppleEvents permission or global ssh_config edits. There is
        // no credential or generated shell script in this document.
        NSURL *directory=[[NSFileManager.defaultManager URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask].firstObject URLByAppendingPathComponent:@"com.mac.virtualintelmac/Connections" isDirectory:YES];
        NSURL *profile=[directory URLByAppendingPathComponent:[self.machine.configuration[@"Identifier"] stringByAppendingPathExtension:@"terminal"]];NSError *failure=nil;
        NSDictionary *settings=@{@"type":VIMLocalized(@"vimmachinesettings.window-settings", @"Window Settings"),@"name":self.machine.name,@"CommandString":sender.identifier,@"RunCommandAsShell":@NO,@"ProfileCurrentVersion":@2.08};
        if(![NSFileManager.defaultManager createDirectoryAtURL:directory withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:&failure]||![settings writeToURL:profile error:&failure]||![NSWorkspace.sharedWorkspace openURL:profile])
            VIMPresentError(failure?:[NSError errorWithDomain:@"VirtualIntelMac.RemoteLogin" code:1 userInfo:@{NSLocalizedDescriptionKey:VIMLocalized(@"settings.terminal-couldn-t-open-this-connection-use-copy-command-and", @"Terminal couldn’t open this connection. Use Copy Command and paste it into Terminal.")}],self.window);
        return;
    }
    // Finder connects over the VM's network, not the guest-control socket.
    // Let Finder report SMB errors even if guest tools are busy or unavailable.
    NSURL *url=[NSURL URLWithString:sender.identifier];
    if(url)[NSWorkspace.sharedWorkspace openURL:url];
}
- (void)openGuestConnection:(NSButton *)sender {
    NSString *title=sender.title;sender.enabled=NO;sender.title=VIMLocalized(@"settings.opening", @"Opening…");
    [VIMSession openHostConnection:[NSURL URLWithString:sender.identifier] inMachine:self.machine completion:^(NSError *error){
        sender.title=title;sender.enabled=YES;if(error)VIMPresentError(error,self.window);
    }];
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
        controller.hardwareIndex=cycle%controller.categories.count;[controller render];
        [controller.window.contentView layoutSubtreeIfNeeded];
        NSScrollView *scroll=controller.sidebar.enclosingScrollView;
        if(scroll.verticalScrollElasticity!=NSScrollElasticityNone||scroll.horizontalScrollElasticity!=NSScrollElasticityNone||
           scroll.hasVerticalScroller||scroll.hasHorizontalScroller||
           controller.sidebar.categories.count!=controller.categories.count||
           fabs(NSWidth(controller.window.contentView.bounds)-660)>.5||
           !VIMConfigurationFormLayoutIsValid(controller.window.contentView)){step=nil;completion(NO);return;}
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
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.Configuration" code:1 userInfo:@{NSLocalizedDescriptionKey:VIMLocalized(@"settings.open-this-mac-s-configuration-before-capturing-its-preview", @"Open this Mac’s configuration before capturing its preview.")}];
    return NO;
}
+ (void)requestMicrophoneAccessFromWindow:(NSWindow *)window {
    if([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]==AVAuthorizationStatusNotDetermined) {
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL granted){
            if(!granted)dispatch_async(dispatch_get_main_queue(),^{NSAlert *alert=[NSAlert new];alert.messageText=VIMLocalized(@"settings.microphone-access-is-turned-off", @"Microphone access is turned off");alert.informativeText=VIMLocalized(@"settings.you-can-allow-virtual-intel-mac-to-use-this-mac", @"You can allow Virtual Intel Mac to use this Mac’s microphone in Privacy settings.");[alert addButtonWithTitle:VIMLocalized(@"common.ok", @"OK")];VIMPresentAlert(alert,window,nil);});
        }];
    } else [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone"]];
}
+ (void)requestCameraAccessFromWindow:(NSWindow *)window {
    if([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]==AVAuthorizationStatusNotDetermined) {
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL granted){
            dispatch_async(dispatch_get_main_queue(),^{
                if(granted)[VIMSession cameraAccessChanged];
                else {NSAlert *alert=[NSAlert new];alert.messageText=VIMLocalized(@"settings.camera-access-is-turned-off", @"Camera access is turned off");alert.informativeText=VIMLocalized(@"settings.you-can-allow-virtual-intel-mac-to-use-this-mac-s-camera", @"You can allow Virtual Intel Mac to use this Mac’s camera in Privacy settings.");[alert addButtonWithTitle:VIMLocalized(@"common.ok", @"OK")];VIMPresentAlert(alert,window,nil);}
            });
        }];
    } else if([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]!=AVAuthorizationStatusAuthorized)
        [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Camera"]];
}
+ (void)handleCameraModeSelection:(NSString *)mode fromWindow:(NSWindow *)window {
    void (^requestAccess)(void)=^{
        if(VIMCameraModeUsesHostCamera(mode)&&[AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]==AVAuthorizationStatusNotDetermined)
            [self requestCameraAccessFromWindow:window];
    };
    if([mode isEqual:@"autosense"]&&![NSUserDefaults.standardUserDefaults boolForKey:@"VIMAutosenseOnboardingSeen"])
        [self presentAutosenseOnboardingFromWindow:window completion:requestAccess];
    else requestAccess();
}
+ (void)presentAutosenseOnboardingFromWindow:(NSWindow *)window completion:(void (^)(void))completion {
    static VIMAutosenseOnboardingSheet *presented;
    if(presented||!window){if(completion)completion();return;}
    VIMAutosenseOnboardingSheet *sheet=[VIMAutosenseOnboardingSheet new];
    presented=sheet;
    [window beginSheet:sheet.window completionHandler:^(NSModalResponse response){
        (void)response;presented=nil;
        [NSUserDefaults.standardUserDefaults setBool:YES forKey:@"VIMAutosenseOnboardingSeen"];
        if(completion)completion();
    }];
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
    if(!windows)windows=[NSMutableDictionary dictionary];NSString *key=[[[NSUUID alloc] initWithUUIDString:machine.configuration[@"Identifier"]] UUIDString]?:machine.URL.URLByResolvingSymlinksInPath.path;
    VIMConfigurationWindow *controller=windows[key];
    if(!controller){controller=[[VIMConfigurationWindow alloc] initWithMachine:machine];windows[key]=controller;}
    else if(!controller.window.visible){controller.machine=machine;controller.invalidField=NO;[controller render];}
    controller.completion=completion;
    if(controller.window.miniaturized)[controller.window deminiaturize:nil];
    [controller showWindow:nil];[controller.window makeKeyAndOrderFront:nil];
}
@end
