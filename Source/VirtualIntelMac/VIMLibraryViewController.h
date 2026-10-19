#import <Cocoa/Cocoa.h>
@class VIMMachine;
@interface VIMLibraryViewController : NSViewController
@property(nonatomic, readonly) VIMMachine *selectedMachine;
@property(nonatomic, readonly) BOOL compact;
@property(nonatomic, readonly) NSString *arrangement;
- (void)reloadLibrary;
- (void)showLibrary;
- (void)openExistingMac:(id)sender;
- (void)registerPackageURL:(NSURL *)URL;
- (void)setDensityFromMenu:(NSMenuItem *)sender;
- (void)arrangeFromMenu:(NSMenuItem *)sender;
- (void)removeSelectedMachine:(id)sender;
@end
