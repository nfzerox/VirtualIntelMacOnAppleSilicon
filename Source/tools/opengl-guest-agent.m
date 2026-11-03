// Guest-only cold-control transport for 10.7–10.9. No network listener, polling
// loop, graphics payloads, shell command construction or host privileges.
// Deliberately uses pre-10.8 Foundation calls (no Obj-C collection subscripting).
#import <Foundation/Foundation.h>
#if VIM_RECOVERY_CONTROL
#import "guest-system-version.h"
#else
#import "opengl-system-version.h"
#endif
#import "opengl-guest-machine.h"
#import "opengl-boot-identity.h"
#import "opengl-agent-framing.h"
#import "opengl-tiger-sleep.h"
#import <IOKit/IOKitLib.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1050
#include <spawn.h>
#endif
#include <crt_externs.h>
#include <sys/wait.h>
#endif
static BOOL WriteAll(int fd,const void *data,size_t size);
// One cold-path diagnostic per failed process, never per request/frame or
// inside the blocking UART read loop. launchd owns the root-only log path.
static int StartupError(const char *stage,int status){
    int savedError=errno;
    fprintf(stderr,"Virtual Intel Mac control service: %s (errno %d)\n",stage,savedError);
    return status;
}
#if VIM_RECOVERY_CONTROL
static int StartRecoveryService(void){
    // Reuse the older bootstrap's root-created launchd job. Starting a child
    // in rc.install's process group can strand it during Recovery startup;
    // launchd also handles the genuine serial node appearing after this hook.
    NSString *path=@"/var/run/com.mac.virtualintelmac.recovery-agent.plist";
    NSDictionary *job=@{@"Label":@"com.mac.virtualintelmac.recovery-agent",@"UserName":@"root",
        @"ProgramArguments":@[@"/usr/libexec/guest-recovery-agent",@"--device",@"/dev/cu.serial2"],
        @"RunAtLoad":@YES,@"KeepAlive":@{@"SuccessfulExit":@NO},@"ThrottleInterval":@10,
        @"StandardErrorPath":@"/var/log/virtualintelmac-recovery.log"};
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:job format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
    if(!data||![data writeToFile:path options:NSDataWritingAtomic error:NULL]||chmod(path.fileSystemRepresentation,0600))
        return StartupError("creating Recovery service",73);
    NSTask *task=[[[NSTask alloc] init] autorelease];task.launchPath=@"/bin/launchctl";task.arguments=@[@"load",path];
    @try{[task launch];[task waitUntilExit];return task.terminationStatus;}
    @catch(NSException *exception){return StartupError("starting Recovery service",69);}
}
#endif

