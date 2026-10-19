// Native policy fixture: only a uniquely named private pasteboard, never the
// user's general clipboard. No guest/host lock or security setting is changed.
#define main VIMEmbeddedClipboardMain
#include "../tools/guest-clipboard.m"
#undef main

@interface LockFixture : ClipboardAgent
@property NSInteger lock;
@property NSPasteboard *privateBoard;
@property NSMutableArray *messages;
@property BOOL stopped;
@property NSUInteger lockReads;
@property NSDictionary *powerState;
@property BOOL forbidPasteboard;
@end
@implementation LockFixture
- (NSInteger)consoleLockState {self.lockReads++;return self.lock;}
- (NSPasteboard *)pasteboard {NSCAssert(!self.forbidPasteboard,@"Metadata channel must never touch pasteboard");return self.privateBoard;}
- (NSDictionary *)displayPowerState {return self.powerState;}
- (void)send:(NSDictionary *)message {[self.messages addObject:message];}
- (void)stop {self.stopped=YES;}
@end
static void Put(NSPasteboard *board,NSString *text) {
    [board clearContents];NSCAssert([board setString:text forType:NSPasteboardTypeString],@"Private fixture write");
}
static NSDictionary *Text(NSUInteger sequence,NSString *text) {
    return @{@"type":@"text",@"sequence":@(sequence),@"text":text};
}
int main(void){@autoreleasepool {
    NSCAssert(ActiveConsoleUser(),@"Run fixture as current console user");
    NSCAssert(VIMConsoleLockStateFromProperty(NULL)==-1,@"Unknown lock state");
    NSCAssert(VIMConsoleLockStateFromProperty(kCFBooleanTrue)==1,@"Locked flag");
    NSCAssert(VIMConsoleLockStateFromProperty(kCFBooleanFalse)==0,@"Unlocked flag");
    int zero=0;CFNumberRef number=CFNumberCreate(NULL,kCFNumberIntType,&zero);
    NSCAssert(VIMConsoleLockStateFromProperty(number)==-1,@"Do not accept an untyped number");CFRelease(number);
    LockFixture *agent=[LockFixture new];agent.messages=[NSMutableArray new];
    agent.privateBoard=[NSPasteboard pasteboardWithUniqueName];agent.lock=1;
    @try {
        Put(agent.privateBoard,@"fixture-before");
        [agent receive:@{@"type":@"ready",@"protocol":@2}];
        NSCAssert(agent.authenticated&&!agent.stopped,@"Locked handshake remains alive");
        [agent tick:nil];NSCAssert(agent.lockReads==0,@"Unchanged idle tick has no lock query");
        Put(agent.privateBoard,@"fixture-locked-local");[agent tick:nil];
        NSCAssert(agent.messages.count==0,@"No locked local export");
        agent.lock=0;[agent tick:nil];NSCAssert(agent.messages.count==0,@"No old export on unlock");
        Put(agent.privateBoard,@"fixture-unlocked-local");[agent tick:nil];
        NSCAssert(agent.messages.count==1,@"Unlocked local export");
        agent.lock=1;[agent receive:Text(1,@"fixture-locked-remote")];
        NSCAssert([[agent.privateBoard stringForType:NSPasteboardTypeString] isEqual:@"fixture-unlocked-local"],@"No locked import");
        NSCAssert(agent.remoteSequence==1,@"Consume rejected text sequence");
        [agent receive:@{@"type":@"ping",@"sequence":@1}];
        NSCAssert([agent.messages.lastObject[@"type"] isEqual:@"pong"]&&!agent.stopped,@"Locked heartbeat");
        agent.lock=0;[agent receive:Text(2,@"fixture-unlocked-remote")];
        NSCAssert([[agent.privateBoard stringForType:NSPasteboardTypeString] isEqual:@"fixture-unlocked-remote"],@"Unlocked import");
        agent.lock=-1;[agent receive:Text(3,@"fixture-unknown-remote")];
        NSCAssert([[agent.privateBoard stringForType:NSPasteboardTypeString] isEqual:@"fixture-unlocked-remote"],@"Unknown state denies import");
        NSUInteger sent=agent.messages.count;
        Put(agent.privateBoard,@"fixture-unknown-local");[agent tick:nil];
        agent.lock=0;[agent tick:nil];NSCAssert(agent.messages.count==sent,@"Unknown state denies export and replay");
        [agent receive:@{@"type":@"stop"}];NSCAssert(agent.stopped,@"Authenticated quit retires GUI helper");agent.stopped=NO;
        agent.lastHeartbeat=NSProcessInfo.processInfo.systemUptime-46;
        [agent tick:nil];NSCAssert(agent.stopped,@"Lost host retires GUI helper without serial EOF");agent.stopped=NO;
        [agent receive:@{@"type":@"ping",@"sequence":@2}];[agent tick:nil];
        NSCAssert(!agent.stopped,@"Valid heartbeat resets watchdog");
        LockFixture *power=[LockFixture new];power.displayPowerOnly=YES;power.forbidPasteboard=YES;
        power.messages=[NSMutableArray new];power.powerState=@{@"CurrentPowerState":@4};
        [power receive:@{@"type":@"ready",@"protocol":@2}];
        NSDictionary *message=power.messages.lastObject;
        NSCAssert([message[@"type"] isEqual:@"display-power"]&&CFGetTypeID((__bridge CFTypeRef)message[@"sleeping"])==CFBooleanGetTypeID()&&![message[@"sleeping"] boolValue],@"Initial awake wire Boolean");
        [power tick:nil];NSCAssert(power.messages.count==1,@"No display polling in idle tick");
        power.powerState=@{@"CurrentPowerState":@1};[power emitDisplayPower];[power emitDisplayPower];
        NSCAssert(power.messages.count==2&&[power.messages.lastObject[@"sleeping"] isEqual:@YES],@"Sleep notification deduplicated");
        power.powerState=@{@"CurrentPowerState":@3};[power emitDisplayPower];
        NSCAssert(power.messages.count==3&&[power.messages.lastObject[@"sleeping"] isEqual:@NO],@"Dim display is usable, not asleep");
        power.powerState=@{};[power emitDisplayPower];NSCAssert(power.messages.count==3,@"Unknown registry state is not guessed");
        [power receive:Text(1,@"not clipboard")];NSCAssert(power.stopped,@"Metadata channel rejects clipboard payloads");
        puts("PASS clipboard lock/unknown/unlock policy; explicit quit, lost-host watchdog and heartbeat recovery; no idle lock query; private pasteboard only");
    } @finally {[agent.privateBoard releaseGlobally];}
    return 0;
}}
