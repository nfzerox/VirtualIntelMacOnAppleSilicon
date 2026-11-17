#pragma once
#import <Foundation/Foundation.h>

static inline NSString *VIMHostInstallRecoveryPath(NSString *path) {
    return path.isAbsolutePath?path.stringByStandardizingPath.stringByResolvingSymlinksInPath:nil;
}
static inline BOOL VIMHostInstallRecoveryContains(NSString *root,NSString *path) {
    return root.length&&path.length&&[path hasPrefix:[root stringByAppendingString:@"/"]];
}
// A retry owns its private image bytes, not every image with the same volume
// name. External installer images qualify only when every mounted filesystem
// belongs to this attempt's recorded private mount directory.
static inline BOOL VIMHostInstallRecoveryOwnsImage(NSDictionary *image,NSString *attempt,NSArray<NSString *> *mounts) {
    NSString *path=VIMHostInstallRecoveryPath(image[@"image-path"]);
    if(VIMHostInstallRecoveryContains(attempt,path))return YES;
    for(NSString *root in mounts)if(VIMHostInstallRecoveryContains(root,path))return YES;
    BOOL mounted=NO;
    for(NSDictionary *entity in image[@"system-entities"]) {
        NSString *point=VIMHostInstallRecoveryPath(entity[@"mount-point"]);
        if(!point)continue;
        BOOL ours=NO;for(NSString *root in mounts)if(VIMHostInstallRecoveryContains(root,point))ours=YES;
        if(!ours)return NO;
        mounted=YES;
    }
    return mounted;
}
