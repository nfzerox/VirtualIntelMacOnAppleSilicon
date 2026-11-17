#import "../tools/HostInstall/VIMBigSurArchive.h"
#import "../tools/HostInstall/VIMBigSurBootAssets.h"
#import "../compat/VIMEmptySourceRecoveryPatch.h"

int main(int argc,char **argv) {
    @autoreleasepool {
        if(argc!=4)return 64;
        NSURL *source=[NSURL fileURLWithPath:@(argv[1])];
        NSError *error=nil;
        if(!strcmp(argv[2],"--decode-recovery")) {
            if(VIMDecodeEmptySourceRecoveryPatch(source,[NSURL fileURLWithPath:@(argv[3])],nil,&error))return 0;
        } else {
            NSURL *manifest=[NSURL fileURLWithPath:@(argv[2])];
            NSInteger major=[@(argv[3]) integerValue];
            NSArray *assets=[NSDictionary dictionaryWithContentsOfURL:manifest][@"Assets"];
            NSDictionary *asset=VIMAssistantAssetForArchive(assets,source,major,&error);
            NSDictionary *inventory=asset?VIMAssistantPayloadInventory(source,asset,major,&error):nil;
            NSArray *members=inventory?VIMBigSurArchiveMembers(source,&error):nil;
            NSDictionary *plan=members?VIMBigSurBootAssetPlan(members,&error):nil;
            NSData *signedRoot=plan?VIMBigSurReadSmallArchiveMember(source,plan[@"IntelRootHashMember"],&error):nil;
            NSData *digest=signedRoot?VIMBigSurIntelRootHash(signedRoot,&error):nil;
            if(digest) {
                NSMutableString *hex=[NSMutableString stringWithCapacity:digest.length*2];
                const uint8_t *bytes=digest.bytes;
                for(NSUInteger i=0;i<digest.length;i++)[hex appendFormat:@"%02x",bytes[i]];
                NSLog(@"%@ (%@): %lu System payload chunks; %@; authenticated root digest %@",
                    inventory[@"Version"],inventory[@"Build"],(unsigned long)[inventory[@"Chunks"] count],
                    plan[@"Recovery"][@"BaseSystem.dmg"],hex);
                return 0;
            }
        }
        fprintf(stderr,"%s\n",error.localizedDescription.UTF8String?:"Could not read the assistant media.");
        return 1;
    }
}
