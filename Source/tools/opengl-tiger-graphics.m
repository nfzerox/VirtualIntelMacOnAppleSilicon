// Native Tiger deployment, not a public setup-success gate. Only an exact
// authorized installation's GLEngine and GA companion are changed.
// Preserve Apple's actual engine and all public OpenGL/PowerPC framework slices.
#import "opengl-period-foundation.h"
#import "opengl-guest-machine.h"
#import "opengl-state-path.h"
#import "opengl-boot-identity.h"
#include "opengl-tiger-kernel-identity.h"
#import <CommonCrypto/CommonDigest.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/utsname.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>

static NSString *const StatePath=VIM_OPENGL_STATE_PATH;
static int Fail(const char *reason){fprintf(stderr,"Tiger graphics candidate: %s\n",reason);return 1;}
static BOOL UUID(id value){CFUUIDRef uuid=[value isKindOfClass:[NSString class]]?CFUUIDCreateFromString(NULL,(CFStringRef)value):NULL;if(uuid)CFRelease(uuid);return uuid!=NULL;}
static BOOL Private(NSString *path,BOOL directory){
    struct stat st;
    return !lstat([path fileSystemRepresentation],&st)&&st.st_uid==0&&!(st.st_mode&0077)&&
        (directory?S_ISDIR(st.st_mode):(S_ISREG(st.st_mode)&&st.st_nlink==1));
}
static BOOL Directory(NSString *path,mode_t mode){
    struct stat st;
    if(!lstat([path fileSystemRepresentation],&st))
        return S_ISDIR(st.st_mode)&&st.st_uid==0&&!(st.st_mode&0002);
    if(errno!=ENOENT||[path isEqual:@"/"]||!Directory([path stringByDeletingLastPathComponent],0755))return NO;
    return !mkdir([path fileSystemRepresentation],mode)&&!chown([path fileSystemRepresentation],0,0)&&!chmod([path fileSystemRepresentation],mode);
}
static NSString *Digest(NSString *path){
    int fd=open([path fileSystemRepresentation],O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return nil;
    struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_size>8*1024*1024){close(fd);return nil;}
    CC_SHA256_CTX hash;CC_SHA256_Init(&hash);unsigned char buffer[16384];ssize_t count;
    do{count=read(fd,buffer,sizeof(buffer));if(count>0)CC_SHA256_Update(&hash,buffer,(CC_LONG)count);}while(count>0||(count<0&&errno==EINTR));
    close(fd);if(count<0)return nil;
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256_Final(digest,&hash);
    NSMutableString *text=[NSMutableString string];unsigned index;
    for(index=0;index<sizeof(digest);index++)[text appendFormat:@"%02x",digest[index]];return text;
}
static BOOL Copy(NSString *source,NSString *destination,mode_t mode){
    struct stat st;if(lstat([destination fileSystemRepresentation],&st)&&errno!=ENOENT)return NO;
    if(!lstat([destination fileSystemRepresentation],&st)&&(!S_ISREG(st.st_mode)||st.st_uid||st.st_nlink!=1))return NO;
    NSString *expected=Digest(source);if(!expected)return NO;
    NSData *data=[NSData dataWithContentsOfFile:source];
    return data&&[data writeToFile:destination options:NSDataWritingAtomic error:NULL]&&
        !chown([destination fileSystemRepresentation],0,0)&&!chmod([destination fileSystemRepresentation],mode)&&
        [expected isEqual:Digest(destination)];
}
static BOOL Write(NSDictionary *value,NSString *path){
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:value format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
    return data&&[data writeToFile:path options:NSDataWritingAtomic error:NULL]&&
        !chown([path fileSystemRepresentation],0,0)&&!chmod([path fileSystemRepresentation],0600);
}
static NSString *SourceFile(NSString *source,NSString *name,BOOL staged){
    if(staged&&[name isEqual:@"VMsvga2GA.plugin/Contents/Info.plist"])name=@"GA-Info.plist";
    if(staged&&[name isEqual:@"VMsvga2GA.plugin/Contents/MacOS/VMsvga2GA"])name=@"VMsvga2GA";
    return [source stringByAppendingPathComponent:name];
}
static NSString *InstalledPath(NSString *target,NSString *absolute){
    return target?[target stringByAppendingString:absolute]:absolute;
}
static unsigned NativeDispatchSlots(NSString *path){
    // Both measured native engines answer this context-free selector BEFORE
    // validating a context. No GL initialization/draw or guest CPU emulation
    // is needed. Never mistake one of our own engines for a native original.
    struct stat file,image;
    if(lstat([path fileSystemRepresentation],&file)||!S_ISREG(file.st_mode)||file.st_uid||
       file.st_nlink!=1||(file.st_mode&0022))return 0;
    void *library=dlopen([path fileSystemRepresentation],RTLD_NOW|RTLD_LOCAL);if(!library)return 0;
    int (*integer)(void *,unsigned,int *)=dlsym(library,"gliGetInteger");Dl_info info={0};
    int bytes=0;unsigned slots=0;
    if(!dlsym(library,"VIMGLINotifyCPUSurfaceCopy")&&integer&&dladdr(integer,&info)&&info.dli_fname&&
       !stat(info.dli_fname,&image)&&image.st_dev==file.st_dev&&image.st_ino==file.st_ino&&
       !integer(NULL,224,&bytes)&&(bytes==686*4||bytes==759*4))slots=(unsigned)bytes/4;
    dlclose(library);return slots;
}
int main(int argc,char **argv){
    NSAutoreleasePool *pool=[NSAutoreleasePool new];struct utsname kernel;umask(0077);
    BOOL staged=argc==3&&!strcmp(argv[1],"--install");
    BOOL preboot=argc==4&&!strcmp(argv[1],"--install-target");
    if((!preboot&&(argc!=3||(!staged&&strcmp(argv[1],"--install-candidate"))))||geteuid()!=0||uname(&kernel)||strncmp(kernel.release,"8.",2)||!VIMOpenGLGuestMachine())return 77;
    NSString *target=preboot?[NSString stringWithUTF8String:argv[3]]:nil;
    struct statfs media,root;struct stat directory;
    if(preboot&&(![target hasPrefix:@"/Volumes/"]||![target isEqual:[target stringByStandardizingPath]]||
       lstat([target fileSystemRepresentation],&directory)||!S_ISDIR(directory.st_mode)||directory.st_uid||
       statfs([target fileSystemRepresentation],&root)||(root.f_flags&MNT_RDONLY)||strcmp(root.f_fstypename,"hfs")||
       strcmp(root.f_mntonname,[target fileSystemRepresentation])))return Fail("the preboot target is not an exact writable native HFS mount");
    if(!preboot&&statfs("/",&root))return 77;
    NSString *State=InstalledPath(target,StatePath);
    NSDictionary *system=[NSDictionary dictionaryWithContentsOfFile:InstalledPath(target,@"/System/Library/CoreServices/SystemVersion.plist")];
    NSString *build=[system objectForKey:@"ProductBuildVersion"];
    if(![[system objectForKey:@"ProductVersion"] hasPrefix:@"10.4."]||
       ![build isKindOfClass:[NSString class]]||![build length]||[build length]>40||
       [build rangeOfCharacterFromSet:[[NSCharacterSet alphanumericCharacterSet] invertedSet]].location!=NSNotFound)return 77;
    // Never publish a new engine into a half-applied update's old kernel.
    if(!preboot&&!VIMTigerRunningKernelMatchesFile())return 75;
    if(preboot&&![build isEqual:@"8R4088"])return 77;
    NSString *source=[NSString stringWithUTF8String:argv[2]];
    if(staged){
        NSString *prefix=@"/private/var/db/VirtualIntelMac.";
        NSString *suffix=[source hasPrefix:prefix]?[source substringFromIndex:[prefix length]]:nil;
        if([suffix length]!=6||[suffix rangeOfCharacterFromSet:[[NSCharacterSet alphanumericCharacterSet] invertedSet]].location!=NSNotFound||
           !Private(source,YES)||!Private([source stringByAppendingPathComponent:@"BuildReceipt.plist"],NO))return Fail("the native staging directory is not the authorized private path");
        // Tiger Foundation rewrites /private/var/... to /var/... when
        // standardizing. The exact prefix + six alphanumeric characters
        // above already excludes traversal, duplicate separators and aliases;
        // do not reject the real app's literal mktemp path for that rewrite.
    }else if(![source isEqual:[source stringByStandardizingPath]]||
             (![source hasPrefix:@"/Volumes/"]&&!(preboot&&[source isEqual:@"/System/Installation/Packages/Extras/VirtualIntelMac/Graphics"]))||
             statfs([source fileSystemRepresentation],&media)||!(media.f_flags&MNT_RDONLY))return 77;
    if(!Private(State,YES)||!Private([State stringByAppendingPathComponent:@"Ownership.plist"],NO))return 77;
    NSDictionary *owner=[NSDictionary dictionaryWithContentsOfFile:[State stringByAppendingPathComponent:@"Ownership.plist"]];
    if(preboot&&(![[owner objectForKey:@"Kind"] isEqual:@"VirtualIntelMacLegacySetup"]||
       ![[owner objectForKey:@"Phase"] isEqual:@"native-files-installed"]||
       ![[NSFileManager defaultManager] fileExistsAtPath:InstalledPath(target,@"/Library/Receipts/Essentials.pkg/Contents/Archive.bom")]))
        return Fail("native package extraction and authorized control handoff are required before preboot graphics");
    DASessionRef session=DASessionCreate(NULL);DADiskRef disk=session?DADiskCreateFromBSDName(NULL,session,root.f_mntfromname):NULL;
    NSDictionary *diskInfo=disk?(NSDictionary *)DADiskCopyDescription(disk):nil;
    CFTypeRef value=(CFTypeRef)[diskInfo objectForKey:(NSString *)kDADiskDescriptionVolumeUUIDKey];
    NSString *volume=value&&CFGetTypeID(value)==CFUUIDGetTypeID()?(NSString *)CFUUIDCreateString(NULL,(CFUUIDRef)value):nil;
    BOOL authorized=UUID(volume)&&[volume isEqual:[owner objectForKey:@"VolumeUUID"]]&&
        UUID([owner objectForKey:@"MachineIdentifier"])&&[[owner objectForKey:@"Guest"] isEqual:@"tiger"];
    [diskInfo release];if(disk)CFRelease(disk);if(session)CFRelease(session);
    if(!authorized)return Fail("installed volume does not match its original authorization");
    int lock=open([[State stringByAppendingPathComponent:@"Graphics.lock"] fileSystemRepresentation],O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    if(lock<0||!Private([State stringByAppendingPathComponent:@"Graphics.lock"],NO)||flock(lock,LOCK_EX|LOCK_NB))return Fail("candidate transaction lock is unavailable");
    NSDictionary *manifest=[NSDictionary dictionaryWithContentsOfFile:[source stringByAppendingPathComponent:@"BuildReceipt.plist"]];
    if((staged||preboot)&&[manifest objectForKey:@"DiagnosticOnly"])
        return Fail("a diagnostic engine cannot be installed through automatic setup");
    NSMutableDictionary *files=[NSMutableDictionary dictionary];
    if(![[manifest objectForKey:@"FormatVersion"] isEqual:[NSNumber numberWithInt:1]]||
       ![[manifest objectForKey:@"Architecture"] isEqual:@"i386"]||![[manifest objectForKey:@"MinimumOS"] isEqual:@"10.4"]||
       ![[manifest objectForKey:@"DispatchCount"] isEqual:[NSNumber numberWithInt:759]]||
       ![[manifest objectForKey:@"NativeDispatchCounts"] isEqual:[NSArray arrayWithObjects:
           [NSNumber numberWithInt:686],[NSNumber numberWithInt:759],nil]])return Fail("candidate ABI receipt is not the measured native Tiger target");
    NSArray *names=[NSArray arrayWithObjects:@"GLEngine",@"libVIMPassthroughOpenGL.dylib",@"VMsvga2GA.plugin/Contents/MacOS/VMsvga2GA",@"VMsvga2GA.plugin/Contents/Info.plist",nil];
    NSEnumerator *enumerator=[names objectEnumerator];NSString *name;
    while((name=[enumerator nextObject])){
        NSString *file=SourceFile(source,name,staged);
        NSString *digest=Digest(file);
        if((staged&&!Private(file,NO))||!digest)return Fail("native payload file is unavailable");
        [files setObject:digest forKey:name];
    }
    NSString *bundle=InstalledPath(target,@"/System/Library/Frameworks/OpenGL.framework/Versions/A/Resources/GLEngine.bundle");
    NSString *engine=[bundle stringByAppendingPathComponent:@"GLEngine"],*backend=[bundle stringByAppendingPathComponent:@"libVIMPassthroughOpenGL.dylib"];
    NSString *backups=[[State stringByAppendingPathComponent:@"OriginalGraphics"] stringByAppendingPathComponent:build];
    NSString *backup=[backups stringByAppendingPathComponent:@"GLEngine"];
    NSString *receiptPath=[State stringByAppendingPathComponent:@"Graphics.plist"];
    NSDictionary *prior=[NSDictionary dictionaryWithContentsOfFile:receiptPath];
    NSString *current=Digest(engine),*original=Digest(backup),*native=[manifest objectForKey:@"NativeEngineSHA256"];
    if(![native isKindOfClass:[NSString class]]||[native length]!=64)return Fail("native original digest is unavailable");
    BOOL priorValid=Private(receiptPath,NO)&&[[prior objectForKey:@"VolumeUUID"] isEqual:volume]&&
        [[prior objectForKey:@"MachineIdentifier"] isEqual:[owner objectForKey:@"MachineIdentifier"]];
    if(prior&&!priorValid)return Fail("another transaction owns the graphics recovery state");
    BOOL knownCurrent=priorValid&&([current isEqual:[prior objectForKey:@"EngineSHA256"]]||
        ([[prior objectForKey:@"Phase"] isEqual:@"candidate-prepared"]&&[current isEqual:[prior objectForKey:@"PreviousEngineSHA256"]]));
    NSString *originalSourceBuild=build;unsigned nativeSlots=0;
    if(!current)return Fail("native engine is missing");
    if(!original){
        NSString *nativeSource=engine;
        if(knownCurrent){
            // A security update may leave GLEngine untouched. Reuse ONLY its
            // actual saved original, not the currently installed replacement.
            originalSourceBuild=[prior objectForKey:@"OriginalSourceBuild"];
            if(![originalSourceBuild isKindOfClass:[NSString class]]||![originalSourceBuild length]||
               [originalSourceBuild length]>40||[originalSourceBuild rangeOfCharacterFromSet:
                   [[NSCharacterSet alphanumericCharacterSet] invertedSet]].location!=NSNotFound)return Fail("the previous native recovery identity is unavailable");
            nativeSource=[[[State stringByAppendingPathComponent:@"OriginalGraphics"] stringByAppendingPathComponent:originalSourceBuild] stringByAppendingPathComponent:@"GLEngine"];
            if(!Private(nativeSource,NO)||![Digest(nativeSource) isEqual:[prior objectForKey:@"OriginalSHA256"]])
                return Fail("the previous native recovery engine is unavailable");
        }
        if(preboot){if(![current isEqual:native])return Fail("the restore DVD's native engine changed");nativeSlots=759;}
        else nativeSlots=NativeDispatchSlots(nativeSource);
        if(!nativeSlots)return Fail("the installed native OpenGL interface is unsupported");
        if(!Directory(backups,0700)||!Copy(nativeSource,backup,0600))return Fail("saving the actual original engine");
        original=Digest(backup);
    }else {
        if(!Private(backup,NO)||(![current isEqual:original]&&
           (!knownCurrent||![original isEqual:[prior objectForKey:@"OriginalSHA256"]])))return Fail("an unexpected engine occupies the native path");
        if(preboot){if(![original isEqual:native])return Fail("the restore DVD's native engine changed");nativeSlots=759;}
        else nativeSlots=NativeDispatchSlots(backup);
        if(!nativeSlots)return Fail("the saved native OpenGL interface is unsupported");
        if(knownCurrent&&[[prior objectForKey:@"ProductBuildVersion"] isEqual:build])
            originalSourceBuild=[prior objectForKey:@"OriginalSourceBuild"]?:build;
    }
    NSString *ga=InstalledPath(target,@"/System/Library/Extensions/VMsvga2GA.plugin/Contents");
    BOOL changed=![current isEqual:[files objectForKey:@"GLEngine"]]||
        ![Digest(backend) isEqual:[files objectForKey:@"libVIMPassthroughOpenGL.dylib"]]||
        ![Digest([ga stringByAppendingPathComponent:@"Info.plist"]) isEqual:[files objectForKey:@"VMsvga2GA.plugin/Contents/Info.plist"]]||
        ![Digest([ga stringByAppendingPathComponent:@"MacOS/VMsvga2GA"]) isEqual:[files objectForKey:@"VMsvga2GA.plugin/Contents/MacOS/VMsvga2GA"]];
    NSString *boot=preboot?nil:VIMOpenGLBootSessionUUID(NULL);
    if(staged&&!boot)return Fail("the live native control epoch is unavailable");
    // Publish the recovery receipt BEFORE replacing code. A partially finished
    // operation can resume against that exact original, never back up our GLI.
    NSMutableDictionary *receipt=[NSMutableDictionary dictionaryWithObjectsAndKeys:@"tiger",@"Guest",volume,@"VolumeUUID",
        [NSNumber numberWithInt:1],@"FormatVersion",
        [owner objectForKey:@"MachineIdentifier"],@"MachineIdentifier",build,@"ProductBuildVersion",
        originalSourceBuild,@"OriginalSourceBuild",original,@"OriginalSHA256",[files objectForKey:@"GLEngine"],@"EngineSHA256",
        [NSNumber numberWithUnsignedInt:nativeSlots],@"NativeDispatchCount",
        current,@"PreviousEngineSHA256",
        [files objectForKey:@"libVIMPassthroughOpenGL.dylib"],@"BackendSHA256",@"candidate-prepared",@"Phase",
        [files objectForKey:@"VMsvga2GA.plugin/Contents/MacOS/VMsvga2GA"],@"GASHA256",
        [files objectForKey:@"VMsvga2GA.plugin/Contents/Info.plist"],@"GAInfoSHA256",
        [NSNumber numberWithBool:NO],@"DesktopQualified",nil];
    if(!Write(receipt,receiptPath))return Fail("saving recovery identity");
    NSString *stock=InstalledPath(target,@"/System/Library/Frameworks/OpenGL.framework/Versions/A/Resources/GLEngineStock.bundle");
    if(!Directory(stock,0755)||!Directory(ga,0755)||!Directory([ga stringByAppendingPathComponent:@"MacOS"],0755)||
       !Copy(backup,[stock stringByAppendingPathComponent:@"GLEngineStock"],0755))return Fail("preparing native software recovery and GA paths");
    if(changed&&(!Copy(SourceFile(source,@"VMsvga2GA.plugin/Contents/Info.plist",staged),[ga stringByAppendingPathComponent:@"Info.plist"],0644)||
       !Copy(SourceFile(source,@"VMsvga2GA.plugin/Contents/MacOS/VMsvga2GA",staged),[ga stringByAppendingPathComponent:@"MacOS/VMsvga2GA"],0755)||
       !Copy(SourceFile(source,@"libVIMPassthroughOpenGL.dylib",staged),backend,0755)||
       !Copy(SourceFile(source,@"GLEngine",staged),engine,0755)))return Fail("publishing verified native graphics files");
    [receipt setObject:(staged||preboot)?@"installed":@"candidate-installed" forKey:@"Phase"];
    [receipt setObject:[NSNumber numberWithBool:changed&&!preboot] forKey:@"ChangedThisBoot"];
    [receipt setObject:[NSNumber numberWithBool:preboot] forKey:@"PrebootInstall"];
    if(boot)[receipt setObject:boot forKey:@"InstalledBootSessionUUID"];
    if(!Write(receipt,receiptPath))return Fail("saving installation evidence");
    puts(preboot?"Native Tiger graphics installed locally before first boot. Desktop qualification is pending.":changed?"Native Tiger graphics installed with original recovery copy. Restart and desktop qualification are required.":
        "Native Tiger graphics files verified unchanged. Desktop qualification is pending.");
    [volume release];close(lock);[pool drain];return 0;
}
