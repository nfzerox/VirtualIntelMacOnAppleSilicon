#import <Cocoa/Cocoa.h>
NS_ASSUME_NONNULL_BEGIN
/// Shared PVG frame consumer. Caller retains frame ownership until completion.
@interface VIMMetalView : NSView
@property(nonatomic, copy, nullable) void (^inputHandler)(NSArray<NSDictionary *> *events);
@property(nonatomic) BOOL displaySleeping;
/// PVG display power is independent of the emulator's system run state.
@property(nonatomic) BOOL guestDisplaySleeping;
@property(nonatomic) BOOL softwareCursor;
@property(nonatomic,readonly) BOOL inputCaptured;
- (void)releaseInputCapture;
- (void)capturePNG:(void (^)(NSData *_Nullable png))completion;
- (void)releasePressedKeys;
/// After disconnecting the producer, retire shared frame storage. Submitted
/// Metal commands retain their resources until GPU completion.
- (void)discardFrameStorage;
- (void)setGuestCursorVisible:(BOOL)visible;
- (void)setGuestCursorPixels:(NSData *)pixels width:(NSUInteger)width height:(NSUInteger)height hotspot:(NSPoint)hotspot;
/// Read-only development snapshot; no guest calls or screen capture.
- (NSDictionary *)cursorDiagnostics;
/// Geometry only; no GPU readback, pixel copies or guest calls.
- (NSDictionary *)frameDiagnostics;
/// Takes ownership of fd on every path; never copies the shared frame pixels.
- (BOOL)installFrameDescriptor:(int)fd length:(NSUInteger)length error:(NSError **)error;
/// Main-thread only. Completion fires once, after GPU use or a skipped frame.
- (void)presentWidth:(NSUInteger)width height:(NSUInteger)height stride:(NSUInteger)stride
    completion:(void (^)(BOOL presented))completion;
@end
NS_ASSUME_NONNULL_END
