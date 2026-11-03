// Finite native10.4 offline-publication fixture, never a boot/OS-install claim.
// Only an explicitly named fresh scratch HFS mount can be populated. The real
// running system's original engine, version and package receipt are read only.
#import <Foundation/Foundation.h>
#import <DiskArbitration/DiskArbitration.h>
#import <CommonCrypto/CommonDigest.h>
#import "../tools/opengl-guest-machine.h"
#include <sys/mount.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#define STATE "/Library/Application Support/Virtual Intel Mac/State/LegacySetup"
static NSString *SHA(NSString *path){
    int fd=open([path fileSystemRepresentation],O_RDONLY|O_NOFOLLOW);if(fd<0)return nil;
    struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<1||st.st_size>8*1024*1024){close(fd);return nil;}
    CC_SHA256_CTX context;CC_SHA256_Init(&context);unsigned char buffer[65536],hash[CC_SHA256_DIGEST_LENGTH];ssize_t n;
    do{n=read(fd,buffer,sizeof(buffer));if(n>0)CC_SHA256_Update(&context,buffer,(CC_LONG)n);}while(n>0||(n<0&&errno==EINTR));
    close(fd);if(n<0)return nil;CC_SHA256_Final(hash,&context);
    NSMutableString *text=[NSMutableString string];for(unsigned i=0;i<sizeof(hash);i++)[text appendFormat:@"%02x",hash[i]];return text;
}
static BOOL Write(NSDictionary *value,NSString *path,mode_t mode){
    return [value writeToFile:path atomically:YES]&&!chown([path fileSystemRepresentation],0,0)&&!chmod([path fileSystemRepresentation],mode);
}
static BOOL Make(NSString *path,mode_t mode){
    struct stat st;if(!lstat([path fileSystemRepresentation],&st))return S_ISDIR(st.st_mode)&&st.st_uid==0&&!(st.st_mode&0002);
    if(errno!=ENOENT||[path isEqual:@"/"]||!Make([path stringByDeletingLastPathComponent],0755))return NO;
    return !mkdir([path fileSystemRepresentation],mode)&&!chown([path fileSystemRepresentation],0,0);
}
int main(int argc,char **argv){
    if(argc!=4)return 64;NSAutoreleasePool *pool=[NSAutoreleasePool new];
    BOOL prepare=!strcmp(argv[1],"--prepare"),verify=!strcmp(argv[1],"--verify");
    NSString *source=[NSString stringWithUTF8String:argv[2]],*target=[NSString stringWithUTF8String:argv[3]];
    struct statfs root,media;struct stat st;
    if((!prepare&&!verify)||geteuid()!=0||!VIMOpenGLGuestMachine()||
       ![target isEqual:@"/Volumes/VIM Tiger Graphics Fixture53"]||lstat([target fileSystemRepresentation],&st)||!S_ISDIR(st.st_mode)||
       statfs([target fileSystemRepresentation],&root)||strcmp(root.f_mntonname,[target fileSystemRepresentation])||
       strcmp(root.f_fstypename,"hfs")||(root.f_flags&MNT_RDONLY)||
       statfs([source fileSystemRepresentation],&media)||!(media.f_flags&MNT_RDONLY))return 77;
    DASessionRef session=DASessionCreate(NULL);DADiskRef disk=session?DADiskCreateFromBSDName(NULL,session,root.f_mntfromname):NULL;
    NSDictionary *info=disk?(NSDictionary *)DADiskCopyDescription(disk):nil;
    CFTypeRef native=(CFTypeRef)[info objectForKey:(NSString *)kDADiskDescriptionVolumeUUIDKey];
    NSString *uuid=native&&CFGetTypeID(native)==CFUUIDGetTypeID()?(NSString *)CFUUIDCreateString(NULL,(CFUUIDRef)native):nil;
    [info release];if(disk)CFRelease(disk);if(session)CFRelease(session);if(!uuid){fputs("No native DA volume UUID\n",stderr);return 1;}
    NSDictionary *record=[NSDictionary dictionaryWithContentsOfFile:[source stringByAppendingPathComponent:@"BuildReceipt.plist"]];
    NSString *state=[target stringByAppendingString:@STATE],*marker=[target stringByAppendingPathComponent:@".VIMTigerGraphicsFixture.plist"];
    NSString *bundle=@"/System/Library/Frameworks/OpenGL.framework/Versions/A/Resources/GLEngine.bundle";
    NSString *engine=[bundle stringByAppendingPathComponent:@"GLEngine"],*backend=[bundle stringByAppendingPathComponent:@"libVIMPassthroughOpenGL.dylib"];
    NSFileManager *fm=[NSFileManager defaultManager];
    if(prepare){
        if([fm fileExistsAtPath:marker]||[fm fileExistsAtPath:[target stringByAppendingPathComponent:@"System"]]||
           [fm fileExistsAtPath:[target stringByAppendingPathComponent:@"Library"]])return 77;
        NSString *original=@STATE "/OriginalGraphics/8R4088/GLEngine";
        NSDictionary *owner=[NSDictionary dictionaryWithContentsOfFile:@STATE "/Ownership.plist"];
        NSString *identifier=[owner objectForKey:@"MachineIdentifier"];
        CFUUIDRef machine=identifier?CFUUIDCreateFromString(NULL,(CFStringRef)identifier):NULL;
        if(!machine||![SHA(original) isEqual:[record objectForKey:@"NativeEngineSHA256"]]){fputs("Original engine or original machine authorization mismatch\n",stderr);return 1;}
        CFRelease(machine);
        NSString *version=[target stringByAppendingPathComponent:@"System/Library/CoreServices"];
        NSString *receipt=[target stringByAppendingPathComponent:@"Library/Receipts/Essentials.pkg/Contents"];
        if(!Make([target stringByAppendingString:bundle],0755)||!Make(version,0755)||!Make(receipt,0755)||!Make(state,0700))return 1;
        if(![fm copyPath:original toPath:[target stringByAppendingString:engine] handler:nil]||
           ![fm copyPath:@"/System/Library/CoreServices/SystemVersion.plist" toPath:[version stringByAppendingPathComponent:@"SystemVersion.plist"] handler:nil]||
           ![fm copyPath:@"/Library/Receipts/Essentials.pkg/Contents/Archive.bom" toPath:[receipt stringByAppendingPathComponent:@"Archive.bom"] handler:nil])return 1;
        NSDictionary *authorization=[NSDictionary dictionaryWithObjectsAndKeys:@"VirtualIntelMacLegacySetup",@"Kind",@"tiger",@"Guest",
            identifier,@"MachineIdentifier",uuid,@"VolumeUUID",@"native-files-installed",@"Phase",nil];
        NSDictionary *baseline=[NSDictionary dictionaryWithObjectsAndKeys:uuid,@"VolumeUUID",SHA(engine),@"LiveEngineSHA256",SHA(backend),@"LiveBackendSHA256",nil];
        if(!Write(authorization,[state stringByAppendingPathComponent:@"Ownership.plist"],0600)||!Write(baseline,marker,0600))return 1;
        puts("Prepared an isolated native HFS fixture from the real original engine/version/package receipt.");
    }else{
        NSDictionary *baseline=[NSDictionary dictionaryWithContentsOfFile:marker];
        NSDictionary *installed=[NSDictionary dictionaryWithContentsOfFile:[state stringByAppendingPathComponent:@"Graphics.plist"]];
        NSDictionary *files=[record objectForKey:@"Files"];
        if(![[baseline objectForKey:@"VolumeUUID"] isEqual:uuid]||![[installed objectForKey:@"VolumeUUID"] isEqual:uuid]||
           ![[installed objectForKey:@"Phase"] isEqual:@"installed"]||![[installed objectForKey:@"PrebootInstall"] boolValue]||
           [[installed objectForKey:@"ChangedThisBoot"] boolValue]||[[installed objectForKey:@"DesktopQualified"] boolValue]||
           ![SHA(engine) isEqual:[baseline objectForKey:@"LiveEngineSHA256"]]||![SHA(backend) isEqual:[baseline objectForKey:@"LiveBackendSHA256"]]||
           ![SHA([state stringByAppendingPathComponent:@"OriginalGraphics/8R4088/GLEngine"]) isEqual:[record objectForKey:@"NativeEngineSHA256"]])return 1;
        NSDictionary *destinations=[NSDictionary dictionaryWithObjectsAndKeys:engine,@"GLEngine",backend,@"libVIMPassthroughOpenGL.dylib",
            @"/System/Library/Extensions/VMsvga2GA.plugin/Contents/MacOS/VMsvga2GA",@"VMsvga2GA.plugin/Contents/MacOS/VMsvga2GA",
            @"/System/Library/Extensions/VMsvga2GA.plugin/Contents/Info.plist",@"VMsvga2GA.plugin/Contents/Info.plist",nil];
        NSEnumerator *keys=[destinations keyEnumerator];NSString *key;
        while((key=[keys nextObject]))if(![SHA([target stringByAppendingString:[destinations objectForKey:key]]) isEqual:[files objectForKey:key]])return 1;
        puts("PASS preboot native graphics/GA bytes, original recovery, volume binding, no false desktop/restart claim, and unchanged live system graphics.");
    }
    [uuid release];[pool drain];return 0;
}
