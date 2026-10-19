#import <Cocoa/Cocoa.h>
#import "VIMMachine.h"
#import "VIMLibraryLocation.h"
#import "VIMInstallerAssembly.h"

int main(int argc, const char * argv[]) {
    @autoreleasepool {
        NSArray<NSString *> *args = NSProcessInfo.processInfo.arguments;
        if([args containsObject:@"--create-vm"]) {
            NSMutableDictionary *options=[NSMutableDictionary dictionary];
            NSSet *required=[NSSet setWithArray:@[@"--create-vm",@"--name",@"--firmware",@"--nvram",@"--boot-media",@"--installer",@"--qemu-img"]];
            NSMutableSet *allowed=[required mutableCopy];[allowed addObjectsFromArray:@[@"--library",@"--disk-gib"]];
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
        if ([args containsObject:@"--create-bigsur-media"]||[args containsObject:@"--create-legacy-media"]) {
            NSMutableDictionary *options=[NSMutableDictionary dictionary];
            NSSet *allowed=[NSSet setWithArray:@[@"--create-bigsur-media",@"--create-legacy-media",@"--guest",@"--job"]];
            for(NSUInteger i=1;i<args.count;i+=2) {
                if(i+1>=args.count||![allowed containsObject:args[i]]||options[args[i]]) {
                    fprintf(stderr,"Invalid or duplicate media argument: %s\n",args[i].UTF8String);return 64;
                }
                options[args[i]]=args[i+1];
            }
            BOOL legacy=options[@"--create-legacy-media"]!=nil;
            if(!options[@"--job"]||(legacy&&(![@[@"mojave",@"catalina"] containsObject:options[@"--guest"]]||options[@"--create-bigsur-media"]))) {
                fprintf(stderr,"Required: --create-bigsur-media installer.app --job new-directory OR --create-legacy-media installer.app --guest mojave|catalina --job new-directory\n");return 64;
            }
            __block NSURL *result=nil;__block NSError *error=nil;
            dispatch_semaphore_t done=dispatch_semaphore_create(0);
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0), ^{
                VIMInstallerAssembly *worker=[VIMInstallerAssembly new];
                if(legacy)result=[worker createLegacyMediaFromInstaller:[NSURL fileURLWithPath:options[@"--create-legacy-media"]] guest:options[@"--guest"] jobDirectory:[NSURL fileURLWithPath:options[@"--job"]] error:&error];
                else result=[worker createBigSurMediaFromInstaller:[NSURL fileURLWithPath:options[@"--create-bigsur-media"]] jobDirectory:[NSURL fileURLWithPath:options[@"--job"]] error:&error];
                dispatch_semaphore_signal(done);
            });
            dispatch_semaphore_wait(done,DISPATCH_TIME_FOREVER);
            if(!result){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
            puts(result.path.UTF8String);return 0;
        }
        if ([args containsObject:@"--assemble-installer"]) {
            NSMutableDictionary *options=[NSMutableDictionary dictionary];
            NSSet *allowed=[NSSet setWithArray:@[@"--assemble-installer",@"--assistant",@"--recovery",@"--esd",@"--job"]];
            for(NSUInteger i=1;i<args.count;i+=2) {
                if(i+1>=args.count||![allowed containsObject:args[i]]||options[args[i]]) {
                    fprintf(stderr,"Invalid or duplicate assembly argument: %s\n",args[i].UTF8String);return 64;
                }
                options[args[i]]=args[i+1];
            }
            if(!options[@"--assistant"]||!options[@"--job"]) {
                fprintf(stderr,"Required: --assemble-installer guest --assistant package --job new-directory [--recovery image --esd image]\n");return 64;
            }
            NSMutableDictionary *files=[NSMutableDictionary dictionary];
            for(NSString *role in @[@"assistant",@"recovery",@"esd"]) {
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
