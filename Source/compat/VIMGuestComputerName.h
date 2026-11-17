#pragma once
#import <Foundation/Foundation.h>

// Native SystemConfiguration preference layout, prepared only on a new guest
// volume. Match the account finalizer's naming convention without contacting
// host configd, renaming existing guests, or publishing an explicit home share.
NS_INLINE NSDictionary *VIMGuestComputerNamePreferences(NSDictionary *original,NSString *username) {
    NSMutableDictionary *preferences=[original mutableCopy]?:[NSMutableDictionary dictionary];
    NSMutableDictionary *system=[preferences[@"System"] mutableCopy]?:[NSMutableDictionary dictionary];
    NSMutableDictionary *identity=[system[@"System"] mutableCopy]?:[NSMutableDictionary dictionary];
    if(![identity[@"ComputerName"] length]) {
        identity[@"ComputerName"]=[username stringByAppendingString:@"’s Mac"];
        identity[@"ComputerNameEncoding"]=@(kCFStringEncodingUTF8);
    }
    NSMutableDictionary *network=[system[@"Network"] mutableCopy]?:[NSMutableDictionary dictionary];
    NSMutableDictionary *hosts=[network[@"HostNames"] mutableCopy]?:[NSMutableDictionary dictionary];
    if(![hosts[@"LocalHostName"] length])
        hosts[@"LocalHostName"]=[[username stringByAppendingString:@"s-Mac"]
            stringByReplacingOccurrencesOfString:@"_" withString:@"-"];
    network[@"HostNames"]=hosts;system[@"Network"]=network;system[@"System"]=identity;
    preferences[@"System"]=system;
    return preferences;
}
