#import <Foundation/Foundation.h>
@class VIMGuestAgent, VIMQMPConnection;
NS_ASSUME_NONNULL_BEGIN
/// Production preparation workflow. Never installs into the system. Owns only
/// the dedicated setup-support drive, never the user's or OS installer's disc.
@interface VIMGuestPreparation : NSObject
+ (void)prepareWithAgent:(VIMGuestAgent *)agent report:(NSDictionary *)report
                 runtime:(NSURL *)runtime journal:(NSURL *)journal control:(VIMQMPConnection *)control
     allowHeadlessRepair:(BOOL)allowHeadlessRepair
                progress:(void (^)(NSString *message))progress
              completion:(void (^)(NSDictionary *_Nullable receipt,NSError *_Nullable error))completion;
@end
NS_ASSUME_NONNULL_END
