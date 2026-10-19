#import <Foundation/Foundation.h>
#import "VIMGuestAgent.h"

@interface PreferenceAgent : VIMGuestAgent
@property NSString *console;
@property NSMutableArray<NSDictionary *> *commands;
@property BOOL launchFailure, transportFailure;
@end
@implementation PreferenceAgent
- (instancetype)init {
    if((self=[super initWithSocketPath:@"/unused-fixture"])) {
        self.console=@"501\n";self.commands=[NSMutableArray array];
    }return self;
}
- (void)runExecutable:(NSString *)path arguments:(NSArray *)arguments timeout:(NSTimeInterval)timeout
           completion:(void (^)(NSDictionary *,NSError *))completion {
    [self.commands addObject:@{@"Path":path,@"Arguments":arguments}];
    if(self.transportFailure){completion(nil,[NSError errorWithDomain:@"Fixture" code:1 userInfo:nil]);return;}
    BOOL stat=[path isEqual:@"/usr/bin/stat"];
    NSCAssert(stat||[path isEqual:@"/bin/launchctl"],@"Never invoke a shell or change a service");
    completion(@{@"exitcode":@(!stat&&self.launchFailure?1:0),
        @"stdout":[(stat?self.console:@"") dataUsingEncoding:NSUTF8StringEncoding],@"stderr":NSData.data},nil);
}
@end
@interface ShutdownControl : NSObject
@property NSUInteger requests;
@property id reply;
@property NSError *failure;
@end
@implementation ShutdownControl
- (void)execute:(NSString *)command arguments:(NSDictionary *)arguments completion:(void (^)(id,NSError *))completion {
    self.requests++;
    NSCAssert([command isEqual:@"guest-exec"]&&[arguments[@"path"] isEqual:@"/sbin/shutdown"]&&
        ([arguments[@"arg"] isEqual:@[@"-h",@"now"]])&&[arguments[@"capture-output"] isEqual:@NO],@"Use native macOS shutdown, not ACPI sleep or guest-shutdown's absent success reply");
    completion(self.reply,self.failure);
}
- (void)close {}
@end
static void Open(PreferenceAgent *agent,id pane,BOOL failure) {
    __block BOOL done=NO;
    [agent openPreferencesPane:pane completion:^(NSError *error){
        NSCAssert((error!=nil)==failure,@"Guest preference result must be accurate");done=YES;
    }];NSCAssert(done,@"Synchronous fixture");
}
int main(void) {@autoreleasepool {
    for(NSUInteger scenario=0;scenario<3;scenario++) {
        VIMGuestAgent *agent=[[VIMGuestAgent alloc] initWithSocketPath:@"/unused-fixture"];
        ShutdownControl *control=[ShutdownControl new];control.reply=scenario==0?@{@"pid":@42}:@{};
        if(scenario==2)control.failure=[NSError errorWithDomain:@"Fixture" code:1 userInfo:nil];
        [agent setValue:control forKey:@"connection"];
        __block BOOL done=NO;
        [agent requestShutdownWithCompletion:^(NSError *error){NSCAssert((error!=nil)==(scenario!=0),@"Shutdown acknowledges only a valid spawn");done=YES;}];
        NSCAssert(done&&control.requests==1,@"Never poll dead agent or retry uncertain shutdown");
    }
    for(NSString *pane in @[@"sharing",@"microphone"]) {
        PreferenceAgent *agent=[PreferenceAgent new];Open(agent,pane,NO);
        NSString *url=[pane isEqual:@"sharing"]?@"x-apple.systempreferences:com.apple.preferences.sharing":
            @"x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone";
        NSCAssert(agent.commands.count==2,@"One console lookup and one GUI launch");
        NSCAssert(([agent.commands[0][@"Arguments"] isEqual:@[@"-f",@"%u",@"/dev/console"]]),@"Discover the current guest account, do not assume UID");
        NSCAssert(([agent.commands[1][@"Arguments"] isEqual:@[@"asuser",@"501",@"/usr/bin/sudo",@"-n",@"-u",@"#501",@"/usr/bin/open",url]]),@"Drop root, enter Aqua and never prompt for credentials");
    }
    for(NSString *console in @[@"0\n",@"248\n",@"-1\n",@"4294967295\n",@"501;open /tmp\n",@"501\n502\n"]) {
        PreferenceAgent *agent=[PreferenceAgent new];agent.console=console;Open(agent,@"sharing",YES);
        NSCAssert(agent.commands.count==1,@"No GUI launch without a valid signed-in account");
    }
    for(id invalid in @[@"/tmp/arbitrary",NSNull.null]) {
        PreferenceAgent *agent=[PreferenceAgent new];Open(agent,invalid,YES);
        NSCAssert(agent.commands.count==0,@"Only the two fixed panes are accepted");
    }
    PreferenceAgent *failed=[PreferenceAgent new];failed.launchFailure=YES;Open(failed,@"sharing",YES);
    NSCAssert(failed.commands.count==2,@"Do not retry an uncertain or rejected GUI launch");
    PreferenceAgent *transport=[PreferenceAgent new];transport.transportFailure=YES;Open(transport,@"sharing",YES);
    NSCAssert(transport.commands.count==1,@"Transport failure must not launch or retry");
    PreferenceAgent *closed=[PreferenceAgent new];[closed close];Open(closed,@"sharing",YES);
    NSCAssert(closed.commands.count==0,@"Closed agent cannot open guest UI");
    puts("Guest preferences: whitelist, Aqua user, no password prompt and failure paths PASS");return 0;
}}
