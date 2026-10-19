#import <Foundation/Foundation.h>
#import "VIMGuestAgent.h"

@interface VIMGuestAgent (StatusCacheFixture)
- (void)writeData:(NSData *)data path:(NSString *)path completion:(void (^)(NSError *))completion;
@end
@interface StatusAgent : VIMGuestAgent
@property NSUInteger writes, launches, directories;
@property NSUInteger bytes;
@property BOOL notRoot, invalidStat, statTimeout, invalidReport;
@property NSString *build;
@property NSString *statText;
@end
@implementation StatusAgent
- (void)writeData:(NSData *)data path:(NSString *)path completion:(void (^)(NSError *))completion {
    self.writes++;self.bytes=data.length;completion(nil);
}
- (void)runExecutable:(NSString *)path arguments:(NSArray *)arguments timeout:(NSTimeInterval)timeout
           completion:(void (^)(NSDictionary *,NSError *))completion {
    self.launches++;NSString *out=@"";NSNumber *exit=@0;
    if([path isEqual:@"/usr/bin/id"])out=self.notRoot?@"501\n":@"0\n";
    else if([path isEqual:@"/usr/bin/mktemp"])
        out=[NSString stringWithFormat:@"/private/var/run/VirtualIntelMacStatus.%06lu\n",(unsigned long)++self.directories];
    else if([path isEqual:@"/usr/bin/stat"]) {
        if(self.statTimeout){completion(nil,[NSError errorWithDomain:@"Fixture" code:1 userInfo:nil]);return;}
        out=[NSString stringWithFormat:@"0:40700:2:64\n0:%@:1:%lu\n",self.invalidStat?@"120777":@"100700",(unsigned long)self.bytes];
        if(self.statText)out=self.statText;
    } else if([path hasSuffix:@"/guest-status"]) {
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
    puts("Status cache: fresh evidence, bounded process count, private metadata and failure paths PASS");
    return 0;
}}