@interface OpenGLGuestTask : NSObject {
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    NSTask *_task;NSLock *_lock;NSMutableData *_output,*_errorOutput;
    BOOL _outputDone,_errorDone,_exited,_truncated;int _status,_processID;
#endif
}
@property(retain) NSTask *task;
@property(retain) NSLock *lock;
@property(retain) NSMutableData *output;
@property(retain) NSMutableData *errorOutput;
@property BOOL outputDone;
@property BOOL errorDone;
@property BOOL exited;
@property BOOL truncated;
@property int status;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
@property int processID;
- (void)collectPipe:(NSArray *)arguments;
- (void)writeInput:(NSArray *)arguments;
- (void)waitForExit:(id)unused;
#endif
@end
@implementation OpenGLGuestTask
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
// Tiger has @synchronized but no objc_getProperty/objc_setProperty exports.
// Preserve retained, atomic object access with its real runtime primitives.
#define VIM_TIGER_TASK_PROPERTY(TYPE,NAME,SETTER) \
- (TYPE *)NAME { @synchronized(self){return [[_##NAME retain] autorelease];} } \
- (void)SETTER:(TYPE *)value { @synchronized(self){[value retain];[_##NAME release];_##NAME=value;} }
VIM_TIGER_TASK_PROPERTY(NSTask,task,setTask)
VIM_TIGER_TASK_PROPERTY(NSLock,lock,setLock)
VIM_TIGER_TASK_PROPERTY(NSMutableData,output,setOutput)
VIM_TIGER_TASK_PROPERTY(NSMutableData,errorOutput,setErrorOutput)
#undef VIM_TIGER_TASK_PROPERTY
@synthesize outputDone=_outputDone,errorDone=_errorDone,exited=_exited,truncated=_truncated,status=_status,processID=_processID;
#endif
- (void)dealloc {[_task release];[_lock release];[_output release];[_errorOutput release];[super dealloc];}
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
// Leopard has NSThread, but no libdispatch/Blocks runtime. Detached workers
// retain their target/argument for their lifetime. Reads/writes/wait block on
// real events; the UART reader never waits for a child to load under TCG.
- (void)collectPipe:(NSArray *)arguments {
    NSAutoreleasePool *pool=[NSAutoreleasePool new];
    NSFileHandle *pipe=[arguments objectAtIndex:0];BOOL standardError=[[arguments objectAtIndex:1] boolValue];
    for(;;){
        NSAutoreleasePool *iteration=[NSAutoreleasePool new];
        NSData *data=[pipe readDataOfLength:4096];BOOL finished=![data length];
        if(!finished){
            [self.lock lock];NSMutableData *destination=standardError?self.errorOutput:self.output;
            NSUInteger capacity=1024*1024-[destination length];
            if([data length]>capacity){self.truncated=YES;[destination appendData:[data subdataWithRange:NSMakeRange(0,capacity)]];}
            else [destination appendData:data];[self.lock unlock];
        }
        [iteration drain];if(finished)break;
    }
    [pipe closeFile];[self.lock lock];if(standardError)self.errorDone=YES;else self.outputDone=YES;[self.lock unlock];
    [pool drain];
}
- (void)writeInput:(NSArray *)arguments {
    NSAutoreleasePool *pool=[NSAutoreleasePool new];
    NSFileHandle *writer=[[arguments objectAtIndex:0] fileHandleForWriting];NSData *data=[arguments objectAtIndex:1];
    WriteAll([writer fileDescriptor],[data bytes],[data length]);[writer closeFile];[pool drain];
}
- (void)waitForExit:(id)unused {
    (void)unused;NSAutoreleasePool *pool=[NSAutoreleasePool new];
    int status=0;pid_t result;do{result=waitpid(self.processID,&status,0);}while(result<0&&errno==EINTR);
    [self.lock lock];self.status=result<0?127:WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);self.exited=YES;[self.lock unlock];
    [pool drain];
}
#endif
@end

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
static BOOL LeopardLaunch(OpenGLGuestTask *record,NSString *path,NSArray *arguments,NSPipe *input,NSPipe *output,NSPipe *errors){
    // Actual 10.5 NSTask waitUntilExit polls a run loop whose task was
    // launched on the blocked UART thread. The native fixture catches that
    // spin/zombie failure. Use Darwin9's real spawn/waitpid contract instead
    // of pumping the control run loop or adding a polling reaper.
    NSFileHandle *reader=input?[input fileHandleForReading]:[NSFileHandle fileHandleWithNullDevice];
    NSMutableArray *handles=[NSMutableArray arrayWithObjects:reader,[output fileHandleForReading],
        [output fileHandleForWriting],[errors fileHandleForReading],[errors fileHandleForWriting],nil];
    if(input)[handles addObject:[input fileHandleForWriting]];
    char **argv=calloc([arguments count]+2,sizeof(char *));
    if(!reader||!argv)return NO;
    argv[0]=(char *)[path fileSystemRepresentation];BOOL valid=argv[0]!=NULL;
    for(NSUInteger i=0;i<[arguments count];i++){
        NSString *argument=[arguments objectAtIndex:i];argv[i+1]=(char *)[argument UTF8String];
        if(!argv[i+1]||strlen(argv[i+1])!=[argument lengthOfBytesUsingEncoding:NSUTF8StringEncoding])valid=NO;
    }
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    // Tiger predates posix_spawn. Prepare all Objective-C data BEFORE fork;
    // the child calls only async-signal-safe descriptor/exec/_exit functions.
    int descriptors[6];NSUInteger index,descriptorCount=[handles count];
    if(descriptorCount>sizeof(descriptors)/sizeof(descriptors[0])){free(argv);return NO;}
    for(index=0;index<descriptorCount;index++)descriptors[index]=[[handles objectAtIndex:index] fileDescriptor];
    char **environment=*_NSGetEnviron();pid_t child=valid?fork():-1;int result=child<0?(valid?errno:EINVAL):0;
    if(child==0){
        if(dup2(descriptors[0],0)<0||dup2(descriptors[2],1)<0||dup2(descriptors[4],2)<0)_exit(127);
        for(index=0;index<descriptorCount;index++)if(descriptors[index]>2)close(descriptors[index]);
        execve(argv[0],argv,environment);_exit(127);
    }
    free(argv);
#else
    posix_spawn_file_actions_t actions;int result=valid?posix_spawn_file_actions_init(&actions):EINVAL;
    BOOL initialized=result==0;
    if(!result)result=posix_spawn_file_actions_adddup2(&actions,[reader fileDescriptor],STDIN_FILENO);
    if(!result)result=posix_spawn_file_actions_adddup2(&actions,[[output fileHandleForWriting] fileDescriptor],STDOUT_FILENO);
    if(!result)result=posix_spawn_file_actions_adddup2(&actions,[[errors fileHandleForWriting] fileDescriptor],STDERR_FILENO);
    for(NSUInteger index=0;index<[handles count];index++) {
        NSFileHandle *handle=[handles objectAtIndex:index];
        if(!result&&[handle fileDescriptor]>2)result=posix_spawn_file_actions_addclose(&actions,[handle fileDescriptor]);
    }
    pid_t child=0;
    if(!result)result=posix_spawn(&child,argv[0],&actions,NULL,argv,*_NSGetEnviron());
    if(initialized)posix_spawn_file_actions_destroy(&actions);free(argv);
#endif
    if(result){for(NSUInteger index=0;index<[handles count];index++)[[handles objectAtIndex:index] closeFile];return NO;}
    [reader closeFile];[[output fileHandleForWriting] closeFile];[[errors fileHandleForWriting] closeFile];
    record.processID=child;return YES;
}
#endif

static void Collect(OpenGLGuestTask *record,NSFileHandle *pipe,BOOL standardError) {
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    [NSThread detachNewThreadSelector:@selector(collectPipe:) toTarget:record
        withObject:[NSArray arrayWithObjects:pipe,[NSNumber numberWithBool:standardError],nil]];
#else
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT,0),^{@autoreleasepool {
        for(;;){
            NSData *data=[pipe readDataOfLength:4096];if(![data length])break;
            [record.lock lock];NSMutableData *destination=standardError?record.errorOutput:record.output;
            NSUInteger capacity=1024*1024-[destination length];
            if([data length]>capacity){record.truncated=YES;[destination appendData:[data subdataWithRange:NSMakeRange(0,capacity)]];}
            else [destination appendData:data];[record.lock unlock];
        }
        [pipe closeFile];[record.lock lock];if(standardError)record.errorDone=YES;else record.outputDone=YES;[record.lock unlock];
    }});
#endif
}
static NSDictionary *Failure(NSString *message) {return [NSDictionary dictionaryWithObject:[NSDictionary dictionaryWithObjectsAndKeys:@"GenericError",@"class",message,@"desc",nil] forKey:@"error"];}
static NSDictionary *Reply(id value) {return [NSDictionary dictionaryWithObject:value forKey:@"return"];}
static NSDictionary *Execute(NSDictionary *request,NSMutableDictionary *tasks,NSMutableDictionary *files) {
    NSString *command=[request objectForKey:@"execute"];NSDictionary *arguments=[request objectForKey:@"arguments"];
    if(![command isKindOfClass:[NSString class]]||([arguments class]&&![arguments isKindOfClass:[NSDictionary class]]))return Failure(@"Invalid request");
    if([command isEqualToString:@"guest-sync-delimited"]||[command isEqualToString:@"guest-sync"]){
        id nonce=[arguments objectForKey:@"id"];return [nonce isKindOfClass:[NSNumber class]]?Reply(nonce):Failure(@"Invalid sync identifier");
    }
    if([command isEqualToString:@"guest-ping"])return Reply([NSDictionary dictionary]);
    if([command isEqualToString:@"guest-file-open"]){
        NSString *path=[arguments objectForKey:@"path"],*mode=[arguments objectForKey:@"mode"];
        // File transfer is only for newly created root-private staging leaves,
        // never an arbitrary system path or an existing executable. Transfers
        // are acknowledged; experiments do not need a sealed stock-binary hash.
        NSString *parent=[path isKindOfClass:[NSString class]]?[path stringByDeletingLastPathComponent]:nil;
        NSString *base=[parent stringByDeletingLastPathComponent];struct stat directory;
        BOOL allowed=((([base isEqual:@"/private/var/tmp"]||[base isEqual:@"/private/var/db"]||[base isEqual:@"/var/tmp"])&&[[parent lastPathComponent] hasPrefix:@"VirtualIntelMac."])||
            ([base isEqual:@"/private/var/run"]&&[[parent lastPathComponent] hasPrefix:@"VirtualIntelMacStatus."]))&&[path isEqual:[path stringByStandardizingPath]]&&
            !lstat([parent fileSystemRepresentation],&directory)&&S_ISDIR(directory.st_mode)&&directory.st_uid==0&&!(directory.st_mode&0077);
        if(!allowed||![mode isEqual:@"wb"]||[files count]>=8)return Failure(@"Invalid private staging destination");
        int fd=open([path fileSystemRepresentation],O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd<0)return Failure(@"Cannot create private staging file");
        NSNumber *handle=[NSNumber numberWithInt:fd];[files setObject:[[[NSFileHandle alloc] initWithFileDescriptor:fd closeOnDealloc:YES] autorelease] forKey:handle];return Reply(handle);
    }
    if([command isEqualToString:@"guest-file-write"]){
        NSFileHandle *file=[files objectForKey:[arguments objectForKey:@"handle"]];NSString *encoded=[arguments objectForKey:@"buf-b64"];
        if(!file||![encoded isKindOfClass:[NSString class]]||[encoded length]>524288)return Failure(@"Invalid staging write");
        NSData *data=[[[NSData alloc] initWithBase64Encoding:encoded] autorelease];
        if(!data||!WriteAll([file fileDescriptor],[data bytes],[data length]))return Failure(@"Incomplete staging write");
        return Reply([NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithUnsignedLong:(unsigned long)[data length]],@"count",[NSNumber numberWithBool:NO],@"eof",nil]);
    }
    if([command isEqualToString:@"guest-file-close"]){
        NSNumber *handle=[arguments objectForKey:@"handle"];NSFileHandle *file=[files objectForKey:handle];
        if(!file)return Failure(@"Unknown staging file");
        BOOL ok=fsync([file fileDescriptor])==0;[file closeFile];[files removeObjectForKey:handle];return ok?Reply([NSDictionary dictionary]):Failure(@"Cannot flush staging file");
    }
    if([command isEqualToString:@"guest-exec"]){
        NSString *path=[arguments objectForKey:@"path"];NSArray *args=[arguments objectForKey:@"arg"];
        if(![path isKindOfClass:[NSString class]]||![path isAbsolutePath]||[path length]>4096||
           ![args isKindOfClass:[NSArray class]]||[args count]>128||[tasks count]>=16)return Failure(@"Invalid command or too many outstanding commands");
        for(NSUInteger index=0;index<[args count];index++) {
            id argument=[args objectAtIndex:index];
            if(![argument isKindOfClass:[NSString class]]||[argument length]>65536)return Failure(@"Invalid argument");
        }
        id encodedInput=[arguments objectForKey:@"input-data"];NSData *input=nil;
        if(encodedInput){
            if(![encodedInput isKindOfClass:[NSString class]]||[encodedInput length]>24000)return Failure(@"Invalid command input");
            input=[[[NSData alloc] initWithBase64Encoding:encodedInput] autorelease];
            if(!input||[input length]>16384)return Failure(@"Invalid command input");
        }
        OpenGLGuestTask *record=[[[OpenGLGuestTask alloc] init] autorelease];
        record.task=[[[NSTask alloc] init] autorelease];record.lock=[[[NSLock alloc] init] autorelease];
        record.output=[NSMutableData data];record.errorOutput=[NSMutableData data];
        NSPipe *output=[NSPipe pipe],*errors=[NSPipe pipe];
        [record.task setLaunchPath:path];[record.task setArguments:args];
        NSPipe *inputPipe=input?[NSPipe pipe]:nil;
        [record.task setStandardInput:inputPipe?:[NSFileHandle fileHandleWithNullDevice]];
        [record.task setStandardOutput:output];[record.task setStandardError:errors];
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
        if(!LeopardLaunch(record,path,args,inputPipe,output,errors))return Failure(@"The guest command could not be launched");
        NSNumber *pid=[NSNumber numberWithInt:record.processID];
#else
        @try{[record.task launch];}@catch(NSException *exception){return Failure(@"The guest command could not be launched");}
        NSNumber *pid=[NSNumber numberWithInt:[record.task processIdentifier]];
#endif
        [tasks setObject:record forKey:pid];
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
        if(inputPipe)[NSThread detachNewThreadSelector:@selector(writeInput:) toTarget:record
            withObject:[NSArray arrayWithObjects:inputPipe,input,nil]];
        Collect(record,[output fileHandleForReading],NO);Collect(record,[errors fileHandleForReading],YES);
        [NSThread detachNewThreadSelector:@selector(waitForExit:) toTarget:record withObject:nil];
#else
        if(inputPipe)dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT,0),^{@autoreleasepool {
            // Keep the control reader responsive even while a TCG child loads.
            NSFileHandle *writer=[inputPipe fileHandleForWriting];
            WriteAll([writer fileDescriptor],[input bytes],[input length]);[writer closeFile];
        }});
        Collect(record,[output fileHandleForReading],NO);Collect(record,[errors fileHandleForReading],YES);
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT,0),^{@autoreleasepool {
            [record.task waitUntilExit];[record.lock lock];record.status=[record.task terminationStatus];record.exited=YES;[record.lock unlock];
        }});
#endif
        return Reply([NSDictionary dictionaryWithObject:pid forKey:@"pid"]);
    }
    if([command isEqualToString:@"guest-exec-status"]){
        id pid=[arguments objectForKey:@"pid"];OpenGLGuestTask *record=[pid isKindOfClass:[NSNumber class]]?[tasks objectForKey:pid]:nil;
        if(!record)return Failure(@"Unknown guest process");
        [record.lock lock];BOOL finished=record.exited&&record.outputDone&&record.errorDone;
        NSMutableDictionary *result=[NSMutableDictionary dictionaryWithObject:[NSNumber numberWithBool:finished] forKey:@"exited"];
        if(finished){
            [result setObject:[NSNumber numberWithInt:record.status] forKey:@"exitcode"];
            [result setObject:[record.output base64Encoding] forKey:@"out-data"];
            [result setObject:[record.errorOutput base64Encoding] forKey:@"err-data"];
            [result setObject:[NSNumber numberWithBool:record.truncated] forKey:@"out-truncated"];
            [result setObject:[NSNumber numberWithBool:record.truncated] forKey:@"err-truncated"];
        }
        [record.lock unlock];if(finished)[tasks removeObjectForKey:pid];return Reply(result);
    }
    return Failure(@"This Pre Metal guest-agent command is not implemented");
}
static BOOL WriteAll(int fd,const void *data,size_t size) {
    while(size){ssize_t n=write(fd,data,size);if(n<0&&errno==EINTR)continue;if(n<=0)return NO;data=(const char *)data+n;size-=(size_t)n;}return YES;
}
int main(int argc,char **argv) {@autoreleasepool {
    BOOL startService=NO;
#if VIM_RECOVERY_CONTROL
    startService=argc==2&&!strcmp(argv[1],"--start-recovery-service");
#endif
    if(!startService&&(argc!=3||strcmp(argv[1],"--device")||strcmp(argv[2],"/dev/cu.serial2")))return 64;
    NSString *version=[[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"] objectForKey:@"ProductVersion"];
    // Do not allow this guest root service to be run on a host/reference Mac.
#if VIM_RECOVERY_CONTROL
    NSString *guest=VIMGuestForSystemVersion(version);
    BOOL recovery=[[NSFileManager defaultManager] fileExistsAtPath:@"/System/Installation/CDIS/macOS Utilities.app"]||
        [[NSFileManager defaultManager] fileExistsAtPath:@"/System/Installation/CDIS/Recovery Springboard.app"];
    if(geteuid()!=0||!recovery||!([guest isEqual:@"mojave"]||[guest isEqual:@"catalina"]))
        return StartupError("unsupported Recovery identity or OS",77);
    if(!VIMModernSetupGuestMachine())return StartupError("guest PCI identity unavailable",77);
    if(startService)return StartRecoveryService();
#else
    if(geteuid()!=0||!VIMOpenGLGuestForSystemVersion(version))return StartupError("unsupported identity or OS",77);
    if(!VIMOpenGLGuestMachine())return StartupError("guest PCI identity unavailable",77);
#endif
    signal(SIGPIPE,SIG_IGN);
    // One reader per UART, even if a recovery job is accidentally launched
    // twice. Separate readers otherwise steal each other's protocol replies.
    int owner=open("/var/run/VirtualIntelMacLegacyAgent.lock",O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
    struct stat ownerInfo;
    if(owner<0||fstat(owner,&ownerInfo)||!S_ISREG(ownerInfo.st_mode)||ownerInfo.st_uid!=0||ownerInfo.st_nlink!=1)return StartupError("control ownership lock",77);
    if(flock(owner,LOCK_EX|LOCK_NB)){close(owner);return 0;}
    if(fchmod(owner,0600))return StartupError("control lock permissions",77);
    BOOL sleepGuard=NO;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    // Keep the native installer/first account alive even before the host's
    // desktop-time policy runs. Tiger has no pmset "disablesleep" option.
    sleepGuard=VIMTigerStartIdleSleepGuard();
    if(!sleepGuard)fputs("Tiger idle-sleep guard could not start.\n",stderr);
#endif
    if(!VIMOpenGLBeginControlSessionWithIdleSleepGuard(sleepGuard))return StartupError("control boot identity",77);
    int fd=open(argv[2],O_RDWR|O_NOFOLLOW|O_CLOEXEC|O_NOCTTY);struct stat info;struct termios attributes;
    if(fd<0)return StartupError("opening serial callout",69);
    if(fstat(fd,&info)||!S_ISCHR(info.st_mode)||fchown(fd,0,0)||fchmod(fd,0600)||tcgetattr(fd,&attributes))return StartupError("serial callout attributes",69);
    cfmakeraw(&attributes);cfsetspeed(&attributes,B115200);attributes.c_cflag|=CLOCAL|CREAD;
    attributes.c_cflag&=~(CCTS_OFLOW|CRTS_IFLOW);attributes.c_cc[VMIN]=1;attributes.c_cc[VTIME]=0;
    if(tcsetattr(fd,TCSANOW,&attributes))return StartupError("serial callout configuration",69);
    // The callout and dial-in paths alias the same UART. Protect both.
    if(chown("/dev/tty.serial2",0,0)||chmod("/dev/tty.serial2",0600))return StartupError("serial dial-in permissions",69);
    NSMutableData *pending=[NSMutableData data];NSMutableDictionary *tasks=[NSMutableDictionary dictionary],*files=[NSMutableDictionary dictionary];
    for(;;){@autoreleasepool {
        unsigned char bytes[4096];ssize_t count=read(fd,bytes,sizeof(bytes));
        if(count<0&&errno==EINTR)continue;if(count<=0){close(fd);return 74;}
        if(!VIMOpenGLAgentAppend(pending,bytes,(NSUInteger)count))return 65;
        for(;;){
            const unsigned char *start=[pending bytes],*newline=memchr(start,'\n',[pending length]);if(!newline)break;
            NSUInteger length=(NSUInteger)(newline-start);
            NSData *line=[NSData dataWithBytes:start length:length];
            [pending replaceBytesInRange:NSMakeRange(0,length+1) withBytes:NULL length:0];
            id request=[NSJSONSerialization JSONObjectWithData:line options:0 error:NULL];
            if(![request isKindOfClass:[NSDictionary class]])continue;
            NSMutableDictionary *reply=[[Execute(request,tasks,files) mutableCopy] autorelease];id identity=[request objectForKey:@"id"];
            if(identity)[reply setObject:identity forKey:@"id"];
            NSData *encoded=[NSJSONSerialization dataWithJSONObject:reply options:0 error:NULL];if(!encoded)return 65;
            if([[request objectForKey:@"execute"] isEqualToString:@"guest-sync-delimited"]){const unsigned char delimiter=0xff;if(!WriteAll(fd,&delimiter,1))return 74;}
            if(!WriteAll(fd,[encoded bytes],[encoded length])||!WriteAll(fd,"\n",1))return 74;
        }
    }}close(fd);return 0;
}}
