#import <Foundation/Foundation.h>
#import "../tools/modern-setup-identity.h"
int main(void){@autoreleasepool{
    NSString *system=@"system-volume",*partition=@"new-disk-partition";
    NSDictionary *container=@{@"Volumes":@[@{@"APFSVolumeUUID":system},@{@"APFSVolumeUUID":@"data-volume"}],
        @"PhysicalStores":@[@{@"DiskUUID":partition}]};
    NSDictionary *layout=@{@"Containers":@[container]};
    NSCAssert(VIMModernAPFSStoreMatches(layout,system,partition),@"Converted system volume retains its authorized partition");
    NSCAssert(VIMModernAPFSStoreMatches(layout,@"data-volume",partition),@"Data volume belongs to the same physical store");
    NSCAssert(!VIMModernAPFSStoreMatches(layout,system,@"another-disk"),@"A different virtual disk is not adopted");
    NSCAssert(!VIMModernAPFSStoreMatches(layout,@"snapshot-uuid",partition),@"Snapshot identity is not confused with the system resource UUID");
    NSCAssert(!VIMModernAPFSStoreMatches(@{@"Containers":@[container,container]},system,partition),@"Ambiguous containers are not adopted");
    NSCAssert(!VIMModernAPFSStoreMatches(nil,system,partition),@"Unavailable metadata is not installation authorization");
    puts("PASS: native APFS system/data identity and physical-partition ownership");
}return 0;}
