#pragma once
#import <Foundation/Foundation.h>

static inline NSString *VIMCPUBackend(NSDictionary *configuration) {
    return configuration[@"CPUBackend"]?:@"hybrid";
}
static inline BOOL VIMCPUUsesEmulation(NSDictionary *configuration,BOOL appleSilicon) {
    if(!appleSilicon)return NO; // Intel hosts use native HVF, even for an ARM-created Mac.
    return [VIMCPUBackend(configuration) isEqual:@"tcg"];
}

// Runtime-only regression comparisons stay in the normally signed app, with
// its existing privacy identity and document handling. Scope the override to
// one explicit bundle: opening another VM in that app must use its own runtime.
static inline BOOL VIMDevelopmentQEMUForMachine(NSArray<NSString *> *arguments,
    NSURL *machine,NSString **executable,NSString **error) {
    if(executable)*executable=nil;
    if(error)*error=nil;
    NSUInteger option=[arguments indexOfObject:@"--development-qemu-for-vm"];
    if(option==NSNotFound)return YES;
    if(option+2>=arguments.count||![arguments[option+1] isAbsolutePath]||
       ![arguments[option+2] isAbsolutePath]) {
        if(error)*error=@"--development-qemu-for-vm requires an absolute VM bundle path and QEMU executable path.";
        return NO;
    }
    NSString *target=[arguments[option+1] stringByResolvingSymlinksInPath];
    if(![target isEqual:machine.path.stringByResolvingSymlinksInPath])return YES;
    NSString *path=arguments[option+2];
    if(![NSFileManager.defaultManager isExecutableFileAtPath:path]) {
        if(error)*error=@"The development QEMU executable could not be opened.";
        return NO;
    }
    if(executable)*executable=path;
    return YES;
}

// Explicit same-disk diagnostic, never a persisted backend or shipping fallback.
// Keep display/device settings unchanged while isolating optional TCG paths.
static inline BOOL VIMApplyTCGReferenceEnvironment(NSMutableDictionary *environment,
    BOOL appleSilicon,BOOL emulation,NSArray<NSString *> *arguments) {
    if(!appleSilicon||!emulation||![arguments containsObject:@"--development-reference-tcg"])return NO;
    for(NSString *key in environment.allKeys)
        if([key hasPrefix:@"PVG_TCG_"])[environment removeObjectForKey:key];
    return YES;
}

// Exclude unqualified research presets from ordinary launches. Qualified engine
// optimizations are defaults, not another set of user-facing switches.
static inline NSDictionary<NSString *,NSString *> *VIMHybridBaselineEnvironment(void) {
    return @{@"LC_HVDBT_KEEP_KPTI":@"0",@"LC_HVDBT_INLINE_SHR8":@"0",
        @"LC_HVDBT_PTE_SAME64":@"0",@"LC_HVDBT_RETAIN_SHARED_DEAD":@"0",
        @"LC_HVDBT_KEEPER_INACTIVE_GUARD":@"0",
        @"LC_HVDBT_PAGE_REP":@"0",@"LC_HVDBT_PROFILE_SHIFT_HELPERS":@"0",
        @"LC_HVDBT_PROFILE_INVALIDATION":@"0",@"LC_HVDBT_PROFILE_COMPILE_INPUTS":@"0",
        @"LC_HVDBT_MMU_OBSERVATIONS":@"0",@"ENGINE_TRACE":@"0"};
}
