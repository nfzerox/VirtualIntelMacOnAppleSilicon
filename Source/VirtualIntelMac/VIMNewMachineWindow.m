#import "VIMNewMachineWindow.h"
#import "VIMMachine.h"
#import "VIMLibraryLocation.h"
#import "VIMSession.h"
#import "VIMHostSupport.h"
#import "VIMInstallerWindow.h"
#import "VIMInstallerCatalog.h"
#import "VIMInstallerSource.h"
#import "VIMInstallerAssembly.h"
#import "VIMDownload.h"
#import "VIMAlerts.h"
#import "VIMAutomaticSetup.h"
#import "VIMOSNaming.h"
#import <CommonCrypto/CommonDigest.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <sys/sysctl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static NSArray<NSString *> *CreationGuests(void) {return @[@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",@"mavericks",@"yosemite",@"elcapitan",@"sierra",@"highsierra",@"mojave",@"catalina",@"bigsur"];}
static NSArray<NSString *> *CreationNames(void) {return @[@"Tiger",@"Leopard",@"Snow Leopard",@"Lion",@"Mountain Lion",@"Mavericks",@"Yosemite",@"El Capitan",@"Sierra",@"High Sierra",@"Mojave",@"Catalina",@"Big Sur"];}

// Keep linked alert prose in AppKit's informative-text column, not a separately
// positioned accessory whose inset can differ between alert presentations.
static NSTextField *AlertParagraph(NSView *view,NSString *copy) {
    if([view isKindOfClass:NSTextField.class]&&[((NSTextField *)view).stringValue isEqual:copy])return (NSTextField *)view;
    for(NSView *child in view.subviews){NSTextField *field=AlertParagraph(child,copy);if(field)return field;}
    return nil;
}

// Reserve native field-editor space for the inline reveal button.
@interface VIMPasswordCell : NSTextFieldCell @end
@implementation VIMPasswordCell
- (NSRect)drawingRectForBounds:(NSRect)bounds {NSRect rect=[super drawingRectForBounds:bounds];rect.size.width=MAX(0,rect.size.width-26);return rect;}
@end
@interface VIMSecurePasswordCell : NSSecureTextFieldCell @end
@implementation VIMSecurePasswordCell
- (NSRect)drawingRectForBounds:(NSRect)bounds {NSRect rect=[super drawingRectForBounds:bounds];rect.size.width=MAX(0,rect.size.width-26);return rect;}
@end

