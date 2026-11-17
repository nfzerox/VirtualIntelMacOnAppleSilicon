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
@interface TemporaryAgent : VIMGuestAgent
@property NSUInteger mode;
@property NSMutableArray *calls;
@end
@implementation TemporaryAgent
- (void)stageFiles:(NSDictionary *)files progress:(void (^)(uint64_t,uint64_t))progress completion:(void (^)(NSDictionary *,NSError *))completion {
    (void)progress;NSCAssert(files.count==1&&files[@"guest-tool"],@"Stage only the requested helper");
    completion(@{@"Directory":@"/private/var/tmp/VirtualIntelMac.tooltest"},nil);
}
- (void)runExecutable:(NSString *)path arguments:(NSArray *)arguments timeout:(NSTimeInterval)timeout completion:(void (^)(NSDictionary *,NSError *))completion {
    [self.calls addObject:path];
    if([path hasSuffix:@"/guest-tool"]){
        NSCAssert(timeout==0&&[arguments isEqual:@[@"--test"]],@"Do not impose a setup deadline or change arguments");
        if(self.mode==2){completion(nil,[NSError errorWithDomain:@"Transport" code:1 userInfo:nil]);return;}
        completion(@{@"exitcode":self.mode==1?@65:@0},nil);return;
    }
    completion(@{@"exitcode":@((self.mode==3&&[path isEqual:@"/bin/rm"])||(self.mode==4&&[path isEqual:@"/bin/chmod"]))},nil);
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
    for(NSUInteger mode=0;mode<5;mode++){
        TemporaryAgent *temporary=[[TemporaryAgent alloc] initWithSocketPath:@"/unused-tool-test"];
        temporary.mode=mode;temporary.calls=[NSMutableArray new];__block BOOL finished=NO;
        [temporary runTemporaryTool:[NSURL fileURLWithPath:@"/bundled/guest-install"] arguments:@[@"--test"] completion:^(NSDictionary *result,NSError *error){
            finished=YES;NSCAssert((error!=nil)==(mode==2||mode==4),@"Cleanup must not invalidate a completed command");
            if(mode==1)NSCAssert([result[@"exitcode"] isEqual:@65],@"Keep the real helper failure");
        }];
        NSCAssert(finished&&temporary.calls.count==(mode==4?1:mode==2?2:mode==3?3:4),@"Do not replay commands or remove uncertain live staging");
        [temporary close];
    }
    puts("PASS: current temporary helper, completion cleanup, native failure, uncertain transport and nonfatal cleanup failure");
}return 0;}
