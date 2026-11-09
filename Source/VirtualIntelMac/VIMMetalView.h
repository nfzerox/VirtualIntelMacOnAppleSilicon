#import <Cocoa/Cocoa.h>
#import <IOSurface/IOSurface.h>
NS_ASSUME_NONNULL_BEGIN
/// Shared PVG frame consumer. Caller retains frame ownership until completion.
@interface VIMMetalView : NSView
@property(nonatomic, copy, nullable) void (^inputHandler)(NSArray<NSDictionary *> *events);
/// Explicit diagnostic only; the session reports the normal QMP input reply.
@property(nonatomic, copy, nullable) void (^diagnosticInputObserver)(CFTimeInterval queued, NSError * _Nullable error);
@property(nonatomic) BOOL displaySleeping;
/// PVG display power is independent of the emulator's system run state.
@property(nonatomic) BOOL guestDisplaySleeping;
@property(nonatomic) BOOL softwareCursor;
/// Compositor sampling only; no guest mode, pixel copy or extra render pass.
@property(nonatomic) BOOL nearestNeighborScaling;
@property(nonatomic,readonly) BOOL inputCaptured;
// Switches pointer movement between the always-present absolute tablet
// (precise, matches the host cursor's position 1:1 - the default, right for
// ordinary GUI use) and the always-present relative mouse (small per-move
// deltas - some classic Mac games read the generic USB mouse class and
// misinterpret the tablet's large absolute coordinates as deltas, which
// looks like wildly oversensitive movement). Both USB devices are declared
// in the guest the whole session; only which one actually receives events
// changes live.
@property(nonatomic) BOOL relativeMouseMode;
// Fires whenever relativeMouseMode actually changes, from any path: the
// menu/toolbar action, the ⌘⇧M tap-level shortcut while captured, or the
// focus-loss/dealloc safety net. The toolbar button's own icon only updates
// when its owner is told to refresh, so it needs to hear about all of these,
// not just the one path that happened to originate the change.
@property(nonatomic, copy, nullable) void (^relativeMouseModeObserver)(void);
// The guest driver's own tracked cursor position (SVGA_REG_CURSOR_X/Y, in
// current-mode guest pixels) - authoritative, already past whatever pointer
// acceleration the guest OS applied. Drives the relative-mode cursor sprite
// directly instead of a host-side position estimate accumulated from raw
// input deltas, which drifts from the guest's real (accelerated) position
// the more the mouse moves.
- (void)setGuestCursorTrackedPosition:(NSPoint)guestPixelPosition;
- (void)releaseInputCapture;
- (void)capturePNG:(void (^)(NSData *_Nullable png))completion;
- (void)releasePressedKeys;
/// After disconnecting the producer, retire shared frame storage. Submitted
/// Metal commands retain their resources until GPU completion.
- (void)discardFrameStorage;
/// Hide the old source and reject its in-flight completion, retaining CPU
/// fallback storage and the current guest/system power state.
- (void)invalidatePresentation;
- (void)setGuestCursorVisible:(BOOL)visible;
- (void)setGuestCursorPixels:(NSData *)pixels width:(NSUInteger)width height:(NSUInteger)height hotspot:(NSPoint)hotspot;
/// Read-only development snapshot; no guest calls or screen capture.
- (NSDictionary *)cursorDiagnostics;
/// Geometry only; no GPU readback, pixel copies or guest calls.
- (NSDictionary *)frameDiagnostics;
/// Explicit developer experiment. Valid Metal swap-queue depths are 2 or 3;
/// no frame contents, producer ownership, VSync or guest mode are changed.
- (BOOL)setPresentationDrawableCount:(NSUInteger)count;
/// Developer-controlled until qualified: wait for a swap drawable away from
/// the input thread, selecting the newest immutable source before encoding.
@property(nonatomic) BOOL asynchronousDrawableAcquisition;
/// Explicit developer-only measurement; GPU fingerprints, not full-image CPU
/// readback. Off in normal launches. No guest or input connection is touched.
- (void)setFingerprintMeasurementEnabled:(BOOL)enabled;
/// Bounded diagnostic trace and optional red-title-button marker search in
/// guest backing pixels. Empty rect disables the marker. Never normal input.
- (void)setFingerprintTraceEnabled:(BOOL)enabled;
- (void)setFingerprintMarkerRect:(NSRect)rect;
/// Takes ownership of fd on every path; never copies the shared frame pixels.
- (BOOL)installFrameDescriptor:(int)fd length:(NSUInteger)length error:(NSError **)error;
/// Main-thread only. Completion fires once, after GPU use or a skipped frame.
- (void)presentWidth:(NSUInteger)width height:(NSUInteger)height stride:(NSUInteger)stride
    completion:(void (^)(BOOL presented))completion;
/// GPU-only source path. Producer must finish writes before calling and must
/// not mutate/recycle the surface until completion. Retains storage through
/// Metal use; completion is a resource-release signal, not scanout evidence.
- (void)presentSurface:(IOSurfaceRef)surface width:(NSUInteger)width height:(NSUInteger)height
    completion:(void (^)(BOOL presented))completion;
@end
NS_ASSUME_NONNULL_END
