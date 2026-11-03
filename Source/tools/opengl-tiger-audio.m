// Native Tiger audio capsule deployment and one-shot boot loading. No Apple
// file replacement, kextcache rebuild, resident poller or installer-time load.
#import "opengl-period-foundation.h"
#import "opengl-guest-machine.h"
#import "opengl-state-path.h"
#import "opengl-boot-identity.h"
#include "opengl-tiger-kernel-identity.h"
#include "opengl-disk-readiness.h"
#import <DiskArbitration/DiskArbitration.h>
#import <CommonCrypto/CommonDigest.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
static NSString *const AudioRoot=@"/Library/Application Support/Virtual Intel Mac/NativeAudio";
static NSString *const ToolsRoot=@"/Library/Application Support/Virtual Intel Mac/GuestTools";
static NSString *const Binary=@"VoodooHDA.kext/Contents/MacOS/VoodooHDA";
static NSString *const Metadata=@"VoodooHDA.kext/Contents/Info.plist";
static int Fail(const char *why){fprintf(stderr,"Tiger sound setup: %s\n",why);return 1;}
static NSString *At(NSString *target,NSString *absolute){return target?[target stringByAppendingString:absolute]:absolute;}
static BOOL Safe(NSString *path,BOOL directory,BOOL private){
    struct stat st;return !lstat([path fileSystemRepresentation],&st)&&!st.st_uid&&
        !(st.st_mode&(private?0077:0022))&&(directory?S_ISDIR(st.st_mode):S_ISREG(st.st_mode)&&st.st_nlink==1);
}
static BOOL NativeReceipt(NSString *path){
    // Apple's Tiger Installer publishes Essentials' BOM as root:admin 0664.
    // This is a native package receipt, not one of our private journals.
    struct stat st;return !lstat([path fileSystemRepresentation],&st)&&
        S_ISREG(st.st_mode)&&st.st_nlink==1&&!st.st_uid&&
        !(st.st_mode&0002)&&(!(st.st_mode&0020)||st.st_gid==80);
}
static BOOL Directory(NSString *path,mode_t mode){
    struct stat st;
    if(!lstat([path fileSystemRepresentation],&st)){
        BOOL native=([path hasSuffix:@"/Library"]||[path hasSuffix:@"/Library/Application Support"])&&st.st_gid==80;
        return S_ISDIR(st.st_mode)&&!st.st_uid&&!(st.st_mode&0002)&&(!(st.st_mode&0020)||native);
    }
    if(errno!=ENOENT||[path isEqual:@"/"]||!Directory([path stringByDeletingLastPathComponent],0755))return NO;
    return !mkdir([path fileSystemRepresentation],mode)&&!chown([path fileSystemRepresentation],0,0)&&!chmod([path fileSystemRepresentation],mode);
}
static NSData *Read(NSString *path){
    int fd=open([path fileSystemRepresentation],O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat st;
    if(fd<0)return nil;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_size<1||st.st_size>1024*1024){close(fd);return nil;}
    NSMutableData *data=[NSMutableData dataWithLength:(NSUInteger)st.st_size];unsigned char *bytes=[data mutableBytes];size_t left=(size_t)st.st_size;
    while(left){ssize_t count=read(fd,bytes,left);if(count<0&&errno==EINTR)continue;if(count<=0){close(fd);return nil;}bytes+=count;left-=(size_t)count;}
    close(fd);return data;
}
static NSString *Digest(NSData *data){
    if(!data)return nil;unsigned char bytes[CC_SHA256_DIGEST_LENGTH];CC_SHA256_CTX context;
    CC_SHA256_Init(&context);CC_SHA256_Update(&context,[data bytes],(CC_LONG)[data length]);CC_SHA256_Final(bytes,&context);
    NSMutableString *text=[NSMutableString string];for(unsigned i=0;i<sizeof(bytes);i++)[text appendFormat:@"%02x",bytes[i]];return text;
}
static NSDictionary *Plist(NSData *data){id value=data?[NSPropertyListSerialization propertyListWithData:data options:0 format:NULL error:NULL]:nil;return [value isKindOfClass:[NSDictionary class]]?value:nil;}
static BOOL Write(NSData *data,NSString *path,mode_t mode){
    struct stat st;if(!lstat([path fileSystemRepresentation],&st)&&!Safe(path,NO,NO))return NO;
    return data&&[data writeToFile:path options:NSDataWritingAtomic error:NULL]&&!chown([path fileSystemRepresentation],0,0)&&
        !chmod([path fileSystemRepresentation],mode)&&[Digest(data) isEqual:Digest(Read(path))];
}
static BOOL Record(NSDictionary *value,NSString *path){return Write([NSPropertyListSerialization dataWithPropertyList:value format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL],path,0600);}
static BOOL UUID(id text){CFUUIDRef value=[text isKindOfClass:[NSString class]]?CFUUIDCreateFromString(NULL,(CFStringRef)text):NULL;if(value)CFRelease(value);return value!=NULL;}
static NSArray *Names(void){return [NSArray arrayWithObjects:Binary,Metadata,nil];}
static BOOL SameOwner(NSDictionary *record,NSDictionary *owner){return
    [[record objectForKey:@"MachineIdentifier"] isEqual:[owner objectForKey:@"MachineIdentifier"]]&&
    [[record objectForKey:@"VolumeUUID"] isEqual:[owner objectForKey:@"VolumeUUID"]];}
