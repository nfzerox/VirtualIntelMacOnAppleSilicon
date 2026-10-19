#import <Foundation/Foundation.h>
@class VIMQMPConnection;
@interface VIMUSBClient : NSObject
+ (void)authorizeUsingTerminalWithCompletion:(void (^)(NSError *error))completion;
- (void)prepareWithCompletion:(void (^)(NSError *error))completion;
- (void)checkStorageInterface:(NSDictionary *)identity inUserSession:(BOOL)userSession
                   completion:(void (^)(NSDictionary *result,NSError *error))completion;
- (void)connectDevice:(NSDictionary *)device processID:(pid_t)pid directory:(NSString *)directory
             control:(VIMQMPConnection *)control completion:(void (^)(NSError *error))completion;
- (void)disconnectDevice:(NSString *)identifier control:(VIMQMPConnection *)control completion:(void (^)(NSError *error))completion;
- (void)deviceWasDeleted:(NSString *)identifier control:(VIMQMPConnection *)control;
- (void)stop;
@end
