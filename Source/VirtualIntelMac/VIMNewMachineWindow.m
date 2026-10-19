#import "VIMNewMachineWindow.h"
#import "VIMMachine.h"
#import "VIMLibraryLocation.h"
#import "VIMSession.h"
#import "VIMHostSupport.h"
#import "VIMInstallerWindow.h"
#import "VIMInstallerCatalog.h"
#import "VIMInstallerAssembly.h"
#import "VIMDownload.h"
#import "VIMAlerts.h"
#import <CommonCrypto/CommonDigest.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <sys/sysctl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

@interface VIMAssistantPage : NSView
@end
@implementation VIMAssistantPage
- (void)drawRect:(NSRect)dirtyRect {[[NSColor.windowBackgroundColor blendedColorWithFraction:.8 ofColor:NSColor.controlBackgroundColor] setFill];NSRectFill(dirtyRect);}
@end

// A single native paragraph, with inline attachments and links, not button rows.
// Keep assistant prose non-selectable while preserving link hit testing.
@interface VIMRecommendationParagraph : NSTextView
@end
@implementation VIMRecommendationParagraph
- (void)mouseDown:(NSEvent *)event {
    NSPoint point=[self convertPoint:event.locationInWindow fromView:nil];
    point.x-=self.textContainerOrigin.x;point.y-=self.textContainerOrigin.y;
    CGFloat fraction=0;
    NSUInteger index=[self.layoutManager characterIndexForPoint:point inTextContainer:self.textContainer fractionOfDistanceBetweenInsertionPoints:&fraction];
    if(index>=self.textStorage.length)return;
    NSRange range;id link=[self.textStorage attribute:NSLinkAttributeName atIndex:index effectiveRange:&range];
    if(!link)return;
    NSRange glyphs=[self.layoutManager glyphRangeForCharacterRange:range actualCharacterRange:NULL];
    if(!NSPointInRect(point,[self.layoutManager boundingRectForGlyphRange:glyphs inTextContainer:self.textContainer]))return;
    [NSWorkspace.sharedWorkspace openURL:[link isKindOfClass:NSURL.class]?link:[NSURL URLWithString:link]];
}
@end

static NSTextField *AssistantText(NSString *text, CGFloat size, NSFontWeight weight) {
    NSTextField *field=[NSTextField wrappingLabelWithString:text];
    field.font=[NSFont systemFontOfSize:size weight:weight];field.textColor=NSColor.labelColor;
    field.selectable=NO;
    return field;
}
static void AddAssistantView(NSView *parent,NSView *view) {
    view.translatesAutoresizingMaskIntoConstraints=NO;[parent addSubview:view];
}
static NSURL *InstallerDownloadDirectory(NSURL *source,NSString *library) {
    NSData *identity=[source.absoluteString dataUsingEncoding:NSUTF8StringEncoding];unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(identity.bytes,(CC_LONG)identity.length,digest);NSMutableString *key=[NSMutableString string];
    for(unsigned i=0;i<sizeof(digest);i++)[key appendFormat:@"%02x",digest[i]];
    return [[VIMLibraryLocation downloadsURLForMachines:[NSURL fileURLWithPath:library]] URLByAppendingPathComponent:key];
}
// The whole row is one accessible button, not nested labels that swallow clicks.
@interface VIMSystemOption : NSButton
@property NSImage *roundel;
@property NSString *benefit;
@property SEL doubleAction;
@end
@implementation VIMSystemOption
- (void)setState:(NSControlStateValue)state {[super setState:state];self.needsDisplay=YES;}
- (BOOL)acceptsFirstMouse:(NSEvent *)event {return YES;}
- (void)performClick:(id)sender {
    if(!self.enabled)return;
    [self.window makeFirstResponder:self];[self sendAction:self.action to:self.target];
}
- (void)mouseDown:(NSEvent *)event {
    // NSRadioButtonCell tracks only its own image/title hit regions, which do
    // not match our custom full-width row. Selection is immediate everywhere.
    [self performClick:nil];
    if(event.clickCount==2&&self.enabled&&self.doubleAction)
        [NSApp sendAction:self.doubleAction to:self.target from:self];
}
- (BOOL)accessibilityPerformPress {if(!self.enabled)return NO;[self performClick:nil];return YES;}
- (void)drawRect:(NSRect)dirtyRect {
    NSRect icon=NSMakeRect(8,8,76,76);
    if(self.state==NSControlStateValueOn) {
        NSBezierPath *ring=[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds,1,1) xRadius:12 yRadius:12];
        [[NSColor.controlAccentColor colorWithAlphaComponent:.14] setFill];[ring fill];
        [NSColor.controlAccentColor setStroke];ring.lineWidth=2;[ring stroke];
    }
    [self.roundel drawInRect:NSInsetRect(icon,6,6) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
    // Center the combined title/two-line description optically on the roundel.
    [self.title drawInRect:NSMakeRect(100,14,NSWidth(self.bounds)-112,25) withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:19 weight:NSFontWeightRegular],NSForegroundColorAttributeName:NSColor.labelColor}];
    NSMutableParagraphStyle *caption=[NSMutableParagraphStyle new];caption.lineSpacing=2;
    [self.benefit drawInRect:NSMakeRect(100,40,NSWidth(self.bounds)-112,35) withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:13],NSForegroundColorAttributeName:NSColor.secondaryLabelColor,NSParagraphStyleAttributeName:caption}];
}
- (BOOL)isFlipped {return YES;}
- (void)keyDown:(NSEvent *)event {
    if(event.keyCode==125||event.keyCode==126) {
        NSArray *options=[self.target valueForKey:@"systemOptions"];
        NSInteger index=MAX(0,MIN((NSInteger)options.count-1,self.tag+(event.keyCode==125?1:-1)));
        NSButton *option=options[index];[option performClick:nil];[self.window makeFirstResponder:option];
    } else [super keyDown:event];
}
@end

@interface VIMInstallerArtwork : NSView
@property NSImage *wallpaper;
@end
@implementation VIMInstallerArtwork
- (void)drawRect:(NSRect)dirtyRect {
    NSRect card=NSMakeRect(0,24,NSWidth(self.bounds)-28,NSHeight(self.bounds)-48);
    NSBezierPath *outline=[NSBezierPath bezierPathWithRoundedRect:card xRadius:5 yRadius:5];
    [NSGraphicsContext saveGraphicsState];
    NSShadow *shadow=[NSShadow new];shadow.shadowBlurRadius=22;shadow.shadowOffset=NSMakeSize(0,-7);shadow.shadowColor=[NSColor.blackColor colorWithAlphaComponent:.20];[shadow set];
    [NSColor.windowBackgroundColor setFill];[outline fill];
    [NSGraphicsContext restoreGraphicsState];
    [NSGraphicsContext saveGraphicsState];
    [outline addClip];
    NSRect picture=card;picture.size.height-=22;
    NSSize size=self.wallpaper.size;
    if(size.width>0&&size.height>0) {
        CGFloat scale=MAX(NSWidth(picture)/size.width,NSHeight(picture)/size.height);
        NSRect crop=NSMakeRect((size.width-NSWidth(picture)/scale)/2,(size.height-NSHeight(picture)/scale)/2,NSWidth(picture)/scale,NSHeight(picture)/scale);
        [self.wallpaper drawInRect:picture fromRect:crop operation:NSCompositingOperationSourceOver fraction:1];
    }
    // A real title bar must remain visible against the assistant's dark page.
    // System colors keep the light variant native, without a black-on-black cap.
    BOOL dark=[[self.effectiveAppearance bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua,NSAppearanceNameDarkAqua]] isEqual:NSAppearanceNameDarkAqua];
    NSColor *titleColor=dark?[NSColor.windowBackgroundColor blendedColorWithFraction:.16 ofColor:NSColor.whiteColor]:NSColor.windowBackgroundColor;
    [titleColor setFill];NSRectFill(NSMakeRect(NSMinX(card),NSMaxY(card)-22,NSWidth(card),22));
    [NSGraphicsContext restoreGraphicsState];
}
- (void)viewDidChangeEffectiveAppearance {[super viewDidChangeEffectiveAppearance];self.needsDisplay=YES;}
@end

