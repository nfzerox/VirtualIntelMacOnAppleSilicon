// Prepare a private payload from this guest's loose native images. Never mounts
// or modifies the live system; the installation transaction is a separate step.
#import <Foundation/Foundation.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

static void Check(BOOL condition,NSString *message) {
    if(!condition)@throw [NSException exceptionWithName:@"PreparationRejected" reason:message userInfo:nil];
}
#include "guest-extract.h"
#include "guest-vpa.h"
#include "guest-aperture-policy.h"
#include "guest-signing-policy.h"
static NSString *GuestCodesignAllocator;
static void Directory(NSString *path) {
    NSError *error=nil;Check([NSFileManager.defaultManager createDirectoryAtPath:path withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:&error],error.localizedDescription);
}
static void Run(NSString *path,NSArray<NSString *> *args,NSString *capture) {
    NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:path];task.arguments=args;
    if([path isEqual:@"/usr/bin/codesign"]&&GuestCodesignAllocator) {
        NSMutableDictionary *environment=[NSProcessInfo.processInfo.environment mutableCopy];
        environment[@"CODESIGN_ALLOCATE"]=GuestCodesignAllocator;task.environment=environment;
    }
    NSFileHandle *file=nil;
    if(capture) {
        Check(![NSFileManager.defaultManager fileExistsAtPath:capture],@"Capture file already exists");
        Check([NSFileManager.defaultManager createFileAtPath:capture contents:NSData.data attributes:@{NSFilePosixPermissions:@0600}],@"Cannot create command log");
        file=[NSFileHandle fileHandleForWritingAtPath:capture];Check(file!=nil,@"Cannot open command log");task.standardOutput=file;
    } else task.standardOutput=NSFileHandle.fileHandleWithStandardError;
    task.standardError=NSFileHandle.fileHandleWithStandardError;
    NSError *error=nil;Check([task launchAndReturnError:&error],error.localizedDescription);
    // Preparation speed depends on emulation and disk load, not correctness.
    [task waitUntilExit];[file closeFile];
    Check(task.terminationReason==NSTaskTerminationReasonExit&&task.terminationStatus==0,
          [NSString stringWithFormat:@"%@ rejected preparation (status %d)",path.lastPathComponent,task.terminationStatus]);
}
int main(int argc,const char **argv) {@autoreleasepool {
    if(argc!=4){fprintf(stderr,"usage: guest-prepare GUEST_ROOT PAYLOAD_DIRECTORY TOOLS_DIRECTORY | --extract ARCHIVE NEW_DIRECTORY | --vpa INPUT NEW_OUTPUT | --aperture-policy INPUT NEW_OUTPUT\n");return 64;}
    @try {
        if(!strcmp(argv[1],"--aperture-policy")) {
            Check(![NSFileManager.defaultManager fileExistsAtPath:@(argv[3])],@"Aperture policy output already exists");
            PrepareAperturePolicy(@(argv[2]),@(argv[3]));return 0;
        }
        if(!strcmp(argv[1],"--vpa")) {
            Check(![NSFileManager.defaultManager fileExistsAtPath:@(argv[3])],@"VPA output already exists");
            PrepareVPA(@(argv[2]),@(argv[3]));return 0;
        }
        if(!strcmp(argv[1],"--extract")) {
            NSDictionary *receipt=ExtractPayload(@(argv[2]),@(argv[3]));
            NSData *json=[NSJSONSerialization dataWithJSONObject:receipt options:NSJSONWritingPrettyPrinted error:NULL];
            fwrite(json.bytes,1,json.length,stdout);fputc('\n',stdout);return 0;
        }
        NSFileManager *fm=NSFileManager.defaultManager;
        NSString *guest=[@(argv[1]) stringByResolvingSymlinksInPath],*payload=[@(argv[2]) stringByResolvingSymlinksInPath],*tools=[@(argv[3]) stringByResolvingSymlinksInPath];
        Check(guest.isAbsolutePath&&payload.isAbsolutePath&&tools.isAbsolutePath,@"Paths must be absolute");
        Check(![payload isEqual:@"/"]&&![payload isEqual:guest],@"Payload must be a separate staging directory");
        Check(![guest hasPrefix:[payload stringByAppendingString:@"/"]],@"Payload cannot contain the guest root");
        if(![guest isEqual:@"/"])Check(![payload hasPrefix:[guest stringByAppendingString:@"/"]],@"Payload overlaps source guest");
        for(NSString *system in @[@"/System",@"/Library",@"/usr",@"/bin",@"/sbin"])
            Check(![payload isEqual:system]&&![payload hasPrefix:[system stringByAppendingString:@"/"]],@"Payload cannot be a system directory");
        BOOL directory=NO;Check([fm fileExistsAtPath:payload isDirectory:&directory]&&directory,@"Missing payload directory");
        // Invalidate any previous success receipt before touching staged files.
        // A failed retry must never leave an apparently prepared payload.
        NSString *receiptPath=[payload stringByAppendingPathComponent:@".VirtualIntelMacPreparation.plist"];
        if([fm fileExistsAtPath:receiptPath]) {
            NSError *receiptError=nil;
            Check([fm moveItemAtPath:receiptPath toPath:[payload.stringByDeletingLastPathComponent stringByAppendingPathComponent:[@"PreviousPreparation-" stringByAppendingString:NSUUID.UUID.UUIDString]] error:&receiptError],receiptError.localizedDescription);
        }
        NSString *(^source)(NSString *)=^NSString *(NSString *relative){return [guest stringByAppendingPathComponent:relative];};
        NSString *(^destination)(NSString *)=^NSString *(NSString *relative){
            NSString *path=[payload stringByAppendingPathComponent:relative];
            Check([path.stringByResolvingSymlinksInPath hasPrefix:[payload stringByAppendingString:@"/"]],@"Staged path escapes payload");return path;
        };
        NSDictionary *version=[NSDictionary dictionaryWithContentsOfFile:source(@"System/Library/CoreServices/SystemVersion.plist")];
        NSString *release=version[@"ProductVersion"],*build=version[@"ProductBuildVersion"];
        Check([release isKindOfClass:NSString.class]&&[build isKindOfClass:NSString.class]&&build.length,@"Missing guest version");
        NSArray *parts=[release componentsSeparatedByString:@"."];
        for(NSString *part in parts)Check(part.length&&[part rangeOfCharacterFromSet:NSCharacterSet.decimalDigitCharacterSet.invertedSet].location==NSNotFound,@"Invalid guest version");
        BOOL bigsur=parts.count>=2&&[parts[0] isEqual:@"11"];
        Check(bigsur||(parts.count>=2&&[parts[0] isEqual:@"10"]&&([parts[1] isEqual:@"14"]||[parts[1] isEqual:@"15"])),@"This preparation tool supports Mojave, Catalina and Big Sur guests only");
        BOOL mojave=!bigsur&&[parts[1] isEqual:@"14"];
        NSString *patch=[tools stringByAppendingPathComponent:@"guest-image-patch"],*macho=[tools stringByAppendingPathComponent:@"guest-macho"];
        Check([fm isExecutableFileAtPath:patch]&&[fm isExecutableFileAtPath:macho],@"Bundled patch tools are unavailable");
        GuestCodesignAllocator=[tools stringByAppendingPathComponent:@"codesign_allocate"];
#ifndef VIM_GUEST_PREPARE_TESTING
        Check([fm isExecutableFileAtPath:GuestCodesignAllocator],@"The bundled guest code-signature allocator is unavailable");
#endif
        NSString *work=[[payload stringByDeletingLastPathComponent] stringByAppendingPathComponent:[@"Prepare-" stringByAppendingString:NSUUID.UUID.UUIDString]];
        Check(![fm fileExistsAtPath:work],@"Work directory collision");Directory(work);
        NSString *signer=@"/usr/bin/codesign";
#ifdef VIM_GUEST_PREPARE_TESTING
        // Compiled only into the fixture harness, never the shipped executable.
        Check(![guest isEqual:@"/"],@"Fixture build cannot prepare a live guest");
        const char *mock=getenv("VIM_TEST_SIGNER");if(mock)signer=@(mock);
#endif
        // Exact entitlements of Apple's Mojave/Catalina CVMServer. The existing
        // payload may already have lost them, so preservation alone is not a
        // repair. Never put these on the app or on unrelated donor binaries.
        NSString *cvmsEntitlements=[work stringByAppendingPathComponent:@"CVMServer.entitlements.plist"];
        NSDictionary *cvmsPolicy=@{@"com.apple.rootless.storage.cvms":@YES,
            @"com.apple.security.cs.allow-jit":@YES,@"jit-codesigning":@YES};
        Check([cvmsPolicy writeToFile:cvmsEntitlements atomically:YES],@"Cannot prepare CPU compiler signing policy");
        NSMutableDictionary *signingPolicies=[NSMutableDictionary dictionary];
        void (^sign)(NSString *,BOOL)=^(NSString *path,BOOL bundle) {
            NSMutableArray *args=[NSMutableArray arrayWithArray:@[@"--force",@"--sign",@"-",@"--preserve-metadata=entitlements"]];
            if(bundle)[args addObject:@"--deep"];
            if([path hasSuffix:@"/OpenGL.framework/Versions/A/Libraries/CVMServer"])
                [args addObjectsFromArray:@[@"--entitlements",cvmsEntitlements]];
            else if(signingPolicies[path])[args addObjectsFromArray:@[@"--entitlements",signingPolicies[path]]];
            [args addObject:path];Run(signer,args,nil);
            Run(signer,bundle?@[@"--verify",@"--deep",@"--strict",path]:@[@"--verify",@"--strict",path],nil);
        };
        void (^patchFile)(NSString *,NSString *,NSString *)=^(NSString *kind,NSString *input,NSString *relative) {
            NSString *target=destination(relative),*candidate=[work stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
            RememberEntitlements(input,target,signingPolicies,work);
            Directory(target.stringByDeletingLastPathComponent);Run(patch,@[kind,input,candidate],nil);
            if([@[@"mojave-coredisplay",@"mojave-skylight"] containsObject:kind]) {
                NSString *compact=[work stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
                Run(macho,@[@"compact-linkedit",candidate,compact],nil);candidate=compact;
            }
            Check(rename(candidate.fileSystemRepresentation,target.fileSystemRepresentation)==0,@"Cannot publish patched staging file");
        };
        void (^copyDirectory)(NSString *,NSString *)=^(NSString *input,NSString *relative) {
            NSString *target=destination(relative);NSError *error=nil;
            RememberEntitlements(input,target,signingPolicies,work);
            if([fm fileExistsAtPath:target])Check([fm moveItemAtPath:target toPath:[work stringByAppendingPathComponent:NSUUID.UUID.UUIDString] error:&error],error.localizedDescription);
            Directory(target);Run(@"/bin/cp",@[bigsur?@"-RX":@"-R",[input stringByAppendingPathComponent:@"."],[target stringByAppendingString:@"/"]],nil);
        };
        if(bigsur) {
            // Both native plugins are loose files in this guest. Keep all
            // system frameworks, compiler code and the sealed root untouched.
            for(NSString *name in @[@"AppleParavirtGPUMetal",@"AppleMetalOpenGLRenderer"]) {
                BOOL metal=[name isEqual:@"AppleParavirtGPUMetal"];
                NSString *bundle=[name stringByAppendingString:@".bundle"];
                NSString *relative=[@"Library/GPUBundles" stringByAppendingPathComponent:bundle];
                NSString *native=[@"System/Library/Extensions" stringByAppendingPathComponent:bundle];
                NSString *helper=metal?@"PVGBigSurMetalCompat.dylib":@"PVGOpenGLMetal.dylib";
                NSString *helperInput=destination([@"GuestHelpers" stringByAppendingPathComponent:helper]);
                Check([fm fileExistsAtPath:helperInput],[@"Missing bundled Big Sur adapter: " stringByAppendingString:helper]);
                Check([fm fileExistsAtPath:source([native stringByAppendingPathComponent:[@"Contents/MacOS" stringByAppendingPathComponent:name]])],@"The guest's native GPU plugin is missing");
                copyDirectory(source(native),relative);
                NSString *binary=[relative stringByAppendingPathComponent:[@"Contents/MacOS" stringByAppendingPathComponent:name]];
                NSString *candidate=[work stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
                Run(macho,@[metal?@"prepare-bigsur-metal":@"prepare-bigsur-gl",source([native stringByAppendingPathComponent:[@"Contents/MacOS" stringByAppendingPathComponent:name]]),candidate],nil);
                Check(rename(candidate.fileSystemRepresentation,destination(binary).fileSystemRepresentation)==0,@"Cannot publish the prepared native plugin");
                NSString *helperOutput=destination([relative stringByAppendingPathComponent:[@"Contents/MacOS" stringByAppendingPathComponent:helper]]);
                NSError *copyError=nil;Check([fm copyItemAtPath:helperInput toPath:helperOutput error:&copyError],copyError.localizedDescription);
                sign(helperOutput,NO);sign(destination(relative),NO);
                Run(signer,@[@"--verify",@"--deep",@"--strict",destination(relative)],nil);
            }
        } else {
        NSString *core=@"System/Library/Frameworks/CoreDisplay.framework/Versions/A/CoreDisplay";
        NSString *compiler=@"System/Library/PrivateFrameworks/MTLCompiler.framework/Versions/3902/MTLCompiler";
        NSString *outputCompiler=[compiler stringByReplacingOccurrencesOfString:@"/3902/" withString:@"/3903/"];
        NSString *service=@"System/Library/Frameworks/Metal.framework/Versions/A/XPCServices/MTLCompilerService.xpc";
        NSString *serviceBinary=[service stringByAppendingPathComponent:@"Contents/MacOS/MTLCompilerService"];
        for(NSString *relative in @[core,compiler,serviceBinary])Check([fm fileExistsAtPath:source(relative)],[@"Missing native guest file: " stringByAppendingString:relative]);
        patchFile(mojave?@"mojave-coredisplay":@"coredisplay",source(core),core);
        if(mojave) {
            for(NSString *app in @[@"Aperture",@"iPhoto"]) {
                NSString *policy=[NSString stringWithFormat:@"Library/Preferences/com.apple.%@.plist",app];
                Directory(destination(policy).stringByDeletingLastPathComponent);
                PrepareAperturePolicy(source(policy),destination(policy));
            }
            NSString *vpa=@"System/Library/PrivateFrameworks/AppleVPA.framework/Versions/A/Resources/Info.plist";
            Directory(destination(vpa).stringByDeletingLastPathComponent);
            PrepareVPA(source(vpa),destination(vpa));
            NSString *sky=@"System/Library/PrivateFrameworks/SkyLight.framework/Versions/A/SkyLight";
            Check([fm fileExistsAtPath:destination(@"usr/lib/PVGGLIOKit.dylib")],@"Missing Mojave Surface2 compatibility helper");
            patchFile(@"mojave-skylight",source(sky),sky);sign(destination(sky),NO);
            NSString *io=@"System/Library/PrivateFrameworks/IOAccelerator.framework/Versions/A/IOAccelerator";
            NSString *candidate=[work stringByAppendingPathComponent:@"IOAccelerator"];
            RememberEntitlements(destination(io),destination(io),signingPolicies,work);
            Run(macho,@[@"replace-x86_64",source(io),destination(io),candidate],nil);
            Check(rename(candidate.fileSystemRepresentation,destination(io).fileSystemRepresentation)==0,@"Cannot publish staged IOAccelerator");sign(destination(io),NO);
        }
        NSString *compilerInput=source(compiler),*serviceInput=source(serviceBinary);
        if(mojave) {
            compilerInput=[work stringByAppendingPathComponent:@"compiler"];serviceInput=[work stringByAppendingPathComponent:@"service"];
            Run(macho,@[@"thin",source(compiler),compilerInput],nil);Run(macho,@[@"thin",source(serviceBinary),serviceInput],nil);
        }
        copyDirectory(source(compiler.stringByDeletingLastPathComponent),outputCompiler.stringByDeletingLastPathComponent);
        patchFile(@"compiler",compilerInput,outputCompiler);
        copyDirectory(source(service),service);patchFile(@"service",serviceInput,serviceBinary);
        sign(destination(core),NO);sign(destination(outputCompiler),NO);sign(destination(service),YES);
        if(mojave) {
            NSString *gl=@"System/Library/Frameworks/OpenGL.framework",*list=[work stringByAppendingPathComponent:@"gl-sign-list"];
            NSString *framework=destination(gl);
            for(NSString *relative in [fm enumeratorAtPath:framework]) {
                NSString *file=[framework stringByAppendingPathComponent:relative];
                BOOL directory=NO;
                if([fm fileExistsAtPath:file isDirectory:&directory]&&!directory)
                    RememberEntitlements(file,file,signingPolicies,work);
            }
            Run(macho,@[@"stage-opengl",source(gl),destination(gl),source(@"System/Library/Frameworks/PVGGL32.dylib"),source(@"Library/PVG/OpenGL32.dylib")],list);
            NSString *lines=[NSString stringWithContentsOfFile:list encoding:NSUTF8StringEncoding error:NULL];Check(lines!=nil,@"Missing GL staging results");
            for(NSString *path in [lines componentsSeparatedByString:@"\n"])if(path.length) {
                Check([path.stringByResolvingSymlinksInPath hasPrefix:[destination(gl) stringByAppendingString:@"/"]],@"Invalid GL signing target");sign(path,NO);
            }
            sign(destination(gl),NO);
        }
        }
        NSDictionary *currentVersion=[NSDictionary dictionaryWithContentsOfFile:source(@"System/Library/CoreServices/SystemVersion.plist")];
        Check([currentVersion isEqual:version],@"Guest version changed during preparation; retry after the update completes");
        NSDictionary *receipt=@{@"FormatVersion":@1,@"Guest":bigsur?@"bigsur":(mojave?@"mojave":@"catalina"),@"ProductVersion":release,@"ProductBuildVersion":build,@"Prepared":@YES,@"Installed":@NO,@"WorkDirectory":work,@"PayloadDirectory":payload,@"PreparedAt":NSDate.date};
        NSError *receiptError=nil;
        NSData *plist=[NSPropertyListSerialization dataWithPropertyList:receipt format:NSPropertyListBinaryFormat_v1_0 options:0 error:&receiptError];
        Check(plist&&[plist writeToFile:receiptPath options:NSDataWritingAtomic error:&receiptError],receiptError.localizedDescription?:@"Cannot record completed preparation");
        int receiptFD=open(receiptPath.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW);
        Check(receiptFD>=0,@"Cannot open preparation receipt");
        int flushed=fsync(receiptFD);close(receiptFD);Check(flushed==0,@"Cannot flush preparation receipt");
        // Keep the transport receipt JSON-compatible; the durable plist keeps
        // the native date type for the installer's audit trail.
        NSMutableDictionary *transport=[receipt mutableCopy];transport[@"PreparedAt"]=@([receipt[@"PreparedAt"] timeIntervalSince1970]);
        NSData *json=[NSJSONSerialization dataWithJSONObject:transport options:NSJSONWritingPrettyPrinted error:NULL];
        fwrite(json.bytes,1,json.length,stdout);fputc('\n',stdout);return 0;
    } @catch(NSException *exception){fprintf(stderr,"%s\n",exception.reason.UTF8String);return 65;}
}}
