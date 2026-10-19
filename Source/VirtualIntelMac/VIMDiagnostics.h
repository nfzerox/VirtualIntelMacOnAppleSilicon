#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN
/// Bounded, redacted host/app records. Never includes disks, NVRAM or screenshots.
@interface VIMDiagnostics : NSObject
+ (void)exportFromWindow:(nullable NSWindow *)window;
/// Same bounded exporter for the development harness. Call off the main thread.
/// Replacing an existing destination requires caller confirmation first.
+ (BOOL)writeArchiveToURL:(NSURL *)destination library:(NSURL *)library error:(NSError **)error;
@end
NS_ASSUME_NONNULL_END
