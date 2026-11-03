#import <Foundation/Foundation.h>
#import "VIMGuestAgent.h"
@interface VIMGuestAgent (IdentityCheck)
- (void)confirmPrivilegedAgent:(void (^)(NSError *))completion;
@end
@interface IdentityAgent : VIMGuestAgent
@property NSDictionary *reply;
@property BOOL requestedShell;
@end
@implementation IdentityAgent
- (void)runExecutable:(NSString *)path arguments:(NSArray *)arguments timeout:(NSTimeInterval)timeout
            completion:(void (^)(NSDictionary *,NSError *))completion {
    self.requestedShell=[path isEqual:@"/bin/sh"]&&[arguments isEqual:@[@"-c",@"test \"${EUID-}\" = 0 && printf '0\\n'"]];
    completion(self.reply,nil);
}
@end
int main(void){@autoreleasepool{
    IdentityAgent *agent=[[IdentityAgent alloc] initWithSocketPath:@"/unused-identity-test"];
    NSArray *cases=@[@[@0,@"0\n",@YES],@[@0,@"501\n",@NO],@[@255,@"",@NO],@[@1,@"0\n",@NO]];
    for(NSArray *entry in cases){
        agent.reply=@{@"exitcode":entry[0],@"stdout":[entry[1] dataUsingEncoding:NSUTF8StringEncoding]};
        __block BOOL called=NO;
        [agent confirmPrivilegedAgent:^(NSError *error){called=YES;NSCAssert((error==nil)==[entry[2] boolValue],@"Only an actual successful root reply is accepted");}];
        NSCAssert(called&&agent.requestedShell,@"Recovery identity does not require optional /usr/bin/id");
    }
    [agent close];puts("PASS: native shell root check, non-root and failed replies; no optional id dependency");
}return 0;}
