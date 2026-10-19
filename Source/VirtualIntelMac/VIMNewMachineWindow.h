#import <Cocoa/Cocoa.h>
@interface VIMNewMachineWindow : NSWindowController
+ (BOOL)isCreating;
+ (BOOL)stopForTermination:(void (^)(void))completion;
+ (void)restorePendingSetup;
+ (void)showWithInstaller:(NSURL *)installer guest:(NSString *)guest;
@end
