#pragma once
#import <Foundation/Foundation.h>

static inline BOOL VIMOpenGLSupportDiskIdentifier(id value) {
    if(![value isKindOfClass:NSString.class]||![value length]||[value length]>64)return NO;
    NSRegularExpression *pattern=[NSRegularExpression regularExpressionWithPattern:@"\\Adisk[0-9]+(s[0-9]+)*\\z" options:0 error:NULL];
    return [pattern numberOfMatchesInString:value options:0 range:NSMakeRange(0,[value length])]==1;
}

// Leopard's diskutil list -plist publishes flat AllDisks/WholeDisks arrays,
// not the later named partition tree. Return its bounded exact device names
// for individual native info queries; never treat VolumesFromDisks indices
// as an association between a name and a device.
static inline NSArray<NSString *> *VIMOpenGLSupportDiskCandidates(NSDictionary *list,NSString *volume,BOOL *needsInfo) {
    *needsInfo=NO;
    id tree=list[@"AllDisksAndPartitions"];
    if(!tree) {
        NSArray *devices=list[@"AllDisks"];
        if(![devices isKindOfClass:NSArray.class]||devices.count>128)return nil;
        NSMutableOrderedSet *result=[NSMutableOrderedSet new];
        for(id device in devices){if(!VIMOpenGLSupportDiskIdentifier(device))return nil;[result addObject:device];}
        *needsInfo=YES;return result.array;
    }
    if(![tree isKindOfClass:NSArray.class]||[tree count]>128)return nil;
    NSMutableArray *queue=[tree mutableCopy],*matches=[NSMutableArray new];
    for(NSUInteger i=0;i<queue.count;i++) {
        NSDictionary *entry=queue[i];if(![entry isKindOfClass:NSDictionary.class])continue;
        if([entry[@"VolumeName"] isEqual:volume]) {
            if(!VIMOpenGLSupportDiskIdentifier(entry[@"DeviceIdentifier"]))return nil;
            [matches addObject:entry[@"DeviceIdentifier"]];
        }
        if([entry[@"Partitions"] isKindOfClass:NSArray.class]) {
            if(queue.count+[entry[@"Partitions"] count]>128)return nil;
            [queue addObjectsFromArray:entry[@"Partitions"]];
        }
    }
    return matches;
}

static inline BOOL VIMOpenGLSupportDiskInfoMatches(NSDictionary *info,NSString *device,NSString *volume) {
    return [info[@"DeviceIdentifier"] isEqual:device]&&[info[@"VolumeName"] isEqual:volume];
}
