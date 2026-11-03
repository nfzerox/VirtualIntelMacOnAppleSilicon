// Cold Darwin8–10 SMB compatibility installation/update recovery. No listener,
// password storage, polling, account changes, or graphics-path work.
#import <Foundation/Foundation.h>
#import <DiskArbitration/DiskArbitration.h>
#import <CommonCrypto/CommonDigest.h>
#import "opengl-guest-machine.h"
#include "opengl-launchd-state.h"
#include "opengl-state-path.h"
#include "opengl-tiger-kernel-identity.h"
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/sysctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
#include "opengl-disk-readiness.h"
#endif

static NSString *const Tools=@"/Library/Application Support/Virtual Intel Mac/GuestTools";
static NSString *const State=VIM_OPENGL_STATE_PATH;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
static NSString *const Guest=@"tiger",*const Metadata=@"TigerSMB.plist";
static NSString *const Server=@"tiger-smbd",*const Authentication=@"tiger-opendirectory.so";
static NSString *const AuthenticationPath=@"/usr/lib/samba/auth/opendirectory.so";
static NSString *const NativeVersion=@"Version 3.0.10\n";
static const int PolicyVersion=2;
static NSString *const Job=@"/Library/LaunchDaemons/com.virtualintelmac.tiger-smb-compat.plist";
static CFStringRef const Label=CFSTR("com.virtualintelmac.tiger-smb-compat");
#elif __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1060
static NSString *const Guest=@"snowleopard",*const Metadata=@"SnowLeopardSMB.plist";
static NSString *const Server=@"snowleopard-smbd",*const Authentication=@"snowleopard-odsam.dylib";
static NSString *const AuthenticationPath=@"/usr/lib/samba/auth/odsam.dylib";
static NSString *const NativeVersion=@"Version 3.0.28a-apple\n";
static const int PolicyVersion=1;
static NSString *const Job=@"/Library/LaunchDaemons/com.virtualintelmac.snowleopard-smb-compat.plist";
static CFStringRef const Label=CFSTR("com.virtualintelmac.snowleopard-smb-compat");
#else
static NSString *const Guest=@"leopard",*const Metadata=@"LeopardSMB.plist";
static NSString *const Server=@"leopard-smbd",*const Authentication=@"leopard-odsam.dylib";
static NSString *const AuthenticationPath=@"/usr/lib/samba/auth/odsam.dylib";
static NSString *const NativeVersion=@"Version 3.0.25b-apple\n";
static const int PolicyVersion=1;
static NSString *const Job=@"/Library/LaunchDaemons/com.virtualintelmac.leopard-smb-compat.plist";
static CFStringRef const Label=CFSTR("com.virtualintelmac.leopard-smb-compat");
#endif
static NSString *const NativeJob=@"/System/Library/LaunchDaemons/smbd.plist";

