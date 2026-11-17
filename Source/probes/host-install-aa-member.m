// Bounded original-media extraction/compression control; no VM installation.
#import "../tools/HostInstall/VIMBigSurArchive.h"
#import "../tools/HostInstall/VIMBigSurFixups.h"
int main(int argc,char **argv) { @autoreleasepool {
    if(argc!=4&&argc!=5)return 64;
    if(argc==4&&!strcmp(argv[1],"--data-protection")) {
        NSError *error=nil;
        BOOL ok=VIMBigSurStageDataProtection([NSURL fileURLWithPath:@(argv[2])],
            [NSURL fileURLWithPath:@(argv[3])],&error);
        if(!ok)fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);
        return ok?0:1;
    }
    if(argc==4&&!strcmp(argv[1],"--compression")) {
        // Resume only this production phase on an explicitly mounted,
        // disposable System volume. No extraction or fresh OS installation.
        NSError *error=nil;
        BOOL ok=VIMBigSurRestoreCompression([NSURL fileURLWithPath:@(argv[2])],
            [NSURL fileURLWithPath:@(argv[3])],nil,^(uint64_t sent,uint64_t total) {
                static uint64_t last;
                if(!sent||sent==total||sent-last>=16*1024*1024) {
                    fprintf(stderr,"compression %llu/%llu\n",(unsigned long long)sent,(unsigned long long)total);
                    last=sent;
                }
            },&error);
        if(!ok)fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);
        return ok?0:1;
    }
    NSURL *zip=[NSURL fileURLWithPath:@(argv[1])],*root=[NSURL fileURLWithPath:@(argv[3])];
    if(argc==5&&!strcmp(argv[2],"--chunks")) {
        // Bounded extraction only, not a complete System/build validation.
        int count=atoi(argv[4]);if(count<1||count>8)return 64;
        for(int i=0;i<count;i++) {
            NSError *error=nil;
            NSString *member=[NSString stringWithFormat:@"AssetData/payloadv2/payload.%03d",i];
            if(!VIMBigSurExtractPayloadMember(zip,member,root,NULL,NULL,&error)) {
                fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;
            }
        }
        printf("Extracted %d original chunks; no full System qualification claimed.\n",count);return 0;
    }
    if(argc==5&&!strcmp(argv[2],"--prefix")) {
        NSMutableArray *chunks=[NSMutableArray array];
        for(int i=0;i<atoi(argv[4]);i++)[chunks addObject:@{@"Member":[NSString stringWithFormat:@"AssetData/payloadv2/payload.%03d",i],@"EncodedLength":@0}];
        NSError *error=nil;
        BOOL ok=VIMBigSurStageSystemPayload(zip,@{@"Chunks":chunks,@"Build":@"prefix-probe"},root,
            VIMAssistantCachesInline,NULL,NULL,&error);
        if(!ok)fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);
        return ok?0:1;
    }
    NSURL *paths=argc==5?[NSURL fileURLWithPath:@(argv[4])]:nil;NSError *error=nil;
    BOOL ok=VIMBigSurExtractPayloadMember(zip,@(argv[2]),root,NULL,NULL,&error);
    if(ok&&paths)ok=VIMBigSurRepairCompression(zip,root,paths,nil,&error);
    if(!ok)fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);
    return ok?0:1;
}}
