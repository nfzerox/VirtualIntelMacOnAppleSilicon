#import <Foundation/Foundation.h>
#import "VIMGuestAgent.h"

@interface VIMGuestAgent (StatusCacheFixture)
- (void)writeData:(NSData *)data path:(NSString *)path completion:(void (^)(NSError *))completion;
- (void)runExecutable:(NSString *)path arguments:(NSArray *)arguments standardInput:(NSData *)input
              timeout:(NSTimeInterval)timeout pollInterval:(NSTimeInterval)interval
           completion:(void (^)(NSDictionary *,NSError *))completion;
@end
@interface StatusAgent : VIMGuestAgent
@property NSUInteger writes, launches, directories;
@property NSUInteger bytes;
@property BOOL notRoot, invalidStat, statTimeout, invalidReport;
@property NSString *build;
@property NSString *statText;
@property BOOL holdStatus;
@property(copy) void (^heldStatus)(NSDictionary *,NSError *);
@end
@implementation StatusAgent
- (void)runExecutable:(NSString *)path arguments:(NSArray *)arguments standardInput:(NSData *)input
              timeout:(NSTimeInterval)timeout pollInterval:(NSTimeInterval)interval
           completion:(void (^)(NSDictionary *,NSError *))completion {
    NSCAssert([path hasSuffix:@"/guest-status"]&&!input&&timeout==0&&interval==1,
        @"A slow status child stays owned with a per-process one-second cadence");
    [self runExecutable:path arguments:arguments timeout:timeout completion:completion];
}
- (void)writeData:(NSData *)data path:(NSString *)path completion:(void (^)(NSError *))completion {
    self.writes++;self.bytes=data.length;completion(nil);
}
- (void)runExecutable:(NSString *)path arguments:(NSArray *)arguments timeout:(NSTimeInterval)timeout
           completion:(void (^)(NSDictionary *,NSError *))completion {
    self.launches++;NSString *out=@"";NSNumber *exit=@0;
    if([path isEqual:@"/bin/sh"]&&
       [arguments isEqual:@[@"-c",@"test \"${EUID-}\" = 0 && printf '0\\n'"]]) {
        // Match the real Recovery-compatible identity check. Period recovery
        // media need not contain /usr/bin/id; do not mock an obsolete path.
        out=self.notRoot?@"":@"0\n";exit=self.notRoot?@1:@0;
    }
    else if([path isEqual:@"/usr/bin/mktemp"])
        out=[NSString stringWithFormat:@"/private/var/run/VirtualIntelMacStatus.%06lu\n",(unsigned long)++self.directories];
    else if([path isEqual:@"/usr/bin/stat"]) {
        if(self.statTimeout){completion(nil,[NSError errorWithDomain:@"Fixture" code:1 userInfo:nil]);return;}
        out=[NSString stringWithFormat:@"0:40700:2:64\n0:%@:1:%lu\n",self.invalidStat?@"120777":@"100700",(unsigned long)self.bytes];
        if(self.statText)out=self.statText;
    } else if([path hasSuffix:@"/guest-status"]) {
        if(self.holdStatus){self.heldStatus=completion;return;}
        NSDictionary *report=@{@"ProtocolVersion":@1,@"Ready":@(self.invalidReport),@"ProductBuildVersion":self.build?:@"first"};
        NSData *json=[NSJSONSerialization dataWithJSONObject:report options:0 error:NULL];
        out=[[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding];
    } else NSCAssert([path isEqual:@"/bin/chmod"],@"Only the production staging/status operations may run");
    completion(@{@"exitcode":exit,@"stdout":[out dataUsingEncoding:NSUTF8StringEncoding],@"stderr":NSData.data},nil);
}
@end

static NSDictionary *Collect(StatusAgent *agent,NSData *tool,BOOL expectError) {
    __block BOOL done=NO;__block NSDictionary *answer=nil;
    [agent collectStatusWithTool:tool completion:^(NSDictionary *report,NSError *error){
        NSCAssert((error!=nil)==expectError,@"Status error must propagate");answer=report;done=YES;
    }];
    NSCAssert(done,@"Fixture is synchronous");return answer;
}
int main(void) {@autoreleasepool {
    NSData *tool=[@"one" dataUsingEncoding:NSUTF8StringEncoding];
    StatusAgent *agent=[[StatusAgent alloc] initWithSocketPath:@"/unused-fixture"];
    NSCAssert([Collect(agent,tool,NO)[@"ProductBuildVersion"] isEqual:@"first"],@"Initial report");
    NSCAssert(agent.writes==1&&agent.launches==4,@"Cold status uses one upload and four processes");
    agent.build=@"updated";NSUInteger launches=agent.launches;
    NSCAssert([Collect(agent,tool,NO)[@"ProductBuildVersion"] isEqual:@"updated"],@"Never cache guest build evidence");
    NSCAssert(agent.writes==1&&agent.launches-launches==2,@"Warm status uses metadata check plus fresh execution only");
    agent.statTimeout=YES;Collect(agent,tool,YES);
    NSCAssert(agent.writes==1,@"Transport uncertainty must not restage/retry a write");
    agent.statTimeout=NO;Collect(agent,tool,NO);
    NSCAssert(agent.writes==1,@"A later valid read reuses the secured file");
    agent.invalidStat=YES;Collect(agent,tool,NO);agent.invalidStat=NO;
    NSCAssert(agent.writes==2,@"Symlink metadata must never authorize cached execution");
    for(NSString *bad in @[@"501:40700:2:64\n0:100700:1:3\n",@"0:40777:2:64\n0:100700:1:3\n",
                           @"0:40700:2:64\n501:100700:1:3\n",@"0:40700:2:64\n0:100700:2:3\n",
                           @"0:40700:2:64\n0:100700:1:4\n",@"malformed\n"]) {
        NSUInteger before=agent.writes;agent.statText=bad;Collect(agent,tool,NO);agent.statText=nil;
        NSCAssert(agent.writes==before+1,@"Untrusted owner/mode/link/size metadata must restage safely");
    }
    NSUInteger before=agent.writes;
    Collect(agent,[@"two" dataUsingEncoding:NSUTF8StringEncoding],NO);
    NSCAssert(agent.writes==before+1,@"Same-size changed payload must be restaged");
    agent.invalidReport=YES;Collect(agent,[@"two" dataUsingEncoding:NSUTF8StringEncoding],YES);
    NSCAssert(agent.writes==before+1,@"Invalid evidence does not become success or trigger repeated uploads");
    StatusAgent *user=[[StatusAgent alloc] initWithSocketPath:@"/unused-fixture"];user.notRoot=YES;
    Collect(user,tool,YES);NSCAssert(user.writes==0,@"Unprivileged agent must not stage a probe");
    [agent close];Collect(agent,tool,YES);NSCAssert(agent.writes==before+1,@"Closed agent cannot use cache");
    StatusAgent *slow=[[StatusAgent alloc] initWithSocketPath:@"/unused-fixture"];slow.holdStatus=YES;
    __block BOOL done=NO;
    [slow collectStatusWithTool:tool completion:^(NSDictionary *report,NSError *error){
        NSCAssert(!error&&[report[@"ProductBuildVersion"] isEqual:@"late"],@"Late native evidence is preserved");done=YES;
    }];
    NSCAssert(!done&&slow.launches==4&&slow.writes==1,@"Only one status child is running");
    Collect(slow,tool,YES);
    NSCAssert(slow.launches==4&&slow.writes==1,@"A concurrent observation cannot spawn another probe");
    void (^finish)(NSDictionary *,NSError *)=slow.heldStatus;slow.heldStatus=nil;slow.holdStatus=NO;
    NSData *late=[NSJSONSerialization dataWithJSONObject:@{@"ProtocolVersion":@1,@"Ready":@NO,@"ProductBuildVersion":@"late"} options:0 error:NULL];
    finish(@{@"exitcode":@0,@"stdout":late,@"stderr":NSData.data},nil);
    NSCAssert(done,@"Original observation completes from the same child");
    Collect(slow,tool,NO);
    NSCAssert(slow.launches==6&&slow.writes==1,@"After exit, a new observation reads fresh state without restaging");
    [slow close];
    puts("Status cache: fresh evidence, one slow child, private metadata and failure paths PASS");
    return 0;
}}