static BOOL File(NSString *path,BOOL private){
    struct stat s;return !lstat([path fileSystemRepresentation],&s)&&S_ISREG(s.st_mode)&&
        s.st_uid==0&&s.st_nlink==1&&!(s.st_mode&(private?0077:0022));
}
static BOOL Directory(NSString *path,mode_t mode){
    struct stat s;if(lstat([path fileSystemRepresentation],&s)){
        if(errno!=ENOENT||mkdir([path fileSystemRepresentation],mode))return NO;
        return !chown([path fileSystemRepresentation],0,0)&&!chmod([path fileSystemRepresentation],mode);
    }return S_ISDIR(s.st_mode)&&s.st_uid==0&&!(s.st_mode&0022);
}
static NSString *Digest(NSString *path){
    int fd=open([path fileSystemRepresentation],O_RDONLY|O_NOFOLLOW);if(fd<0)return nil;
    struct stat s;if(fstat(fd,&s)||!S_ISREG(s.st_mode)||s.st_size>16*1024*1024){close(fd);return nil;}
    CC_SHA256_CTX context;CC_SHA256_Init(&context);unsigned char bytes[32768];ssize_t n;
    do{n=read(fd,bytes,sizeof(bytes));if(n>0)CC_SHA256_Update(&context,bytes,(CC_LONG)n);}while(n>0||(n<0&&errno==EINTR));close(fd);if(n<0)return nil;
    unsigned char hash[CC_SHA256_DIGEST_LENGTH];CC_SHA256_Final(hash,&context);
    NSMutableString *result=[NSMutableString string];for(unsigned i=0;i<sizeof(hash);i++)[result appendFormat:@"%02x",hash[i]];return result;
}
static BOOL Copy(NSString *from,NSString *to,mode_t mode){
    if(!File(from,NO))return NO;struct stat s;
    if(!lstat([to fileSystemRepresentation],&s)&&!File(to,NO))return NO;
    NSData *data=[NSData dataWithContentsOfFile:from];
    return data&&[data length]<=16*1024*1024&&[data writeToFile:to options:NSDataWritingAtomic error:NULL]&&
        !chown([to fileSystemRepresentation],0,0)&&!chmod([to fileSystemRepresentation],mode);
}
static BOOL Save(NSDictionary *value,NSString *path,mode_t mode){
    struct stat s;if(!lstat([path fileSystemRepresentation],&s)&&!File(path,NO))return NO;
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:value format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
    return data&&[data writeToFile:path options:NSDataWritingAtomic error:NULL]&&
        !chown([path fileSystemRepresentation],0,0)&&!chmod([path fileSystemRepresentation],mode);
}
static NSData *Run(NSString *program,NSArray *arguments){
    NSTask *task=[[[NSTask alloc] init] autorelease];NSPipe *pipe=[NSPipe pipe];
    [task setLaunchPath:program];[task setArguments:arguments];[task setStandardOutput:pipe];
    [task setStandardError:[NSFileHandle fileHandleWithNullDevice]];[task setStandardInput:[NSFileHandle fileHandleWithNullDevice]];
    @try{[task launch];NSData *out=[[pipe fileHandleForReading] readDataToEndOfFile];[task waitUntilExit];
        return [task terminationStatus]==0&&[out length]<65536?out:nil;}@catch(NSException *error){return nil;}
}
static NSString *RootUUID(void){
    DASessionRef session=DASessionCreate(NULL);if(!session)return nil;
    DADiskRef disk=DADiskCreateFromVolumePath(NULL,session,(CFURLRef)[NSURL fileURLWithPath:@"/"]);
    NSDictionary *info=disk?(NSDictionary *)DADiskCopyDescription(disk):nil;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    if(![info objectForKey:(NSString *)kDADiskDescriptionVolumeUUIDKey]){
        [info release];if(disk)CFRelease(disk);
        VIMOpenGLWaitForDisks(session);
        disk=DADiskCreateFromVolumePath(NULL,session,(CFURLRef)[NSURL fileURLWithPath:@"/"]);
        info=disk?(NSDictionary *)DADiskCopyDescription(disk):nil;
    }
#endif
    CFUUIDRef uuid=(CFUUIDRef)[info objectForKey:(NSString *)kDADiskDescriptionVolumeUUIDKey];
    NSString *text=uuid?[(NSString *)CFUUIDCreateString(NULL,uuid) autorelease]:nil;
    [info release];if(disk)CFRelease(disk);CFRelease(session);return text;
}
static BOOL UUID(id text){CFUUIDRef u=[text isKindOfClass:[NSString class]]?CFUUIDCreateFromString(NULL,(CFStringRef)text):NULL;if(u)CFRelease(u);return u!=NULL;}
static BOOL SameOwner(NSDictionary *receipt,NSDictionary *owner){
    return receipt&&[[receipt objectForKey:@"MachineIdentifier"] isEqual:[owner objectForKey:@"MachineIdentifier"]]&&
        [[receipt objectForKey:@"VolumeUUID"] isEqual:[owner objectForKey:@"VolumeUUID"]];
}
static int Error(const char *what);
#include "opengl-native-smb-client.h"
static NSDictionary *ServerPayload(NSString *directory){
    NSString *path=[directory stringByAppendingPathComponent:Metadata];
    if(!File(path,NO))return nil;NSDictionary *manifest=[NSDictionary dictionaryWithContentsOfFile:path];
    if(![[manifest objectForKey:@"FormatVersion"] isEqual:[NSNumber numberWithInt:1]]||
       ![[manifest objectForKey:@"PolicyVersion"] isEqual:[NSNumber numberWithInt:PolicyVersion]]||
       ![[manifest objectForKey:@"Guest"] isEqual:Guest])return nil;
    NSMutableDictionary *files=[NSMutableDictionary dictionary];
    NSArray *names=[NSArray arrayWithObjects:Server,Authentication,@"Samba-COPYING.txt",nil];
    NSEnumerator *enumerator=[names objectEnumerator];NSString *name;
    while((name=[enumerator nextObject])){path=[directory stringByAppendingPathComponent:name];
        NSString *digest=Digest(path);if(!File(path,NO)||!digest)return nil;
        [files setObject:digest forKey:name];
    }
    NSMutableDictionary *payload=[[manifest mutableCopy] autorelease];[payload setObject:files forKey:@"Files"];return payload;
}
static NSDictionary *Payload(NSString *directory){
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    if(!ClientPayload(directory))return nil;
#endif
    return ServerPayload(directory);
}
static BOOL PrivateStage(NSString *directory){
    if(![[directory stringByDeletingLastPathComponent] isEqual:@"/private/var/tmp"])return NO;
    NSString *leaf=[directory lastPathComponent],*prefix=@"VirtualIntelMac.";
    if(![leaf hasPrefix:prefix]||[leaf length]!=[prefix length]+6)return NO;
    NSString *suffix=[leaf substringFromIndex:[prefix length]];
    if([suffix rangeOfCharacterFromSet:[[NSCharacterSet alphanumericCharacterSet] invertedSet]].location!=NSNotFound)return NO;
    struct stat s;return !lstat([directory fileSystemRepresentation],&s)&&S_ISDIR(s.st_mode)&&s.st_uid==0&&!(s.st_mode&0077);
}
static BOOL Register(void){
    NSMutableDictionary *job=[NSMutableDictionary dictionaryWithObjectsAndKeys:(NSString *)Label,@"Label",@"root",@"UserName",
        [NSArray arrayWithObjects:[Tools stringByAppendingPathComponent:@"opengl-file-sharing"],@"--repair",nil],@"ProgramArguments",
        [NSNumber numberWithBool:YES],@"RunAtLoad",@"/var/log/VirtualIntelMacFileSharing.log",@"StandardErrorPath",nil];
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    // Tiger's native spelling for a one-shot job, like the sound repair job.
    [job setObject:[NSNumber numberWithBool:YES] forKey:@"OnDemand"];
#endif
    if(!Save(job,Job,0644))return NO;
    // One cold process per boot, exits after checking/repairing. No KeepAlive.
    return VIMOpenGLSystemJobLoaded(Label)||Run(@"/bin/launchctl",[NSArray arrayWithObjects:@"load",Job,nil])!=nil;
}
static int Error(const char *what){fprintf(stderr,"Virtual Intel Mac File Sharing: %s. Existing account and sharing preferences were not changed.\n",what);return 1;}
static int Denied(const char *what){Error(what);return 77;}
static int Repair(NSDictionary *owner,NSString *build){
    NSDictionary *manifest=Payload(Tools);if(!manifest)return Error("compiled compatibility support is missing or changed");
    NSDictionary *files=[manifest objectForKey:@"Files"];
    NSString *receiptPath=[State stringByAppendingPathComponent:@"FileSharing.plist"];
    NSDictionary *prior=File(receiptPath,YES)?[NSDictionary dictionaryWithContentsOfFile:receiptPath]:nil;
    if(prior&&!SameOwner(prior,owner))return Error("another transaction owns the saved compatibility state");
    NSDictionary *native=[NSDictionary dictionaryWithContentsOfFile:NativeJob];
    NSArray *arguments=[native objectForKey:@"ProgramArguments"];
    if(![[native objectForKey:@"Label"] isEqual:@"org.samba.smbd"]||![arguments isKindOfClass:[NSArray class]]||
       ![arguments count]||![[arguments objectAtIndex:0] isEqual:@"/usr/sbin/smbd"])return Error("the native sharing service is not recognized");
    NSData *version=Run(@"/usr/sbin/smbd",[NSArray arrayWithObject:@"-V"]);
    NSString *versionText=version?[[[NSString alloc] initWithData:version encoding:NSUTF8StringEncoding] autorelease]:nil;
    if(![versionText isEqual:NativeVersion])return Error("the native sharing executable has an unsupported version");
    NSArray *names=[NSArray arrayWithObjects:Authentication,Server,nil];
    NSDictionary *targets=[NSDictionary dictionaryWithObjectsAndKeys:AuthenticationPath,Authentication,@"/usr/sbin/smbd",Server,nil];
    NSString *originals=[State stringByAppendingPathComponent:@"OriginalFileSharing"];
    if(!Directory(originals,0700))return Error("the original sharing files could not be preserved");
    NSMutableDictionary *components=[NSMutableDictionary dictionary];BOOL changed=NO;
    NSEnumerator *enumerator=[names objectEnumerator];NSString *name;
    while((name=[enumerator nextObject])){
        NSString *target=[targets objectForKey:name],*current=Digest(target),*expected=[files objectForKey:name];
        if(!File(target,NO)||!current)return Error("a native sharing file is unavailable");
        NSDictionary *previous=[[prior objectForKey:@"Components"] objectForKey:name];
        BOOL ours=[[previous objectForKey:@"InstalledSHA256"] isEqual:current];
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1060
        // Apple 235.7 has server-side fixes but prints the same -V string.
        // Never replace an unqualified newer native service with 235.6.
        if(!ours){
            NSData *bytes=[NSData dataWithContentsOfMappedFile:target];
            NSData *tag=[@"/SourceCache/samba/samba-235.6/" dataUsingEncoding:NSASCIIStringEncoding];
            if(!bytes||[bytes rangeOfData:tag options:0 range:NSMakeRange(0,[bytes length])].location==NSNotFound)
                return Error("this native Samba revision is not supported by the bundled update");
        }
#endif
        // An interrupted publication/rollback can leave either saved native
        // bytes OR our bytes. Both retain the same verified original identity.
        BOOL known=ours||[[previous objectForKey:@"OriginalSHA256"] isEqual:current];
        NSString *originalBuild=known?[previous objectForKey:@"OriginalSourceBuild"]:build;
        NSString *originalHash=known?[previous objectForKey:@"OriginalSHA256"]:current;
        if(![originalBuild isKindOfClass:[NSString class]]||![originalBuild length]||
           [originalBuild rangeOfCharacterFromSet:[[NSCharacterSet alphanumericCharacterSet] invertedSet]].location!=NSNotFound||
           ![originalHash isKindOfClass:[NSString class]]||[originalHash length]!=64||
           (!ours&&[current isEqual:expected]))return Error("the original sharing identity is ambiguous");
        NSString *folder=[originals stringByAppendingPathComponent:originalBuild],*backup=[folder stringByAppendingPathComponent:name];
        if(!Directory(folder,0700))return Error("the original sharing backup directory is unavailable");
        if(known){if(!File(backup,YES)||![Digest(backup) isEqual:originalHash])return Error("the saved native sharing file changed");}
        else {
            struct stat s;if(!lstat([backup fileSystemRepresentation],&s)){
                if(!File(backup,YES)||![Digest(backup) isEqual:current])return Error("an unexpected backup occupies this OS build");
            }else if(errno!=ENOENT||!Copy(target,backup,0600)||![Digest(backup) isEqual:current])return Error("the native sharing backup could not be verified");
        }
        [components setObject:[NSDictionary dictionaryWithObjectsAndKeys:originalBuild,@"OriginalSourceBuild",originalHash,@"OriginalSHA256",expected,@"InstalledSHA256",nil] forKey:name];
        changed|=![current isEqual:expected];
    }
    NSMutableDictionary *receipt=[NSMutableDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithInt:1],@"FormatVersion",
        [owner objectForKey:@"MachineIdentifier"],@"MachineIdentifier",[owner objectForKey:@"VolumeUUID"],@"VolumeUUID",
        build,@"ProductBuildVersion",components,@"Components",@"prepared",@"Phase",
        [manifest objectForKey:@"PolicyVersion"],@"PolicyVersion",Guest,@"Guest",
        [files objectForKey:Server],@"ServerSHA256",[files objectForKey:Authentication],@"AuthSHA256",nil];
    // Record recovery provenance BEFORE publishing either executable.
    if(!Save(receipt,receiptPath,0600))return Error("the sharing transaction could not be saved");
    BOOL loaded=VIMOpenGLSystemJobLoaded(CFSTR("org.samba.smbd"));
    if(changed&&loaded&&!Run(@"/bin/launchctl",[NSArray arrayWithObjects:@"unload",NativeJob,nil]))return Error("the current native sharing service could not stop");
    BOOL copied=YES;
    enumerator=[names objectEnumerator];
    while(changed&&(name=[enumerator nextObject])){NSString *target=[targets objectForKey:name];
        if(![Digest(target) isEqual:[files objectForKey:name]]&&
           !Copy([Tools stringByAppendingPathComponent:name],target,0755)){copied=NO;break;}
        if(![Digest(target) isEqual:[files objectForKey:name]]){copied=NO;break;}
    }
    if(!copied){
        BOOL restored=YES;enumerator=[names objectEnumerator];
        while((name=[enumerator nextObject])){NSDictionary *record=[components objectForKey:name];
            NSString *backup=[[originals stringByAppendingPathComponent:[record objectForKey:@"OriginalSourceBuild"]] stringByAppendingPathComponent:name];
            restored&=File(backup,YES)&&[Digest(backup) isEqual:[record objectForKey:@"OriginalSHA256"]]&&Copy(backup,[targets objectForKey:name],0755);
        }
        if(loaded&&restored)Run(@"/bin/launchctl",[NSArray arrayWithObjects:@"load",NativeJob,nil]);
        return Error(restored?"publishing failed; original sharing files were restored":"publishing failed; the verified recovery transaction was retained");
    }
    if(changed&&loaded&&!Run(@"/bin/launchctl",[NSArray arrayWithObjects:@"load",NativeJob,nil]))return Error("the native sharing service could not restart");
    [receipt setObject:@"installed" forKey:@"Phase"];
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    BOOL restart=NO;int client=RepairClient(owner,build,&restart);if(client)return client;
    [receipt setObject:[NSNumber numberWithInt:2] forKey:@"ClientPolicyVersion"];
    [receipt setObject:[NSNumber numberWithBool:restart] forKey:@"RestartRequired"];
