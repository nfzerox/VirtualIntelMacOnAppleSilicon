#import <Foundation/Foundation.h>
#import "../VirtualIntelMac/VIMOpenGLSupportDisks.h"
#include <assert.h>
int main(int argc,char **argv){@autoreleasepool {
    assert(argc==2);
    NSDictionary *captured=[NSDictionary dictionaryWithContentsOfFile:@(argv[1])];
    BOOL needsInfo=NO;
    NSArray *devices=VIMOpenGLSupportDiskCandidates(captured,@"VIM GL FD2127C1",&needsInfo);
    assert(needsInfo&&devices.count==9&&[devices containsObject:@"disk2s1s1"]);
    NSDictionary *info=@{@"DeviceIdentifier":@"disk2s1s1",@"VolumeName":@"VIM GL FD2127C1"};
    assert(VIMOpenGLSupportDiskInfoMatches(info,@"disk2s1s1",@"VIM GL FD2127C1"));
    assert(!VIMOpenGLSupportDiskInfoMatches(info,@"disk0s1",@"VIM GL FD2127C1"));
    assert(!VIMOpenGLSupportDiskCandidates(@{@"AllDisks":@[@"disk0",@"../../disk1"]},@"x",&needsInfo));
    NSArray *modern=VIMOpenGLSupportDiskCandidates(@{@"AllDisksAndPartitions":@[@{@"Partitions":@[info]}]},@"VIM GL FD2127C1",&needsInfo);
    assert(!needsInfo&&modern.count==1&&[modern.firstObject isEqual:@"disk2s1s1"]);
    puts("PASS captured Leopard flat list, exact info identity, invalid device and modern partition tree");
    return 0;
}}
