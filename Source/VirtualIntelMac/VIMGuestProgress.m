#import "VIMGuestProgress.h"
#import "VIMGuestAgent.h"
static NSError *ProgressError(NSString *message){return [NSError errorWithDomain:@"VirtualIntelMac.GuestProgress" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];}
static NSDictionary *ProgressJSON(NSDictionary *command) {
    id value=[command[@"exitcode"] isEqual:@0]?[NSJSONSerialization JSONObjectWithData:command[@"stdout"]?:NSData.data options:0 error:NULL]:nil;
    return [value isKindOfClass:NSDictionary.class]?value:nil;
}
@interface VIMGuestProgress ()
@property VIMGuestAgent *agent;
@property(nonatomic) NSDictionary *receipt;
@end
@implementation VIMGuestProgress
+ (instancetype)progressWithAgent:(VIMGuestAgent *)agent receipt:(NSDictionary *)receipt {
    NSString *directory=receipt[@"Directory"],*tool=receipt[@"Tool"];
    NSRegularExpression *pattern=[NSRegularExpression regularExpressionWithPattern:@"^/private/var/tmp/VirtualIntelMacUI\\.[A-Za-z0-9]{6}$" options:0 error:NULL];
    if(![directory isKindOfClass:NSString.class]||[pattern numberOfMatchesInString:directory options:0 range:NSMakeRange(0,directory.length)]!=1||
       ![tool isKindOfClass:NSString.class]||(![tool hasPrefix:@"/private/var/tmp/VirtualIntelMac."]&&![tool hasPrefix:@"/private/var/db/VirtualIntelMac."])||![tool.lastPathComponent isEqual:@"guest-progress"]||
       [tool.pathComponents containsObject:@".."]||tool.pathComponents.count!=6)return nil;
    VIMGuestProgress *progress=[self new];progress.agent=agent;progress.receipt=receipt;return progress;
}
+ (void)startWithAgent:(VIMGuestAgent *)agent staging:(NSString *)directory completion:(void (^)(VIMGuestProgress *,NSError *))completion {
    NSString *tool=[directory stringByAppendingPathComponent:@"guest-progress"];
    [agent runExecutable:@"/bin/chmod" arguments:@[@"0700",tool] timeout:15 completion:^(NSDictionary *mode,NSError *error){
        if(error||![mode[@"exitcode"] isEqual:@0]){completion(nil,error?:ProgressError(@"Could not prepare the guest setup window."));return;}
        [agent runExecutable:tool arguments:@[@"--start",[directory stringByAppendingPathComponent:@"SupportIcon.png"]] timeout:60 completion:^(NSDictionary *result,NSError *failure){
            NSDictionary *started=ProgressJSON(result);NSMutableDictionary *receipt=[started mutableCopy];receipt[@"Tool"]=tool;
            VIMGuestProgress *progress=[self progressWithAgent:agent receipt:receipt];
            NSString *detail=[[NSString alloc] initWithData:result[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding];
            completion(progress,progress?nil:failure?:ProgressError(detail.length?detail:@"The guest setup window could not open. Nothing was installed."));
        }];
    }];
}
- (void)updateMode:(NSString *)mode fraction:(double)fraction message:(NSString *)message completion:(void (^)(NSError *))completion {
    [self.agent runExecutable:self.receipt[@"Tool"] arguments:@[@"--update",self.receipt[@"Directory"],mode,[@(fraction) stringValue],message] timeout:15 completion:^(NSDictionary *command,NSError *error){
        if(completion)completion(error?:([ProgressJSON(command)[@"Updated"] isEqual:@YES]?nil:ProgressError(@"The guest setup window did not acknowledge its status.")));
    }];
}
- (void)waitForRestartChoice:(void (^)(NSString *,NSError *))completion {
    [self.agent runExecutable:self.receipt[@"Tool"] arguments:@[@"--wait",self.receipt[@"Directory"]] timeout:180 completion:^(NSDictionary *command,NSError *error){
        NSString *choice=ProgressJSON(command)[@"Action"];
        if(![@[@"restart",@"postpone"] containsObject:choice])completion(nil,error?:ProgressError(@"The guest restart choice could not be confirmed. Restart this Mac manually when ready."));
        else completion(choice,nil);
    }];
}
@end
