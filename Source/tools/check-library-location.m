#import <Cocoa/Cocoa.h>
#import "VIMLibraryLocation.h"

// Bounded, read-only qualification. The argument domain shadows the app's saved
// preference without changing it. Run with a real library root to list its Macs.
int main(int argc,const char **argv) {
    @autoreleasepool {
        NSURL *root=[NSURL fileURLWithPath:argc>1?@(argv[1]):@"/tmp/VIM-Unmarked-Library" isDirectory:YES];
        for(NSURL *input in @[root,[root URLByAppendingPathComponent:@"Machines"]]) {
            [NSUserDefaults.standardUserDefaults setVolatileDomain:@{@"VIMLibraryPath":input.path} forName:NSArgumentDomain];
            NSCAssert([VIMLibraryLocation.currentURL.path isEqual:root.path],@"Library must mean root");
            NSCAssert([VIMLibraryLocation.machinesURL.path isEqual:[root.path stringByAppendingPathComponent:@"Machines"]],@"Machines child exactly once");
            NSCAssert([VIMLibraryLocation.downloadsURL.path isEqual:[root.path stringByAppendingPathComponent:@"Downloads"]],@"Downloads belongs to root");
            NSCAssert([[VIMLibraryLocation installersURLForMachines:input].path isEqual:[root.path stringByAppendingPathComponent:@"Installers"]],@"Old job/root installer lookup agrees");
        }
        if(argc>1) {
            NSError *error=nil;
            NSArray *files=[NSFileManager.defaultManager contentsOfDirectoryAtURL:VIMLibraryLocation.machinesURL includingPropertiesForKeys:nil options:NSDirectoryEnumerationSkipsHiddenFiles error:&error];
            if(!files){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
            NSUInteger count=0;
            for(NSURL *file in files)if([file.pathExtension isEqual:@"vintelmac"]) {
                NSDictionary *machine=[NSDictionary dictionaryWithContentsOfURL:[file URLByAppendingPathComponent:@"Machine.plist"]];
                if(!machine){fprintf(stderr,"Unreadable Mac: %s\n",file.lastPathComponent.UTF8String);return 1;}
                puts(file.lastPathComponent.UTF8String);count++;
            }
            printf("Found %lu Macs from library root.\n",(unsigned long)count);
        }
        puts("Library root / legacy Machines selection checks passed.");
        return 0;
    }
}