@interface VIMNewMachineWindow () <NSWindowDelegate>
@property NSPopUpButton *system;
@property NSTextField *nameField;
@property NSTextField *diskField;
@property NSTextField *status;
@property NSTextField *configurationStatus;
@property NSTextField *mediaLabel;
@property NSButton *chooseButton;
@property NSButton *createButton;
@property NSProgressIndicator *progress;
@property NSURL *installer;
@property BOOL creating;
@property NSTextField *cpuField;
@property NSTextField *memoryField;
@property NSView *choices;
@property NSView *configuration;
@property NSMutableDictionary *job;
@property VIMDownload *transfer;
@property VIMInstallerAssembly *assembly;
@property NSArray *files;
@property NSMutableDictionary *inputs;
@property NSUInteger fileIndex;
@property(copy) void (^terminationCompletion)(void);
@property NSButton *pauseResume;
@property NSButton *cancelButton;
@property BOOL cancelling;
@property BOOL resumeAfterTermination;
@property NSTextField *systemTitle;
@property NSSlider *memorySlider;
@property NSSlider *cpuSlider;
@property NSImageView *configurationIcon;
@property NSArray<VIMSystemOption *> *systemOptions;
@property VIMInstallerArtwork *configurationArtwork;
@property NSTextField *progressDetail;
@property NSPopover *newerSystemsPopover;
@property double overallProgress;
@property VIMInstallationActivity *sleepActivity;
@property BOOL showingProgress;
@property BOOL closeAfterCancellation;
@property NSString *pendingFailure;
@property NSString *pendingFailureTitle;
@property NSMutableDictionary<NSString *,NSNumber *> *downloadLengths;
@property NSMutableDictionary<NSString *,NSNumber *> *downloadReceived;
@property NSMutableArray<NSURLSessionDataTask *> *lengthProbes;
@property NSUInteger downloadGeneration;
@property NSTimeInterval rateSampleTime;
@property int64_t rateSampleBytes;
@property double bytesPerSecond;
@end
@implementation VIMNewMachineWindow
static VIMNewMachineWindow *creationController;
+ (BOOL)isCreating {return creationController.creating;}
+ (void)restorePendingSetup {
    NSDictionary *saved=[NSUserDefaults.standardUserDefaults dictionaryForKey:@"VIMCreationJob"];
    if(!saved)return;
    [self showWithInstaller:nil guest:nil];
    // A user-paused job stays paused. Interrupted active work reuses downloads,
    // but restarts preparation in a new attempt directory, never in-place.
    if([@[@"downloading",@"preparing",@"creating"] containsObject:saved[@"Phase"]])
        [creationController beginSetup:nil];
}
+ (BOOL)stopForTermination:(void (^)(void))completion {
    if(!creationController.transfer&&!creationController.assembly)return NO;
    creationController.resumeAfterTermination=![creationController.job[@"Phase"] isEqual:@"stopping"];
    creationController.terminationCompletion=completion;[creationController pause:nil];return YES;
}
+ (void)showWithInstaller:(NSURL *)installer guest:(NSString *)guest {
    static VIMNewMachineWindow *controller;static dispatch_once_t once;
    dispatch_once(&once, ^{controller=[self new];creationController=controller;});
    if(!controller.creating&&!controller.transfer&&!controller.assembly&&installer) {
        controller.installer=installer;controller.mediaLabel.stringValue=installer.path;
        NSUInteger index=[@[@"mojave",@"catalina",@"bigsur"] indexOfObject:guest];
        if(index!=NSNotFound){[controller.system selectItemAtIndex:index];[controller systemChanged:nil];}
        controller.window.contentView=controller.configuration;
    }
    if(!installer&&!controller.creating&&!controller.transfer&&!controller.assembly&&!controller.window.visible) {
        controller.installer=nil;controller.showingProgress=NO;[controller showChoices:nil];
    }
    [controller showWindow:nil];[controller.window makeKeyAndOrderFront:nil];
}
- (instancetype)init {
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,872,584)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskFullSizeContentView backing:NSBackingStoreBuffered defer:NO];
    if((self=[super initWithWindow:window])) {
        window.title=@"Create Virtual Intel Mac";window.titleVisibility=NSWindowTitleHidden;window.titlebarAppearsTransparent=YES;
        window.releasedWhenClosed=NO;window.delegate=self;
        window.backgroundColor=NSColor.windowBackgroundColor;
        window.collectionBehavior=NSWindowCollectionBehaviorFullScreenNone;
        window.contentMinSize=NSMakeSize(872,584);
        window.contentMaxSize=NSMakeSize(872,584);
        window.contentView=[VIMAssistantPage new];
        self.system=[NSPopUpButton new];[self.system addItemsWithTitles:@[@"macOS Mojave",@"macOS Catalina",@"macOS Big Sur"]];
        [self.system selectItemAtIndex:0];
        self.nameField=[NSTextField textFieldWithString:@"Mojave"];
        self.system.target=self;self.system.action=@selector(systemChanged:);
        NSDictionary *space=[NSFileManager.defaultManager attributesOfFileSystemForPath:VIMLibraryLocation.currentURL.path error:nil];
        // The disk is sparse. Match the iPad app's 128 GB headroom policy,
        // while keeping this app's maximum default at 128 GB.
        BOOL roomy=[space[NSFileSystemSize] unsignedLongLongValue]>=(256ULL<<30)&&
            [space[NSFileSystemFreeSize] unsignedLongLongValue]>=(96ULL<<30);
        self.diskField=[NSTextField textFieldWithString:roomy?@"128":@"64"];
        self.cpuField=[NSTextField textFieldWithString:[NSString stringWithFormat:@"%lu",(unsigned long)MIN(4,NSProcessInfo.processInfo.processorCount)]];
        self.memoryField=[NSTextField textFieldWithString:@"4"];
        for(NSTextField *field in @[self.cpuField,self.memoryField,self.diskField]) {
            [field.widthAnchor constraintEqualToConstant:56].active=YES;field.alignment=NSTextAlignmentRight;
        }
        self.mediaLabel=AssistantText(@"Downloaded from Apple. Your Mac starts automatically when its installer is ready.",12,NSFontWeightRegular);self.mediaLabel.textColor=NSColor.secondaryLabelColor;
        self.chooseButton=[NSButton buttonWithTitle:@"Choose Installer…" target:self action:@selector(choose:)];
        self.createButton=[NSButton buttonWithTitle:@"Create" target:self action:@selector(beginSetup:)];self.createButton.keyEquivalent=@"\r";
        self.status=AssistantText(@"",12,NSFontWeightRegular);self.status.textColor=NSColor.secondaryLabelColor;
        self.configurationStatus=self.status;
        self.progress=[NSProgressIndicator new];self.progress.style=NSProgressIndicatorStyleBar;self.progress.indeterminate=NO;self.progress.minValue=0;self.progress.maxValue=1;
        self.systemTitle=AssistantText(@"Configure Your Mac",27,NSFontWeightSemibold);
        NSUInteger hostGiB=NSProcessInfo.processInfo.physicalMemory/(1024ULL*1024*1024);
        self.memorySlider=[NSSlider sliderWithValue:4 minValue:1 maxValue:MAX(1,MIN(64,hostGiB-1)) target:self action:@selector(memoryChanged:)];
        self.memorySlider.numberOfTickMarks=(NSInteger)self.memorySlider.maxValue;
        self.memorySlider.allowsTickMarkValuesOnly=YES;
        [self.memorySlider.widthAnchor constraintEqualToConstant:140].active=YES;self.memorySlider.accessibilityLabel=@"Memory in gigabytes";
        self.memoryField.target=self;self.memoryField.action=@selector(memoryFieldChanged:);
        self.cpuSlider=[NSSlider sliderWithValue:self.cpuField.doubleValue minValue:1 maxValue:MIN(64,NSProcessInfo.processInfo.processorCount) target:self action:@selector(cpuChanged:)];
        [self.cpuSlider.widthAnchor constraintEqualToConstant:140].active=YES;
        self.cpuSlider.accessibilityLabel=@"Number of processors";self.cpuField.target=self;self.cpuField.action=@selector(cpuFieldChanged:);
        NSStackView *memory=[NSStackView stackViewWithViews:@[self.memorySlider,self.memoryField,[NSTextField labelWithString:@"GB"]]];memory.spacing=8;
        NSStackView *cpu=[NSStackView stackViewWithViews:@[self.cpuSlider,self.cpuField,[NSTextField labelWithString:@"cores"]]];cpu.spacing=8;
        NSStackView *disk=[NSStackView stackViewWithViews:@[self.diskField,[NSTextField labelWithString:@"GB"]]];disk.spacing=8;
        NSGridView *form=[NSGridView gridViewWithViews:@[
            @[[self configurationLabel:@"Name" symbol:@"desktopcomputer"],self.nameField],
            @[[self configurationLabel:@"Processors" symbol:@"cpu"],cpu],
            @[[self configurationLabel:@"Memory" symbol:@"memorychip"],memory],
            @[[self configurationLabel:@"Storage" symbol:@"internaldrive"],disk]]];
        form.rowSpacing=16;form.columnSpacing=14;[form columnAtIndex:0].xPlacement=NSGridCellPlacementLeading;
        NSTextField *intro=AssistantText(@"Start with the recommended settings, or make room for the apps you want to run.",14,NSFontWeightRegular);
        NSTextField *storage=AssistantText(@"Storage size can’t be changed later. If you need additional storage in the future, set a larger size now.",12,NSFontWeightRegular);storage.textColor=NSColor.secondaryLabelColor;
        NSStackView *heading=[self headingWithTitle:self.systemTitle guest:@"mojave"];
        self.configurationIcon=(NSImageView *)heading.arrangedSubviews.firstObject;
        NSStackView *stack=[NSStackView stackViewWithViews:@[heading,intro,form,storage]];
        stack.orientation=NSUserInterfaceLayoutOrientationVertical;stack.alignment=NSLayoutAttributeLeading;stack.spacing=18;
        stack.translatesAutoresizingMaskIntoConstraints=NO;[window.contentView addSubview:stack];
        for(NSView *text in @[heading,intro,storage])[text.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active=YES;
        [NSLayoutConstraint activateConstraints:@[[stack.leadingAnchor constraintEqualToAnchor:window.contentView.leadingAnchor constant:432],
            [stack.trailingAnchor constraintEqualToAnchor:window.contentView.trailingAnchor constant:-40],
            [stack.centerYAnchor constraintEqualToAnchor:window.contentView.centerYAnchor constant:-10],
            [self.nameField.widthAnchor constraintEqualToConstant:240]]];
        self.configurationArtwork=[self artworkForGuest:@"mojave" page:window.contentView];
        NSButton *back=[NSButton buttonWithTitle:@"Go Back" target:self action:@selector(showChoices:)];
        [self footerOnPage:window.contentView leading:nil trailing:@[back,self.createButton]];
        self.configuration=window.contentView;
        self.job=[[NSUserDefaults.standardUserDefaults dictionaryForKey:@"VIMCreationJob"] mutableCopy];
        if(self.job&&![@[@"mojave",@"catalina",@"bigsur"] containsObject:self.job[@"Guest"]])self.job=nil;
        if(self.job) {
            [self.system selectItemAtIndex:[@[@"mojave",@"catalina",@"bigsur"] indexOfObject:self.job[@"Guest"]]];
            self.nameField.stringValue=self.job[@"Name"]?:@"Mac";self.diskField.stringValue=[self.job[@"Disk"] stringValue]?:@"64";
            self.cpuField.stringValue=[self.job[@"CPU"] stringValue]?:@"4";self.memoryField.stringValue=[self.job[@"Memory"] stringValue]?:@"4";
            [self systemChanged:nil];self.memorySlider.doubleValue=self.memoryField.doubleValue;self.cpuSlider.doubleValue=self.cpuField.doubleValue;
            self.createButton.title=@"Resume Setup";
            self.overallProgress=[self.job[@"Progress"] doubleValue];
        } else [self showChoices:nil];
        [window center];
    }return self;
}
- (BOOL)windowShouldClose:(NSWindow *)sender {
    if(self.creating){[self presentCreationError:@"Your virtual Mac is being created. Wait for this step to finish before closing the window."];return NO;}
    if(!self.showingProgress||!self.job)return YES;
    [self confirmCancellationClosing:YES];return NO;
}
- (void)windowDidEndSheet:(NSNotification *)notification {dispatch_async(dispatch_get_main_queue(),^{[self presentPendingFailure];});}
- (void)presentCreationError:(NSString *)message {
    [self presentCreationTitle:@"Your Mac couldn’t be set up" detail:message?:@"Setup could not finish. Please try again."];
}
- (void)presentCreationTitle:(NSString *)title detail:(NSString *)detail {
    self.pendingFailureTitle=title;self.pendingFailure=detail;[self presentPendingFailure];
}
- (NSAlert *)creationAlertWithTitle:(NSString *)title detail:(NSString *)detail {
    NSAlert *alert=[NSAlert new];alert.messageText=title;alert.informativeText=detail;
    VIMSetInstallerAlertIcon(alert,@[@"mojave",@"catalina",@"bigsur"][self.system.indexOfSelectedItem]);
    [alert addButtonWithTitle:@"OK"];return alert;
}
- (void)presentPendingFailure {
    if(!self.pendingFailure||self.window.attachedSheet)return;
    NSAlert *alert=[self creationAlertWithTitle:self.pendingFailureTitle detail:self.pendingFailure];self.pendingFailure=nil;self.pendingFailureTitle=nil;
    VIMPresentAlert(alert,self.window,nil);
}
- (NSStackView *)headingWithTitle:(NSTextField *)title guest:(NSString *)guest {
    NSImageView *icon=[NSImageView imageViewWithImage:[NSImage imageNamed:[guest stringByAppendingString:@"Icon"]]];
    [icon.widthAnchor constraintEqualToConstant:52].active=YES;[icon.heightAnchor constraintEqualToConstant:64].active=YES;
    NSStackView *heading=[NSStackView stackViewWithViews:@[icon,title]];heading.spacing=8;heading.alignment=NSLayoutAttributeCenterY;
    [title.widthAnchor constraintEqualToAnchor:heading.widthAnchor constant:-60].active=YES;return heading;
}
- (NSView *)configurationLabel:(NSString *)title symbol:(NSString *)symbol {
    NSImageView *icon=[NSImageView imageViewWithImage:[NSImage imageWithSystemSymbolName:symbol accessibilityDescription:nil]];icon.contentTintColor=NSColor.secondaryLabelColor;
    [icon.widthAnchor constraintEqualToConstant:18].active=YES;[icon.heightAnchor constraintEqualToConstant:18].active=YES;
    NSStackView *label=[NSStackView stackViewWithViews:@[icon,[NSTextField labelWithString:title]]];label.spacing=8;return label;
}
- (VIMInstallerArtwork *)artworkForGuest:(NSString *)guest page:(NSView *)page {
    VIMInstallerArtwork *art=[VIMInstallerArtwork new];art.wallpaper=[NSImage imageNamed:[guest stringByAppendingString:@"Wallpaper"]];
    AddAssistantView(page,art);
    [NSLayoutConstraint activateConstraints:@[[art.leadingAnchor constraintEqualToAnchor:page.leadingAnchor constant:-28],[art.widthAnchor constraintEqualToConstant:416],[art.heightAnchor constraintEqualToConstant:328],[art.centerYAnchor constraintEqualToAnchor:page.centerYAnchor constant:-10]]];return art;
}
- (void)footerOnPage:(NSView *)page leading:(NSView *)leading trailing:(NSArray<NSButton *> *)buttons {
    NSBox *line=[NSBox new];line.boxType=NSBoxSeparator;AddAssistantView(page,line);
    [NSLayoutConstraint activateConstraints:@[[line.leadingAnchor constraintEqualToAnchor:page.leadingAnchor],[line.trailingAnchor constraintEqualToAnchor:page.trailingAnchor],[line.bottomAnchor constraintEqualToAnchor:page.bottomAnchor constant:-80]]];
    NSStackView *trailing=[NSStackView stackViewWithViews:buttons];trailing.spacing=12;AddAssistantView(page,trailing);
    for(NSButton *button in buttons){button.bezelStyle=NSBezelStyleRounded;button.controlSize=NSControlSizeLarge;button.font=[NSFont systemFontOfSize:NSFont.systemFontSize];[button.widthAnchor constraintGreaterThanOrEqualToConstant:112].active=YES;[button.heightAnchor constraintEqualToConstant:40].active=YES;}
    if(buttons.count>1)[buttons.firstObject.widthAnchor constraintEqualToAnchor:buttons.lastObject.widthAnchor].active=YES;
    [NSLayoutConstraint activateConstraints:@[[trailing.trailingAnchor constraintEqualToAnchor:page.trailingAnchor constant:-24],[trailing.centerYAnchor constraintEqualToAnchor:page.bottomAnchor constant:-40]]];
    if(leading){
        if([leading isKindOfClass:NSButton.class]){NSButton *button=(NSButton *)leading;button.controlSize=NSControlSizeLarge;button.font=[NSFont systemFontOfSize:NSFont.systemFontSize];[button.heightAnchor constraintEqualToConstant:40].active=YES;}
        AddAssistantView(page,leading);[NSLayoutConstraint activateConstraints:@[[leading.leadingAnchor constraintEqualToAnchor:page.leadingAnchor constant:24],[leading.centerYAnchor constraintEqualToAnchor:trailing.centerYAnchor]]];
    }
}
- (void)showChoices:(id)sender {
    if(self.transfer||self.assembly||self.creating)return;
    self.showingProgress=NO;
    NSView *page=[VIMAssistantPage new];self.choices=page;self.window.contentView=page;
    self.status=self.configurationStatus;
    NSTextField *title=AssistantText(@"Create Virtual Intel Mac",27,NSFontWeightSemibold);
    NSImageView *appIcon=[NSImageView imageViewWithImage:NSApp.applicationIconImage];[appIcon.widthAnchor constraintEqualToConstant:56].active=YES;[appIcon.heightAnchor constraintEqualToConstant:56].active=YES;
    NSStackView *heading=[NSStackView stackViewWithViews:@[appIcon,title]];heading.spacing=12;AddAssistantView(page,heading);
    NSTextField *intro=AssistantText(@"Run the Intel apps you love and revisit classic Mac designs.\nEvery version includes Metal and OpenGL graphics acceleration.",13,NSFontWeightRegular);intro.alignment=NSTextAlignmentCenter;AddAssistantView(page,intro);
    NSMutableParagraphStyle *paragraph=[NSMutableParagraphStyle new];paragraph.alignment=NSTextAlignmentCenter;paragraph.lineSpacing=5;
    intro.attributedStringValue=[[NSAttributedString alloc] initWithString:intro.stringValue attributes:@{NSFontAttributeName:intro.font,NSParagraphStyleAttributeName:paragraph,NSForegroundColorAttributeName:NSColor.labelColor}];
    [NSLayoutConstraint activateConstraints:@[[heading.topAnchor constraintEqualToAnchor:page.topAnchor constant:32],[heading.centerXAnchor constraintEqualToAnchor:page.centerXAnchor],[intro.topAnchor constraintEqualToAnchor:heading.bottomAnchor constant:12],[intro.centerXAnchor constraintEqualToAnchor:page.centerXAnchor],[intro.widthAnchor constraintEqualToConstant:670]]];
    NSMutableArray *rows=[NSMutableArray array];
    NSArray *guests=@[@"mojave",@"catalina",@"bigsur"];
    for(NSUInteger i=0;i<guests.count;i++) {
        VIMSystemOption *button=[VIMSystemOption new];button.title=@[@"macOS Mojave 10.14",@"macOS Catalina 10.15",@"macOS Big Sur 11"][i];button.target=self;button.action=@selector(selectSystem:);
        button.doubleAction=@selector(continueToConfiguration:);
        button.benefit=@[@"Keep using classic 32-bit and 64-bit Intel apps, with Dark Mode and a familiar Mac desktop.",
            @"Run 64-bit Intel apps in the last macOS with the classic Mac desktop design, before Big Sur’s visual refresh.",
            @"Explore a redesigned Mac desktop with Control Center, updated system apps, and support for newer Intel software."][i];
        button.tag=i;button.roundel=[NSImage imageNamed:[guests[i] stringByAppendingString:@"Icon"]];button.bordered=NO;[button setButtonType:NSButtonTypeRadio];
        button.focusRingType=NSFocusRingTypeNone;
        button.state=i==self.system.indexOfSelectedItem?NSControlStateValueOn:NSControlStateValueOff;
        button.accessibilityLabel=[NSString stringWithFormat:@"%@. %@",button.title,button.benefit];
        [button.heightAnchor constraintEqualToConstant:92].active=YES;[button.widthAnchor constraintEqualToConstant:540].active=YES;[rows addObject:button];
    }
    self.systemOptions=[rows copy];
    NSStackView *stack=[NSStackView stackViewWithViews:rows];stack.orientation=NSUserInterfaceLayoutOrientationVertical;stack.alignment=NSLayoutAttributeLeading;stack.spacing=8;AddAssistantView(page,stack);
    [NSLayoutConstraint activateConstraints:@[[stack.centerXAnchor constraintEqualToAnchor:page.centerXAnchor],[stack.topAnchor constraintEqualToAnchor:intro.bottomAnchor constant:20]]];
    NSButton *newer=[NSButton buttonWithTitle:@"Looking for a newer version of macOS?" target:self action:@selector(showNewerSystems:)];
    newer.bordered=NO;newer.contentTintColor=NSColor.linkColor;newer.font=[NSFont systemFontOfSize:12];AddAssistantView(page,newer);
    [NSLayoutConstraint activateConstraints:@[[newer.topAnchor constraintEqualToAnchor:stack.bottomAnchor constant:12],[newer.centerXAnchor constraintEqualToAnchor:page.centerXAnchor]]];
    self.chooseButton.title=@"Use an Existing Installer…";self.chooseButton.bezelStyle=NSBezelStyleRounded;
    NSButton *next=[NSButton buttonWithTitle:@"Continue" target:self action:@selector(continueToConfiguration:)];next.keyEquivalent=@"\r";
    [self footerOnPage:page leading:self.chooseButton trailing:@[next]];
    [self.window makeFirstResponder:self.systemOptions[self.system.indexOfSelectedItem]];
}
- (void)showNewerSystems:(NSButton *)sender {
    if(self.newerSystemsPopover.shown){[self.newerSystemsPopover close];return;}
    int arm=0;size_t size=sizeof(arm);sysctlbyname("hw.optional.arm64",&arm,&size,NULL,0);
    NSArray *names=arm?@[@"VirtualBuddy",@"UTM",@"Virtual Mac on iPad"]:@[@"Parallels Desktop 26"];
    NSArray *images=arm?@[@"RecommendedVirtualBuddy",@"RecommendedUTM",@"RecommendedVirtualMac"]:@[@"RecommendedParallels"];
    NSArray *URLs=arm?@[@"https://github.com/insidegui/VirtualBuddy",@"https://getutm.app",@"https://github.com/nfzerox/VirtualMacOniPad"]:@[@"https://www.parallels.com/products/desktop/"];
    NSFont *font=[NSFont systemFontOfSize:13];
    NSMutableParagraphStyle *paragraph=[NSMutableParagraphStyle new];paragraph.lineSpacing=3;
    NSDictionary *attributes=@{NSFontAttributeName:font,NSForegroundColorAttributeName:NSColor.labelColor,NSParagraphStyleAttributeName:paragraph};
    NSMutableAttributedString *text=[[NSMutableAttributedString alloc] initWithString:@"You can virtualize macOS Monterey 12 and later using " attributes:attributes];
    for(NSUInteger i=0;i<names.count;i++) {
        if(i)[text appendAttributedString:[[NSAttributedString alloc] initWithString:i==names.count-1?@", or ":@", " attributes:attributes]];
        NSTextAttachment *icon=[NSTextAttachment new];icon.image=[NSImage imageNamed:images[i]];icon.bounds=NSMakeRect(0,-3,18,18);
        [text appendAttributedString:[NSAttributedString attributedStringWithAttachment:icon]];
        // Attachments otherwise create an independent Unicode line-break
        // opportunity: keep the icon with the app's first word.
        [text appendAttributedString:[[NSAttributedString alloc] initWithString:@"\u2060\u00a0\u2060" attributes:attributes]];
        NSUInteger start=text.length;
        [text appendAttributedString:[[NSAttributedString alloc] initWithString:names[i] attributes:attributes]];
        [text appendAttributedString:[[NSAttributedString alloc] initWithString:@"\u2060" attributes:attributes]];
        // The symbol is an inline part of the same underlined link. No button
        // image padding or stack-view gap between the name and arrow.
        NSTextAttachment *arrow=[NSTextAttachment new];
        NSImage *symbol=[NSImage imageWithSystemSymbolName:@"arrow.up.right" accessibilityDescription:@"Opens website"];
        if(@available(macOS 12.0,*))
            arrow.image=[symbol imageWithSymbolConfiguration:[NSImageSymbolConfiguration configurationWithHierarchicalColor:NSColor.linkColor]];
        else arrow.image=[NSImage imageWithSize:symbol.size flipped:NO drawingHandler:^BOOL(NSRect bounds){
            [symbol drawInRect:bounds];[NSColor.linkColor setFill];
            NSRectFillUsingOperation(bounds,NSCompositingOperationSourceIn);return YES;
        }];
        arrow.bounds=NSMakeRect(1,0,10,10);
        [text appendAttributedString:[NSAttributedString attributedStringWithAttachment:arrow]];
        [text addAttributes:@{NSLinkAttributeName:[NSURL URLWithString:URLs[i]],NSForegroundColorAttributeName:NSColor.linkColor,NSUnderlineStyleAttributeName:@(NSUnderlineStyleSingle)} range:NSMakeRange(start,text.length-start)];
    }
    [text appendAttributedString:[[NSAttributedString alloc] initWithString:arm?@", then run Intel-based apps using Rosetta.":@"." attributes:attributes]];
    CGFloat width=350;
    VIMRecommendationParagraph *body=[[VIMRecommendationParagraph alloc] initWithFrame:NSMakeRect(18,16,width,200)];
    body.editable=NO;body.selectable=NO;body.drawsBackground=NO;body.textContainerInset=NSZeroSize;body.textContainer.lineFragmentPadding=0;
    body.textContainer.containerSize=NSMakeSize(width,CGFLOAT_MAX);[body.textStorage setAttributedString:text];
    [body.layoutManager ensureLayoutForTextContainer:body.textContainer];
    CGFloat height=ceil([body.layoutManager usedRectForTextContainer:body.textContainer].size.height);
    body.frame=NSMakeRect(18,16,width,height);
    NSViewController *controller=[NSViewController new];controller.view=[[NSView alloc] initWithFrame:NSMakeRect(0,0,width+36,height+32)];[controller.view addSubview:body];
    controller.preferredContentSize=controller.view.frame.size;
    self.newerSystemsPopover=[NSPopover new];self.newerSystemsPopover.behavior=NSPopoverBehaviorTransient;self.newerSystemsPopover.contentViewController=controller;
    [self.newerSystemsPopover showRelativeToRect:sender.bounds ofView:sender preferredEdge:NSRectEdgeMinY];
}
- (void)selectSystem:(NSButton *)sender {
    [self.system selectItemAtIndex:sender.tag];[self systemChanged:nil];self.installer=nil;
    for(VIMSystemOption *option in self.systemOptions)option.state=option.tag==sender.tag?NSControlStateValueOn:NSControlStateValueOff;
    self.mediaLabel.stringValue=@"macOS will be downloaded from Apple and prepared automatically.";
}
- (void)continueToConfiguration:(id)sender {
    self.showingProgress=NO;
    self.status=self.configurationStatus;
    self.window.contentView=self.configuration;
    [self.window makeFirstResponder:self.nameField];
}
- (void)systemChanged:(id)sender {
    NSString *guest=@[@"mojave",@"catalina",@"bigsur"][self.system.indexOfSelectedItem];
    self.configurationArtwork.wallpaper=[NSImage imageNamed:[guest stringByAppendingString:@"Wallpaper"]];
    self.configurationIcon.image=[NSImage imageNamed:[guest stringByAppendingString:@"Icon"]];self.configurationArtwork.needsDisplay=YES;
    if(!self.nameField.stringValue.length||[@[@"Mojave",@"Catalina",@"Big Sur",@"macOS Mojave",@"macOS Catalina",@"macOS Big Sur"] containsObject:self.nameField.stringValue])
        self.nameField.stringValue=@[@"Mojave",@"Catalina",@"Big Sur"][self.system.indexOfSelectedItem];
}
- (void)memoryChanged:(NSSlider *)sender {self.memoryField.integerValue=(NSInteger)llround(sender.doubleValue);sender.doubleValue=self.memoryField.integerValue;}
- (void)memoryFieldChanged:(id)sender {self.memorySlider.doubleValue=self.memoryField.doubleValue;}
- (void)cpuChanged:(NSSlider *)sender {self.cpuField.integerValue=(NSInteger)llround(sender.doubleValue);sender.doubleValue=self.cpuField.integerValue;}
- (void)cpuFieldChanged:(id)sender {self.cpuSlider.doubleValue=self.cpuField.doubleValue;}
- (void)choose:(id)sender {
    NSOpenPanel *panel=[NSOpenPanel openPanel];panel.canChooseDirectories=NO;panel.allowsMultipleSelection=NO;
    panel.allowedContentTypes=@[UTTypeApplicationBundle];panel.treatsFilePackagesAsDirectories=NO;panel.prompt=@"Use Installer";
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response){
        if(response!=NSModalResponseOK)return;
        NSError *error=nil;NSString *guest=[VIMInstallerAssembly guestForInstallerApplication:panel.URL error:&error];
        if(!guest){[self presentCreationTitle:@"Choose a supported macOS installer" detail:error.localizedDescription];return;}
        NSUInteger index=[@[@"mojave",@"catalina",@"bigsur"] indexOfObject:guest];
        [self selectSystem:self.systemOptions[index]];
        self.installer=panel.URL;self.mediaLabel.stringValue=[NSString stringWithFormat:@"Installer: %@",panel.URL.lastPathComponent];[self continueToConfiguration:nil];
    }];
}
- (void)saveJob {
    [NSUserDefaults.standardUserDefaults setObject:self.job forKey:@"VIMCreationJob"];
    [NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
}
- (void)setPhase:(NSString *)phase {
    if(self.job){self.job[@"Phase"]=phase;[self saveJob];}
    BOOL busy=[@[@"downloading",@"preparing",@"creating",@"stopping"] containsObject:phase];
    if(busy&&!self.sleepActivity)self.sleepActivity=[VIMInstallationActivity beginWithReason:@"Preparing a virtual Mac installer"];
    else if(!busy)self.sleepActivity=nil;
    self.pauseResume.title=busy?@"Pause":([phase isEqual:@"failed"]?@"Try Again":@"Resume");
    self.pauseResume.enabled=![phase isEqual:@"creating"]&&![phase isEqual:@"stopping"];
    self.cancelButton.enabled=![phase isEqual:@"creating"]&&![phase isEqual:@"stopping"];
}
- (void)togglePause:(id)sender {
    if(self.transfer||self.assembly)[self pause:sender];else [self beginSetup:sender];
}
- (BOOL)wholeNumber:(NSTextField *)field minimum:(NSInteger)minimum maximum:(NSInteger)maximum {
    NSScanner *scanner=[NSScanner scannerWithString:field.stringValue];NSInteger value=0;
    return [scanner scanInteger:&value]&&scanner.isAtEnd&&value>=minimum&&value<=maximum;
}
- (void)beginSetup:(id)sender {
    if(self.creating||self.transfer||self.assembly)return;
    NSString *name=[self.nameField.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    NSString *recipient=name.length?name:@"this virtual Mac";
    NSInteger maxCores=self.cpuSlider.maxValue,maxMemory=self.memorySlider.maxValue;
    if(![self wholeNumber:self.cpuField minimum:1 maximum:maxCores]) {
        [self presentCreationTitle:@"Enter a valid number of cores" detail:[NSString stringWithFormat:@"You can assign between 1 and %ld cores to %@.",(long)maxCores,recipient]];return;
    }
    if(![self wholeNumber:self.memoryField minimum:1 maximum:maxMemory]) {
        [self presentCreationTitle:@"Enter a valid amount of memory" detail:[NSString stringWithFormat:@"You can assign between 1 and %ld GB of memory to %@. Leave memory available for macOS and graphics processing.",(long)maxMemory,recipient]];return;
    }
    if(![self wholeNumber:self.diskField minimum:40 maximum:2048]) {
        [self presentCreationTitle:@"Enter a valid storage size" detail:[NSString stringWithFormat:@"You can assign between 40 and 2048 GB of storage to %@.",recipient]];return;
    }
    if(!name.length) {
        [self presentCreationTitle:@"Enter a name for your Mac" detail:@"Give this virtual Mac a name so you can find it in your library."];return;
    }
    self.nameField.stringValue=name;
    if((uint64_t)self.memoryField.integerValue*1024*1024*1024>=NSProcessInfo.processInfo.physicalMemory) {
        [self presentCreationError:@"Leave memory available for this Mac. Allocating all of your host’s memory can make both Macs unresponsive. Choose a smaller amount."];return;
    }
    NSString *guest=@[@"mojave",@"catalina",@"bigsur"][self.system.indexOfSelectedItem];
    if(self.job&&![self.job[@"Guest"] isEqual:guest])self.job=nil;
    if(!self.job){self.job=[@{@"Identifier":NSUUID.UUID.UUIDString,@"Library":VIMLibraryLocation.currentURL.path,@"Guest":guest} mutableCopy];self.overallProgress=0;}
    if(!self.installer&&!self.job[@"SourceInstaller"]&&!self.job[@"Media"]) {
        NSDictionary *retained=[VIMInstallerAssembly retainedInstallerForGuest:guest library:[NSURL fileURLWithPath:self.job[@"Library"]]];
        if(retained[@"Media"])self.job[@"Media"]=[retained[@"Media"] path];
        else if(retained[@"SourceInstaller"])self.job[@"SourceInstaller"]=[retained[@"SourceInstaller"] path];
    }
    NSError *spaceError=nil;
    if(![NSFileManager.defaultManager createDirectoryAtPath:self.job[@"Library"] withIntermediateDirectories:YES attributes:nil error:&spaceError]){[self presentCreationError:spaceError.localizedDescription];return;}
    NSDictionary *space=[NSFileManager.defaultManager attributesOfFileSystemForPath:self.job[@"Library"] error:&spaceError];
    if(!space){[self presentCreationError:spaceError.localizedDescription?:@"Connect your library drive before continuing."];return;}
    // The guest disk is sparse, so reserve what setup actually writes, not its
    // capacity: downloads, the peak of installer assembly and media creation
    // (scratch is deleted as each step finishes), and the installed system.
    BOOL bigSur=[@[@"mojave",@"catalina",@"bigsur"][self.system.indexOfSelectedItem] isEqual:@"bigsur"];
    uint64_t required=(self.job[@"Media"]?(bigSur?28ULL:22ULL):(bigSur?64ULL:44ULL))<<30;
    if(!self.job[@"Machine"]&&[space[NSFileSystemFreeSize] unsignedLongLongValue]<required) {
        [self presentCreationError:[NSString stringWithFormat:@"Your library drive needs at least %@ available for this disk and installer preparation.",[NSByteCountFormatter stringFromByteCount:required countStyle:NSByteCountFormatterCountStyleFile]]];return;
    }
    self.job[@"Name"]=self.nameField.stringValue;self.job[@"CPU"]=@(self.cpuField.integerValue);
    self.job[@"Memory"]=@(self.memoryField.integerValue);self.job[@"Disk"]=@(self.diskField.integerValue);
    if([self.installer.pathExtension.lowercaseString isEqual:@"app"]){self.job[@"SourceInstaller"]=self.installer.path;[self.job removeObjectForKey:@"Media"];}
    else if(self.installer){self.job[@"Media"]=self.installer.path;[self.job removeObjectForKey:@"SourceInstaller"];}
    [self saveJob];
    if(self.job[@"Machine"]) {
        NSError *error=nil;VIMMachine *machine=[VIMMachine loadURL:[NSURL fileURLWithPath:self.job[@"Machine"]] error:&error];
        if(machine&&[machine saveSettings:@{@"CPUCount":self.job[@"CPU"],@"MemoryMiB":@([self.job[@"Memory"] integerValue]*1024)} error:&error]&&[VIMSession openMachine:machine error:&error]){[self finishJob];[self close];}
        else [self presentCreationError:error.localizedDescription];return;
    }
    if(self.job[@"Media"]&&[NSFileManager.defaultManager fileExistsAtPath:self.job[@"Media"]]) {
        self.installer=[NSURL fileURLWithPath:self.job[@"Media"]];[self create:nil];return;
    }
    if(self.job[@"SourceInstaller"]) {
        NSError *issue=nil;NSURL *app=[NSURL fileURLWithPath:self.job[@"SourceInstaller"]];
        if(![VIMInstallerAssembly validateInstallerApplication:app guest:guest error:&issue]){[self presentCreationTitle:@"Choose a matching macOS installer" detail:issue.localizedDescription];return;}
        [self showProgress];[self prepareMedia];return;
    }
    NSError *error=nil;VIMInstallerCatalog *catalog=[VIMInstallerCatalog currentCatalogWithError:&error];
    self.files=nil;for(NSDictionary *entry in catalog.supportedInstallers)if([entry[@"Guest"] isEqual:guest])self.files=entry[@"Files"];
    if(!self.files.count){[self presentCreationError:error.localizedDescription?:@"The installer list is unavailable. Try again later."];return;}
    self.inputs=[NSMutableDictionary dictionary];self.fileIndex=0;[self showProgress];[self loadDownloadLengths];[self nextFile];
}
- (void)showProgress {
    self.showingProgress=YES;
    NSView *page=[VIMAssistantPage new];self.window.contentView=page;
    [self artworkForGuest:self.job[@"Guest"] page:page];
    self.status=AssistantText(@"Preparing your Mac…",13,NSFontWeightRegular);
    self.progressDetail=AssistantText(@"Estimating remaining time…",12,NSFontWeightRegular);self.progressDetail.textColor=NSColor.secondaryLabelColor;
    self.progressDetail.font=[NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular];
    NSString *system=@{@"mojave":@"macOS Mojave",@"catalina":@"macOS Catalina",@"bigsur":@"macOS Big Sur"}[self.job[@"Guest"]]?:self.system.titleOfSelectedItem;
    BOOL existing=self.job[@"SourceInstaller"]!=nil;
    NSTextField *title=AssistantText([(existing?@"Preparing\n":@"Downloading\n") stringByAppendingString:system],27,NSFontWeightSemibold);
    NSStackView *heading=[self headingWithTitle:title guest:self.job[@"Guest"]];
    NSTextField *intro=AssistantText(existing?@"We’ll prepare your macOS installer. When macOS Utilities appears, choose Reinstall macOS and install on Macintosh HD. Installation may take up to 2 hours.":@"We’ll download macOS from Apple and prepare its installer. When macOS Utilities appears, choose Reinstall macOS and install on Macintosh HD. Installation may take up to 2 hours.",13,NSFontWeightRegular);
    NSMutableParagraphStyle *paragraph=[NSMutableParagraphStyle new];paragraph.lineSpacing=4;paragraph.lineBreakMode=NSLineBreakByWordWrapping;
    intro.attributedStringValue=[[NSAttributedString alloc] initWithString:intro.stringValue attributes:@{NSFontAttributeName:intro.font,NSForegroundColorAttributeName:NSColor.labelColor,NSParagraphStyleAttributeName:paragraph}];
    self.pauseResume=[NSButton buttonWithTitle:@"Pause" target:self action:@selector(togglePause:)];
    self.cancelButton=[NSButton buttonWithTitle:@"Cancel" target:self action:@selector(cancelSetup:)];
    self.progress.style=NSProgressIndicatorStyleBar;self.progress.indeterminate=NO;self.progress.doubleValue=self.overallProgress;
    NSStackView *stack=[NSStackView stackViewWithViews:@[heading,intro,self.status,self.progress,self.progressDetail]];
    stack.orientation=NSUserInterfaceLayoutOrientationVertical;stack.alignment=NSLayoutAttributeLeading;stack.spacing=16;AddAssistantView(page,stack);
    [stack setCustomSpacing:28 afterView:intro];[stack setCustomSpacing:4 afterView:self.status];[stack setCustomSpacing:4 afterView:self.progress];
    for(NSView *view in stack.arrangedSubviews)[view.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active=YES;
    [NSLayoutConstraint activateConstraints:@[[stack.leadingAnchor constraintEqualToAnchor:page.leadingAnchor constant:432],[stack.trailingAnchor constraintEqualToAnchor:page.trailingAnchor constant:-40],[stack.centerYAnchor constraintEqualToAnchor:page.centerYAnchor constant:-10]]];
    [self footerOnPage:page leading:nil trailing:@[self.cancelButton,self.pauseResume]];
    [self setPhase:self.job[@"Phase"]?:@"downloading"];
}
- (void)updateOverallProgress:(double)fraction {
    // One monotonic bar: download 60%, preparation 40%. The worker reports
    // tool percentages, falling back to measured-duration estimates.
    self.overallProgress=MAX(self.overallProgress,MIN(1,MAX(0,fraction)));
    self.progress.indeterminate=NO;self.progress.doubleValue=self.overallProgress;
    if([self.job[@"Phase"] isEqual:@"preparing"])self.progressDetail.stringValue=@"Download complete. Preparing the installer.";
    if(self.job&&fabs([self.job[@"Progress"] doubleValue]-self.overallProgress)>=.005){
        self.job[@"Progress"]=@(self.overallProgress);
        // Phase changes refresh the library. Byte updates should not rescan it.
        [NSUserDefaults.standardUserDefaults setObject:self.job forKey:@"VIMCreationJob"];
    }
}
- (void)stopLengthProbes {
    self.downloadGeneration++;for(NSURLSessionDataTask *task in self.lengthProbes)[task cancel];self.lengthProbes=nil;
}
- (void)loadDownloadLengths {
    [self stopLengthProbes];NSUInteger generation=self.downloadGeneration;
    self.downloadLengths=[NSMutableDictionary dictionary];self.downloadReceived=[NSMutableDictionary dictionary];self.lengthProbes=[NSMutableArray array];
    __weak typeof(self) weakSelf=self;
    for(NSDictionary *file in self.files) {
        NSString *key=file[@"URL"];NSURL *source=[NSURL URLWithString:key];
        NSDictionary *metadata=[NSDictionary dictionaryWithContentsOfURL:[InstallerDownloadDirectory(source,self.job[@"Library"]) URLByAppendingPathComponent:@"Download.plist"]];
        if([metadata[@"SourceURL"] isEqual:key]&&[metadata[@"Length"] isKindOfClass:NSNumber.class]&&[metadata[@"Length"] longLongValue]>0) {self.downloadLengths[key]=metadata[@"Length"];continue;}
        // Headers only: do not download any installer a second time. The same
        // URL mapping as VIMDownload keeps development on the mock server.
        NSMutableURLRequest *request=[NSMutableURLRequest requestWithURL:[VIMInstallerCatalog downloadURLForAppleURL:source] cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:15];
        request.HTTPMethod=@"HEAD";[request setValue:@"identity" forHTTPHeaderField:@"Accept-Encoding"];
        NSURLSessionDataTask *task=[NSURLSession.sharedSession dataTaskWithRequest:request completionHandler:^(NSData *data,NSURLResponse *response,NSError *error){
            dispatch_async(dispatch_get_main_queue(),^{
                typeof(self) self=weakSelf;if(!self||generation!=self.downloadGeneration)return;
                if(!error&&[response isKindOfClass:NSHTTPURLResponse.class]&&[(NSHTTPURLResponse *)response statusCode]==200&&response.expectedContentLength>0&&response.expectedContentLength<=100LL*1024*1024*1024) {
                    self.downloadLengths[key]=@(response.expectedContentLength);[self refreshDownloadDetail];
                }
            });
        }];[self.lengthProbes addObject:task];[task resume];
    }
}
- (void)refreshDownloadDetail {
    int64_t received=0,total=0;BOOL known=self.files.count>0;
    for(NSDictionary *file in self.files) {
        NSString *key=file[@"URL"];received+=[self.downloadReceived[key] longLongValue];
        int64_t length=[self.downloadLengths[key] longLongValue];if(length<=0)known=NO;else total+=length;
    }
    NSString *amount=known?[NSString stringWithFormat:@"%.2f GB of %.2f GB",received/1e9,total/1e9]:[NSString stringWithFormat:@"%.2f GB downloaded",received/1e9];
    NSString *rate=self.bytesPerSecond>0?[NSString stringWithFormat:@" (%.2f MB/s)",self.bytesPerSecond/1e6]:@" (Calculating speed…)";
    NSString *remaining=@"Estimating time remaining…";
    if(known&&self.bytesPerSecond>0&&total>received) {
        NSInteger minutes=MAX(1,(NSInteger)ceil((total-received)/self.bytesPerSecond/60));
        if(minutes<60)remaining=[NSString stringWithFormat:@"%ld minute%@ remaining",(long)minutes,minutes==1?@"":@"s"];
        else {
            NSInteger hours=minutes/60,remainder=minutes%60;
            remaining=remainder?[NSString stringWithFormat:@"%ld hour%@ %ld minute%@ remaining",(long)hours,hours==1?@"":@"s",(long)remainder,remainder==1?@"":@"s"]:[NSString stringWithFormat:@"%ld hour%@ remaining",(long)hours,hours==1?@"":@"s"];
        }
    }
    if(known&&received>=total)remaining=@"Download complete";
    self.progressDetail.stringValue=[NSString stringWithFormat:@"%@%@ - %@",amount,rate,remaining];
}
- (double)downloadProgressForFileFraction:(double)fraction {
    // Legacy Apple downloads consist of a tiny assistant, recovery image, and
    // large OS payload. Fixed role weights prevent each part restarting at 0.
    NSDictionary *weights=@{@"assistant":@.03,@"recovery":@.1,@"esd":@.87};
    double total=0,completed=0;
    for(NSUInteger index=0;index<self.files.count;index++) {
        double weight=[weights[self.files[index][@"Role"]] doubleValue];if(weight<=0)weight=1;
        total+=weight;if(index<self.fileIndex)completed+=weight;else if(index==self.fileIndex)completed+=weight*fraction;
    }
    return total>0?.70*completed/total:0;
}
- (void)nextFile {
    if(self.fileIndex==self.files.count){[self prepareMedia];return;}
    NSURL *source=[NSURL URLWithString:self.files[self.fileIndex][@"URL"]];
    NSError *error=nil;self.transfer=[[VIMDownload alloc] initWithSourceURL:source directory:InstallerDownloadDirectory(source,self.job[@"Library"]) error:&error];
    if(!self.transfer){[self setPhase:@"failed"];[self presentCreationError:error.localizedDescription];return;}
    self.rateSampleTime=0;self.bytesPerSecond=0;
    [self setPhase:@"downloading"];
    self.status.stringValue=@"Downloading macOS…";
    __weak typeof(self) weakSelf=self;
    self.transfer.progress=^(int64_t received,int64_t total){
        typeof(self) self=weakSelf;if(!self)return;
        self.downloadReceived[source.absoluteString]=@(received);if(total>0)self.downloadLengths[source.absoluteString]=@(total);
        NSTimeInterval now=NSProcessInfo.processInfo.systemUptime;
        if(self.rateSampleTime==0||received<self.rateSampleBytes){self.rateSampleTime=now;self.rateSampleBytes=received;self.bytesPerSecond=0;}
        else if(now-self.rateSampleTime>=.5) {
            double rate=(received-self.rateSampleBytes)/(now-self.rateSampleTime);
            self.bytesPerSecond=self.bytesPerSecond>0?.7*self.bytesPerSecond+.3*rate:rate;self.rateSampleTime=now;self.rateSampleBytes=received;
        }
        [self updateOverallProgress:[self downloadProgressForFileFraction:total>0?(double)received/total:0]];
        self.status.stringValue=@"Downloading macOS…";
        [self refreshDownloadDetail];
    };
    [self.transfer startWithCompletion:^(NSURL *file,NSError *failure,BOOL paused){
        typeof(self) self=weakSelf;if(!self)return;self.transfer=nil;
        if([self finishCancellation]||[self completeTermination])return;
        if(paused||failure){[self stopLengthProbes];[self setPhase:failure?@"failed":@"paused"];if(failure)[self presentCreationError:failure.localizedDescription];else self.status.stringValue=@"Download paused";[self.progress stopAnimation:nil];return;}
        NSDictionary *attributes=[NSFileManager.defaultManager attributesOfItemAtPath:file.path error:nil];
        if(attributes[NSFileSize])self.downloadReceived[source.absoluteString]=self.downloadLengths[source.absoluteString]=attributes[NSFileSize];
        [self updateOverallProgress:[self downloadProgressForFileFraction:1]];
        self.inputs[self.files[self.fileIndex][@"Role"]]=file;self.fileIndex++;[self nextFile];
    }];
}
/// Earlier failed, paused or superseded attempts are never resumed (each run
/// uses a new directory). Keep only attempts whose media a Mac still boots.
static void VIMRemoveAbandonedPreparation(NSURL *root,NSURL *library,NSString *jobMedia,NSString *jobInstaller) {
    NSFileManager *fm=NSFileManager.defaultManager;
    NSMutableSet *referenced=[NSMutableSet set];
    if(jobMedia.length)[referenced addObject:jobMedia.stringByResolvingSymlinksInPath];
    if(jobInstaller.length)[referenced addObject:jobInstaller.stringByResolvingSymlinksInPath];
    NSArray *packages=[fm contentsOfDirectoryAtURL:[VIMLibraryLocation machinesURLForLibrary:library] includingPropertiesForKeys:nil options:0 error:nil];
    if(!packages)return; // Offline/unreadable library: never infer unused media.
    for(NSURL *package in packages) {
        if(![package.pathExtension isEqual:@"vintelmac"])continue;
        NSDictionary *config=[NSDictionary dictionaryWithContentsOfURL:[package URLByAppendingPathComponent:@"Machine.plist"]];
        NSString *media=config[@"InstallerMedia"];
        if(!config)return; // Unreadable configuration: keep everything.
        if([media isKindOfClass:NSString.class]&&media.isAbsolutePath)[referenced addObject:media.stringByResolvingSymlinksInPath];
    }
    for(NSURL *attempt in [fm contentsOfDirectoryAtURL:root includingPropertiesForKeys:nil options:0 error:nil]?:@[]) {
        // Completed products can be reused and may be between preparation and
        // publication in a different app process. Never treat them as scratch.
        if([fm fileExistsAtPath:[attempt URLByAppendingPathComponent:@"Media/Media.plist"].path]||
           [fm fileExistsAtPath:[attempt URLByAppendingPathComponent:@"Assembly/Assembly.plist"].path])continue;
        NSString *prefix=[attempt.URLByResolvingSymlinksInPath.path stringByAppendingString:@"/"];
        BOOL used=NO;for(NSString *path in referenced)if([path hasPrefix:prefix])used=YES;
        if(!used&&[[NSUUID alloc] initWithUUIDString:attempt.lastPathComponent])[fm removeItemAtURL:attempt error:nil];
    }
}
- (void)prepareMedia {
    [self stopLengthProbes];
    [self setPhase:@"preparing"];
    self.status.stringValue=@"Checking download and preparing your Mac…";[self updateOverallProgress:.70];
    VIMInstallerAssembly *worker=[VIMInstallerAssembly new];self.assembly=worker;
    NSString *guest=self.job[@"Guest"];NSDictionary *inputs=[self.inputs copy];NSString *existingInstaller=[self.job[@"SourceInstaller"] copy];
    NSString *preparedInput=[self.job[@"PreparedInstaller"] copy];
    NSURL *root=[VIMLibraryLocation installersURLForMachines:[NSURL fileURLWithPath:self.job[@"Library"]]];
    NSURL *attempt=[root URLByAppendingPathComponent:NSUUID.UUID.UUIDString];
    NSError *lockError=nil;NSFileManager *fm=NSFileManager.defaultManager;
    if(![fm createDirectoryAtURL:root withIntermediateDirectories:YES attributes:nil error:&lockError]){self.assembly=nil;[self setPhase:@"failed"];[self presentCreationError:lockError.localizedDescription];return;}
    int preparationLock=open([root URLByAppendingPathComponent:@".Preparation.lock"].fileSystemRepresentation,O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);struct stat lockInfo;
    if(preparationLock<0||fstat(preparationLock,&lockInfo)||!S_ISREG(lockInfo.st_mode)||lockInfo.st_nlink!=1||flock(preparationLock,LOCK_EX|LOCK_NB)) {
        if(preparationLock>=0)close(preparationLock);self.assembly=nil;[self setPhase:@"failed"];[self presentCreationError:@"Another Mac is preparing an installer in this library. Try again when it finishes."];return;
    }
    VIMRemoveAbandonedPreparation(root,[NSURL fileURLWithPath:self.job[@"Library"]],self.job[@"Media"],existingInstaller);
    worker.progress=^(NSString *stage){self.status.stringValue=stage;};
    worker.workProgress=^(double fraction){[self updateOverallProgress:.70+.30*fraction];};
    worker.timeRemaining=^(NSTimeInterval seconds){
        NSInteger minutes=MAX(1,(NSInteger)ceil(seconds/60));
        self.progressDetail.stringValue=[NSString stringWithFormat:@"About %ld minute%@ remaining",(long)minutes,minutes==1?@"":@"s"];
    };
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        NSError *error=nil;NSURL *installer=nil;
        if([NSFileManager.defaultManager createDirectoryAtURL:attempt withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:&error]) {
            if(existingInstaller)installer=[NSURL fileURLWithPath:existingInstaller];
            else installer=[worker assembleGuest:guest files:inputs jobDirectory:[attempt URLByAppendingPathComponent:@"Assembly"] error:&error];
        }
        if(installer&&!existingInstaller) {
            // Persist the replacement input before retiring completed downloads.
            // A cancelled/failed media step resumes from this complete app.
            dispatch_sync(dispatch_get_main_queue(),^{self.job[@"SourceInstaller"]=installer.path;self.job[@"PreparedInstaller"]=installer.path;[self saveJob];});
            [VIMInstallerAssembly retireCompletedDownloads:inputs];
        }
        NSURL *media=nil;
        if(installer&&!worker.cancelled)media=[guest isEqual:@"bigsur"]?
            [worker createBigSurMediaFromInstaller:installer jobDirectory:[attempt URLByAppendingPathComponent:@"Media"] error:&error]:
            [worker createLegacyMediaFromInstaller:installer guest:guest jobDirectory:[attempt URLByAppendingPathComponent:@"Media"] error:&error];
        if(media) {
            // Publish the replacement first; keep potentially slow exFAT
            // deletion off AppKit's main thread, including multi-GB app data.
            dispatch_sync(dispatch_get_main_queue(),^{self.installer=media;self.job[@"Media"]=media.path;[self saveJob];});
            NSString *prepared=existingInstaller?preparedInput:installer.path;
            if(![NSUserDefaults.standardUserDefaults boolForKey:@"VIMKeepInstallers"]&&prepared) {
                NSURL *app=[NSURL fileURLWithPath:prepared],*assembly=app.URLByDeletingLastPathComponent,*oldAttempt=assembly.URLByDeletingLastPathComponent;
                struct stat appInfo,assemblyInfo,attemptInfo;
                if([assembly.lastPathComponent isEqual:@"Assembly"]&&[[NSUUID alloc] initWithUUIDString:oldAttempt.lastPathComponent]&&
                   [oldAttempt.URLByDeletingLastPathComponent.URLByResolvingSymlinksInPath.path isEqual:root.URLByResolvingSymlinksInPath.path]&&
                   !lstat(oldAttempt.fileSystemRepresentation,&attemptInfo)&&S_ISDIR(attemptInfo.st_mode)&&
                   !lstat(assembly.fileSystemRepresentation,&assemblyInfo)&&S_ISDIR(assemblyInfo.st_mode)&&
                   !lstat(app.fileSystemRepresentation,&appInfo)&&S_ISDIR(appInfo.st_mode)&&[NSFileManager.defaultManager removeItemAtURL:assembly error:nil])
                    dispatch_sync(dispatch_get_main_queue(),^{[self.job removeObjectsForKeys:@[@"SourceInstaller",@"PreparedInstaller"]];[self saveJob];});
            }
        }
        dispatch_async(dispatch_get_main_queue(),^{
            worker.progress=nil;worker.workProgress=nil;worker.timeRemaining=nil;self.assembly=nil;[self.progress stopAnimation:nil];
            close(preparationLock);
            if([self finishCancellation]||[self completeTermination])return;
            if(!media){[self setPhase:worker.cancelled?@"paused":@"failed"];if(!worker.cancelled)[self presentCreationTitle:@"The installer couldn’t be prepared" detail:[NSString stringWithFormat:@"%@\n\n%@",error.localizedDescription?:@"Preparation could not finish.",error.localizedRecoverySuggestion?:@"Your downloads are saved. Try again to resume setup."]];else self.status.stringValue=@"Preparation paused";return;}
            [self updateOverallProgress:1];self.progressDetail.stringValue=@"Installer ready. Creating your virtual Mac.";[self create:nil];
        });
    });
}
- (void)pause:(id)sender {
    [self stopLengthProbes];
    [self setPhase:@"stopping"];
    if(self.assembly){self.assembly.cancelled=YES;self.status.stringValue=@"Stopping preparation safely…";}
    else [self.transfer pause];
}
- (NSAlert *)cancellationAlert {
    NSAlert *alert=[NSAlert new];alert.messageText=@"Are you sure you want to cancel creating this Mac?";
    VIMSetInstallerAlertIcon(alert,@[@"mojave",@"catalina",@"bigsur"][self.system.indexOfSelectedItem]);
    alert.informativeText=@"Downloaded files and any Mac already created will be kept.";
    // AppKit lays out the first button on the right. Keep No on the left and
    // make it the safe Return-key default, as in the reference confirmation.
    [alert addButtonWithTitle:@"Yes"];[alert addButtonWithTitle:@"No"];alert.buttons.firstObject.keyEquivalent=@"";alert.buttons.lastObject.keyEquivalent=@"\r";
    return alert;
}
- (void)cancelSetup:(id)sender {
    [self confirmCancellationClosing:NO];
}
- (void)confirmCancellationClosing:(BOOL)closing {
    if(self.creating||self.window.attachedSheet)return;
    self.closeAfterCancellation=closing;
    NSAlert *alert=[self cancellationAlert];
    VIMPresentAlert(alert,self.window,^(NSModalResponse response){
        if(response!=NSAlertFirstButtonReturn||self.creating){self.closeAfterCancellation=NO;return;}
        self.cancelling=YES;
        if(self.transfer||self.assembly)[self pause:nil];else [self finishCancellation];
    });
}
- (BOOL)finishCancellation {
    if(!self.cancelling)return NO;
    BOOL close=self.closeAfterCancellation;self.closeAfterCancellation=NO;
    self.cancelling=NO;[self finishJob];[self.progress stopAnimation:nil];[self showChoices:nil];
    if(close)[self.window close];
    [self completeTermination];return YES;
}
- (BOOL)completeTermination {
    if(!self.terminationCompletion)return NO;
    // Preserve active intent for launch recovery, except an explicit user pause.
    if(self.job){self.job[@"Phase"]=self.resumeAfterTermination?@"downloading":@"paused";[self saveJob];}
    void (^completion)(void)=self.terminationCompletion;self.terminationCompletion=nil;dispatch_async(dispatch_get_main_queue(),completion);return YES;
}
- (void)finishJob {
    [self stopLengthProbes];self.pendingFailure=nil;self.pendingFailureTitle=nil;self.showingProgress=NO;
    self.sleepActivity=nil;
    self.job=nil;self.installer=nil;self.overallProgress=0;[NSUserDefaults.standardUserDefaults removeObjectForKey:@"VIMCreationJob"];
    [NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
}
- (void)create:(id)sender {
    if(self.creating)return;
    NSString *guest=@[@"mojave",@"catalina",@"bigsur"][self.system.indexOfSelectedItem];
    NSString *name=[self.nameField.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    NSScanner *scanner=[NSScanner scannerWithString:self.diskField.stringValue];long long size=0;
    if(!self.installer||!name.length||![scanner scanLongLong:&size]||!scanner.isAtEnd||size<40||size>2048){
        [self setPhase:@"failed"];
        [self presentCreationError:@"Choose an installer, enter a name, and use a disk size from 40 to 2048 GB."];return;
    }
    NSURL *runtime=[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:@"Runtime"];
    NSURL *firmware=[runtime URLByAppendingPathComponent:[@"Firmware" stringByAppendingPathComponent:guest]];
    NSURL *code=[firmware URLByAppendingPathComponent:@"CODE.fd"];
    NSURL *vars=[firmware URLByAppendingPathComponent:@"VARS.fd"];
    NSURL *boot=[firmware URLByAppendingPathComponent:@"EFI.img"];
    NSURL *image=[runtime URLByAppendingPathComponent:@"bin/qemu-img"];
    NSString *root=VIMDevelopmentSourceRoot();
    if(root.length) {
        NSURL *source=[NSURL fileURLWithPath:root isDirectory:YES];
        firmware=[source URLByAppendingPathComponent:[@"Build/DevelopmentFirmware" stringByAppendingPathComponent:guest]];
        code=[firmware URLByAppendingPathComponent:@"CODE.fd"];vars=[firmware URLByAppendingPathComponent:@"VARS.fd"];
        // Development uses the same quiet GPT/ESP image as packaged builds,
        // not the legacy optical boot media with a visible desktop volume.
        boot=[source URLByAppendingPathComponent:[NSString stringWithFormat:@"Build/DevelopmentFirmware/%@/EFI.img",guest]];
        image=[source URLByAppendingPathComponent:@"Build/qemu/build-arm64-pvg/qemu-img"];
    }
    for(NSURL *file in @[code,vars,boot,image])if(![NSFileManager.defaultManager fileExistsAtPath:file.path]){
        [self setPhase:@"failed"];
        [self presentCreationError:[NSString stringWithFormat:@"This build is missing a creation resource: %@",file.lastPathComponent]];return;
    }
    NSURL *library=self.job?[NSURL fileURLWithPath:self.job[@"Library"]]:VIMLibraryLocation.currentURL,*installer=self.installer;
    self.creating=YES;self.createButton.enabled=self.chooseButton.enabled=self.system.enabled=self.nameField.enabled=self.diskField.enabled=NO;
    [self setPhase:@"creating"];
    self.status.stringValue=@"Creating Macintosh HD. Keep the library drive connected.";
    [self.progress startAnimation:nil];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        NSError *error=nil;
        VIMMachine *machine=[VIMMachine createGuest:guest name:name diskGiB:(NSUInteger)size library:library
            firmware:code nvram:vars bootMedia:boot installer:installer qemuImage:image error:&error];
        dispatch_async(dispatch_get_main_queue(), ^{
            self.creating=NO;self.createButton.enabled=self.chooseButton.enabled=self.system.enabled=self.nameField.enabled=self.diskField.enabled=YES;
            [self.progress stopAnimation:nil];
            if(!machine){[self setPhase:@"failed"];[self presentCreationError:error.localizedDescription?:@"Could not create this Mac."];return;}
            self.job[@"Machine"]=machine.URL.path;[self saveJob];
            NSError *settingsError=nil;
            if(![machine saveSettings:@{@"CPUCount":self.job[@"CPU"]?:@4,@"MemoryMiB":@([self.job[@"Memory"] integerValue]*1024)} error:&settingsError]) {
                [self setPhase:@"failed"];[self presentCreationError:settingsError.localizedDescription];return;
            }
            [NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
            self.status.stringValue=@"Your Mac is ready to install macOS.";
            NSError *launchError=nil;
            if([VIMSession openMachine:machine error:&launchError]){[self finishJob];[self close];}
            else {[self setPhase:@"failed"];[self presentCreationError:[NSString stringWithFormat:@"Your Mac was saved in the library, but could not start: %@",launchError.localizedDescription]];}
        });
    });
}
@end
