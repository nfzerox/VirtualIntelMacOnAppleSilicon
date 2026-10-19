#import <Foundation/Foundation.h>

#define VIM_USB_SERVICE @"com.mac.virtualintelmac.usb-helper"
#define VIM_USB_PROTOCOL 1

/// No paths, commands, disk operations or executable uploads cross this API.
/// The helper accepts only a listening Unix socket and an exact external USB
/// identity. It authenticates both the app and its unprivileged QEMU child.
@protocol VIMUSBHelperProtocol
- (void)statusWithReply:(void (^)(NSDictionary *status))reply;
/// Read-only development diagnosis; never claims, captures or transfers USB data.
- (void)checkStorageInterface:(NSDictionary *)identity inUserSession:(BOOL)userSession
                        reply:(void (^)(NSDictionary *result,NSError *error))reply;
- (void)beginDevice:(NSDictionary *)identity listener:(NSFileHandle *)listener
          processID:(NSNumber *)processID reply:(void (^)(NSString *sessionID,NSError *error))reply;
- (void)waitForCapture:(NSString *)sessionID reply:(void (^)(NSError *error))reply;
- (void)endSession:(NSString *)sessionID reply:(void (^)(void))reply;
@end

static inline NSXPCInterface *VIMUSBInterface(void) {
    return [NSXPCInterface interfaceWithProtocol:@protocol(VIMUSBHelperProtocol)];
}
static inline NSError *VIMUSBError(NSString *message) {
    return [NSError errorWithDomain:@"VirtualIntelMac.USB" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];
}