static BOOL ExactFiles(NSDictionary *files){return [files isKindOfClass:[NSDictionary class]]&&
    [[NSSet setWithArray:[files allKeys]] isEqual:[NSSet setWithArray:Names()]];}
static NSString *SourceFile(NSString *directory,NSString *name,BOOL staged){
    return [directory stringByAppendingPathComponent:staged?([name isEqual:Binary]?@"VoodooHDA":@"Info.plist"):name];
}
static int Load(NSString *root,NSString *state,NSDictionary *owner){
    NSString *recordPath=[state stringByAppendingPathComponent:@"Audio.plist"];
    if(!Safe(recordPath,NO,YES))return 66;
    NSDictionary *record=Plist(Read(recordPath)),*files=[record objectForKey:@"Files"];
    if(!SameOwner(record,owner)||![[record objectForKey:@"Phase"] isEqual:@"installed"]||!ExactFiles(files)||!Safe(root,YES,NO))return Fail("the installed sound receipt could not be verified");
    NSEnumerator *names=[Names() objectEnumerator];NSString *name;
    while((name=[names nextObject]))if(!Safe([root stringByAppendingPathComponent:name],NO,NO))return Fail("an installed sound file is missing");
    if(!VIMTigerRunningKernelMatchesFile())return Fail("restart the installed system before loading sound after an update");
    NSString *boot=VIMOpenGLBootSessionUUID(NULL);
    if([[record objectForKey:@"RestartRequired"] boolValue]&&boot&&
       [boot isEqual:[record objectForKey:@"InstalledBootSessionUUID"]])return 76;
    io_service_t device=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VoodooHDADevice"));
    if(device){
        IOObjectRelease(device);
        if([[record objectForKey:@"RestartRequired"] boolValue]&&boot){
            NSMutableDictionary *confirmed=[[record mutableCopy] autorelease];[confirmed setObject:[NSNumber numberWithBool:NO] forKey:@"RestartRequired"];
            if(!Record(confirmed,recordPath))return Fail("confirming the sound restart");
        }
        puts("Native Tiger sound device is available.");return 0;
    }
    NSString *pending=[state stringByAppendingPathComponent:@"AudioLoadPending.plist"];struct stat st;
    if(!lstat([pending fileSystemRepresentation],&st)||errno!=ENOENT)return Fail("the prior sound load was interrupted; the virtual Mac remains bootable and the recovery record was retained");
    if(!Record([NSDictionary dictionaryWithObjectsAndKeys:[owner objectForKey:@"MachineIdentifier"],@"MachineIdentifier",
        [owner objectForKey:@"VolumeUUID"],@"VolumeUUID",[record objectForKey:@"BuildReceiptSHA256"],@"BuildReceiptSHA256",nil],pending))return Fail("saving sound load recovery state");
    NSTask *task=[[[NSTask alloc] init] autorelease];[task setLaunchPath:@"/sbin/kextload"];
    [task setArguments:[NSArray arrayWithObject:[root stringByAppendingPathComponent:@"VoodooHDA.kext"]]];
    [task setStandardInput:[NSFileHandle fileHandleWithNullDevice]];int status=1;
    @try{[task launch];[task waitUntilExit];status=[task terminationStatus];}@catch(NSException *exception){(void)exception;}
    // A real panic/reboot never reaches here, so an interrupted boot will not
    // repeatedly load a suspect driver. Ordinary loader errors remain retryable.
    if(!Safe(pending,NO,YES)||unlink([pending fileSystemRepresentation]))return Fail("retiring sound load recovery state");
    if(status)return Fail("the native driver could not start; its files and loader diagnostic were retained");
    device=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VoodooHDADevice"));
    // Tiger's kextload starts matching asynchronously. A successful loader
    // exit is not an IOAudioDevice publication fence. Let the existing cold
    // host coordinator revisit readiness; no timer or busy-wait in the guest.
    if(!device){puts("Native Tiger sound matching is in progress.");return 75;}
    IOObjectRelease(device);
    if([[record objectForKey:@"RestartRequired"] boolValue]){
        NSMutableDictionary *confirmed=[[record mutableCopy] autorelease];[confirmed setObject:[NSNumber numberWithBool:NO] forKey:@"RestartRequired"];
        if(!Record(confirmed,recordPath))return Fail("confirming the sound restart");
    }
    puts("Native Tiger sound driver loaded.");return 0;
}
int main(int argc,char **argv){
    NSAutoreleasePool *pool=[NSAutoreleasePool new];struct utsname kernel;umask(0077);
    BOOL load=argc==2&&!strcmp(argv[1],"--load"),staged=argc==3&&!strcmp(argv[1],"--install"),
        preboot=argc==4&&!strcmp(argv[1],"--install-target");
    if((!load&&!staged&&!preboot)||geteuid()!=0||uname(&kernel)||strncmp(kernel.release,"8.",2)||!VIMOpenGLGuestMachine())return 77;
    NSString *target=preboot?[NSString stringWithUTF8String:argv[3]]:nil;struct statfs volume;struct stat st;
    if(preboot&&(![target hasPrefix:@"/Volumes/"]||![target isEqual:[target stringByStandardizingPath]]||
        lstat([target fileSystemRepresentation],&st)||!S_ISDIR(st.st_mode)||st.st_uid||statfs([target fileSystemRepresentation],&volume)||
        (volume.f_flags&MNT_RDONLY)||strcmp(volume.f_fstypename,"hfs")||strcmp(volume.f_mntonname,[target fileSystemRepresentation])))return 77;
    if(!preboot&&(statfs("/",&volume)||(volume.f_flags&MNT_RDONLY)))return 77;
    NSString *state=At(target,VIM_OPENGL_STATE_PATH),*ownerPath=[state stringByAppendingPathComponent:@"Ownership.plist"];
    NSDictionary *system=Plist(Read(At(target,@"/System/Library/CoreServices/SystemVersion.plist")));
    if(![[system objectForKey:@"ProductVersion"] hasPrefix:@"10.4."])
        return Fail("the authorized target is not an installed Tiger system");
    if(preboot&&!NativeReceipt(At(target,@"/Library/Receipts/Essentials.pkg/Contents/Archive.bom")))
        return Fail("the native Essentials package receipt is unavailable or has unexpected ownership");
    NSDictionary *owner=Safe(state,YES,YES)&&Safe(ownerPath,NO,YES)?Plist(Read(ownerPath)):nil;
    if(![[owner objectForKey:@"FormatVersion"] isEqual:[NSNumber numberWithInt:1]]||
       ![[owner objectForKey:@"Kind"] isEqual:@"VirtualIntelMacLegacySetup"]||
       ![[owner objectForKey:@"Guest"] isEqual:@"tiger"]||!UUID([owner objectForKey:@"MachineIdentifier"])||!UUID([owner objectForKey:@"VolumeUUID"]))return 77;
    DASessionRef session=DASessionCreate(NULL);DADiskRef disk=session?DADiskCreateFromBSDName(NULL,session,volume.f_mntfromname):NULL;
    NSDictionary *description=disk?(NSDictionary *)DADiskCopyDescription(disk):nil;
    // The one-shot boot job may precede root-volume publication, just like
    // the native sharing job. Missing identity is readiness, not a mismatch;
    // after the actual idle notification retain the exact UUID comparison.
    if(load&&session&&![description objectForKey:(NSString *)kDADiskDescriptionVolumeUUIDKey]){
        [description release];if(disk)CFRelease(disk);
        VIMOpenGLWaitForDisks(session);
        disk=DADiskCreateFromBSDName(NULL,session,volume.f_mntfromname);
        description=disk?(NSDictionary *)DADiskCopyDescription(disk):nil;
    }
    CFTypeRef uuid=(CFTypeRef)[description objectForKey:(NSString *)kDADiskDescriptionVolumeUUIDKey];
    BOOL missingIdentity=uuid==NULL;
    NSString *actual=uuid&&CFGetTypeID(uuid)==CFUUIDGetTypeID()?(NSString *)CFUUIDCreateString(NULL,(CFUUIDRef)uuid):nil;
    BOOL authorized=[actual isEqual:[owner objectForKey:@"VolumeUUID"]];[actual release];[description release];if(disk)CFRelease(disk);if(session)CFRelease(session);
    if(!authorized){
        // An absent description is not evidence of a foreign volume. Cold
        // matching can still be completing after an idle notification; use
        // the existing explicit pending protocol, never skip the UUID guard.
        if(load&&missingIdentity){puts("Native Tiger root-disk discovery is in progress.");return 75;}
        return Fail("the installed volume does not match its authorized setup");
    }
    NSString *root=At(target,AudioRoot),*receiptPath=[state stringByAppendingPathComponent:@"Audio.plist"];
    NSString *lockPath=[state stringByAppendingPathComponent:@"Audio.lock"];
    int lock=open([lockPath fileSystemRepresentation],O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    if(lock<0||!Safe(lockPath,NO,YES))return Fail("the sound transaction lock is unavailable");
    if(flock(lock,LOCK_EX|LOCK_NB))return errno==EWOULDBLOCK||errno==EAGAIN?75:Fail("another sound transaction owns this volume");
    if(load){int result=Load(root,state,owner);close(lock);[pool drain];return result;}
    NSString *source=[NSString stringWithUTF8String:argv[2]];
    if(staged){
        NSString *prefix=@"/private/var/db/VirtualIntelMac.",*suffix=[source hasPrefix:prefix]?[source substringFromIndex:[prefix length]]:nil;
        if([suffix length]!=6||[suffix rangeOfCharacterFromSet:[[NSCharacterSet alphanumericCharacterSet] invertedSet]].location!=NSNotFound||!Safe(source,YES,YES))return 77;
    }else {struct statfs media;
        if(![source isEqual:@"/System/Installation/Packages/Extras/VirtualIntelMac/Audio"]||
           statfs([source fileSystemRepresentation],&media)||!(media.f_flags&MNT_RDONLY)||
           ![[owner objectForKey:@"Phase"] isEqual:@"native-files-installed"])return 77;
    }
    NSString *sourceReceipt=[source stringByAppendingPathComponent:@"BuildReceipt.plist"];
    NSData *buildData=Read(sourceReceipt);NSDictionary *build=Plist(buildData);
    NSMutableDictionary *files=[NSMutableDictionary dictionary];
    if((staged&&!Safe(sourceReceipt,NO,YES))||![[build objectForKey:@"SDKVersion"] isEqual:@"10.4"]||
       ![[build objectForKey:@"KernelArchitecture"] isEqual:@"i386"]||![[build objectForKey:@"AudioOnly"] isEqual:[NSNumber numberWithBool:YES]])return Fail("the sound payload is not a genuine native Tiger build");
    NSEnumerator *names=[Names() objectEnumerator];NSString *name;
    while((name=[names nextObject])){
        NSString *file=SourceFile(source,name,staged);
        NSString *digest=Digest(Read(file));
        if((staged&&!Safe(file,NO,YES))||!digest)return Fail("a sound payload file is missing");
        [files setObject:digest forKey:name];
    }
    NSData *image=Read(SourceFile(source,Binary,staged));uint32_t native[4]={0};
    if([image length]>=sizeof(native))memcpy(native,[image bytes],sizeof(native));
    if(native[0]!=0xfeedface||native[1]!=7||native[3]!=1)return Fail("the sound driver is not a native i386 kernel object");
    NSDictionary *info=Plist(Read(SourceFile(source,Metadata,staged)));
    if(![[info objectForKey:@"CFBundleIdentifier"] isEqual:@"org.voodoo.driver.VoodooHDA"]||
       ![[[[info objectForKey:@"IOKitPersonalities"] objectForKey:@"VoodooHDA"] objectForKey:@"IOPCIPrimaryMatch"] isEqual:@"0x26688086"])return Fail("sound must be restricted to the virtual Intel controller");
    NSDictionary *controls=staged?Plist(Read([source stringByAppendingPathComponent:@"ControlBuildReceipt.plist"])):nil;
    if(staged){
        if(!Safe([source stringByAppendingPathComponent:@"ControlBuildReceipt.plist"],NO,YES)||
           ![[controls objectForKey:@"Architecture"] isEqual:@"i386"]||![[controls objectForKey:@"MinimumOS"] isEqual:@"10.4"])
            return Fail("the native sound control record is unavailable");
        NSEnumerator *tools=[[NSArray arrayWithObjects:@"opengl-tiger-audio",@"opengl-audio-defaults",nil] objectEnumerator];NSString *name;
        while((name=[tools nextObject])){
            NSString *file=[source stringByAppendingPathComponent:name];NSData *data=Read(file);uint32_t header[4]={0};
            if([data length]>=sizeof(header))memcpy(header,[data bytes],sizeof(header));
            if(!Safe(file,NO,YES)||
               header[0]!=0xfeedface||header[1]!=7||header[3]!=2)return Fail("a native sound control could not be verified");
        }
    }
    NSDictionary *prior=Safe(receiptPath,NO,YES)?Plist(Read(receiptPath)):nil;
    if(!lstat([receiptPath fileSystemRepresentation],&st)&&!prior)return Fail("the prior sound record is not a private, valid receipt");
    if(prior&&(!SameOwner(prior,owner)||![[prior objectForKey:@"FormatVersion"] isEqual:[NSNumber numberWithInt:1]]||
        ![[NSArray arrayWithObjects:@"prepared",@"installed",nil] containsObject:[prior objectForKey:@"Phase"]]||
        !ExactFiles([prior objectForKey:@"Files"])))return Fail("the prior sound transaction belongs to another installation");
    if(!prior&&!lstat([root fileSystemRepresentation],&st))return Fail("an unrelated object occupies the sound support folder");
    names=[Names() objectEnumerator];
    while((name=[names nextObject])){
        NSString *destination=[root stringByAppendingPathComponent:name];
        if(!lstat([destination fileSystemRepresentation],&st)){
            if(!Safe(destination,NO,NO))return Fail("an unexpected object occupies an owned sound path");
        }else if(errno!=ENOENT)return Fail("an owned sound path could not be inspected");
    }
    NSMutableDictionary *receipt=[NSMutableDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithInt:1],@"FormatVersion",
        [owner objectForKey:@"MachineIdentifier"],@"MachineIdentifier",[owner objectForKey:@"VolumeUUID"],@"VolumeUUID",
        files,@"Files",Digest(buildData),@"BuildReceiptSHA256",@"prepared",@"Phase",nil];
    if(staged){
        NSString *boot=VIMOpenGLBootSessionUUID(NULL);if(!boot)return Fail("the live setup control epoch is unavailable");
        io_service_t active=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VoodooHDADevice"));
        BOOL changed=prior?![[prior objectForKey:@"Files"] isEqual:files]:active!=0;
        if(active)IOObjectRelease(active);
        BOOL needsRestart=changed||([[prior objectForKey:@"RestartRequired"] boolValue]&&[boot isEqual:[prior objectForKey:@"InstalledBootSessionUUID"]]);
        [receipt setObject:boot forKey:@"InstalledBootSessionUUID"];
        [receipt setObject:[NSNumber numberWithBool:needsRestart] forKey:@"RestartRequired"];
    }
    if(prior)[receipt setObject:[prior objectForKey:@"Files"] forKey:@"PreviousFiles"];
    if(!Record(receipt,receiptPath))return Fail("saving sound recovery metadata");
    names=[Names() objectEnumerator];
    while((name=[names nextObject])){
        NSString *destination=[root stringByAppendingPathComponent:name];
        if(!Directory([destination stringByDeletingLastPathComponent],0755)||
            !Write(Read(SourceFile(source,name,staged)),destination,[name isEqual:Metadata]?0644:0755))return Fail("publishing native sound files");
    }
    if(!Write(buildData,[root stringByAppendingPathComponent:@"BuildReceipt.plist"],0644))return Fail("publishing the native sound build record");
    // A repair carries two verified controls, without pretending that every
    // unrelated installed control was also rebuilt/replaced.
    if(staged){
        if(!Directory(ToolsRoot,0755))return Fail("the native sound control folder is unavailable");
        NSEnumerator *tools=[[NSArray arrayWithObjects:@"opengl-tiger-audio",@"opengl-audio-defaults",nil] objectEnumerator];NSString *name;
        while((name=[tools nextObject])){
            NSString *file=[source stringByAppendingPathComponent:name];
            if(!Safe(file,NO,YES)||
                !Write(Read(file),[ToolsRoot stringByAppendingPathComponent:name],0755))return Fail("publishing verified native sound controls");
        }
    }
    NSDictionary *daemon=[NSDictionary dictionaryWithObjectsAndKeys:@"com.mac.virtualintelmac.tiger-audio",@"Label",
        [NSArray arrayWithObjects:[ToolsRoot stringByAppendingPathComponent:@"opengl-tiger-audio"],@"--load",nil],@"ProgramArguments",
        [NSNumber numberWithBool:YES],@"RunAtLoad",[NSNumber numberWithBool:YES],@"OnDemand",
        @"/var/log/VirtualIntelMacTigerAudio.log",@"StandardErrorPath",nil];
    NSString *jobs=At(target,@"/Library/LaunchDaemons");
    NSData *jobData=[NSPropertyListSerialization dataWithPropertyList:daemon format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
    if(!Directory(jobs,0755)||!Write(jobData,[jobs stringByAppendingPathComponent:@"com.mac.virtualintelmac.tiger-audio.plist"],0644))return Fail("publishing the one-shot sound boot job");
    [receipt setObject:@"installed" forKey:@"Phase"];
    if(!Record(receipt,receiptPath))return Fail("confirming native sound publication");
    // Only an explicit, fully verified repair/publication can reset a retained
    // interrupted-load guard. An ordinary boot never silently retries it.
    NSString *pending=[state stringByAppendingPathComponent:@"AudioLoadPending.plist"];
    if(!lstat([pending fileSystemRepresentation],&st)){
        NSDictionary *interrupted=Safe(pending,NO,YES)?Plist(Read(pending)):nil;
        if(!SameOwner(interrupted,owner)||unlink([pending fileSystemRepresentation]))return Fail("the interrupted sound load record could not be retired safely");
    }else if(errno!=ENOENT)return Fail("the sound recovery state could not be inspected");
    puts("Native Tiger sound files installed. Driver loading and duplex checks are separate.");close(lock);[pool drain];return 0;
}
