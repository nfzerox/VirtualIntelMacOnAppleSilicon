#import "../tools/VIMFileTransaction.h"
#include <stdlib.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

static void Check(BOOL value, NSString *message) {
    if (!value) { fprintf(stderr,"FAIL: %s\n",message.UTF8String); exit(1); }
}
int main(void) { @autoreleasepool {
    NSFileManager *fm=NSFileManager.defaultManager;
    NSString *base=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    NSURL *root=[NSURL fileURLWithPath:[base stringByAppendingPathComponent:@"root"]];
    NSURL *payload=[NSURL fileURLWithPath:[base stringByAppendingPathComponent:@"payload"]];
    NSURL *journal=[NSURL fileURLWithPath:[base stringByAppendingPathComponent:@"journal.plist"]];
    NSError *error=nil;
    for(NSURL *dir in @[root,payload]) Check([fm createDirectoryAtURL:dir withIntermediateDirectories:YES attributes:nil error:&error],error.description);
    Check([@"original" writeToURL:[root URLByAppendingPathComponent:@"existing"] atomically:YES encoding:NSUTF8StringEncoding error:&error],error.description);
    for(NSString *name in @[@"existing",@"added"]) Check([@"replacement" writeToURL:[payload URLByAppendingPathComponent:name] atomically:YES encoding:NSUTF8StringEncoding error:&error],error.description);
    VIMFileTransaction *transaction=[VIMFileTransaction createAtURL:journal root:root payload:payload targets:@[@"existing",@"added"] metadata:@{} error:&error];
    Check(transaction!=nil,error.description);
    Check(![transaction verifyPublished:&error],@"Unpublished transaction verified");
    int lock=open([journal.path stringByAppendingString:@".lock"].fileSystemRepresentation,O_CREAT|O_RDWR,0600);
    Check(lock>=0&&flock(lock,LOCK_EX|LOCK_NB)==0,@"Cannot acquire fixture lock");
    Check(![transaction publish:&error],@"Concurrent publication was accepted");close(lock);
    VIMFileTransaction *stale=[VIMFileTransaction loadURL:journal error:&error];
    Check([transaction publish:&error],error.description);
    Check([transaction verifyPublished:&error],error.description);
    NSURL *changed=[root URLByAppendingPathComponent:@"added"];
    Check([@"corrupt" writeToURL:changed atomically:YES encoding:NSUTF8StringEncoding error:&error],error.description);
    Check(![transaction verifyPublished:&error],@"Changed installed file verified");
    Check([@"replacement" writeToURL:changed atomically:YES encoding:NSUTF8StringEncoding error:&error],error.description);
    Check([transaction verifyPublished:&error],error.description);
    Check(![stale rollback:&error],@"Stale transaction was accepted");
    Check(![transaction.state[@"Ready"] boolValue],@"File publication must not imply boot readiness");
    transaction=[VIMFileTransaction loadURL:journal error:&error];
    Check(transaction&&[transaction publish:&error],error.description);
    Check([transaction rollback:&error],error.description);
    transaction=[VIMFileTransaction loadURL:journal error:&error];
    Check(transaction&&[transaction rollback:&error],error.description);
    Check([[NSString stringWithContentsOfURL:[root URLByAppendingPathComponent:@"existing"] encoding:NSUTF8StringEncoding error:&error] isEqual:@"original"],@"Original was not restored");
    Check(![fm fileExistsAtPath:[root URLByAppendingPathComponent:@"added"].path],@"New target was not withdrawn");
    Check(![transaction publish:&error],@"Rolled-back transaction was republished");
    NSMutableDictionary *tampered=[[NSDictionary dictionaryWithContentsOfURL:journal] mutableCopy];
    NSMutableArray *entries=[tampered[@"Entries"] mutableCopy];
    NSMutableDictionary *entry=[entries[0] mutableCopy];entry[@"Target"]=[base stringByAppendingPathComponent:@"unrelated"];
    entries[0]=entry;tampered[@"Entries"]=entries;
    Check([tampered writeToURL:journal atomically:YES],@"Could not save tampered fixture");
    Check([VIMFileTransaction loadURL:journal error:&error]==nil,@"Redirected journal target was accepted");
    printf("PASS: publication, reload, idempotency, rollback, retained originals; fixture %s\n",base.UTF8String);
} return 0; }
