#import "VIMBigSurSealTree.h"
#import "VIMAPFSFirmlinks.h"
#import "VIMBigSurTree.h"
#import <CommonCrypto/CommonDigest.h>
#include "../host-install-bigsur-digest.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

enum { Block=4096 };
static uint16_t U16(const uint8_t *p) {return (uint16_t)p[0]|(uint16_t)p[1]<<8;}
static uint32_t U32(const uint8_t *p) {return U16(p)|(uint32_t)U16(p+2)<<16;}
static uint64_t U64(const uint8_t *p) {return U32(p)|(uint64_t)U32(p+4)<<32;}
static void Write64(uint8_t *p,uint64_t v) {for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(i*8));}
static id Failure(NSError **error,int code,NSString *text) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurSealTree" code:code
        userInfo:@{NSLocalizedDescriptionKey:text}];
    return nil;
}

typedef struct {
    __unsafe_unretained NSArray<NSArray<NSData *> *> *records;
    __unsafe_unretained VIMBigSurTreeBuilder *builder;
    __unsafe_unretained BOOL (^cancelled)(void);
    NSUInteger index;
    uint64_t lastID,lastOffset,seen;
} Merge;

static int MergeDigest(const uint8_t raw[16],const uint8_t value[36],void *opaque) {
    @autoreleasepool {
        Merge *merge=opaque;
        uint64_t id=U64(raw),offset=U64(raw+8);
        if(!id||id>=(UINT64_C(1)<<60)||offset>=(UINT64_C(1)<<56)||!U16(value)||U16(value)>4||
           value[2]!=32||value[35]||
           (merge->seen&&(id<merge->lastID||(id==merge->lastID&&offset<=merge->lastOffset))))return EBADMSG;
        merge->lastID=id;merge->lastOffset=offset;
        if(!(++merge->seen&4095)&&merge->cancelled&&merge->cancelled())return ECANCELED;
        if(offset&(Block-1))return 0;
        uint8_t bytes[16];Write64(bytes,(UINT64_C(13)<<60)|id);Write64(bytes+8,(UINT64_C(1)<<56)|offset);
        NSData *key=[NSData dataWithBytes:bytes length:sizeof(bytes)];
        while(merge->index<merge->records.count) {
            NSArray<NSData *> *record=merge->records[merge->index];
            if(VIMCompareBigSurRecordKeys(record[0],key)!=NSOrderedAscending)break;
            if(![merge->builder appendKey:record[0] value:record[1]])return EBADMSG;
            merge->index++;
        }
        return [merge->builder appendKey:key value:[NSData dataWithBytes:value length:35]]?0:EBADMSG;
    }
}

NSDictionary *VIMBuildBigSurTreeWithDigests(NSMutableArray<NSArray<NSData *> *> *records,
    NSURL *database,NSData *rootHash,BOOL (^sink)(uint64_t,NSData *),BOOL (^cancelled)(void),NSError **error) {
    if(!records.count||rootHash.length!=32)return Failure(error,EINVAL,@"The original Big Sur seal inputs are missing.");
    [records sortUsingComparator:^NSComparisonResult(NSArray<NSData *> *a,NSArray<NSData *> *b) {
        return VIMCompareBigSurRecordKeys(a[0],b[0]);
    }];
    if(cancelled&&cancelled())return Failure(error,ECANCELED,@"Installation cancelled.");
    VIMBigSurTreeBuilder *builder=[[VIMBigSurTreeBuilder alloc] initWithSink:sink];
    Merge merge={.records=records,.builder=builder,.cancelled=cancelled};
    if(VIMVisitBigSurDigests(database.fileSystemRepresentation,MergeDigest,&merge)||!merge.seen) {
        int reason=errno?:EBADMSG;
        return Failure(error,reason,reason==ECANCELED?@"Installation cancelled.":
            @"Could not merge the original Big Sur file digests into the filesystem tree.");
    }
    for(;merge.index<records.count;merge.index++) @autoreleasepool {
        if(!(merge.index&4095)&&cancelled&&cancelled())return Failure(error,ECANCELED,@"Installation cancelled.");
        NSArray<NSData *> *record=records[merge.index];
        if(![builder appendKey:record[0] value:record[1]])return Failure(error,EBADMSG,@"Could not append the original Big Sur filesystem record.");
    }
    return [builder finishWithRootHash:rootHash error:error];
}

static BOOL Transfer(int fd,void *bytes,uint64_t block,BOOL writing) {
    off_t offset=(off_t)(block*Block);
    for(size_t done=0;done<Block;) {
        ssize_t n=writing?pwrite(fd,(uint8_t *)bytes+done,Block-done,offset+done):
            pread(fd,(uint8_t *)bytes+done,Block-done,offset+done);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)return NO;
        done+=(size_t)n;
    }
    return YES;
}

