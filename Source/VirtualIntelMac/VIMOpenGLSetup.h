#import <Foundation/Foundation.h>
@class VIMMachine,VIMGuestAgent,VIMQMPConnection;
NS_ASSUME_NONNULL_BEGIN
/// Main-thread, cold provisioning using the session's existing agent owner.
/// Does not poll, own another socket, inject UI code or stage passwords on disk.
@interface VIMOpenGLSetup : NSObject
/// First installed boot only; never hides a Setup Assistant that needs input.
@property (nonatomic, copy, nullable) void (^showProgress)(BOOL automatic, BOOL automaticLogin);
/// Terminal verifier output only, never the account tool or its private input.
@property (nonatomic, copy, nullable) void (^logVerificationFailure)(NSString *message);
- (instancetype)initWithMachine:(VIMMachine *)machine agent:(VIMGuestAgent *)agent control:(VIMQMPConnection *)control;
- (void)advance:(void (^)(NSDictionary *_Nullable outcome,NSError *_Nullable error))completion;
/// One resumed attempt only; never saves the supplied password on disk.
- (BOOL)providePassword:(NSString *)password error:(NSError *_Nullable *_Nullable)error;
/// Explicit, one-shot recovery retry of an owned interrupted OS installation.
- (void)requestInstallerResume;
@end
NS_ASSUME_NONNULL_END
