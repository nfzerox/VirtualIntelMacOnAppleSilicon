// Read-only installed-guest evidence, using the same public query as kextstat.
// Resolve dynamically because newer public SDK stubs omit these old APIs.
#import <Foundation/Foundation.h>
#include <dlfcn.h>

int main(void) { @autoreleasepool {
    void *library=dlopen("/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit",RTLD_LAZY);
    typedef CFDictionaryRef (*CopyInfo)(CFArrayRef,CFArrayRef);
    CopyInfo copy=library?(CopyInfo)dlsym(library,"OSKextCopyLoadedKextInfo"):NULL;
    if(!copy)return 1;
    CFDictionaryRef loaded=copy(NULL,NULL);
    if(!loaded)return 1;
    NSDictionary *entries=(NSDictionary *)loaded;
    for(NSString *identifier in [[entries allKeys] sortedArrayUsingSelector:@selector(compare:)]) {
        NSDictionary *entry=[entries objectForKey:identifier];
        for(NSString *key in @[@"OSBundlePath",@"_PrelinkBundlePath",@"CFBundlePath"])
            if([entry objectForKey:key])printf("%s %s=%s\n",identifier.UTF8String,key.UTF8String,
                [[entry objectForKey:key] description].UTF8String);
    }
    printf("loaded_kexts=%lu\n",(unsigned long)entries.count);
    CFRelease(loaded);dlclose(library);return 0;
}}
