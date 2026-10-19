#import <Foundation/Foundation.h>
@class VIMMachine;
NS_ASSUME_NONNULL_BEGIN
/// Local-only DNS-SD preserves the SMB port through Finder's RPC discovery.
/// No TCP proxy, credentials, privileged ports or LAN advertisements.
@interface VIMSMBService : NSObject
@property(nonatomic,readonly) NSURL *URL;
@property(nonatomic,readonly,nullable) NSError *error;
+ (NSURL *)URLForMachine:(VIMMachine *)machine;
- (instancetype)initWithMachine:(VIMMachine *)machine port:(uint16_t)port;
- (BOOL)start:(NSError **)error;
- (void)stop;
@end
NS_ASSUME_NONNULL_END
