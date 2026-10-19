#import <Cocoa/Cocoa.h>
@class VIMMachine;
NS_ASSUME_NONNULL_BEGIN
@interface VIMSession : NSWindowController <NSMenuItemValidation>
@property(nonatomic, readonly) BOOL retinaEnabled;
@property(nonatomic, readonly) NSString *displayMode;
/// Opens/focuses the window without starting an emulator or acquiring the disk.
+ (void)showMachineWindow:(VIMMachine *)machine;
/// YES accepts the request; Apple silicon first verifies Rosetta asynchronously.
/// Missing Rosetta cancels that start and presents Apple's installation option.
+ (BOOL)openMachine:(VIMMachine *)machine error:(NSError **)error;
/// Development/production-path preparation entry point; does not install yet.
+ (BOOL)prepareMachine:(VIMMachine *)machine error:(NSError **)error;
/// Prepare and install using the native transaction; leaves reboot explicit.
+ (BOOL)installMachine:(VIMMachine *)machine error:(NSError **)error;
+ (BOOL)repairMachine:(VIMMachine *)machine error:(NSError **)error;
+ (BOOL)hasRunningMachines;
+ (BOOL)isRunningMachine:(VIMMachine *)machine;
+ (NSDictionary *)networkDetailsForMachine:(VIMMachine *)machine;
+ (void)changeNetworkMode:(NSString *)mode connected:(BOOL)connected machine:(VIMMachine *)machine window:(NSWindow *)window completion:(void (^)(NSError *_Nullable))completion;
+ (void)setNetworkConnected:(BOOL)connected machine:(VIMMachine *)machine completion:(void (^)(NSError *_Nullable))completion;
+ (void)enableGuestRemoteLogin:(VIMMachine *)machine completion:(void (^)(NSError *_Nullable))completion;
+ (void)clipboardPreferencesChangedForMachine:(VIMMachine *)machine;
+ (void)openGuestPreferences:(NSString *)pane machine:(VIMMachine *)machine completion:(void (^)(NSError *_Nullable))completion;
+ (void)checkGuestFileSharing:(VIMMachine *)machine completion:(void (^)(NSError *_Nullable))completion;
+ (void)openHostConnection:(NSURL *)URL inMachine:(VIMMachine *)machine completion:(void (^)(NSError *_Nullable))completion;
+ (NSString *)statusForMachine:(VIMMachine *)machine;
+ (void)stopAll;
+ (NSArray<NSString *> *)runningMachineNames;
+ (void)requestShutdownAll;
- (IBAction)configureMachine:(id)sender;
- (IBAction)startAgain:(id)sender;
- (IBAction)shutdown:(id)sender;
- (IBAction)forceStop:(id)sender;
- (IBAction)showInstallationGuide:(id)sender;
- (IBAction)showSessionDetails:(id)sender;
- (IBAction)finishGraphicsSetup:(id)sender;
- (IBAction)checkGraphicsSetup:(id)sender;
- (IBAction)showGuestDiagnostics:(id)sender;
- (void)releaseGuestKeys;
- (NSMenu *)deviceMenuForKind:(NSString *)kind;
- (IBAction)sendMenuShortcut:(NSMenuItem *)sender;
- (IBAction)resetMachine:(id)sender;
- (IBAction)changeRetinaMode:(NSMenuItem *)sender;
- (IBAction)takeScreenshot:(id)sender;
- (IBAction)showNetworkSettings:(id)sender;
- (IBAction)showSoundSettings:(id)sender;
- (IBAction)showMicrophoneSettings:(id)sender;
- (IBAction)showSharingSettings:(id)sender;
- (IBAction)showSSHSettings:(id)sender;
- (IBAction)showKeyboardSettings:(id)sender;
- (IBAction)sendForceQuitKey:(id)sender;
- (IBAction)toggleDeviceToolbar:(id)sender;
- (IBAction)chooseOpticalMedia:(id)sender;
- (IBAction)ejectOpticalMedia:(id)sender;
+ (void)chooseOpticalMediaForMachine:(VIMMachine *)machine window:(NSWindow *)window;
+ (void)ejectOpticalMediaForMachine:(VIMMachine *)machine window:(NSWindow *)window;
+ (NSString *)connectedOpticalMediaForMachine:(VIMMachine *)machine;
/// Same media operation used by the picker/eject UI. Empty path ejects.
+ (void)insertOpticalMedia:(NSString *)path machine:(VIMMachine *)machine completion:(void (^)(NSError *_Nullable))completion;
@end
NS_ASSUME_NONNULL_END
