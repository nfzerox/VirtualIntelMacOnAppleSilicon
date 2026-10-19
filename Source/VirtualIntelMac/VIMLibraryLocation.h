#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN
/// User-selected library root with Machines, Downloads and Installers children.
/// Legacy items in that same root are renamed only while all machines are idle;
/// choosing another library never transfers Macs from the previous library.
@interface VIMLibraryLocation : NSObject
+ (NSURL *)downloadsURL;
+ (NSURL *)installersURL;
+ (NSURL *)rootURL;
+ (NSURL *)machinesURL;
/// Accepts the selected root, or the reserved Machines child from older jobs.
+ (NSURL *)rootURLForLibrary:(NSURL *)library;
+ (NSURL *)machinesURLForLibrary:(NSURL *)library;
+ (NSURL *)downloadsURLForMachines:(NSURL *)machines;
+ (NSURL *)installersURLForMachines:(NSURL *)machines;
+ (BOOL)prepareLayout:(NSError **)error;
+ (NSURL *)defaultURL;
+ (NSURL *)currentURL; // The selected library root, never its Machines child.
+ (NSImage *)folderIcon;
+ (BOOL)setURL:(NSURL *)URL error:(NSError **)error;
+ (void)chooseFromWindow:(nullable NSWindow *)window completion:(void (^)(BOOL changed))completion;
@end
NS_ASSUME_NONNULL_END
