#import <Foundation/Foundation.h>
#include <unistd.h>
static void Check(BOOL ok,NSString *message){if(!ok)@throw [NSException exceptionWithName:@"Fixture" reason:message userInfo:nil];}
#include "../tools/guest-boot-cache.h"
int main(int argc,const char **argv){@autoreleasepool {@try {
    if(argc==3&&!strcmp(argv[1],"-i")) {
        if(getenv("VIM_TEST_CACHE_NO_OUTPUT"))return 0;
        NSURL *root=[NSURL fileURLWithPath:@(argv[2])];
        NSDictionary *version=[NSDictionary dictionaryWithContentsOfURL:[root URLByAppendingPathComponent:@"System/Library/CoreServices/SystemVersion.plist"]];
        BOOL mojave=[version[@"ProductVersion"] hasPrefix:@"10.14."]||[root.lastPathComponent isEqual:@"mojave"];
        [NSFileManager.defaultManager createDirectoryAtURL:[root URLByAppendingPathComponent:CachePaths(mojave).lastObject].URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:NULL];
        Check([@"fresh generated cache" writeToURL:[root URLByAppendingPathComponent:CachePaths(mojave).lastObject] atomically:YES encoding:NSUTF8StringEncoding error:NULL],@"Fixture builder failed");return 0;
    }
    NSFileManager *fm=NSFileManager.defaultManager;NSError *error=nil;
    Check([BootCacheArguments(@"/",YES,YES,YES) isEqual:@[@"-z",@"-i",@"/"]],@"Mojave guest metadata policy missing");
    for(NSArray *policy in @[@[@YES,@NO,@YES],@[@YES,@YES,@NO],@[@NO,@YES,@YES]])
        Check([BootCacheArguments(@"/",[policy[0] boolValue],[policy[1] boolValue],[policy[2] boolValue]) isEqual:@[@"-i",@"/"]],@"Relaxed metadata policy escaped its admission gates");
    Check([BootCacheArguments(@"/fixture",YES,YES,YES) isEqual:@[@"-i",@"/fixture"]],@"Offline root used live guest metadata policy");
    NSURL *base=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]].URLByResolvingSymlinksInPath;
    for(NSNumber *mojave in @[@YES,@NO]) {
        NSURL *root=[base URLByAppendingPathComponent:mojave.boolValue?@"mojave":@"catalina"];
        NSURL *version=[root URLByAppendingPathComponent:@"System/Library/CoreServices/SystemVersion.plist"];
        Check([fm createDirectoryAtURL:version.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:&error],error.description);
        Check([@{@"ProductBuildVersion":@"fixture-build"} writeToURL:version atomically:YES],@"Version write failed");
        NSArray *paths=CachePaths(mojave.boolValue);
        for(NSString *relative in paths){NSURL *cache=[root URLByAppendingPathComponent:relative];Check([fm createDirectoryAtURL:cache.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:&error],error.description);}
        NSURL *system=[root URLByAppendingPathComponent:paths[0]];
        Check([@"original" writeToURL:system atomically:YES encoding:NSUTF8StringEncoding error:&error],error.description);
        NSURL *snapshot=[root URLByAppendingPathComponent:@"snapshot"];
        SnapshotBootCaches(root,snapshot,mojave.boolValue,@"fixture-build");
#ifdef VIM_INSTALL_PLAN_TESTING
        for(NSString *relative in @[@"System/Library/Extensions",@"Library/Extensions"])Check([fm createDirectoryAtURL:[root URLByAppendingPathComponent:relative] withIntermediateDirectories:YES attributes:nil error:&error],error.description);
        setenv("VIM_TEST_CACHE_BUILDER",argv[0],1);
        NSDictionary *rebuilt=RebuildBootCaches(root,mojave.boolValue,[root URLByAppendingPathComponent:@"build.log"]);
        Check([rebuilt[@"CacheRebuilt"] boolValue]&&![rebuilt[@"Ready"] boolValue],@"Invalid rebuild result");
        Check([rebuilt[@"BuilderArguments"] isEqual:@[@"-i",root.path]]&&[rebuilt[@"BuildSeconds"] doubleValue]>=0,@"Cache build provenance missing");
        Check([[NSString stringWithContentsOfURL:system encoding:NSUTF8StringEncoding error:NULL] isEqual:@"fresh generated cache"],@"Rebuilt cache did not reach System volume");
        setenv("VIM_TEST_CACHE_NO_OUTPUT","1",1);BOOL staleRefused=NO;
        @try{RebuildBootCaches(root,mojave.boolValue,[root URLByAppendingPathComponent:@"stale.log"]);}@catch(NSException *exception){staleRefused=YES;}
        unsetenv("VIM_TEST_CACHE_NO_OUTPUT");Check(staleRefused,@"Successful no-op builder accepted stale cache");
#endif
        for(NSString *relative in paths)Check([@"builder replacement" writeToURL:[root URLByAppendingPathComponent:relative] atomically:YES encoding:NSUTF8StringEncoding error:&error],error.description);
        Check([NSData.data writeToURL:system atomically:YES],@"Cannot simulate truncated cache");
        RestoreBootCaches(root,snapshot,mojave.boolValue,@"fixture-build");
        RestoreBootCaches(root,snapshot,mojave.boolValue,@"fixture-build");
        Check([[NSString stringWithContentsOfURL:system encoding:NSUTF8StringEncoding error:&error] isEqual:@"original"],@"Original not restored");
        if(!mojave.boolValue)Check(![fm fileExistsAtPath:[root URLByAppendingPathComponent:paths[1]].path],@"Originally absent Data cache retained");
        BOOL refused=NO;@try {RestoreBootCaches(root,snapshot,mojave.boolValue,@"different-build");}@catch(NSException *exception){refused=YES;}Check(refused,@"Wrong build accepted");
    }
    printf("PASS: cache restoration, repeat restoration, absent Data cache, build mismatch; retained %s\n",base.path.UTF8String);
} @catch(NSException *exception){fprintf(stderr,"FAIL: %s\n",exception.reason.UTF8String);return 1;}}return 0;}
