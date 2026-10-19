#import <Foundation/Foundation.h>
#define VIM_NETWORK_SERVICE @"com.mac.virtualintelmac.network-helper"
#define VIM_NETWORK_PROTOCOL 1
@protocol VIMNetworkHelperProtocol
- (void)statusWithReply:(void (^)(NSDictionary *))reply;
- (void)startMode:(NSString *)mode bridge:(NSString *)bridge reply:(void (^)(NSFileHandle *,NSDictionary *,NSError *))reply;
- (void)stopWithReply:(void (^)(void))reply;
@end
static inline NSError *VIMNetworkError(NSString *message) {
    return [NSError errorWithDomain:@"VirtualIntelMac.Network" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];
}
