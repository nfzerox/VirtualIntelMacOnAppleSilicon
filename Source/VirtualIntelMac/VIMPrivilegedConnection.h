#import <Foundation/Foundation.h>

/// Typed helper control only. XPC validates both sides on every supported host.
@interface VIMPrivilegedConnection : NSObject
@property(copy) void (^invalidationHandler)(void);
- (instancetype)initWithMachServiceName:(NSString *)name requirement:(NSString *)requirement;
- (id)remoteObjectProxyWithErrorHandler:(void (^)(NSError *))handler;
- (void)resume;
- (void)invalidate;
@end
