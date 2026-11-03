// Deterministic pasteboard ownership/data publication race, private board only.
#define main VIMEmbeddedClipboardMain
#include "../tools/guest-clipboard.m"
#undef main
@interface PublicationFixture : ClipboardAgent
@property NSPasteboard *board;
@property NSMutableArray *messages;
@property NSInteger lock;
@end
@implementation PublicationFixture
- (NSInteger)consoleLockState {return self.lock;}
- (NSPasteboard *)pasteboard {return self.board;}
- (void)send:(NSDictionary *)message {[self.messages addObject:message];}
@end
int main(void){@autoreleasepool{
    PublicationFixture *agent=[PublicationFixture new];
    agent.board=[NSPasteboard pasteboardWithUniqueName];agent.messages=[NSMutableArray new];
    @try {
        [agent.board clearContents];[agent.board setString:@"before" forType:NSPasteboardTypeString];
        [agent receive:@{@"type":@"ready",@"protocol":@2}];
        NSInteger cleared=[agent.board clearContents];
        [agent tick:nil]; // The owner has not provided its content yet.
        [agent.board addTypes:@[NSPasteboardTypeString] owner:nil];
        [agent.board setString:@"after" forType:NSPasteboardTypeString];
        printf("clear_count=%ld completed_count=%ld\n",(long)cleared,(long)agent.board.changeCount);
        usleep(250000); // A single normal 200ms sampling interval, not a data deadline.
        [agent tick:nil];
        BOOL delivered=agent.messages.count==1&&[agent.messages[0][@"text"] isEqual:@"after"];
        printf("deferred publication delivered=%d\n",delivered);if(!delivered)return 1;
        [agent.board clearContents];[agent tick:nil];
        agent.lock=1;[agent tick:nil]; // Lock during the pending-read backoff.
        [agent.board setString:@"behind lock" forType:NSPasteboardTypeString];
        agent.lock=0;usleep(250000);[agent tick:nil];
        if(agent.messages.count!=1){fprintf(stderr,"FAIL: pending content escaped the lock boundary\n");return 1;}
        [agent.board clearContents];
        [agent.board setData:[NSData dataWithBytes:"x" length:1] forType:@"public.png"];
        [agent tick:nil];
        if(agent.observedChange!=agent.board.changeCount||agent.publication.delay){fprintf(stderr,"FAIL: unsupported content retained a pending read\n");return 1;}
        puts("PASS: deferred text publication, lock boundary and unsupported content; private pasteboard only");return 0;
    } @finally {[agent.board releaseGlobally];}
}}
