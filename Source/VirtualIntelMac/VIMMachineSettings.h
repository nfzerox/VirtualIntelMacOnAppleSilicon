#import <Cocoa/Cocoa.h>
@class VIMMachine;
@interface VIMMachineSettings : NSObject
+ (void)showForMachine:(VIMMachine *)machine window:(NSWindow *)window completion:(void (^)(void))completion;
+ (void)showForMachine:(VIMMachine *)machine section:(NSString *)section category:(NSString *)category window:(NSWindow *)window;
+ (void)requestMicrophoneAccessFromWindow:(NSWindow *)window;
+ (void)requestCameraAccessFromWindow:(NSWindow *)window;
+ (void)handleCameraModeSelection:(NSString *)mode fromWindow:(NSWindow *)window;
/// Development harness: render only this app's own configuration content.
+ (BOOL)captureForMachine:(VIMMachine *)machine toURL:(NSURL *)URL error:(NSError **)error;
+ (void)checkPaneCyclingForMachine:(VIMMachine *)machine completion:(void (^)(BOOL passed))completion;
@end
