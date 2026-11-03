#import <Cocoa/Cocoa.h>
#import "VIMMachine.h"
#import "VIMLibraryLocation.h"
#import "VIMInstallerAssembly.h"
#import "VIMAutomaticSetup.h"
#include <unistd.h>
#include <errno.h>

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        NSArray<NSString *> *args = NSProcessInfo.processInfo.arguments;
        if(args.count==3&&[args[1] isEqual:@"--identify-installer"]) {
            NSError *error=nil;
            NSString *guest=[VIMInstallerAssembly guestForInstallerApplication:[NSURL fileURLWithPath:args[2]] error:&error];
            if(!guest){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
            puts(guest.UTF8String);return 0;
        }
        if(args.count==3&&[args[1] isEqual:@"--prepare-setup-media"]) {
            NSError *error=nil;VIMMachine *machine=[VIMMachine loadURL:[NSURL fileURLWithPath:args[2]] error:&error];
            NSURL *media=machine?[VIMAutomaticSetup configurationMediaForMachine:machine error:&error]:nil;
            if(!media){fprintf(stderr,"%s\n",(error.localizedDescription?:@"This Mac has no pending classic installation target.").UTF8String);return 1;}
            puts(media.path.UTF8String);return 0;
        }
        if([args containsObject:@"--configure-automatic-setup"]) {
            NSMutableDictionary *options=[NSMutableDictionary new];
            NSSet *flags=[NSSet setWithArray:@[@"--password-stdin",@"--no-file-sharing",@"--no-remote-login",@"--no-auto-login"]];
            NSSet *values=[NSSet setWithArray:@[@"--configure-automatic-setup",@"--username"]];
            for(NSUInteger i=1;i<args.count;i++) {
                NSString *key=args[i];
                if(options[key]){fprintf(stderr,"Duplicate setup option.\n");return 64;}
                if([flags containsObject:key])options[key]=@YES;
                else if([values containsObject:key]&&i+1<args.count)options[key]=args[++i];
                else {fprintf(stderr,"Use --configure-automatic-setup machine.vintelmac [--username name] [--password-stdin] [--no-file-sharing] [--no-remote-login] [--no-auto-login].\n");return 64;}
            }
            NSError *error=nil;VIMMachine *machine=[VIMMachine loadURL:[NSURL fileURLWithPath:options[@"--configure-automatic-setup"]] error:&error];
            if(!machine||![VIMAutomaticSetup supportsGuest:machine.guest]){fprintf(stderr,"%s\n",(error.localizedDescription?:@"Choose a new supported virtual Mac.").UTF8String);return 1;}
            if([NSFileManager.defaultManager fileExistsAtPath:[machine.URL URLByAppendingPathComponent:@"State/AutomaticSetup.plist"].path]){
                fprintf(stderr,"This Mac already has a saved setup policy. Resume it instead of replacing its account transaction.\n");return 1;
            }
            NSString *username=options[@"--username"]?:VIMAutomaticSetup.defaultUsername,*password=@"123456";
            if(options[@"--password-stdin"]) {
                NSMutableData *input=[NSMutableData new];unsigned char bytes[256];ssize_t n;
                while((n=read(STDIN_FILENO,bytes,sizeof(bytes)))!=0){
                    if(n<0){if(errno==EINTR)continue;return 74;}
                    if(input.length+(NSUInteger)n>513){fprintf(stderr,"Setup password exceeds 511 UTF-8 bytes.\n");return 64;}
                    [input appendBytes:bytes length:(NSUInteger)n];
                }
                // Accept a raw password or one terminal/pipe line. Do not
                // save the Enter key as part of a password that users need
                // to type into a native SSH or Finder prompt later.
                const unsigned char *data=input.bytes;NSUInteger length=input.length;
                if(length&&data[length-1]=='\n'){
                    --length;if(length&&data[length-1]=='\r')--length;
                    input.length=length;
                }
                password=[[NSString alloc] initWithData:input encoding:NSUTF8StringEncoding];
                if(!password||[password rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"\r\n"]].location!=NSNotFound){
                    fprintf(stderr,"Supply exactly one password line on stdin.\n");return 64;
                }
                if(!password.length)password=@"123456";
            }
            BOOL sharing=!options[@"--no-file-sharing"],remote=!options[@"--no-remote-login"];
            if(![VIMAutomaticSetup validateUsername:username error:&error]||![VIMAutomaticSetup validatePassword:password error:&error]){
                fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 64;
            }
            NSString *credential=NSUUID.UUID.UUIDString;
            if(![VIMAutomaticSetup savePassword:password identifier:credential error:&error]){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
            NSDictionary *policy=@{@"FormatVersion":@1,@"Enabled":@YES,@"Username":username,@"FileSharing":@(sharing),@"RemoteLogin":@(remote),
                @"AutomaticLogin":@(!options[@"--no-auto-login"]),@"CredentialIdentifier":credential};
            if(![machine saveAutomaticSetup:policy error:&error]){
                [VIMAutomaticSetup removePasswordForIdentifier:credential error:NULL];fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;
            }
            puts("Automatic setup saved; its temporary password is private to your host account, not the VM package.");return 0;
        }
        if([args containsObject:@"--create-vm"]) {
            NSMutableDictionary *options=[NSMutableDictionary dictionary];
            NSSet *required=[NSSet setWithArray:@[@"--create-vm",@"--name",@"--firmware",@"--nvram",@"--boot-media",@"--installer",@"--qemu-img"]];
            NSMutableSet *allowed=[required mutableCopy];[allowed addObjectsFromArray:@[@"--library",@"--disk-gib",@"--opengl-guest-research"]];
            for(NSUInteger i=1;i<args.count;i+=2) {
                if(i+1>=args.count||![allowed containsObject:args[i]]||options[args[i]]) {
                    fprintf(stderr,"Invalid or duplicate creation argument: %s\n",args[i].UTF8String);return 64;
                }
                options[args[i]]=args[i+1];
            }
            for(NSString *key in required)if(!options[key]){fprintf(stderr,"Missing %s\n",key.UTF8String);return 64;}
            NSString *size=options[@"--disk-gib"]?:@"64";
            if(!size.length||[size rangeOfCharacterFromSet:NSCharacterSet.decimalDigitCharacterSet.invertedSet].location!=NSNotFound) {
                fprintf(stderr,"Disk size must be an integer in GB.\n");return 64;
            }
            NSError *error=nil;
            VIMMachine *machine=[VIMMachine createGuest:options[@"--create-vm"] name:options[@"--name"] diskGiB:size.integerValue
                library:options[@"--library"]?[NSURL fileURLWithPath:options[@"--library"]]:VIMLibraryLocation.currentURL
                firmware:[NSURL fileURLWithPath:options[@"--firmware"]] nvram:[NSURL fileURLWithPath:options[@"--nvram"]]
                bootMedia:[NSURL fileURLWithPath:options[@"--boot-media"]] installer:[NSURL fileURLWithPath:options[@"--installer"]]
                qemuImage:[NSURL fileURLWithPath:options[@"--qemu-img"]] error:&error];
            if(!machine){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
            puts(machine.URL.path.UTF8String);return 0;
        }
        if ([args containsObject:@"--create-bigsur-media"]||[args containsObject:@"--create-recovery-media"]) {
            NSMutableDictionary *options=[NSMutableDictionary dictionary];
            NSSet *allowed=[NSSet setWithArray:@[@"--create-bigsur-media",@"--create-recovery-media",@"--guest",@"--job",@"--opengl-support"]];
            for(NSUInteger i=1;i<args.count;i+=2) {
                if(i+1>=args.count||![allowed containsObject:args[i]]||options[args[i]]) {
                    fprintf(stderr,"Invalid or duplicate media argument: %s\n",args[i].UTF8String);return 64;
                }
                options[args[i]]=args[i+1];
            }
            BOOL legacy=options[@"--create-recovery-media"]!=nil;
            if(!options[@"--job"]||(legacy&&(![@[@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",@"mavericks",@"yosemite",@"elcapitan",@"sierra",@"highsierra",@"mojave",@"catalina"] containsObject:options[@"--guest"]]||options[@"--create-bigsur-media"]))) {
                fprintf(stderr,"Required: --create-bigsur-media installer.app --job new-directory OR --create-recovery-media installer --guest tiger|leopard|snowleopard|lion|mountainlion|mavericks|yosemite|elcapitan|sierra|highsierra|mojave|catalina --job new-directory\n");return 64;
            }
            __block NSURL *result=nil;__block NSError *error=nil;
            dispatch_semaphore_t done=dispatch_semaphore_create(0);
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0), ^{
                VIMInstallerAssembly *worker=[VIMInstallerAssembly new];
                if(options[@"--opengl-support"])worker.openGLSupportDirectory=[NSURL fileURLWithPath:options[@"--opengl-support"]];
                if(legacy)result=[worker createRecoveryMediaFromInstaller:[NSURL fileURLWithPath:options[@"--create-recovery-media"]] guest:options[@"--guest"] jobDirectory:[NSURL fileURLWithPath:options[@"--job"]] error:&error];
                else result=[worker createBigSurMediaFromInstaller:[NSURL fileURLWithPath:options[@"--create-bigsur-media"]] jobDirectory:[NSURL fileURLWithPath:options[@"--job"]] error:&error];
                dispatch_semaphore_signal(done);
            });
            dispatch_semaphore_wait(done,DISPATCH_TIME_FOREVER);
            if(!result){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
            puts(result.path.UTF8String);return 0;
        }
        if ([args containsObject:@"--assemble-installer"]) {
            NSMutableDictionary *options=[NSMutableDictionary dictionary];
            NSSet *allowed=[NSSet setWithArray:@[@"--assemble-installer",@"--assistant",@"--recovery",@"--esd",@"--diskimage",@"--job"]];
            for(NSUInteger i=1;i<args.count;i+=2) {
                if(i+1>=args.count||![allowed containsObject:args[i]]||options[args[i]]) {
                    fprintf(stderr,"Invalid or duplicate assembly argument: %s\n",args[i].UTF8String);return 64;
                }
                options[args[i]]=args[i+1];
            }
            BOOL diskImage=[@[@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",@"yosemite",@"elcapitan",@"sierra"] containsObject:options[@"--assemble-installer"]];
            if((!diskImage&&![options[@"--assemble-installer"] isEqual:@"mavericks"]&&!options[@"--assistant"])||
               (diskImage&&!options[@"--diskimage"])||
               ([options[@"--assemble-installer"] isEqual:@"mavericks"]&&!options[@"--esd"])||!options[@"--job"]) {
                fprintf(stderr,"Required: --assemble-installer guest --assistant package --job new-directory [--recovery image --esd image]\n");return 64;
            }
            NSMutableDictionary *files=[NSMutableDictionary dictionary];
            for(NSString *role in @[@"assistant",@"recovery",@"esd",@"diskimage"]) {
                NSString *value=options[[@"--" stringByAppendingString:role]];
                if(value)files[role]=[NSURL fileURLWithPath:value];
            }
            __block NSURL *result=nil;__block NSError *error=nil;
            dispatch_semaphore_t done=dispatch_semaphore_create(0);
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0), ^{
                VIMInstallerAssembly *assembly=[VIMInstallerAssembly new];
                result=[assembly assembleGuest:options[@"--assemble-installer"] files:files
                    jobDirectory:[NSURL fileURLWithPath:options[@"--job"]] error:&error];
                dispatch_semaphore_signal(done);
            });
            dispatch_semaphore_wait(done,DISPATCH_TIME_FOREVER);
            if(!result){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
            puts(result.path.UTF8String);return 0;
        }
        if ([args containsObject:@"--import-qcow2"]) {
            NSMutableDictionary *options = [NSMutableDictionary dictionary];
            NSSet *allowed = [NSSet setWithArray:@[@"--import-qcow2", @"--guest", @"--name", @"--library", @"--firmware", @"--nvram", @"--boot-media", @"--qemu-img"]];
            for (NSUInteger i = 1; i < args.count; i += 2) {
                if (i + 1 >= args.count || ![allowed containsObject:args[i]] || options[args[i]]) {
                    fprintf(stderr, "Invalid or duplicate import argument: %s\n", args[i].UTF8String); return 64;
                }
                options[args[i]] = args[i+1];
            }
            for (NSString *key in allowed) if (![key isEqual:@"--library"] && !options[key]) {
                fprintf(stderr, "Missing %s\n", key.UTF8String); return 64;
            }
            NSError *error = nil;
            VIMMachine *machine = [VIMMachine importDisk:[NSURL fileURLWithPath:options[@"--import-qcow2"]]
                guest:options[@"--guest"] name:options[@"--name"]
                library:options[@"--library"] ? [NSURL fileURLWithPath:options[@"--library"]] : VIMLibraryLocation.currentURL
                firmware:[NSURL fileURLWithPath:options[@"--firmware"]] nvram:[NSURL fileURLWithPath:options[@"--nvram"]]
                bootMedia:[NSURL fileURLWithPath:options[@"--boot-media"]] qemuImage:[NSURL fileURLWithPath:options[@"--qemu-img"]] error:&error];
            if (!machine) { fprintf(stderr, "%s\n", error.localizedDescription.UTF8String); return 1; }
            puts(machine.URL.path.UTF8String); return 0;
        }
    }
    return NSApplicationMain(argc, argv);
}