#endif
    if(!Save(receipt,receiptPath,0600)||!Register())return Error("the cold update-recovery registration could not complete");
    NSData *json=[NSJSONSerialization dataWithJSONObject:receipt options:0 error:NULL];
    if(!json)return Error("the installation evidence could not be encoded");
    fwrite([json bytes],1,[json length],stdout);fputc('\n',stdout);return 0;
}
int main(int argc,char **argv){
    NSAutoreleasePool *pool=[NSAutoreleasePool new];
    BOOL clientOnly=NO;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    clientOnly=argc==3&&!strcmp(argv[1],"--install-client");
#endif
    BOOL install=clientOnly||(argc==3&&!strcmp(argv[1],"--install"));
    if(!install&&(argc!=2||strcmp(argv[1],"--repair")))return 64;
    if(geteuid()!=0)return Denied("native compatibility setup requires the root service");
    if(!VIMOpenGLGuestMachine())return Denied("the compatible virtual PCI device is unavailable");
    umask(0077);
    NSDictionary *system=[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"];
    NSString *version=[system objectForKey:@"ProductVersion"],*build=[system objectForKey:@"ProductBuildVersion"];
    NSString *prefix=[Guest isEqual:@"tiger"]?@"10.4.":[Guest isEqual:@"snowleopard"]?@"10.6.":@"10.5.";
    if(![version hasPrefix:prefix]||![build isKindOfClass:[NSString class]]||![build length]||[build length]>40||
       [build rangeOfCharacterFromSet:[[NSCharacterSet alphanumericCharacterSet] invertedSet]].location!=NSNotFound)
        return Denied("the installed system version is unavailable or unsupported");
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    if(!VIMTigerRunningKernelMatchesFile())return 75;
#else
    char kernel[128]={0};size_t count=sizeof(kernel);
    if(sysctlbyname("kern.osversion",kernel,&count,NULL,0)||![build isEqual:[NSString stringWithUTF8String:kernel]])return 75;
#endif
    NSString *path=[State stringByAppendingPathComponent:@"Ownership.plist"];
    if(!File(path,YES))return Denied("the owned setup record is unavailable");
    NSDictionary *owner=[NSDictionary dictionaryWithContentsOfFile:path];
    if(!UUID([owner objectForKey:@"MachineIdentifier"])||!UUID([owner objectForKey:@"VolumeUUID"])||
       ![[owner objectForKey:@"Guest"] isEqual:Guest])return Denied("the owned setup record has an incompatible identity");
    NSString *rootUUID=RootUUID();
    if(!rootUUID)return 75; // Native publication can lag the cold launch job.
    if(![[owner objectForKey:@"VolumeUUID"] isEqual:rootUUID])return Denied("the root volume does not match the owned setup record");
    int lock=open([[State stringByAppendingPathComponent:@"FileSharing.lock"] fileSystemRepresentation],O_RDWR|O_CREAT|O_NOFOLLOW,0600);
    struct stat s;if(lock<0||fstat(lock,&s)||!S_ISREG(s.st_mode)||s.st_uid||s.st_nlink!=1||(s.st_mode&0077)){
        if(lock>=0)close(lock);return Denied("the private compatibility lock is unavailable");}
    if(fcntl(lock,F_SETFD,FD_CLOEXEC)){close(lock);return Denied("the private compatibility lock could not be secured");}
    if(flock(lock,LOCK_EX|LOCK_NB)){close(lock);return 75;}
    if(install){
        NSString *from=[NSString stringWithUTF8String:argv[2]];
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
        if(clientOnly){
            // Upgrade a client beside an already-current, root-owned native
            // server. Copy those existing bytes locally into the private
            // stage, avoiding another multi-MB transfer through Tiger's UART.
            if(!PrivateStage(from)||!ClientPayload(from)||!ServerPayload(Tools)){
                close(lock);return Error("the existing native sharing support could not be reused");
            }
            NSEnumerator *existing=[[NSArray arrayWithObjects:Server,Authentication,@"Samba-COPYING.txt",Metadata,nil] objectEnumerator];NSString *name;
            while((name=[existing nextObject]))if(!Copy([Tools stringByAppendingPathComponent:name],[from stringByAppendingPathComponent:name],
                ([name hasSuffix:@".txt"]||[name hasSuffix:@".plist"])?0644:0755)){
                close(lock);return Error("the existing native sharing support could not be staged locally");
            }
        }
#endif
        NSDictionary *manifest=PrivateStage(from)?Payload(from):nil;
        if(!manifest||!Directory([Tools stringByDeletingLastPathComponent],0755)||!Directory(Tools,0755)){close(lock);return Error("the staged compatibility payload could not be verified");}
        NSMutableArray *names=[NSMutableArray arrayWithObjects:Server,Authentication,@"Samba-COPYING.txt",Metadata,@"opengl-file-sharing",nil];
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
        [names addObjectsFromArray:ClientFiles()];
#endif
        NSEnumerator *enumerator=[names objectEnumerator];NSString *name;
        while((name=[enumerator nextObject])){
            if(!Copy([from stringByAppendingPathComponent:name],[Tools stringByAppendingPathComponent:name],
                ([name hasSuffix:@".txt"]||[name hasSuffix:@".plist"])?0644:0755)){close(lock);return Error("the compiled compatibility payload could not be published");}
        }
    }
    if(!install&&!Payload(Tools)){close(lock);[pool drain];return 66;}
    int result=Repair(owner,build);close(lock);[pool drain];return result;
}