BOOL VIMVisitBigSurAuthenticatedTree(uint64_t count,NSData *rootHash,NSData * (^readBlock)(uint64_t),
    VIMBigSurSealRecord record,BOOL (^cancelled)(void)) {
    if(!count||count>NSUIntegerMax||rootHash.length!=CC_SHA256_DIGEST_LENGTH||!readBlock)return NO;
    NSMutableIndexSet *visited=[NSMutableIndexSet indexSet];
    NSMutableArray *pending=[NSMutableArray arrayWithObject:@[@0,rootHash,@(-1)]];
    while(pending.count) @autoreleasepool {
        NSArray *item=pending.lastObject;[pending removeLastObject];
        NSUInteger address=[item[0] unsignedIntegerValue];NSData *expected=item[1];int level=[item[2] intValue];
        if(address>=count||[visited containsIndex:address]||
            (!(visited.count&1023)&&cancelled&&cancelled()))return NO;
        [visited addIndex:address];
        NSData *node=readBlock(address);uint8_t digest[CC_SHA256_DIGEST_LENGTH];
        if(node.length!=Block||expected.length!=sizeof(digest))return NO;
        const uint8_t *bytes=node.bytes;
        CC_SHA256(bytes,Block,digest);
        if(memcmp(digest,expected.bytes,sizeof(digest)))return NO;
        uint16_t flags=U16(bytes+32),height=U16(bytes+34);
        uint32_t rows=U32(bytes+36),table=56+U16(bytes+40),keys=table+U16(bytes+42),end=Block-(flags&1?40:0);
        if((flags&0x18)!=0x18||((flags&1)!=0)!=(address==0)||((flags&2)!=0)!=(height==0)||
            (level>=0&&height!=level)||!rows||keys>end||rows>U16(bytes+42)/8)return NO;
        if(!height) {
            if(record)for(size_t i=0;i<rows;i++) {
                const uint8_t *slot=bytes+table+i*8;
                size_t key=keys+U16(slot),k=U16(slot+2),offset=U16(slot+4),v=U16(slot+6);
                if(!k||!v||key>end||k>end-key||offset<v||offset>end-keys||key+k>end-offset||
                    !record(bytes+key,k,bytes+end-offset,v))return NO;
            }
            continue;
        }
        for(size_t i=0;i<rows;i++) {
            const uint8_t *slot=bytes+table+i*8;
            size_t off=U16(slot+4);
            if(off<40||off>end-keys||U16(slot+6)!=40)return NO;
            const uint8_t *value=bytes+end-off;
            [pending addObject:@[@(U64(value)),[NSData dataWithBytes:value+8 length:32],@(height-1)]];
        }
    }
    return visited.count==count;
}

BOOL VIMVerifyBigSurTreeNodes(uint64_t count,NSData *rootHash,NSData * (^readBlock)(uint64_t),BOOL (^cancelled)(void)) {
    return VIMVisitBigSurAuthenticatedTree(count,rootHash,readBlock,nil,cancelled);
}

NSDictionary *VIMBuildBigSurSealedFilesystem(NSURL *raw,uint64_t offset,uint64_t size,NSUUID *system,
    NSURL *mtree,NSURL *database,NSURL *zip,NSData *rootHash,NSURL *output,
    BOOL (^cancelled)(void),NSError **error) {
    NSMutableArray<NSArray<NSData *> *> *records=[NSMutableArray array];
    NSDictionary *plan=VIMVisitBigSurCanonicalRecords(raw,offset,size,system,mtree,zip,^BOOL(NSData *key,NSData *value){
        if(!(records.count&4095)&&cancelled&&cancelled())return NO;
        [records addObject:@[key,value]];return YES;
    },error);
    if(!plan)return nil;
    int fd=open(output.fileSystemRepresentation,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)return Failure(error,errno,@"Could not create the unpublished Big Sur filesystem tree.");
    BOOL verified=NO;
    @try {
        NSDictionary *tree=VIMBuildBigSurTreeWithDigests(records,database,rootHash,^BOOL(uint64_t address,NSData *block){
            return address<=INT64_MAX/Block&&block.length==Block&&Transfer(fd,(void *)block.bytes,address,YES);
        },cancelled,error);
        if(!tree)return nil;
        // The builder already matched Apple's root. Authenticate readback once
        // from the final disk, not here and again in the snapshot planner.
        verified=!fsync(fd);
        if(!verified)return Failure(error,cancelled&&cancelled()?ECANCELED:EIO,
            @"The staged Big Sur filesystem tree could not be finished.");
        NSMutableDictionary *result=[tree mutableCopy];
        result[@"ObjectIDs"]=plan[@"ObjectIDs"];
        return result;
    } @finally {
        close(fd);
        if(!verified)unlink(output.fileSystemRepresentation);
    }
}