@interface NSTextField (VIMGuestPasswordAutofill)
- (void)_setPasswordAutofillEnabled:(BOOL)enabled;
@end

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
@property BOOL recommendation;
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
    NSRect icon=NSMakeRect(10,(NSHeight(self.bounds)-36)/2,36,36);
    if(self.state==NSControlStateValueOn) {
        NSBezierPath *ring=[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds,1,1) xRadius:12 yRadius:12];
        [[NSColor.controlAccentColor colorWithAlphaComponent:.14] setFill];[ring fill];
        [NSColor.controlAccentColor setStroke];ring.lineWidth=2;[ring stroke];
    }
    [self.roundel drawInRect:icon fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
    [self.title drawInRect:NSMakeRect(56,(NSHeight(self.bounds)-20)/2,NSWidth(self.bounds)-68,20)
        withAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:15 weight:NSFontWeightRegular],NSForegroundColorAttributeName:NSColor.labelColor}];
}
- (BOOL)isFlipped {return YES;}
- (void)keyDown:(NSEvent *)event {
    if(event.keyCode>=123&&event.keyCode<=126) {
        NSArray *options=[self.target valueForKey:@"systemOptions"];
        NSInteger index=self.tag;
        if(event.keyCode==125&&index+1<(NSInteger)options.count)index++;
        if(event.keyCode==126&&index>0)index--;
        if(event.keyCode==123&&index>=7)index-=7;
        if(event.keyCode==124&&index<7&&index+7<(NSInteger)options.count)index+=7;
        VIMSystemOption *option=options[index];if(index!=self.tag)[option performClick:nil];
        [self.window makeFirstResponder:option];
    } else if(self.recommendation&&(event.keyCode==36||event.keyCode==76)) {
        [self performClick:nil];
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
@property NSURL *pendingArchiveFailureURL;
@property BOOL archiveInstallerRecovery;
@property BOOL pendingInstallerRecovery;
@property NSMutableDictionary<NSString *,NSNumber *> *downloadLengths;
@property NSMutableDictionary<NSString *,NSNumber *> *downloadReceived;
@property NSMutableArray<NSURLSessionDataTask *> *lengthProbes;
@property NSUInteger downloadGeneration;
@property NSTimeInterval rateSampleTime;
@property int64_t rateSampleBytes;
@property double bytesPerSecond;
@property BOOL downloadOnly;
@property NSStackView *configurationStack;
@property NSLayoutConstraint *configurationCenterY;
@property NSGridView *hardwareForm;
@property NSStackView *automaticSection;
@property NSView *passwordFieldContainer;
@property NSGridRow *usernameRow;
@property NSGridRow *passwordRow;
@property NSButton *advancedButton;
@property NSWindow *automaticSheet;
@property NSArray<NSNumber *> *automaticDraft;
@property BOOL editingAutomaticOptions;
@property NSSwitch *automaticSwitch;
@property NSSwitch *automaticSharing;
@property NSSwitch *automaticRemoteLogin;
@property NSSwitch *automaticLogin;
@property NSTextField *guestUsername;
@property NSSecureTextField *guestPassword;
@property NSTextField *visibleGuestPassword;
@property NSButton *passwordVisibility;
- (void)focusConfigurationEditor;
- (void)resetConfigurationUnlessResuming;
@end
@implementation VIMNewMachineWindow
static VIMNewMachineWindow *creationController;
+ (BOOL)isCreating {return creationController.creating;}
+ (void)showConfigurationForGuest:(NSString *)guest {
    if(![CreationGuests() containsObject:guest])return;
    [self showWithInstaller:nil guest:nil];
    if(creationController.creating||creationController.transfer||creationController.assembly)return;
    [creationController.system selectItemAtIndex:[CreationGuests() indexOfObject:guest]];
    [creationController continueToConfiguration:nil];
    // Volatile launch defaults can exercise the real recovery sheet without
    // downloading, overwriting a saved job or creating another VM.
    if([NSUserDefaults.standardUserDefaults boolForKey:@"VIMSimulateArchiveDownloadFailure"]&&
       [@[@"tiger",@"leopard",@"snowleopard"] containsObject:guest]) {
        NSURL *source=[NSURL URLWithString:[VIMInstallerCatalog fileForGuest:guest role:@"diskimage"][@"URL"]];
        [creationController presentDownloadFailure:[NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorNotConnectedToInternet userInfo:nil] source:source];
    }
}
+ (void)downloadInstallerForGuest:(NSString *)guest {
    if(![CreationGuests() containsObject:guest])return;
    [self showWithInstaller:nil guest:nil];
    if(creationController.creating||creationController.transfer||creationController.assembly)return;
    creationController.downloadOnly=YES;
    creationController.job=nil;creationController.installer=nil;creationController.overallProgress=0;
    [creationController.system selectItemAtIndex:[CreationGuests() indexOfObject:guest]];
    [creationController resetConfigurationUnlessResuming];
    [creationController systemChanged:nil];[creationController beginSetup:nil];
}
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
        NSUInteger index=[CreationGuests() indexOfObject:guest];
        if(index!=NSNotFound){[controller.system selectItemAtIndex:index];[controller resetConfigurationUnlessResuming];[controller systemChanged:nil];}
        controller.window.contentView=controller.configuration;
    }
    if(!installer&&!controller.creating&&!controller.transfer&&!controller.assembly&&!controller.window.visible) {
        controller.installer=nil;controller.showingProgress=NO;[controller showChoices:nil];
    }
    [controller showWindow:nil];[controller.window makeKeyAndOrderFront:nil];
    if(installer&&controller.window.contentView==controller.configuration)
        [controller focusConfigurationEditor];
}
- (instancetype)init {
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,872,584)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskFullSizeContentView backing:NSBackingStoreBuffered defer:NO];
    if((self=[super initWithWindow:window])) {
        window.title=@"Create Virtual Intel Mac";window.titleVisibility=NSWindowTitleHidden;window.titlebarAppearsTransparent=YES;
        window.releasedWhenClosed=NO;window.delegate=self;
        window.autorecalculatesKeyViewLoop=YES;
        window.backgroundColor=NSColor.windowBackgroundColor;
        window.collectionBehavior=NSWindowCollectionBehaviorFullScreenNone;
        window.contentMinSize=NSMakeSize(872,584);
        window.contentMaxSize=NSMakeSize(872,584);
        window.contentView=[VIMAssistantPage new];
        self.system=[NSPopUpButton new];for(NSUInteger index=0;index<CreationGuests().count;index++)
            [self.system addItemWithTitle:[NSString stringWithFormat:@"%@ %@",VIMOperatingSystemName(CreationGuests()[index]),CreationNames()[index]]];
        [self.system selectItemAtIndex:[CreationGuests() indexOfObject:@"mavericks"]];
        self.nameField=[NSTextField textFieldWithString:@"Mavericks"];
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
        self.automaticSection=[self automaticSetupSection];
        NSGridView *form=[NSGridView gridViewWithViews:@[
            @[[self configurationLabel:@"Name" symbol:@"desktopcomputer"],self.nameField],
            @[[self configurationLabel:@"Username" symbol:@"person"],self.guestUsername],
            @[[self configurationLabel:@"Password" symbol:@"key"],self.passwordFieldContainer],
            @[[self configurationLabel:@"Processors" symbol:@"cpu"],cpu],
            @[[self configurationLabel:@"Memory" symbol:@"memorychip"],memory],
            @[[self configurationLabel:@"Storage" symbol:@"internaldrive"],disk]]];
        form.rowSpacing=16;form.columnSpacing=14;[form columnAtIndex:0].xPlacement=NSGridCellPlacementLeading;
        self.hardwareForm=form;
        self.usernameRow=[form rowAtIndex:1];self.passwordRow=[form rowAtIndex:2];
        NSTextField *intro=AssistantText(@"Start with the recommended settings, or make room for the apps you want to run.",14,NSFontWeightRegular);
        NSTextField *storage=AssistantText(@"Storage size can’t be changed later. If you need additional storage in the future, set a larger size now.",12,NSFontWeightRegular);storage.textColor=NSColor.secondaryLabelColor;
        NSStackView *heading=[self headingWithTitle:self.systemTitle guest:@"mojave"];
        self.configurationIcon=(NSImageView *)heading.arrangedSubviews.firstObject;
        NSStackView *stack=[NSStackView stackViewWithViews:@[heading,intro,form,storage]];
        self.configurationStack=stack;
        stack.orientation=NSUserInterfaceLayoutOrientationVertical;stack.alignment=NSLayoutAttributeLeading;stack.spacing=18;
        stack.translatesAutoresizingMaskIntoConstraints=NO;[window.contentView addSubview:stack];
        for(NSView *text in @[heading,intro,storage])[text.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active=YES;
        self.configurationCenterY=[stack.centerYAnchor constraintEqualToAnchor:window.contentView.centerYAnchor constant:-25];
        [NSLayoutConstraint activateConstraints:@[[stack.leadingAnchor constraintEqualToAnchor:window.contentView.leadingAnchor constant:432],
            [stack.trailingAnchor constraintEqualToAnchor:window.contentView.trailingAnchor constant:-40],
            self.configurationCenterY,
            [self.nameField.widthAnchor constraintEqualToConstant:240]]];
        self.configurationArtwork=[self artworkForGuest:@"mojave" page:window.contentView];
        NSButton *back=[NSButton buttonWithTitle:@"Go Back" target:self action:@selector(showChoices:)];
        self.advancedButton=[NSButton buttonWithTitle:@"Advanced…" target:self action:@selector(showAutomaticOptions:)];
        [self footerOnPage:window.contentView leading:self.advancedButton trailing:@[back,self.createButton]];
        self.configuration=window.contentView;
        self.job=[[NSUserDefaults.standardUserDefaults dictionaryForKey:@"VIMCreationJob"] mutableCopy];
        if(self.job&&![CreationGuests() containsObject:self.job[@"Guest"]])self.job=nil;
        if(self.job) {
            [self.system selectItemAtIndex:[CreationGuests() indexOfObject:self.job[@"Guest"]]];
            self.nameField.stringValue=self.job[@"Name"]?:@"Mac";self.diskField.stringValue=[self.job[@"Disk"] stringValue]?:@"64";
            self.cpuField.stringValue=[self.job[@"CPU"] stringValue]?:@"4";self.memoryField.stringValue=[self.job[@"Memory"] stringValue]?:@"4";
            NSDictionary *setup=self.job[@"AutomaticSetup"];
            if([setup isKindOfClass:NSDictionary.class]) {
                self.automaticSwitch.state=[setup[@"Enabled"] boolValue]?NSControlStateValueOn:NSControlStateValueOff;
                self.guestUsername.stringValue=setup[@"Username"]?:VIMAutomaticSetup.defaultUsername;
                self.automaticSharing.state=[setup[@"FileSharing"] boolValue]?NSControlStateValueOn:NSControlStateValueOff;
                self.automaticRemoteLogin.state=[setup[@"RemoteLogin"] boolValue]?NSControlStateValueOn:NSControlStateValueOff;
                self.automaticLogin.state=[setup[@"AutomaticLogin"] boolValue]?NSControlStateValueOn:NSControlStateValueOff;
                self.guestPassword.stringValue=[VIMAutomaticSetup passwordForIdentifier:setup[@"CredentialIdentifier"] error:NULL]?:@"";
                [self automaticSetupChanged:nil];
            }
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
    VIMSetInstallerAlertIcon(alert,CreationGuests()[self.system.indexOfSelectedItem]);
    [alert addButtonWithTitle:@"OK"];return alert;
}
- (void)presentPendingFailure {
    if(self.window.attachedSheet)return;
    if(self.pendingArchiveFailureURL){
        NSURL *source=self.pendingArchiveFailureURL;self.pendingArchiveFailureURL=nil;self.archiveInstallerRecovery=YES;
        NSString *guest=CreationGuests()[self.system.indexOfSelectedItem];
        NSString *name=CreationNames()[self.system.indexOfSelectedItem];
        NSAlert *alert=[NSAlert new];VIMSetInstallerAlertIcon(alert,guest);
        alert.messageText=[NSString stringWithFormat:@"Unable to Download Mac OS X %@ from the Internet Archive",name];
        NSString *filename=source.lastPathComponent;
        NSString *copy=[NSString stringWithFormat:@"Installing Mac OS X %@ requires downloading the installer from the Internet Archive, which is not available in your region. Download %@ using a connection where the Internet Archive is available, then choose Use Existing Installer.\n\nYou can also install Mac OS X Lion 10.7 or later instead, which are directly downloaded from Apple and does not require access to the Internet Archive.",name,filename];
        alert.informativeText=copy;
        [alert addButtonWithTitle:@"Use Existing Installer"];[alert addButtonWithTitle:@"Choose Different Version"];
        [alert layout];
        NSTextField *paragraph=AlertParagraph(alert.window.contentView,copy);
        NSMutableParagraphStyle *style=[NSMutableParagraphStyle new];style.alignment=NSTextAlignmentLeft;
        NSMutableAttributedString *text=[[NSMutableAttributedString alloc] initWithString:copy attributes:@{NSFontAttributeName:paragraph.font?:[NSFont systemFontOfSize:13],NSForegroundColorAttributeName:NSColor.labelColor,NSParagraphStyleAttributeName:style}];
        NSRange link=[copy rangeOfString:filename];
        [text addAttributes:@{NSLinkAttributeName:source,NSUnderlineStyleAttributeName:@(NSUnderlineStyleSingle)} range:link];
        paragraph.editable=NO;paragraph.selectable=YES;paragraph.allowsEditingTextAttributes=YES;
        paragraph.attributedStringValue=text;paragraph.alignment=NSTextAlignmentLeft;
        VIMPresentAlert(alert,self.window,^(NSModalResponse response){
            if(response==NSAlertFirstButtonReturn)[self choose:nil];else [self showChoices:nil];
        });return;
    }
    if(!self.pendingFailure)return;
    if(self.pendingInstallerRecovery){
        NSAlert *alert=[NSAlert new];VIMSetInstallerAlertIcon(alert,CreationGuests()[self.system.indexOfSelectedItem]);
        alert.messageText=self.pendingFailureTitle;alert.informativeText=self.pendingFailure;
        self.pendingFailure=nil;self.pendingFailureTitle=nil;self.pendingInstallerRecovery=NO;
        [alert addButtonWithTitle:@"Use Existing Installer"];[alert addButtonWithTitle:@"Choose Different Version"];
        VIMPresentAlert(alert,self.window,^(NSModalResponse response){
            if(response==NSAlertFirstButtonReturn)[self choose:nil];else [self showChoices:nil];
        });return;
    }
    NSAlert *alert=[self creationAlertWithTitle:self.pendingFailureTitle detail:self.pendingFailure];self.pendingFailure=nil;self.pendingFailureTitle=nil;
    VIMPresentAlert(alert,self.window,nil);
}
- (void)presentDownloadFailure:(NSError *)failure source:(NSURL *)source {
    BOOL simulated=[NSUserDefaults.standardUserDefaults boolForKey:@"VIMSimulateArchiveDownloadFailure"];
    NSString *guest=CreationGuests()[self.system.indexOfSelectedItem];
    if([VIMInstallerSource shouldExplainArchiveFailure:failure source:source guest:guest
        region:simulated?@"CN":[NSLocale.currentLocale objectForKey:NSLocaleCountryCode] timeZone:NSTimeZone.localTimeZone.name]){
        self.pendingArchiveFailureURL=source;[self presentPendingFailure];
    } else [self presentCreationError:failure.localizedDescription];
}
- (void)presentInstallerChoiceFailure:(NSString *)detail {
    self.pendingInstallerRecovery=self.archiveInstallerRecovery;
    [self presentCreationTitle:@"Choose a supported OS installer" detail:detail];
}
- (NSStackView *)headingWithTitle:(NSTextField *)title guest:(NSString *)guest {
    // Artwork may be acquiring its original installer asset during development.
    // AppKit's imageViewWithImage: requires a non-nil image; missing art must
    // never abort the entire download page before its transfer starts.
    NSImageView *icon=[NSImageView new];icon.image=[NSImage imageNamed:[guest stringByAppendingString:@"Icon"]];
    [icon.widthAnchor constraintEqualToConstant:52].active=YES;[icon.heightAnchor constraintEqualToConstant:64].active=YES;
    NSStackView *heading=[NSStackView stackViewWithViews:@[icon,title]];heading.spacing=8;heading.alignment=NSLayoutAttributeCenterY;
    [title.widthAnchor constraintEqualToAnchor:heading.widthAnchor constant:-60].active=YES;return heading;
}
- (NSView *)configurationLabel:(NSString *)title symbol:(NSString *)symbol {
    NSImageView *icon=[NSImageView imageViewWithImage:[NSImage imageWithSystemSymbolName:symbol accessibilityDescription:nil]];icon.contentTintColor=NSColor.secondaryLabelColor;
    [icon.widthAnchor constraintEqualToConstant:18].active=YES;[icon.heightAnchor constraintEqualToConstant:18].active=YES;
    NSStackView *label=[NSStackView stackViewWithViews:@[icon,[NSTextField labelWithString:title]]];label.spacing=8;return label;
}
- (NSStackView *)automaticSetupSection {
    self.automaticSwitch=[NSSwitch new];self.automaticSwitch.state=NSControlStateValueOn;
    self.automaticSwitch.controlSize=NSControlSizeRegular;
    self.automaticSwitch.target=self;self.automaticSwitch.action=@selector(automaticSetupChanged:);
    self.automaticSwitch.accessibilityLabel=@"Automatic Setup";
    self.guestUsername=[NSTextField textFieldWithString:VIMAutomaticSetup.defaultUsername];self.guestUsername.accessibilityLabel=@"Guest account name";
    self.guestPassword=[NSSecureTextField new];self.guestPassword.cell=[[VIMSecurePasswordCell alloc] initTextCell:@""];self.guestPassword.bordered=YES;self.guestPassword.bezeled=YES;self.guestPassword.editable=YES;
    self.guestPassword.accessibilityLabel=@"Guest password";
    self.visibleGuestPassword=[NSTextField new];self.visibleGuestPassword.cell=[[VIMPasswordCell alloc] initTextCell:@""];self.visibleGuestPassword.bordered=YES;self.visibleGuestPassword.bezeled=YES;self.visibleGuestPassword.editable=YES;
    // This creates a guest account, not a login to a host app or website.
    // Keep native secure entry without offering the host's saved passwords.
    for(NSTextField *field in @[self.guestPassword,self.visibleGuestPassword])
        if([field respondsToSelector:@selector(_setPasswordAutofillEnabled:)])[field _setPasswordAutofillEnabled:NO];
    self.visibleGuestPassword.hidden=YES;self.visibleGuestPassword.accessibilityLabel=@"Guest password, visible";
    self.passwordVisibility=[NSButton buttonWithImage:[NSImage imageWithSystemSymbolName:@"eye.slash" accessibilityDescription:@"Show password"] target:self action:@selector(toggleGuestPassword:)];
    self.passwordVisibility.bordered=NO;self.passwordVisibility.controlSize=NSControlSizeSmall;self.passwordVisibility.contentTintColor=NSColor.secondaryLabelColor;
    self.passwordVisibility.toolTip=@"Show password";self.passwordVisibility.accessibilityLabel=@"Show password";
    NSView *password=[NSView new];
    self.passwordFieldContainer=password;
    [password.widthAnchor constraintEqualToConstant:240].active=YES;[password.heightAnchor constraintEqualToConstant:22].active=YES;
    for(NSTextField *field in @[self.guestPassword,self.visibleGuestPassword]){
        field.translatesAutoresizingMaskIntoConstraints=NO;field.font=[NSFont systemFontOfSize:NSFont.systemFontSize];field.focusRingType=NSFocusRingTypeExterior;
        field.drawsBackground=YES;field.backgroundColor=NSColor.textBackgroundColor;field.textColor=NSColor.textColor;
        field.cell.wraps=NO;field.cell.scrollable=YES;field.cell.usesSingleLineMode=YES;
        [password addSubview:field];[NSLayoutConstraint activateConstraints:@[[field.leadingAnchor constraintEqualToAnchor:password.leadingAnchor],[field.trailingAnchor constraintEqualToAnchor:password.trailingAnchor],[field.topAnchor constraintEqualToAnchor:password.topAnchor],[field.bottomAnchor constraintEqualToAnchor:password.bottomAnchor]]];
    }
    self.passwordVisibility.translatesAutoresizingMaskIntoConstraints=NO;[password addSubview:self.passwordVisibility];
    [NSLayoutConstraint activateConstraints:@[[self.passwordVisibility.trailingAnchor constraintEqualToAnchor:password.trailingAnchor constant:-4],[self.passwordVisibility.centerYAnchor constraintEqualToAnchor:password.centerYAnchor],[self.passwordVisibility.widthAnchor constraintEqualToConstant:20],[self.passwordVisibility.heightAnchor constraintEqualToConstant:18]]];
    [self.guestUsername.widthAnchor constraintEqualToConstant:240].active=YES;
    [self.guestUsername.heightAnchor constraintEqualToConstant:22].active=YES;
    self.automaticSharing=[NSSwitch new];self.automaticSharing.state=NSControlStateValueOn;self.automaticSharing.accessibilityLabel=@"Enable File Sharing";
    self.automaticRemoteLogin=[NSSwitch new];self.automaticRemoteLogin.state=NSControlStateValueOn;self.automaticRemoteLogin.accessibilityLabel=@"Enable Remote Login";
    self.automaticSharing.controlSize=NSControlSizeSmall;self.automaticRemoteLogin.controlSize=NSControlSizeSmall;
    self.automaticLogin=[NSSwitch new];self.automaticLogin.state=NSControlStateValueOn;
    self.automaticLogin.controlSize=NSControlSizeSmall;self.automaticLogin.accessibilityLabel=@"Automatic Login";
    self.automaticLogin.toolTip=@"Sign in to this virtual Mac automatically after it starts. Its password is stored by the guest’s standard automatic-login service.";
    NSView *headingSpacer=[NSView new];[headingSpacer setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
    NSStackView *headingTitle=(NSStackView *)[self configurationLabel:@"Automatic Setup" symbol:@"wand.and.stars"];
    ((NSTextField *)headingTitle.arrangedSubviews.lastObject).font=[NSFont systemFontOfSize:17 weight:NSFontWeightSemibold];
    NSStackView *heading=[NSStackView stackViewWithViews:@[headingTitle,headingSpacer,self.automaticSwitch]];
    heading.spacing=12;heading.alignment=NSLayoutAttributeCenterY;
    NSTextField *explanation=AssistantText(@"Install the operating system and create your account automatically. Turn this off to complete installation and account setup yourself.",13,NSFontWeightRegular);
    explanation.textColor=NSColor.secondaryLabelColor;
    NSBox *divider=[NSBox new];divider.boxType=NSBoxSeparator;
    NSMutableArray *rows=[NSMutableArray array];
    NSArray *titles=@[@"File Sharing",@"Remote Login",@"Automatic Login"];
    NSArray *symbols=@[@"folder",@"terminal",@"person.crop.circle"];
    NSArray *switches=@[self.automaticSharing,self.automaticRemoteLogin,self.automaticLogin];
    for(NSUInteger index=0;index<titles.count;index++){
        NSView *spacer=[NSView new];[spacer setContentHuggingPriority:1 forOrientation:NSLayoutConstraintOrientationHorizontal];
        NSStackView *row=[NSStackView stackViewWithViews:@[[self configurationLabel:titles[index] symbol:symbols[index]],spacer,switches[index]]];
        row.spacing=12;row.alignment=NSLayoutAttributeCenterY;[rows addObject:row];
    }
    NSStackView *options=[NSStackView stackViewWithViews:rows];
    options.orientation=NSUserInterfaceLayoutOrientationVertical;options.alignment=NSLayoutAttributeLeading;options.spacing=18;
    for(NSView *row in rows)[row.widthAnchor constraintEqualToAnchor:options.widthAnchor].active=YES;
    NSStackView *section=[NSStackView stackViewWithViews:@[heading,explanation,divider,options]];
    section.orientation=NSUserInterfaceLayoutOrientationVertical;section.alignment=NSLayoutAttributeLeading;section.spacing=14;
    for(NSView *row in @[heading,explanation,divider,options])[row.widthAnchor constraintEqualToAnchor:section.widthAnchor].active=YES;
    return section;
}
- (void)automaticSetupChanged:(id)sender {
    BOOL enabled=self.automaticSwitch.state==NSControlStateValueOn;
    for(NSControl *control in @[self.guestUsername,self.guestPassword,self.visibleGuestPassword,self.passwordVisibility,self.automaticSharing,self.automaticRemoteLogin,self.automaticLogin])control.enabled=enabled;
    if(!self.editingAutomaticOptions){
        NSString *guest=CreationGuests()[self.system.indexOfSelectedItem];
        BOOL visible=enabled&&[VIMAutomaticSetup supportsGuest:guest];
        self.usernameRow.hidden=!visible;self.passwordRow.hidden=!visible;
        // Collapse the rows and keep labels/editors in sync with the mode.
        for(NSUInteger row=1;row<=2;row++)for(NSUInteger column=0;column<2;column++)
            [self.hardwareForm cellAtColumnIndex:column rowIndex:row].contentView.hidden=!visible;
    }
}
- (void)showAutomaticOptions:(id)sender {
    if(self.window.attachedSheet)return;
    [self.window makeFirstResponder:nil];
    NSArray<NSSwitch *> *switches=@[self.automaticSwitch,self.automaticSharing,self.automaticRemoteLogin,self.automaticLogin];
    NSMutableArray *saved=[NSMutableArray array];for(NSSwitch *control in switches)[saved addObject:@(control.state)];self.automaticDraft=saved;
    if(!self.automaticSheet){
        self.automaticSheet=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,420,320) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        self.automaticSheet.title=@"Advanced Setup";self.automaticSheet.releasedWhenClosed=NO;
        NSView *page=self.automaticSheet.contentView;
        AddAssistantView(page,self.automaticSection);
        NSButton *cancel=[NSButton buttonWithTitle:@"Cancel" target:self action:@selector(finishAutomaticOptions:)];cancel.tag=0;cancel.keyEquivalent=@"\e";
        NSButton *done=[NSButton buttonWithTitle:@"OK" target:self action:@selector(finishAutomaticOptions:)];done.tag=1;done.keyEquivalent=@"\r";
        NSStackView *buttons=[NSStackView stackViewWithViews:@[cancel,done]];buttons.spacing=12;AddAssistantView(page,buttons);
        for(NSButton *button in @[cancel,done])[button.widthAnchor constraintGreaterThanOrEqualToConstant:80].active=YES;
        [NSLayoutConstraint activateConstraints:@[
            [self.automaticSection.leadingAnchor constraintEqualToAnchor:page.leadingAnchor constant:24],
            [self.automaticSection.trailingAnchor constraintEqualToAnchor:page.trailingAnchor constant:-24],
            [self.automaticSection.topAnchor constraintEqualToAnchor:page.topAnchor constant:24],
            [buttons.trailingAnchor constraintEqualToAnchor:page.trailingAnchor constant:-20],[buttons.bottomAnchor constraintEqualToAnchor:page.bottomAnchor constant:-20]]];
    }
    __weak typeof(self) weakSelf=self;
    self.editingAutomaticOptions=YES;
    [self.window beginSheet:self.automaticSheet completionHandler:^(NSModalResponse result){
        typeof(self) self=weakSelf;if(!self)return;
        if(result!=NSModalResponseOK)for(NSUInteger i=0;i<switches.count;i++)switches[i].state=self.automaticDraft[i].integerValue;
        self.automaticDraft=nil;self.editingAutomaticOptions=NO;[self automaticSetupChanged:nil];[self focusConfigurationEditor];
    }];
}
- (void)finishAutomaticOptions:(NSButton *)sender {
    [self.window endSheet:self.automaticSheet returnCode:sender.tag?NSModalResponseOK:NSModalResponseCancel];
}
- (void)toggleGuestPassword:(id)sender {
    BOOL show=self.visibleGuestPassword.hidden;
    if(show)self.visibleGuestPassword.stringValue=self.guestPassword.stringValue;
    else self.guestPassword.stringValue=self.visibleGuestPassword.stringValue;
    self.visibleGuestPassword.hidden=!show;self.guestPassword.hidden=show;
    NSString *description=show?@"Hide password":@"Show password";
    self.passwordVisibility.image=[NSImage imageWithSystemSymbolName:show?@"eye":@"eye.slash" accessibilityDescription:description];
    self.passwordVisibility.accessibilityLabel=description;self.passwordVisibility.toolTip=description;
    [self.window recalculateKeyViewLoop];
    [self.window makeFirstResponder:show?self.visibleGuestPassword:self.guestPassword];
}
- (void)sizeAssistantForConfiguration:(BOOL)configuration {
    (void)configuration;
    NSSize size=NSMakeSize(872,584);
    self.window.contentMinSize=NSZeroSize;self.window.contentMaxSize=size;self.window.contentMinSize=size;[self.window setContentSize:size];
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
    self.archiveInstallerRecovery=NO;
    self.showingProgress=NO;
    [self sizeAssistantForConfiguration:NO];
    NSView *page=[VIMAssistantPage new];self.choices=page;self.window.contentView=page;
    self.status=self.configurationStatus;
    NSTextField *title=AssistantText(@"Create Virtual Intel Mac",27,NSFontWeightSemibold);
    NSImageView *appIcon=[NSImageView imageViewWithImage:NSApp.applicationIconImage];[appIcon.widthAnchor constraintEqualToConstant:48].active=YES;[appIcon.heightAnchor constraintEqualToConstant:48].active=YES;
    NSStackView *heading=[NSStackView stackViewWithViews:@[appIcon,title]];heading.spacing=12;AddAssistantView(page,heading);
    NSTextField *intro=AssistantText(@"Choose a version to run the Intel or PowerPC apps you love and revisit classic Mac designs. Every version supports graphics acceleration.",13,NSFontWeightRegular);intro.alignment=NSTextAlignmentCenter;AddAssistantView(page,intro);
    NSMutableParagraphStyle *paragraph=[NSMutableParagraphStyle new];paragraph.alignment=NSTextAlignmentCenter;paragraph.lineSpacing=5;
    intro.attributedStringValue=[[NSAttributedString alloc] initWithString:intro.stringValue attributes:@{NSFontAttributeName:intro.font,NSParagraphStyleAttributeName:paragraph,NSForegroundColorAttributeName:NSColor.labelColor}];
    [NSLayoutConstraint activateConstraints:@[[heading.topAnchor constraintEqualToAnchor:page.topAnchor constant:25],[heading.centerXAnchor constraintEqualToAnchor:page.centerXAnchor],[intro.topAnchor constraintEqualToAnchor:heading.bottomAnchor constant:5],[intro.centerXAnchor constraintEqualToAnchor:page.centerXAnchor],[intro.widthAnchor constraintEqualToConstant:450]]];
    NSMutableArray *rows=[NSMutableArray array];
    NSArray *guests=CreationGuests();
    for(NSUInteger i=0;i<guests.count;i++) {
        VIMSystemOption *button=[VIMSystemOption new];button.title=@[@"Mac OS X Tiger 10.4",@"Mac OS X Leopard 10.5",@"Mac OS X Snow Leopard 10.6",@"Mac OS X Lion 10.7",@"OS X Mountain Lion 10.8",@"OS X Mavericks 10.9",@"OS X Yosemite 10.10",@"OS X El Capitan 10.11",@"macOS Sierra 10.12",@"macOS High Sierra 10.13",@"macOS Mojave 10.14",@"macOS Catalina 10.15",@"macOS Big Sur 11"][i];button.target=self;button.action=@selector(selectSystem:);
        button.doubleAction=@selector(continueToConfiguration:);
        button.tag=i;button.roundel=[NSImage imageNamed:[guests[i] stringByAppendingString:@"Icon"]];button.bordered=NO;[button setButtonType:NSButtonTypeRadio];
        button.focusRingType=NSFocusRingTypeNone;
        button.state=i==self.system.indexOfSelectedItem?NSControlStateValueOn:NSControlStateValueOff;
        button.accessibilityLabel=button.title;
        [button.heightAnchor constraintEqualToConstant:46].active=YES;[rows addObject:button];
    }
    VIMSystemOption *newer=[VIMSystemOption new];newer.title=@"macOS Monterey 12 or later";
    newer.target=self;newer.action=@selector(showNewerSystems:);newer.recommendation=YES;newer.tag=rows.count;
    newer.roundel=[NSImage imageNamed:@"montereyIcon"];newer.bordered=NO;newer.focusRingType=NSFocusRingTypeNone;
    newer.accessibilityLabel=newer.title;
    [newer.heightAnchor constraintEqualToConstant:46].active=YES;
    [rows addObject:newer];
    // Match the row drawing's icon/title inset, with a little trailing room.
    // Equal columns fit the longest actual label rather than spreading short
    // names across nearly the whole assistant window.
    CGFloat width=0;
    for(VIMSystemOption *row in rows)
        width=MAX(width,ceil([row.title sizeWithAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:15 weight:NSFontWeightRegular]}].width)+84);
    for(VIMSystemOption *row in rows)[row.widthAnchor constraintEqualToConstant:width].active=YES;
    self.systemOptions=[rows copy];
    NSMutableArray *pairs=[NSMutableArray array];
    for(NSUInteger index=0;index<7;index++){
        NSMutableArray *pair=[NSMutableArray arrayWithObject:rows[index]];
        [pair addObject:rows[index+7]];
        NSStackView *line=[NSStackView stackViewWithViews:pair];line.spacing=16;line.alignment=NSLayoutAttributeCenterY;[pairs addObject:line];
    }
    NSStackView *stack=[NSStackView stackViewWithViews:pairs];stack.orientation=NSUserInterfaceLayoutOrientationVertical;stack.alignment=NSLayoutAttributeLeading;stack.spacing=4;AddAssistantView(page,stack);
    [NSLayoutConstraint activateConstraints:@[[stack.centerXAnchor constraintEqualToAnchor:page.centerXAnchor],[stack.topAnchor constraintEqualToAnchor:intro.bottomAnchor constant:20]]];
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
    [self.newerSystemsPopover close];
    [self.system selectItemAtIndex:sender.tag];[self systemChanged:nil];self.installer=nil;
    for(VIMSystemOption *option in self.systemOptions)option.state=option.tag==sender.tag?NSControlStateValueOn:NSControlStateValueOff;
    NSString *guest=CreationGuests()[self.system.indexOfSelectedItem];
    self.mediaLabel.stringValue=[NSString stringWithFormat:@"%@ will be downloaded from %@ and prepared automatically.",
        VIMOperatingSystemName(guest),[@[@"tiger",@"leopard",@"snowleopard"] containsObject:guest]?@"the Internet Archive":@"Apple"];
}
- (void)continueToConfiguration:(id)sender {
    self.showingProgress=NO;
    [self resetConfigurationUnlessResuming];
    [self systemChanged:nil];
    [self sizeAssistantForConfiguration:YES];
    self.status=self.configurationStatus;
    self.window.contentView=self.configuration;
    [self focusConfigurationEditor];
}
- (void)resetConfigurationUnlessResuming {
    NSUInteger index=(NSUInteger)self.system.indexOfSelectedItem;
    if([self.job[@"Guest"] isEqual:CreationGuests()[index]])return;
    self.nameField.stringValue=CreationNames()[index];
    self.cpuField.doubleValue=MIN(4,self.cpuSlider.maxValue);
    self.cpuSlider.doubleValue=self.cpuField.doubleValue;
    self.memoryField.doubleValue=MIN(4,self.memorySlider.maxValue);
    self.memorySlider.doubleValue=self.memoryField.doubleValue;
    NSDictionary *space=[NSFileManager.defaultManager attributesOfFileSystemForPath:VIMLibraryLocation.currentURL.path error:nil];
    self.diskField.integerValue=[space[NSFileSystemSize] unsignedLongLongValue]>=(256ULL<<30)&&
        [space[NSFileSystemFreeSize] unsignedLongLongValue]>=(96ULL<<30)?128:64;
    for(NSSwitch *control in @[self.automaticSwitch,self.automaticSharing,self.automaticRemoteLogin,self.automaticLogin])
        control.state=NSControlStateValueOn;
    self.guestUsername.stringValue=VIMAutomaticSetup.defaultUsername;
    self.guestPassword.stringValue=@"";self.visibleGuestPassword.stringValue=@"";
    if(!self.visibleGuestPassword.hidden)[self toggleGuestPassword:nil];
}
- (void)focusConfigurationEditor {
    [self.configuration layoutSubtreeIfNeeded];
    [self.window recalculateKeyViewLoop];
    NSTextField *password=self.visibleGuestPassword.hidden?self.guestPassword:self.visibleGuestPassword;
    [self.window makeFirstResponder:self.passwordRow.hidden||!password.enabled?self.nameField:password];
}
- (void)systemChanged:(id)sender {
    NSString *guest=CreationGuests()[self.system.indexOfSelectedItem];
    BOOL classic=[VIMAutomaticSetup supportsGuest:guest];
    self.advancedButton.hidden=!classic;self.configurationStack.spacing=16;self.hardwareForm.rowSpacing=10;
    self.configurationCenterY.constant=classic?-35:-25;[self automaticSetupChanged:nil];
    for(NSLayoutConstraint *constraint in self.configurationIcon.constraints)
        if(constraint.firstAttribute==NSLayoutAttributeHeight&&constraint.secondItem==nil)constraint.constant=classic?40:64;
    self.configurationArtwork.wallpaper=[NSImage imageNamed:[guest stringByAppendingString:@"Wallpaper"]];
    self.configurationIcon.image=[NSImage imageNamed:[guest stringByAppendingString:@"Icon"]];self.configurationArtwork.needsDisplay=YES;
    if(!self.nameField.stringValue.length||[CreationNames() containsObject:self.nameField.stringValue]||[@[@"macOS Mojave",@"macOS Catalina",@"macOS Big Sur"] containsObject:self.nameField.stringValue])
        self.nameField.stringValue=CreationNames()[self.system.indexOfSelectedItem];
}
- (void)memoryChanged:(NSSlider *)sender {self.memoryField.integerValue=(NSInteger)llround(sender.doubleValue);sender.doubleValue=self.memoryField.integerValue;}
- (void)memoryFieldChanged:(id)sender {self.memorySlider.doubleValue=self.memoryField.doubleValue;}
- (void)cpuChanged:(NSSlider *)sender {self.cpuField.integerValue=(NSInteger)llround(sender.doubleValue);sender.doubleValue=self.cpuField.integerValue;}
- (void)cpuFieldChanged:(id)sender {self.cpuSlider.doubleValue=self.cpuField.doubleValue;}
- (void)choose:(id)sender {
    NSOpenPanel *panel=[NSOpenPanel openPanel];panel.canChooseDirectories=YES;panel.canChooseFiles=YES;panel.allowsMultipleSelection=NO;
    panel.canCreateDirectories=NO;panel.resolvesAliases=YES;panel.title=@"Choose an OS Installer";
    panel.allowedContentTypes=@[UTTypeApplicationBundle,UTTypeDiskImage];panel.treatsFilePackagesAsDirectories=NO;panel.prompt=@"Use Installer";
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response){
        if(response!=NSModalResponseOK){
            if(self.archiveInstallerRecovery)[self presentInstallerChoiceFailure:@"Choose a downloaded installer, or choose a different OS version to continue."];
            return;
        }
        self.chooseButton.enabled=NO;
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0), ^{
        NSError *error=nil;NSString *guest=[VIMInstallerAssembly guestForInstallerApplication:panel.URL error:&error];
        dispatch_async(dispatch_get_main_queue(), ^{
        self.chooseButton.enabled=YES;
        if(!guest){[self presentInstallerChoiceFailure:error.localizedDescription];return;}
        NSUInteger index=[CreationGuests() indexOfObject:guest];
        if(index==NSNotFound){[self presentInstallerChoiceFailure:@"Choose a Tiger, Leopard or Snow Leopard restore DVD, or a Lion through Big Sur installer app."];return;}
        self.archiveInstallerRecovery=NO;
        [self selectSystem:self.systemOptions[index]];
        self.installer=panel.URL;self.mediaLabel.stringValue=[NSString stringWithFormat:@"Installer: %@",panel.URL.lastPathComponent];[self continueToConfiguration:nil];
        });
        });
    }];
}
- (void)saveJob {
    [NSUserDefaults.standardUserDefaults setObject:self.job forKey:self.downloadOnly?@"VIMInstallerDownloadJob":@"VIMCreationJob"];
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
    NSString *guest=CreationGuests()[self.system.indexOfSelectedItem];
    NSString *password=self.visibleGuestPassword.hidden?self.guestPassword.stringValue:self.visibleGuestPassword.stringValue;
    BOOL automatic=!self.downloadOnly&&[VIMAutomaticSetup supportsGuest:guest]&&self.automaticSwitch.state==NSControlStateValueOn;
    if(automatic) {
        NSError *issue=nil;
        if(![VIMAutomaticSetup validateUsername:self.guestUsername.stringValue error:&issue]||![VIMAutomaticSetup validatePassword:password error:&issue]) {
            [self presentCreationTitle:@"Check your virtual Mac’s account" detail:issue.localizedDescription];return;
        }
        if(!password.length) {
            NSAlert *alert=[NSAlert new];alert.messageText=@"Enter a password for your virtual Mac";
            alert.informativeText=@"Automatic Setup needs a password to create your account. To set up the account yourself, turn off Automatic Setup in \"Advanced...\".";
            VIMSetInstallerAlertIcon(alert,guest);[alert addButtonWithTitle:@"OK"];
            VIMPresentAlert(alert,self.window,^(NSModalResponse result){
                [self.window makeFirstResponder:self.visibleGuestPassword.hidden?self.guestPassword:self.visibleGuestPassword];
            });return;
        }
    }
    if(self.job&&![self.job[@"Guest"] isEqual:guest]){
        if(self.job[@"AutomaticSetup"]&&!self.job[@"Machine"])[VIMAutomaticSetup removePasswordForIdentifier:self.job[@"Identifier"] error:NULL];
        self.job=nil;
    }
    if(!self.job){self.job=[@{@"Identifier":NSUUID.UUID.UUIDString,@"Library":VIMLibraryLocation.currentURL.path,@"Guest":guest} mutableCopy];self.overallProgress=0;}
    if(automatic) {
        NSError *issue=nil;
        if(![VIMAutomaticSetup savePassword:password identifier:self.job[@"Identifier"] error:&issue]){[self presentCreationError:issue.localizedDescription];return;}
        self.job[@"AutomaticSetup"]=@{@"FormatVersion":@1,@"Enabled":@YES,@"Username":self.guestUsername.stringValue,
            @"FileSharing":@(self.automaticSharing.state==NSControlStateValueOn),@"RemoteLogin":@(self.automaticRemoteLogin.state==NSControlStateValueOn),
            @"AutomaticLogin":@(self.automaticLogin.state==NSControlStateValueOn),@"CredentialIdentifier":self.job[@"Identifier"]};
    } else {
        if(self.job[@"AutomaticSetup"]&&!self.job[@"Machine"])[VIMAutomaticSetup removePasswordForIdentifier:self.job[@"Identifier"] error:NULL];
        [self.job removeObjectForKey:@"AutomaticSetup"];
    }
    if(!self.downloadOnly&&!self.installer&&!self.job[@"SourceInstaller"]&&!self.job[@"Media"]) {
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
    BOOL bigSur=[guest isEqual:@"bigsur"];
    uint64_t required=(self.job[@"Media"]?(bigSur?28ULL:22ULL):(bigSur?64ULL:44ULL))<<30;
    if(!self.job[@"Machine"]&&[space[NSFileSystemFreeSize] unsignedLongLongValue]<required) {
        [self presentCreationError:[NSString stringWithFormat:@"Your library drive needs at least %@ available for this disk and installer preparation.",[NSByteCountFormatter stringFromByteCount:required countStyle:NSByteCountFormatterCountStyleFile]]];return;
    }
    self.job[@"Name"]=self.nameField.stringValue;self.job[@"CPU"]=@(self.cpuField.integerValue);
    self.job[@"Memory"]=@(self.memoryField.integerValue);self.job[@"Disk"]=@(self.diskField.integerValue);
    BOOL restoreSource=[@[@"tiger",@"leopard",@"snowleopard"] containsObject:guest]&&[self.installer.pathExtension.lowercaseString isEqual:@"dmg"];
    if([self.installer.pathExtension.lowercaseString isEqual:@"app"]||restoreSource){self.job[@"SourceInstaller"]=self.installer.path;[self.job removeObjectForKey:@"Media"];}
    else if(self.installer){self.job[@"Media"]=self.installer.path;[self.job removeObjectForKey:@"SourceInstaller"];}
    [self saveJob];
    if(self.job[@"Machine"]) {
        NSError *error=nil;VIMMachine *machine=[VIMMachine loadURL:[NSURL fileURLWithPath:self.job[@"Machine"]] error:&error];
        if(machine&&(!self.job[@"AutomaticSetup"]||[machine saveAutomaticSetup:self.job[@"AutomaticSetup"] error:&error])&&[machine saveSettings:@{@"CPUCount":self.job[@"CPU"],@"MemoryMiB":@([self.job[@"Memory"] integerValue]*1024)} error:&error]&&[VIMSession openMachine:machine error:&error]){[self finishJob];[self close];}
        else [self presentCreationError:error.localizedDescription];return;
    }
    if(self.job[@"Media"]&&[NSFileManager.defaultManager fileExistsAtPath:self.job[@"Media"]]) {
        self.installer=[NSURL fileURLWithPath:self.job[@"Media"]];[self create:nil];return;
    }
    if(self.job[@"SourceInstaller"]) {
        NSError *issue=nil;NSURL *app=[NSURL fileURLWithPath:self.job[@"SourceInstaller"]];
        if(![VIMInstallerAssembly validateInstallerApplication:app guest:guest error:&issue]){[self presentCreationTitle:[NSString stringWithFormat:@"Choose a matching %@ installer",VIMOperatingSystemName(guest)] detail:issue.localizedDescription];return;}
        [self showProgress];[self prepareMedia];return;
    }
    NSError *error=nil;VIMInstallerCatalog *catalog=[VIMInstallerCatalog currentCatalogWithError:&error];
    self.files=nil;for(NSDictionary *entry in catalog.supportedInstallers)if([entry[@"Guest"] isEqual:guest])self.files=entry[@"Files"];
    if(!self.files.count){[self presentCreationError:error.localizedDescription?:@"The installer list is unavailable. Try again later."];return;}
    self.inputs=[NSMutableDictionary dictionary];self.fileIndex=0;[self showProgress];[self loadDownloadLengths];[self nextFile];
}
- (void)showProgress {
    self.showingProgress=YES;
    [self sizeAssistantForConfiguration:NO];
    NSView *page=[VIMAssistantPage new];self.window.contentView=page;
    [self artworkForGuest:self.job[@"Guest"] page:page];
    self.status=AssistantText(@"Preparing your Mac…",13,NSFontWeightRegular);
    self.progressDetail=AssistantText(@"Estimating remaining time…",12,NSFontWeightRegular);self.progressDetail.textColor=NSColor.secondaryLabelColor;
    self.progressDetail.font=[NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular];
    NSString *system=@{@"mojave":@"macOS Mojave",@"catalina":@"macOS Catalina",@"bigsur":@"macOS Big Sur"}[self.job[@"Guest"]]?:self.system.titleOfSelectedItem;
    BOOL existing=self.job[@"SourceInstaller"]!=nil;
    NSTextField *title=AssistantText([(existing?@"Preparing\n":@"Downloading\n") stringByAppendingString:system],27,NSFontWeightSemibold);
    NSStackView *heading=[self headingWithTitle:title guest:self.job[@"Guest"]];
    NSString *os=VIMOperatingSystemName(self.job[@"Guest"]);
    NSString *preparation=existing?[NSString stringWithFormat:@"We’ll prepare your %@ installer.",os]:
        [@[@"tiger",@"leopard",@"snowleopard"] containsObject:self.job[@"Guest"]]?[NSString stringWithFormat:@"We’ll download Mac OS X %@ from the Internet Archive and prepare its installer.",CreationNames()[[CreationGuests() indexOfObject:self.job[@"Guest"]]]]:
        [NSString stringWithFormat:@"We’ll download %@ from Apple and prepare its installer.",os];
    NSString *instructions=[self.job[@"AutomaticSetup"][@"Enabled"] boolValue]?@"Your virtual Mac will install and set itself up automatically.":
        [@[@"tiger",@"leopard",@"snowleopard"] containsObject:self.job[@"Guest"]]?@"When the installer opens, follow the onscreen instructions and install on Macintosh HD.":
        [NSString stringWithFormat:@"When %@ Utilities appears, choose Reinstall %@ and install on Macintosh HD.",os,os];
    NSTextField *intro=AssistantText([NSString stringWithFormat:@"%@ %@ Installation may take up to 2 hours.",preparation,instructions],13,NSFontWeightRegular);
    NSMutableParagraphStyle *paragraph=[NSMutableParagraphStyle new];paragraph.lineSpacing=4;paragraph.lineBreakMode=NSLineBreakByWordWrapping;
    intro.attributedStringValue=[[NSAttributedString alloc] initWithString:intro.stringValue attributes:@{NSFontAttributeName:intro.font,NSForegroundColorAttributeName:NSColor.labelColor,NSParagraphStyleAttributeName:paragraph}];
    self.pauseResume=[NSButton buttonWithTitle:@"Pause" target:self action:@selector(togglePause:)];
    self.cancelButton=[NSButton buttonWithTitle:@"Cancel" target:self action:@selector(cancelSetup:)];
    // AppKit can animate a reused indicator from the preceding job's end value.
    // Set the new page's initial value before the fresh indicator is attached.
    self.progress=[NSProgressIndicator new];self.progress.style=NSProgressIndicatorStyleBar;
    self.progress.indeterminate=NO;self.progress.minValue=0;self.progress.maxValue=1;self.progress.doubleValue=self.overallProgress;
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
        [NSUserDefaults.standardUserDefaults setObject:self.job forKey:self.downloadOnly?@"VIMInstallerDownloadJob":@"VIMCreationJob"];
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
    if(self.fileIndex==self.files.count){
        if(self.downloadOnly){
            [self stopLengthProbes];[self setPhase:@"downloaded"];
            self.status.stringValue=@"Download complete";self.progressDetail.stringValue=@"The installer download is saved in your library’s Downloads folder.";
            [self updateOverallProgress:1];self.pauseResume.enabled=NO;return;
        }
        [self prepareMedia];return;
    }
    NSURL *source=[NSURL URLWithString:self.files[self.fileIndex][@"URL"]];
    if([NSUserDefaults.standardUserDefaults boolForKey:@"VIMSimulateArchiveDownloadFailure"]&&
       [@[@"tiger",@"leopard",@"snowleopard"] containsObject:self.job[@"Guest"]]){
        [self stopLengthProbes];[self setPhase:@"failed"];
        [self presentDownloadFailure:[NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorNotConnectedToInternet userInfo:nil] source:source];return;
    }
    NSError *error=nil;self.transfer=[[VIMDownload alloc] initWithSourceURL:source directory:InstallerDownloadDirectory(source,self.job[@"Library"]) error:&error];
    if(!self.transfer){[self setPhase:@"failed"];[self presentCreationError:error.localizedDescription];return;}
    self.rateSampleTime=0;self.bytesPerSecond=0;
    [self setPhase:@"downloading"];
    self.status.stringValue=[NSString stringWithFormat:@"Downloading %@…",VIMOperatingSystemName(self.job[@"Guest"])];
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
        self.status.stringValue=[NSString stringWithFormat:@"Downloading %@…",VIMOperatingSystemName(self.job[@"Guest"])];
        [self refreshDownloadDetail];
    };
    [self.transfer startWithCompletion:^(NSURL *file,NSError *failure,BOOL paused){
        typeof(self) self=weakSelf;if(!self)return;self.transfer=nil;
        if([self finishCancellation]||[self completeTermination])return;
        if(paused||failure){[self stopLengthProbes];[self setPhase:failure?@"failed":@"paused"];if(failure)[self presentDownloadFailure:failure source:source];else self.status.stringValue=@"Download paused";[self.progress stopAnimation:nil];return;}
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
            // Restore-DVD guests use the download itself as the installer.
            // Only an assembled app supersedes its downloaded inputs here.
            if([installer.pathExtension.lowercaseString isEqual:@"app"])
                [VIMInstallerAssembly retireCompletedDownloads:inputs];
        }
        NSURL *media=nil;
        if(installer&&!worker.cancelled)media=[guest isEqual:@"bigsur"]?
            [worker createBigSurMediaFromInstaller:installer jobDirectory:[attempt URLByAppendingPathComponent:@"Media"] error:&error]:
            [worker createRecoveryMediaFromInstaller:installer guest:guest jobDirectory:[attempt URLByAppendingPathComponent:@"Media"] error:&error];
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
    VIMSetInstallerAlertIcon(alert,CreationGuests()[self.system.indexOfSelectedItem]);
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
    if(!self.job[@"Machine"]&&self.job[@"AutomaticSetup"])
        [VIMAutomaticSetup removePasswordForIdentifier:self.job[@"Identifier"] error:NULL];
    [NSUserDefaults.standardUserDefaults removeObjectForKey:self.downloadOnly?@"VIMInstallerDownloadJob":@"VIMCreationJob"];
    self.job=nil;self.installer=nil;self.overallProgress=0;self.downloadOnly=NO;
    [NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
}
- (void)create:(id)sender {
    if(self.creating)return;
    NSString *guest=CreationGuests()[self.system.indexOfSelectedItem];
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
    NSURL *common=[runtime URLByAppendingPathComponent:@"Firmware/Common"];
    if(![NSFileManager.defaultManager fileExistsAtPath:code.path])code=[common URLByAppendingPathComponent:@"CODE.fd"];
    NSURL *boot=[common URLByAppendingPathComponent:@"EFI.img"];
    if(![NSFileManager.defaultManager fileExistsAtPath:boot.path])boot=[firmware URLByAppendingPathComponent:@"EFI.img"];
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
    self.status.stringValue=@"Creating Macintosh HD.";
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
            if(self.job[@"AutomaticSetup"]&&![machine saveAutomaticSetup:self.job[@"AutomaticSetup"] error:&settingsError]) {
                [self setPhase:@"failed"];[self presentCreationError:settingsError.localizedDescription];return;
            }
            if(![machine saveSettings:@{@"CPUCount":self.job[@"CPU"]?:@4,@"MemoryMiB":@([self.job[@"Memory"] integerValue]*1024)} error:&settingsError]) {
                [self setPhase:@"failed"];[self presentCreationError:settingsError.localizedDescription];return;
            }
            [NSNotificationCenter.defaultCenter postNotificationName:@"VIMLibraryChanged" object:nil];
            self.status.stringValue=[NSString stringWithFormat:@"Your Mac is ready to install %@.",VIMOperatingSystemName(guest)];
            NSError *launchError=nil;
            if([VIMSession openMachine:machine error:&launchError]){[self finishJob];[self close];}
            else {[self setPhase:@"failed"];[self presentCreationError:[NSString stringWithFormat:@"Your Mac was saved in the library, but could not start: %@",launchError.localizedDescription]];}
        });
    });
}
@end
