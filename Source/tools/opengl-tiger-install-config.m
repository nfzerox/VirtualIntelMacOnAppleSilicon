// Genuine Tiger minstall preparation. Explicit fresh-volume authorization,
// native Disk Arbitration identity, no GUI/timer automation or account edits.
#import "opengl-period-foundation.h"
#import "opengl-guest-machine.h"
#import <DiskArbitration/DiskArbitration.h>
#import <CommonCrypto/CommonDigest.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "opengl-disk-readiness.h"
static NSString *const MediaTools=@"/System/Installation/Packages/Extras/VirtualIntelMac";
static int Error(const char *stage){fprintf(stderr,"Tiger installation preparation: %s\n",stage);return 1;}
static BOOL UUID(id text){
    CFUUIDRef value=[text isKindOfClass:[NSString class]]?CFUUIDCreateFromString(NULL,(CFStringRef)text):NULL;
    if(value)CFRelease(value);return value!=NULL;
}
static NSDictionary *Volume(DASessionRef session,const struct statfs *mount){
    DADiskRef disk=DADiskCreateFromBSDName(NULL,session,mount->f_mntfromname);
    NSDictionary *info=disk?[(NSDictionary *)DADiskCopyDescription(disk) autorelease]:nil;
    if(disk)CFRelease(disk);return info;
}
static NSString *VolumeUUID(NSDictionary *info){
    CFTypeRef value=(CFTypeRef)[info objectForKey:(NSString *)kDADiskDescriptionVolumeUUIDKey];
    return value&&CFGetTypeID(value)==CFUUIDGetTypeID()?[(NSString *)CFUUIDCreateString(NULL,(CFUUIDRef)value) autorelease]:nil;
}
static BOOL Directory(NSString *path,mode_t mode){
    struct stat st;
    // Apple's installed Library ancestors are root:admin and group writable.
    // Admit only those two native ancestors, never our control/state paths.
    if(!lstat([path fileSystemRepresentation],&st)){
        BOOL nativeAncestor=([path hasSuffix:@"/Library"]||[path hasSuffix:@"/Library/Application Support"])&&st.st_gid==80;
        return S_ISDIR(st.st_mode)&&st.st_uid==0&&!(st.st_mode&0002)&&(!(st.st_mode&0020)||nativeAncestor);
    }
    if(errno!=ENOENT||[path isEqual:@"/"]||!Directory([path stringByDeletingLastPathComponent],0755))return NO;
    return !mkdir([path fileSystemRepresentation],mode)&&!chown([path fileSystemRepresentation],0,0);
}
static BOOL Write(NSDictionary *value,NSString *path,mode_t mode){
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:value format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
    return data&&[data writeToFile:path options:NSDataWritingAtomic error:NULL]&&
        !chown([path fileSystemRepresentation],0,0)&&!chmod([path fileSystemRepresentation],mode);
}
static NSString *Digest(NSData *data){
    CC_SHA256_CTX context;unsigned char bytes[CC_SHA256_DIGEST_LENGTH];CC_SHA256_Init(&context);
    CC_SHA256_Update(&context,[data bytes],(CC_LONG)[data length]);CC_SHA256_Final(bytes,&context);
    NSMutableString *text=[NSMutableString string];unsigned i;for(i=0;i<sizeof(bytes);i++)[text appendFormat:@"%02x",bytes[i]];return text;
}
#include "opengl-tiger-control-files.h"
static NSDictionary *NativeControls(NSData **receiptData){
    NSString *path=[MediaTools stringByAppendingPathComponent:@"BuildReceipt.plist"];
    struct stat entry;struct statfs media;
    if(lstat([path fileSystemRepresentation],&entry)||!S_ISREG(entry.st_mode)||entry.st_nlink!=1||
       entry.st_size<1||entry.st_size>65536||statfs([path fileSystemRepresentation],&media)||!(media.f_flags&MNT_RDONLY))return nil;
    NSDictionary *record=[NSDictionary dictionaryWithContentsOfFile:path];
    NSDictionary *files=[record objectForKey:@"Files"];
    NSSet *expected=[NSSet setWithArray:VIMTigerControlNames()];
    if(![[record objectForKey:@"FormatVersion"] isEqual:[NSNumber numberWithInt:1]]||
       ![[record objectForKey:@"MinimumOS"] isEqual:@"10.4"]||![[record objectForKey:@"Architecture"] isEqual:@"i386"]||
       ![files isKindOfClass:[NSDictionary class]]||![[NSSet setWithArray:[files allKeys]] isEqual:expected])return nil;
    NSMutableDictionary *payload=[NSMutableDictionary dictionary];NSEnumerator *names=[expected objectEnumerator];NSString *name;
    while((name=[names nextObject])){
        NSString *source=[MediaTools stringByAppendingPathComponent:name];
        if(lstat([source fileSystemRepresentation],&entry)||!S_ISREG(entry.st_mode)||entry.st_nlink!=1||entry.st_size<1||entry.st_size>8*1024*1024||
           statfs([source fileSystemRepresentation],&media)||!(media.f_flags&MNT_RDONLY))return nil;
        NSData *data=[NSData dataWithContentsOfFile:source];uint32_t header[4]={0};
        if([data length]>=sizeof(header))memcpy(header,[data bytes],sizeof(header));
        if([name isEqual:@"TigerSMB.plist"]){
            NSDictionary *metadata=[NSDictionary dictionaryWithContentsOfFile:source];
            if(![[metadata objectForKey:@"Guest"] isEqual:@"tiger"]||
               ![[metadata objectForKey:@"PolicyVersion"] isEqual:[NSNumber numberWithInt:2]])return nil;
        }else if([name isEqual:@"TigerSMBClient.plist"]){
            NSDictionary *metadata=[NSDictionary dictionaryWithContentsOfFile:source];
            if(![[metadata objectForKey:@"Guest"] isEqual:@"tiger"]||
               ![[metadata objectForKey:@"PolicyVersion"] isEqual:[NSNumber numberWithInt:2]])return nil;
        }else if([name isEqual:@"TigerSMBClient-Info.plist"]){
            NSDictionary *metadata=[NSDictionary dictionaryWithContentsOfFile:source];
            if(![[metadata objectForKey:@"CFBundleIdentifier"] isEqual:@"com.apple.filesystems.smbfs"]||
               ![[metadata objectForKey:@"CFBundleVersion"] isEqual:@"1.3.7"])return nil;
        }else if(![name hasSuffix:@".txt"]){
            uint32_t kind=[name isEqual:@"tiger-smbfs"]?1:([name isEqual:@"tiger-opendirectory.so"]?8:2);
            if(!data||header[0]!=0xfeedface||header[1]!=7||header[3]!=kind)return nil;
        }
        [payload setObject:data forKey:name];
    }
    if(receiptData)*receiptData=[NSData dataWithContentsOfFile:path];return payload;
}
int main(int argc,char **argv){
    NSAutoreleasePool *pool=[NSAutoreleasePool new];
    BOOL automatic=argc==2&&!strcmp(argv[1],"--prepare-automatic");
    BOOL installOnly=argc==2&&!strcmp(argv[1],"--prepare-install");
    BOOL finish=argc==3&&!strcmp(argv[1],"--finish-install");
    BOOL inspect=argc==2&&!strcmp(argv[1],"--inspect-target");struct utsname kernel;
    if((!automatic&&!installOnly&&!finish&&!inspect)||geteuid()!=0||uname(&kernel)||strncmp(kernel.release,"8.",2)||!VIMOpenGLGuestMachine())return 77;
    NSDictionary *system=[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"];
    struct statfs root;
    if(![[system objectForKey:@"ProductBuildVersion"] isEqual:@"8R4088"]||statfs("/",&root)||!(root.f_flags&MNT_RDONLY))return 77;
    DASessionRef session=DASessionCreate(NULL);if(!session)return Error("opening native disk identity service");
    if(automatic)VIMOpenGLWaitForDisks(session);
    struct statfs *mounts=NULL;int count=getmntinfo(&mounts,MNT_NOWAIT),i;
    NSDictionary *policy=nil;unsigned matches=0;
    for(i=0;i<count;i++){
        if(!(mounts[i].f_flags&MNT_RDONLY))continue;
        NSDictionary *info=Volume(session,&mounts[i]);
        if(![[info objectForKey:(NSString *)kDADiskDescriptionVolumeNameKey] isEqual:@"VIM Setup"])continue;
        NSString *path=[[NSString stringWithUTF8String:mounts[i].f_mntonname] stringByAppendingPathComponent:@"VirtualIntelMac.plist"];
        NSDictionary *candidate=[NSDictionary dictionaryWithContentsOfFile:path];
        if(![[candidate objectForKey:@"Kind"] isEqual:@"VirtualIntelMacLegacySetup"]||
           ![[candidate objectForKey:@"Guest"] isEqual:@"tiger"]||![[candidate objectForKey:@"FormatVersion"] isEqual:[NSNumber numberWithInt:1]]||
           !UUID([candidate objectForKey:@"VolumeUUID"])||!UUID([candidate objectForKey:@"MachineIdentifier"]))continue;
        policy=candidate;matches++;
    }
    if(matches!=1){CFRelease(session);return Error("a unique Tiger setup medium was not found");}
    if(automatic&&![[policy objectForKey:@"Enabled"] isEqual:[NSNumber numberWithBool:YES]]){CFRelease(session);return 0;}
    NSString *target=nil;matches=0;
    for(i=0;i<count;i++){
        if((mounts[i].f_flags&MNT_RDONLY)||strcmp(mounts[i].f_fstypename,"hfs")||strncmp(mounts[i].f_mntonname,"/Volumes/",9))continue;
        NSString *uuid=VolumeUUID(Volume(session,&mounts[i]));
        if(![uuid isEqual:[policy objectForKey:@"VolumeUUID"]])continue;
        target=[NSString stringWithUTF8String:mounts[i].f_mntonname];matches++;
    }
    CFRelease(session);
    if(matches!=1)return Error("the authorized HFS target is not uniquely mounted and writable");
    if(inspect){printf("Tiger target matched: %s\n",[target fileSystemRepresentation]);[pool drain];return 0;}
    NSFileManager *manager=[NSFileManager defaultManager];
    if(finish){
        // The actual package postflight runs after payload extraction. Its
        // native minstall config lives on the installer's RAM disk, so package
        // replacement of /Library cannot destroy this authorization.
        NSDictionary *configuration=[NSDictionary dictionaryWithContentsOfFile:@"/var/tmp/VirtualIntelMac-minstallconfig.plist"];
        NSDictionary *installed=[NSDictionary dictionaryWithContentsOfFile:[target stringByAppendingPathComponent:@"System/Library/CoreServices/SystemVersion.plist"]];
        // The actual8R4088 postflight uses $3 as the selected volume. Manual
        // installs have no RAM minstall file, but still must select the exact
        // UUID-authorized mount. Never adopt whichever disk was installed.
        NSString *selected=[[NSString stringWithUTF8String:argv[2]] stringByStandardizingPath];
        BOOL requiresConfiguration=configuration||[[policy objectForKey:@"Enabled"] boolValue];
        BOOL configured=[[configuration objectForKey:@"Target"] isEqual:target]&&
            [[configuration objectForKey:@"Package"] isEqual:@"/System/Installation/Packages/OSInstall.mpkg"]&&
            [[configuration objectForKey:@"InstallType"] isEqual:@"automated"]&&
            [[configuration objectForKey:@"ShouldErase"] isEqual:[NSNumber numberWithBool:NO]];
        if(![selected isEqual:target]||(requiresConfiguration&&!configured)||
           ![[installed objectForKey:@"ProductBuildVersion"] isEqual:@"8R4088"]||
           ![manager fileExistsAtPath:[target stringByAppendingPathComponent:@"Library/Receipts/Essentials.pkg/Contents/Archive.bom"]])
            return Error("the authorized native installation has not extracted its system payload");
    }else if([manager fileExistsAtPath:[target stringByAppendingPathComponent:@"System"]]||
             [manager fileExistsAtPath:[target stringByAppendingPathComponent:@"private"]])
        return Error("target is not a new, uninstalled system; no installation was retried");
    NSString *support=[target stringByAppendingPathComponent:@"Library/Application Support/Virtual Intel Mac"];
    NSString *state=[support stringByAppendingPathComponent:@"State/LegacySetup"];
    NSString *tools=[support stringByAppendingPathComponent:@"GuestTools"];
    NSString *journal=[state stringByAppendingPathComponent:@"Ownership.plist"];
    NSDictionary *previous=[NSDictionary dictionaryWithContentsOfFile:journal];
    if([manager fileExistsAtPath:journal]&&(!finish||
       ![[previous objectForKey:@"MachineIdentifier"] isEqual:[policy objectForKey:@"MachineIdentifier"]]||
       ![[previous objectForKey:@"VolumeUUID"] isEqual:[policy objectForKey:@"VolumeUUID"]]))
        return Error("this volume already has an installation transaction; its original state was retained");
    NSData *controlRecord=nil;NSDictionary *payload=NativeControls(&controlRecord);
    if(!payload||!controlRecord)return Error("verified native control payload is unavailable");
    // Only before native installation; leave Apple's installed root metadata.
    if(!finish&&(chown([target fileSystemRepresentation],0,0)||chmod([target fileSystemRepresentation],0755)))
        return Error("preparing the new volume root owner");
    if(!Directory(state,0700)||!Directory(tools,0755))return Error("creating root-owned support directories");
    int owner=open([[state stringByAppendingPathComponent:@"Prepare.lock"] fileSystemRepresentation],O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    struct stat lock;
    if(owner<0||fstat(owner,&lock)||!S_ISREG(lock.st_mode)||lock.st_uid||lock.st_nlink!=1||(lock.st_mode&0077)||flock(owner,LOCK_EX|LOCK_NB))
        return Error("another preparation owns this volume, or its private lock is unsafe");
    if(!finish&&[manager fileExistsAtPath:journal])return Error("this volume already has an installation transaction; its original state was retained");
    NSEnumerator *names=[payload keyEnumerator];NSString *name;
    while((name=[names nextObject])){
        NSString *destination=[tools stringByAppendingPathComponent:name];struct stat existing;
        if(!lstat([destination fileSystemRepresentation],&existing)&&
           (!S_ISREG(existing.st_mode)||existing.st_uid||existing.st_nlink!=1))return Error("an unrelated object occupies the native control path");
        NSData *data=[payload objectForKey:name];
        if(![data writeToFile:destination options:NSDataWritingAtomic error:NULL]||
           chown([destination fileSystemRepresentation],0,0)||chmod([destination fileSystemRepresentation],
               ([name hasSuffix:@".txt"]||[name hasSuffix:@".plist"])?0644:0755)||
           ![Digest([NSData dataWithContentsOfFile:destination]) isEqual:Digest(data)])return Error("staging verified native installed-system controls");
    }
    NSString *controlPath=[tools stringByAppendingPathComponent:@"BuildReceipt.plist"];
    if(![controlRecord writeToFile:controlPath options:NSDataWritingAtomic error:NULL]||
       chown([controlPath fileSystemRepresentation],0,0)||chmod([controlPath fileSystemRepresentation],0644))return Error("saving the installed control receipt");
    NSDictionary *daemon=[NSDictionary dictionaryWithObjectsAndKeys:@"com.mac.virtualintelmac.opengl-agent",@"Label",
        [NSArray arrayWithObjects:@"/Library/Application Support/Virtual Intel Mac/GuestTools/opengl-guest-agent",@"--device",@"/dev/cu.serial2",nil],@"ProgramArguments",
        [NSNumber numberWithBool:YES],@"RunAtLoad",[NSNumber numberWithBool:NO],@"OnDemand",nil];
    NSString *jobs=[target stringByAppendingPathComponent:@"Library/LaunchDaemons"];
    if(!Directory(jobs,0755)||!Write(daemon,[jobs stringByAppendingPathComponent:@"com.mac.virtualintelmac.opengl-agent.plist"],0644))return Error("publishing native Tiger launchd control");
    NSMutableDictionary *record=[[policy mutableCopy] autorelease];
    [record setObject:finish?@"native-files-installed":@"native-install-prepared" forKey:@"Phase"];
    [record setObject:Digest([payload objectForKey:@"opengl-guest-agent"]) forKey:@"GuestAgentSHA256"];
    [record setObject:Digest(controlRecord) forKey:@"GuestToolsBuildReceiptSHA256"];
    [record setObject:[NSNumber numberWithBool:installOnly||![[policy objectForKey:@"Enabled"] boolValue]] forKey:@"InstallOnly"];
    if(!Write(record,journal,0600))return Error("claiming the authorized installation transaction");
    if(finish){
        NSString *graphics=[MediaTools stringByAppendingPathComponent:@"Graphics"],*program=[graphics stringByAppendingPathComponent:@"opengl-tiger-graphics"];
        struct stat executable;struct statfs source;
        NSDictionary *receipt=[NSDictionary dictionaryWithContentsOfFile:[graphics stringByAppendingPathComponent:@"BuildReceipt.plist"]];
        if(![receipt isKindOfClass:[NSDictionary class]]||![[receipt objectForKey:@"Files"] isKindOfClass:[NSDictionary class]]||
           lstat([program fileSystemRepresentation],&executable)||!S_ISREG(executable.st_mode)||executable.st_nlink!=1||!(executable.st_mode&0111)||
           executable.st_size<1||executable.st_size>1024*1024||statfs([program fileSystemRepresentation],&source)||!(source.f_flags&MNT_RDONLY)||
           ![[NSFileManager defaultManager] isExecutableFileAtPath:program])
            return Error("the native local graphics installer could not be verified");
        NSTask *task=[[[NSTask alloc] init] autorelease];[task setLaunchPath:program];
        [task setArguments:[NSArray arrayWithObjects:@"--install-target",graphics,target,nil]];
        [task setStandardInput:[NSFileHandle fileHandleWithNullDevice]];
        @try{[task launch];[task waitUntilExit];if([task terminationStatus])return Error("publishing native graphics on the authorized target");}
        @catch(NSException *exception){return Error("starting the native local graphics installer");}
        NSString *sound=[MediaTools stringByAppendingPathComponent:@"opengl-tiger-audio"];
        NSTask *audio=[[[NSTask alloc] init] autorelease];[audio setLaunchPath:sound];
        [audio setArguments:[NSArray arrayWithObjects:@"--install-target",[MediaTools stringByAppendingPathComponent:@"Audio"],target,nil]];
        [audio setStandardInput:[NSFileHandle fileHandleWithNullDevice]];
        @try{[audio launch];[audio waitUntilExit];if([audio terminationStatus])return Error("publishing native sound on the authorized target");}
        @catch(NSException *exception){(void)exception;return Error("starting the native local sound installer");}
        puts("Tiger native controls, graphics and sound staged locally after package extraction.");[pool drain];return 0;
    }
    NSDictionary *config=[NSDictionary dictionaryWithObjectsAndKeys:@"/System/Installation/Packages/OSInstall.mpkg",@"Package",@"automated",@"InstallType",
        @"English",@"Language",target,@"Target",[NSNumber numberWithBool:NO],@"ShouldErase",
        [NSNumber numberWithBool:NO],@"ShouldArchiveAndInstall",[NSNumber numberWithBool:YES],@"QuitWhenDone",nil];
    if(!Write(config,@"/var/tmp/VirtualIntelMac-minstallconfig.plist",0600))return Error("preparing the native Installer configuration");
    puts("Tiger native installation prepared for the authorized new volume.");[pool drain];return 0;
}
