#import "../tools/VIMFileTransaction.h"
#include <stdlib.h>
static void Check(BOOL ok,NSString *reason){if(!ok){fprintf(stderr,"FAIL: %s\n",reason.UTF8String);exit(1);}}
int main(int argc,const char **argv){@autoreleasepool {
    NSError *error=nil;
    if(argc==4){
        VIMFileTransaction *t=[VIMFileTransaction loadURL:[NSURL fileURLWithPath:@(argv[1])] error:&error];
        Check(t!=nil,error.description);
        setenv("VIM_TRANSACTION_INTERRUPT",argv[3],1);
        Check([@(argv[2]) isEqual:@"publish"]?[t publish:&error]:[t rollback:&error],error.description);
        return 0;
    }
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *base=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]];
    for(NSString *operation in @[@"publish",@"rollback"]) {
        unsigned interrupted=0;
        for(unsigned point=1;point<100;point++){
            NSURL *fixture=[base URLByAppendingPathComponent:[NSString stringWithFormat:@"%@-%u",operation,point]];
            NSURL *root=[fixture URLByAppendingPathComponent:@"root"],*payload=[fixture URLByAppendingPathComponent:@"payload"],*journal=[fixture URLByAppendingPathComponent:@"journal.plist"];
            for(NSURL *dir in @[root,payload])Check([fm createDirectoryAtURL:dir withIntermediateDirectories:YES attributes:nil error:&error],error.description);
            Check([@"old" writeToURL:[root URLByAppendingPathComponent:@"existing"] atomically:YES encoding:NSUTF8StringEncoding error:&error],error.description);
            for(NSString *name in @[@"existing",@"added"])Check([@"new" writeToURL:[payload URLByAppendingPathComponent:name] atomically:YES encoding:NSUTF8StringEncoding error:&error],error.description);
            VIMFileTransaction *t=[VIMFileTransaction createAtURL:journal root:root payload:payload targets:@[@"existing",@"added"] metadata:@{} error:&error];Check(t!=nil,error.description);
            if([operation isEqual:@"rollback"])Check([t publish:&error],error.description);
            NSTask *child=[NSTask new];child.executableURL=[NSURL fileURLWithPath:NSProcessInfo.processInfo.arguments[0]];
            child.arguments=@[journal.path,operation,[NSString stringWithFormat:@"%u",point]];
            Check([child launchAndReturnError:&error],error.description);[child waitUntilExit];
            Check(child.terminationStatus==77||child.terminationStatus==0,@"Unexpected fixture child failure");
            t=[VIMFileTransaction loadURL:journal error:&error];Check(t!=nil,error.description);
            if([operation isEqual:@"publish"])Check([t publish:&error],error.description);
            Check([t rollback:&error],error.description);
            Check([[NSString stringWithContentsOfURL:[root URLByAppendingPathComponent:@"existing"] encoding:NSUTF8StringEncoding error:&error] isEqual:@"old"],@"Interrupted transaction lost original");
            Check(![fm fileExistsAtPath:[root URLByAppendingPathComponent:@"added"].path],@"Interrupted rollback retained added target");
            if(child.terminationStatus==0)break;
            interrupted++;
        }
        Check(interrupted>0,@"No interruptions exercised");
        printf("PASS: %s recovery at %u journal/rename interruption points\n",operation.UTF8String,interrupted);
    }
    printf("Retained fixtures: %s\n",base.path.UTF8String);
}return 0;}
