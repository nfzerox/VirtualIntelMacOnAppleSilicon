#import "../VirtualIntelMac/VIMCPUBackend.h"
#include <assert.h>
int main(void){@autoreleasepool {
    NSDictionary *baseline=VIMHybridBaselineEnvironment();
    assert([baseline[@"LC_HVDBT_KEEP_KPTI"] isEqual:@"0"]);
    for(NSString *key in baseline)assert([baseline[key] isEqual:@"0"]);
    assert(!baseline[@"LC_HVDBT_TALLY"]);
    assert(!baseline[@"LC_HVDBT_INVALIDATION_MEMBERSHIP"]);
    for(NSString *guest in @[@"tiger",@"leopard",@"snowleopard",@"mavericks",@"mojave",@"tahoe"]) {
        NSDictionary *defaults=@{@"Guest":guest};
        assert([VIMCPUBackend(defaults) isEqual:@"hybrid"]);
        assert(!VIMCPUUsesEmulation(defaults,NO));
        assert(!VIMCPUUsesEmulation(defaults,YES));
        NSDictionary *tcg=@{@"Guest":guest,@"CPUBackend":@"tcg"};
        assert(VIMCPUUsesEmulation(tcg,YES));
        assert(!VIMCPUUsesEmulation(tcg,NO));
    }
    NSDictionary *normal=@{@"PVG_TCG_FAST_PTW":@"1",@"PVG_TCG_PCID_CONTEXTS":@"1",
        @"PVG_TCG_NATIVE_PACKED_FP":@"1",@"LC_HVDBT":@"0",@"PVG_SHADOW_COALESCE":@"1",
        @"PVG_OOP_HELPER":@"/unchanged/helper",@"OTHER":@"unchanged"};
    NSArray *control=@[@"--development-reference-tcg"];
    NSMutableDictionary *environment=[normal mutableCopy];
    assert(!VIMApplyTCGReferenceEnvironment(environment,YES,YES,@[]));
    assert([environment isEqual:normal]);
    assert(!VIMApplyTCGReferenceEnvironment(environment,YES,NO,control));
    assert([environment isEqual:normal]);
    assert(!VIMApplyTCGReferenceEnvironment(environment,NO,YES,control));
    assert([environment isEqual:normal]);
    assert(VIMApplyTCGReferenceEnvironment(environment,YES,YES,control));
    assert(environment.count==4);
    for(NSString *key in environment)assert([environment[key] isEqual:normal[key]]);
    char fixture[]="/private/tmp/vim-runtime-policy.XXXXXX";
    assert(mkdtemp(fixture));
    NSString *base=@(fixture);
    NSURL *target=[NSURL fileURLWithPath:[base stringByAppendingPathComponent:@"Target.vintelmac"]];
    NSURL *other=[NSURL fileURLWithPath:[base stringByAppendingPathComponent:@"Other.vintelmac"]];
    assert([NSFileManager.defaultManager createDirectoryAtURL:target withIntermediateDirectories:NO attributes:nil error:NULL]);
    NSString *path=@"untouched",*error=@"untouched";
    assert(VIMDevelopmentQEMUForMachine(@[],target,&path,&error));
    assert(!path&&!error);
    NSArray *historical=@[@"--development-qemu-for-vm",target.path,@"/usr/bin/true"];
    assert(VIMDevelopmentQEMUForMachine(historical,target,&path,&error));
    assert([path isEqual:@"/usr/bin/true"]&&!error);
    assert(VIMDevelopmentQEMUForMachine(historical,other,&path,&error));
    assert(!path&&!error);
    // /tmp and /private/tmp identify the same bundle on macOS.
    assert(VIMDevelopmentQEMUForMachine(historical,
        [NSURL fileURLWithPath:[target.path substringFromIndex:@"/private".length]],&path,&error));
    assert([path isEqual:@"/usr/bin/true"]);
    assert(!VIMDevelopmentQEMUForMachine(@[@"--development-qemu-for-vm"],target,&path,&error));
    assert(!path&&error);
    assert(!VIMDevelopmentQEMUForMachine(@[@"--development-qemu-for-vm",target.path,@"relative"],target,&path,&error));
    assert(!path&&error);
    NSArray *missing=@[@"--development-qemu-for-vm",target.path,@"/private/tmp/VIM-no-such-qemu"];
    assert(!VIMDevelopmentQEMUForMachine(missing,target,&path,&error));
    assert(!path&&error);
    assert(VIMDevelopmentQEMUForMachine(missing,other,&path,&error));
    assert(!path&&!error);
    assert([NSFileManager.defaultManager removeItemAtPath:base error:NULL]);
    puts("PASS CPU defaults for all guest families, explicit TCG and native Intel HVF");
    puts("PASS per-VM development runtime selection without a new app identity");
    return 0;
}}
