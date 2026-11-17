#import "../tools/HostInstall/VIMBigSurMtree.h"
#if VIM_MTREE_APFS_PROBE
#import "../tools/HostInstall/VIMAPFSFirmlinks.h"
#import "../tools/HostInstall/VIMHFSCatalog.h"
#endif

int main(int argc,char **argv) { @autoreleasepool {
#if VIM_MTREE_APFS_PROBE
    if((argc==5||argc==7)&&!strcmp(argv[1],"--map")) {
        NSURL *raw=[NSURL fileURLWithPath:@(argv[2])];
        NSUUID *uuid=[[NSUUID alloc] initWithUUIDString:@(argv[3])];
        NSURL *manifest=[NSURL fileURLWithPath:@(argv[4])];
        uint64_t offset=0,length=0;NSError *error=nil;
        BOOL partition=argc==5?VIMGetAPFSPartition(raw,&offset,&length,&error):
            sscanf(argv[5],"%llu",&offset)==1&&sscanf(argv[6],"%llu",&length)==1;
        NSDictionary *mapped=partition?
            VIMPlanBigSurCanonicalMap(raw,offset,length,uuid,manifest,&error):nil;
        if(!mapped){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String?:"Invalid APFS partition.");return 1;}
        printf("entries=%llu objectIDs=%zu\n",[mapped[@"Entries"] unsignedLongLongValue],[mapped[@"ObjectIDs"] count]);
        return 0;
    }
#endif
    if(argc!=2)return 64;
    NSURL *manifest=[NSURL fileURLWithPath:@(argv[1])];
    __block uint64_t entries=0,links=0,external=0;
    uint64_t stamp=0;NSError *error=nil;
    BOOL parsed=VIMBigSurVisitCanonicalTree(manifest,^BOOL(__unused NSData *path,VIMBigSurCanonicalEntry entry){
        entries++;links+=(entry.siblingID!=0);external+=(entry.externalStream!=0);
        return YES;
    },&stamp,&error);
    if(!parsed){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
    printf("entries=%llu links=%llu external=%llu timestamp=%llu\n",entries,links,external,stamp);
    return 0;
}}
