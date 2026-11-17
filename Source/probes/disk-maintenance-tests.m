#import <Foundation/Foundation.h>
#import "VIMDiskMaintenance.h"
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/xattr.h>
#include <sys/acl.h>

static NSURL *tool;
static NSFileManager *fm;
static BOOL testLockDuringProgress;
static NSString *ACLText(NSURL *URL) {
    acl_t acl=acl_get_file(URL.fileSystemRepresentation,ACL_TYPE_EXTENDED);NSCAssert(acl,@"Read disk ACL");
    char *text=acl_to_text(acl,NULL);NSCAssert(text,@"Read ACL text");NSString *value=@(text);acl_free(text);acl_free(acl);return value;
}
static NSString *Run(NSArray *arguments) {
    NSTask *task=[NSTask new];task.executableURL=tool;task.arguments=arguments;
    NSPipe *pipe=[NSPipe pipe];task.standardOutput=pipe;task.standardError=pipe;
    NSError *error=nil;NSCAssert([task launchAndReturnError:&error],@"Launch: %@",error);
    NSData *output=[pipe.fileHandleForReading readDataToEndOfFile];[task waitUntilExit];
    NSString *text=[[NSString alloc] initWithData:output encoding:NSUTF8StringEncoding];
    NSCAssert(task.terminationStatus==0,@"Tool %@: %@",arguments,text);return text;
}
static NSURL *Fixture(NSURL *root,NSString *name) {
    NSURL *package=[root URLByAppendingPathComponent:[name stringByAppendingString:@".vintelmac"]];
    for(NSString *dir in @[@"Disks",@"State"])[fm createDirectoryAtURL:[package URLByAppendingPathComponent:dir] withIntermediateDirectories:YES attributes:nil error:nil];
    [@{@"Identifier":NSUUID.UUID.UUIDString,@"Disk":@"Disks/System.qcow2"} writeToURL:[package URLByAppendingPathComponent:@"Machine.plist"] atomically:YES];
    NSURL *raw=[root URLByAppendingPathComponent:[name stringByAppendingString:@".raw"]];
    int fd=open(raw.fileSystemRepresentation,O_CREAT|O_RDWR|O_EXCL,0600);NSCAssert(fd>=0,@"Create fixture");
    NSCAssert(ftruncate(fd,16<<20)==0,@"Capacity");
    NSMutableData *data=[NSMutableData dataWithLength:2<<20];memset(data.mutableBytes,'A',data.length);
    NSCAssert(write(fd,data.bytes,data.length)==(ssize_t)data.length,@"Fixture contents");close(fd);
    Run(@[@"convert",@"-f",@"raw",@"-O",@"qcow2",@"-S",@"0",raw.path,[package URLByAppendingPathComponent:@"Disks/System.qcow2"].path]);
    return package;
}
static NSDictionary *Maintain(NSURL *package,BOOL compressed,BOOL expectSuccess) {
    __block BOOL done=NO,sawProgress=NO;__block NSDictionary *result=nil;__block NSError *failure=nil;
    NSString *identifier=[NSDictionary dictionaryWithContentsOfURL:[package URLByAppendingPathComponent:@"Machine.plist"]][@"Identifier"];
    NSError *error=nil;
    BOOL started=[VIMDiskMaintenance startForPackage:package identifier:identifier qemuImage:tool compressed:compressed
        progress:^(NSString *phase,double fraction){
            NSCAssert(NSThread.isMainThread&&phase.length&&fraction<=1,@"UI progress contract");sawProgress=YES;
            if(testLockDuringProgress&&fraction==.12) {
                int fd=open([package URLByAppendingPathComponent:@"State/Session.lock"].fileSystemRepresentation,O_RDWR);
                NSCAssert(fd>=0&&flock(fd,LOCK_EX|LOCK_NB)!=0,@"Session lock covers conversion progress");close(fd);testLockDuringProgress=NO;
            }
        }
        completion:^(NSDictionary *value,NSError *issue){NSCAssert(NSThread.isMainThread,@"Main completion");result=value;failure=issue;done=YES;} error:&error];
    NSCAssert(started,@"Maintenance starts: %@",error);
    NSCAssert([VIMDiskMaintenance hasActiveOperation]&&[VIMDiskMaintenance isMaintainingPackage:package],@"Busy before dispatch");
    NSError *busy=nil;
    NSCAssert(![VIMDiskMaintenance startForPackage:package identifier:identifier qemuImage:tool compressed:NO progress:^(NSString *p,double f){} completion:^(NSDictionary *v,NSError *e){} error:&busy]&&busy,@"Reject overlapping maintenance");
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:60];
    while(!done&&deadline.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    NSCAssert(done,@"Maintenance completed");
    NSCAssert(![VIMDiskMaintenance hasActiveOperation],@"Busy cleared before completion");
    NSCAssert(expectSuccess?(result&&!failure):(!result&&failure),@"Expected success=%d: %@",expectSuccess,failure);
    if(expectSuccess)NSCAssert(sawProgress,@"Progress delivered");
    return result;
}
static void UnchangedFailure(NSURL *package) {
    NSURL *disk=[package URLByAppendingPathComponent:@"Disks/System.qcow2"];
    NSData *before=[NSData dataWithContentsOfURL:disk];Maintain(package,NO,NO);
    NSCAssert([[NSData dataWithContentsOfURL:disk] isEqual:before],@"Failed operation preserves original bytes");
    for(NSString *name in [fm contentsOfDirectoryAtPath:disk.URLByDeletingLastPathComponent.path error:nil])
        NSCAssert(![name hasPrefix:@".DiskMaintenance-"],@"Clean only the current staging directory");
}
static NSURL *Proxy(NSURL *root,NSString *body) {
    NSURL *script=[root URLByAppendingPathComponent:[@"tool-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    NSString *text=[NSString stringWithFormat:@"#!/bin/sh\n%@\nexec \"$VIM_DISK_TEST_TOOL\" \"$@\"\n",body];
    NSCAssert([text writeToURL:script atomically:YES encoding:NSUTF8StringEncoding error:nil],@"Write test tool");
    NSCAssert(chmod(script.fileSystemRepresentation,0700)==0,@"Make test tool executable");return script;
}
int main(int argc,const char *argv[]) {@autoreleasepool {
    if(argc==2&&!strcmp(argv[1],"--check-lock"))return flock(STDIN_FILENO,LOCK_EX|LOCK_NB)==0?0:24;
    if(argc==4&&!strcmp(argv[1],"--interrupt")) {
        NSURL *package=[NSURL fileURLWithPath:@(argv[2])];
        NSString *identifier=[NSDictionary dictionaryWithContentsOfURL:[package URLByAppendingPathComponent:@"Machine.plist"]][@"Identifier"];
        NSCAssert([VIMDiskMaintenance startForPackage:package identifier:identifier qemuImage:[NSURL fileURLWithPath:@(argv[3])] compressed:NO
            progress:^(NSString *phase,double fraction){if(fraction==.12)_exit(77);}
            completion:^(NSDictionary *value,NSError *error){_exit(78);} error:nil],@"Start interruption fixture");
        [NSRunLoop.currentRunLoop run];return 79;
    }
    BOOL basic=argc==3&&!strcmp(argv[2],"--basic");
    NSCAssert(argc==2||basic,@"Pass qemu-img [--basic]");tool=[NSURL fileURLWithPath:@(argv[1])];fm=NSFileManager.defaultManager;
    NSURL *root=[fm.temporaryDirectory URLByAppendingPathComponent:[@"vim-disk-tests-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    [fm createDirectoryAtURL:root withIntermediateDirectories:YES attributes:nil error:nil];
    @try {
        NSURL *package=Fixture(root,@"Sparse"),*disk=[package URLByAppendingPathComponent:@"Disks/System.qcow2"];
        NSURL *reference=[root URLByAppendingPathComponent:@"reference.qcow2"];NSCAssert([fm copyItemAtURL:disk toURL:reference error:nil],@"Reference");
        NSDictionary *result=Maintain(package,NO,YES);
        NSCAssert([result[@"Changed"] boolValue]&&[result[@"AfterBytes"] unsignedLongLongValue]<[result[@"BeforeBytes"] unsignedLongLongValue],@"Reclaim allocation");
        Run(@[@"compare",@"-f",@"qcow2",@"-F",@"qcow2",reference.path,disk.path]);
        Maintain(package,YES,YES);Run(@[@"compare",@"-f",@"qcow2",@"-F",@"qcow2",reference.path,disk.path]);
        NSData *compressed=[NSData dataWithContentsOfURL:disk];
        result=Maintain(package,NO,YES);
        NSCAssert(![result[@"Changed"] boolValue]&&[[NSData dataWithContentsOfURL:disk] isEqual:compressed],@"Retain compressed source when sparse conversion grows");
        if(basic){puts("PASS: sparse/compressed content equivalence and no-growth retention on selected temporary volume");return 0;}
        Run(@[@"snapshot",@"-c",@"preserve",disk.path]);NSData *snapshot=[NSData dataWithContentsOfURL:disk];
        Maintain(package,YES,NO);NSCAssert([[NSData dataWithContentsOfURL:disk] isEqual:snapshot],@"Do not discard snapshots");
        NSURL *locked=Fixture(root,@"Locked"),*lock=[locked URLByAppendingPathComponent:@"State/Session.lock"];
        int fd=open(lock.fileSystemRepresentation,O_CREAT|O_RDWR,0600);NSCAssert(fd>=0&&flock(fd,LOCK_EX|LOCK_NB)==0,@"Simulate live VM");
        NSData *before=[NSData dataWithContentsOfURL:[locked URLByAppendingPathComponent:@"Disks/System.qcow2"]];
        Maintain(locked,NO,NO);NSCAssert([[NSData dataWithContentsOfURL:[locked URLByAppendingPathComponent:@"Disks/System.qcow2"]] isEqual:before],@"Locked disk untouched");close(fd);
        NSURL *bitmap=Fixture(root,@"Bitmap");Run(@[@"bitmap",@"--add",[bitmap URLByAppendingPathComponent:@"Disks/System.qcow2"].path,@"preserve"]);UnchangedFailure(bitmap);
        NSURL *backed=Fixture(root,@"Backing"),*backedDisk=[backed URLByAppendingPathComponent:@"Disks/System.qcow2"];
        [fm removeItemAtURL:backedDisk error:nil];Run(@[@"create",@"-f",@"qcow2",@"-F",@"qcow2",@"-b",reference.path,backedDisk.path]);UnchangedFailure(backed);
        NSURL *corrupt=Fixture(root,@"Corrupt");[@"invalid" writeToURL:[corrupt URLByAppendingPathComponent:@"Disks/System.qcow2"] atomically:YES encoding:NSUTF8StringEncoding error:nil];UnchangedFailure(corrupt);
        NSURL *linked=Fixture(root,@"Linked"),*linkedDisk=[linked URLByAppendingPathComponent:@"Disks/System.qcow2"];
        [fm removeItemAtURL:linkedDisk error:nil];NSCAssert([fm createSymbolicLinkAtURL:linkedDisk withDestinationURL:reference error:nil],@"External link fixture");UnchangedFailure(linked);
        NSURL *hard=Fixture(root,@"Hard"),*hardDisk=[hard URLByAppendingPathComponent:@"Disks/System.qcow2"];
        NSCAssert(link(hardDisk.fileSystemRepresentation,[root URLByAppendingPathComponent:@"hardlink"].fileSystemRepresentation)==0,@"Hard link fixture");UnchangedFailure(hard);
        NSURL *realTool=tool;setenv("VIM_DISK_TEST_TOOL",realTool.path.UTF8String,1);setenv("VIM_DISK_TEST_PROBE",argv[0],1);
        NSURL *failed=Fixture(root,@"Failure");
        tool=Proxy(root,@"if [ \"$1\" = convert ]; then\n  i=0; while [ $i -lt 3000 ]; do echo 'injected conversion failure'; i=$((i+1)); done\n  exit 23\nfi");
        UnchangedFailure(failed);
        tool=Proxy(root,@"if [ \"$1\" = measure ]; then echo '{\"required\":9223372036854775807}'; exit 0; fi");UnchangedFailure(failed);
        tool=realTool;
        tool=Proxy(root,@"if [ \"$1\" = info ]; then \"$VIM_DISK_TEST_PROBE\" --check-lock || exit 24; fi");
        Maintain(failed,NO,YES);tool=realTool;
        NSURL *wrong=[root URLByAppendingPathComponent:@"wrong.qcow2"];Run(@[@"create",@"-f",@"qcow2",wrong.path,@"16M"]);setenv("VIM_DISK_TEST_WRONG",wrong.path.UTF8String,1);
        tool=Proxy(root,@"if [ \"$1\" = convert ]; then for target do :; done; cp \"$VIM_DISK_TEST_WRONG\" \"$target\"; exit 0; fi");UnchangedFailure(failed);
        setenv("VIM_DISK_TEST_CONFIG",[failed URLByAppendingPathComponent:@"Machine.plist"].path.UTF8String,1);
        NSData *configuration=[NSData dataWithContentsOfURL:[failed URLByAppendingPathComponent:@"Machine.plist"]];
        tool=Proxy(root,@"if [ \"$1\" = compare ]; then /bin/rm \"$VIM_DISK_TEST_CONFIG\"; fi");UnchangedFailure(failed);
        NSCAssert([configuration writeToURL:[failed URLByAppendingPathComponent:@"Machine.plist"] atomically:YES],@"Restore configuration fixture");
        tool=Proxy(root,@"if [ \"$1\" = convert ]; then printf '(12.00/100%%)\\r'; sleep 1; fi");
        testLockDuringProgress=YES;Maintain(failed,NO,YES);NSCAssert(!testLockDuringProgress,@"Parsed carriage-return progress while child runs");tool=realTool;
        NSURL *permissions=Fixture(root,@"Permissions"),*permissionsDisk=[permissions URLByAppendingPathComponent:@"Disks/System.qcow2"];
        NSCAssert(setxattr(permissionsDisk.fileSystemRepresentation,"com.virtualintelmac.fixture","keep",4,0,0)==0,@"Set metadata fixture");
        NSTask *acl=[NSTask new];acl.executableURL=[NSURL fileURLWithPath:@"/bin/chmod"];acl.arguments=@[@"+a",@"everyone allow read",permissionsDisk.path];
        NSCAssert([acl launchAndReturnError:nil],@"Set ACL fixture");[acl waitUntilExit];NSCAssert(!acl.terminationStatus,@"ACL fixture succeeds");NSString *access=ACLText(permissionsDisk);
        chmod(permissionsDisk.fileSystemRepresentation,0600);Maintain(permissions,NO,YES);struct stat permissionsInfo;stat(permissionsDisk.fileSystemRepresentation,&permissionsInfo);
        NSCAssert((permissionsInfo.st_mode&0777)==0600,@"Preserve private disk permissions");
        NSCAssert([ACLText(permissionsDisk) isEqual:access],@"Preserve disk ACL");
        char attribute[8]={0};NSCAssert(getxattr(permissionsDisk.fileSystemRepresentation,"com.virtualintelmac.fixture",attribute,sizeof(attribute),0,0)==4&&!memcmp(attribute,"keep",4),@"Preserve disk metadata");
        NSURL *interrupted=Fixture(root,@"Interrupted"),*interruptedDisk=[interrupted URLByAppendingPathComponent:@"Disks/System.qcow2"];
        NSData *interruptedBytes=[NSData dataWithContentsOfURL:interruptedDisk];
        NSURL *slow=Proxy(root,@"if [ \"$1\" = convert ]; then printf '(12.00/100%%)\\r'; sleep 2; fi");
        NSTask *child=[NSTask new];child.executableURL=[NSURL fileURLWithPath:@(argv[0])];child.arguments=@[@"--interrupt",interrupted.path,slow.path];
        child.standardOutput=NSFileHandle.fileHandleWithNullDevice;child.standardError=NSFileHandle.fileHandleWithNullDevice;
        NSCAssert([child launchAndReturnError:nil],@"Launch interrupted parent");[child waitUntilExit];NSCAssert(child.terminationStatus==77,@"Interrupt during conversion");
        int inherited=open([interrupted URLByAppendingPathComponent:@"State/Session.lock"].fileSystemRepresentation,O_RDWR);
        NSCAssert(inherited>=0&&flock(inherited,LOCK_EX|LOCK_NB)!=0,@"Orphaned tool retains session lock");
        NSDate *limit=[NSDate dateWithTimeIntervalSinceNow:10];BOOL acquired=NO;
        while(!(acquired=flock(inherited,LOCK_EX|LOCK_NB)==0)&&limit.timeIntervalSinceNow>0)usleep(20000);
        NSCAssert(acquired,@"Orphaned tool eventually exits");close(inherited);
        NSCAssert([[NSData dataWithContentsOfURL:interruptedDisk] isEqual:interruptedBytes],@"Interrupted parent cannot publish output");
        Maintain(interrupted,NO,YES);
        for(NSString *name in [fm contentsOfDirectoryAtPath:interruptedDisk.URLByDeletingLastPathComponent.path error:nil])
            NSCAssert(![name hasPrefix:@".DiskMaintenance-"],@"Recover only owned interrupted staging files");
        NSURL *unknown=[interruptedDisk.URLByDeletingLastPathComponent URLByAppendingPathComponent:[@".DiskMaintenance-" stringByAppendingString:NSUUID.UUID.UUIDString]];
        [fm createDirectoryAtURL:unknown withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:nil];
        [@"unowned" writeToURL:[unknown URLByAppendingPathComponent:@"Image.qcow2"] atomically:YES encoding:NSUTF8StringEncoding error:nil];
        Maintain(interrupted,NO,YES);NSCAssert([fm fileExistsAtPath:[unknown URLByAppendingPathComponent:@"Image.qcow2"].path],@"Never delete unidentified staging files");
        puts("PASS: sparse/compressed contents, no-growth retention, snapshot refusal, session locking, busy lifecycle and progress");
        puts("PASS: metadata/link refusal, tool/space/content failures, configuration races, pipe draining, ACLs and extended attributes");
        puts("PASS: forced parent exit, orphaned tool locking, original disk preservation and identified staging recovery");
    } @finally {[fm removeItemAtURL:root error:nil];}
}return 0;}
