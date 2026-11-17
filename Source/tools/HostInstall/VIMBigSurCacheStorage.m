#import "VIMBigSurCacheStorage.h"
#import "VIMBigSurMtree.h"
#import <CommonCrypto/CommonDigest.h>
#include <compression.h>
#include <stdio.h>

enum { Block=4096, Chunk=65536, CacheCount=2 };
typedef struct {uint64_t logical,length,physical;} CacheExtent;
typedef struct {uint64_t offset;uint32_t length;uint8_t digest[CC_SHA256_DIGEST_LENGTH];} CacheDigest;
typedef struct {
    uint64_t owner,stream,compressed,logical;
    __unsafe_unretained NSMutableData *extents,*digests;
} Cache;

static uint16_t U16(const uint8_t *p) {return (uint16_t)p[0]|(uint16_t)p[1]<<8;}
static uint32_t U32(const uint8_t *p) {return (uint32_t)U16(p)|(uint32_t)U16(p+2)<<16;}
static uint64_t U64(const uint8_t *p) {return (uint64_t)U32(p)|(uint64_t)U32(p+4)<<32;}
static void Put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static BOOL Failure(NSError **error,int code,NSString *description) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurCacheStorage" code:code
        userInfo:@{NSLocalizedDescriptionKey:description}];
    return NO;
}

static int CompareOffset(const void *a,const void *b) {
    uint64_t x,y;memcpy(&x,a,sizeof(x));memcpy(&y,b,sizeof(y));
    return x<y?-1:x>y;
}

// Failure-only metadata: distinguish missing storage from a decoder or signed
// content mismatch without logging file contents or emitting per-page noise.
static BOOL CacheProblem(const char *step,uint64_t offset,uint64_t observed,uint64_t expected) {
    fprintf(stderr,"Big Sur cache verification: %s at 0x%llx (observed %llu, expected %llu).\n",
        step,(unsigned long long)offset,(unsigned long long)observed,(unsigned long long)expected);
    return NO;
}

static BOOL ReadFork(const CacheExtent *extents,size_t count,size_t *index,uint64_t position,uint8_t *out,size_t length,
    BOOL (^read)(uint64_t,void *,size_t)) {
    while(length) {
        while(*index<count&&position>=extents[*index].logical+extents[*index].length)(*index)++;
        if(*index>=count||position<extents[*index].logical)return NO;
        CacheExtent e=extents[*index];uint64_t offset=position-e.logical;
        size_t n=(size_t)MIN(e.length-offset,length);
        if(!read(e.physical*Block+offset,out,n))return NO;
        position+=n;length-=n;out+=n;
    }
    return YES;
}

static BOOL VerifyCache(Cache cache,uint64_t blocks,BOOL (^read)(uint64_t,void *,size_t),BOOL (^cancelled)(void)) {
    size_t extentCount=cache.extents.length/sizeof(CacheExtent),digestCount=cache.digests.length/sizeof(CacheDigest);
    if(!extentCount||!digestCount||!cache.compressed||cache.compressed>UINT32_MAX||!cache.logical||cache.logical>(UINT64_C(4)<<30))
        return CacheProblem("stream geometry",0,cache.logical,cache.compressed);
    CacheExtent *extents=cache.extents.mutableBytes;
    CacheDigest *digests=cache.digests.mutableBytes;
    qsort(extents,extentCount,sizeof(*extents),CompareOffset);
    qsort(digests,digestCount,sizeof(*digests),CompareOffset);
    uint64_t covered=0;
    for(size_t i=0;i<extentCount;i++) {
        CacheExtent e=extents[i];
        if(e.logical!=covered||!e.length||(e.length&(Block-1))||!e.physical||e.physical>=blocks||
            e.length/Block>blocks-e.physical||e.length>UINT64_MAX-covered)
            return CacheProblem("extent geometry",e.physical*Block,e.logical,covered);
        covered+=e.length;
    }
    if(covered<cache.compressed)return CacheProblem("extent coverage",0,covered,cache.compressed);
    size_t chunks=(size_t)((cache.logical+Chunk-1)/Chunk),tableLength=(chunks+1)*sizeof(uint32_t);
    if(tableLength>=cache.compressed)return CacheProblem("table geometry",0,tableLength,cache.compressed);
    NSMutableData *table=[NSMutableData dataWithLength:tableLength];
    size_t extent=0,digest=0;
    if(!ReadFork(extents,extentCount,&extent,0,table.mutableBytes,tableLength,read))
        return CacheProblem("table read",0,0,tableLength);
    const uint8_t *offsets=table.bytes;
    if(U32(offsets)!=tableLength)return CacheProblem("first slot",0,U32(offsets),tableLength);
    if(U32(offsets+4*chunks)!=cache.compressed)
        return CacheProblem("final slot",chunks*Chunk,U32(offsets+4*chunks),cache.compressed);
    uint8_t frame[12+Chunk+1+4],output[Chunk+1];
    void *scratch=malloc(compression_decode_scratch_buffer_size(COMPRESSION_LZFSE)?:1);
    if(!scratch)return NO;
    @try {
        for(size_t i=0;i<chunks;i++) {
            if(!(i&127)&&cancelled&&cancelled())return NO;
            uint32_t start=U32(offsets+i*4),end=U32(offsets+(i+1)*4);
            uint64_t logical=(uint64_t)i*Chunk;
            size_t wanted=(size_t)MIN(cache.logical-logical,Chunk);
            // The final LZVN slot can retain a full 64 KiB allocation even
            // when its logical tail is shorter (including Monterey caches).
            if(end<=start||end>cache.compressed||end-start>Chunk+1)
                return CacheProblem("slot geometry",logical,end,start);
            size_t payloadSize=end-start;uint8_t *payload=frame+12;
            if(!ReadFork(extents,extentCount,&extent,start,payload,payloadSize,read))
                return CacheProblem("slot read",logical,0,payloadSize);
            const uint8_t *bytes;
            if(payload[0]==6&&payloadSize==wanted+1)bytes=payload+1;
            else {
                // LZVN may leave zeros after its eight-byte EOS. Apple's
                // LZFSE decoder expects the wrapper to end at that EOS.
                size_t tail=payloadSize;
                while(tail&&!payload[tail-1])tail--;
                if(tail&&payload[tail-1]==6&&tail+7<payloadSize)payloadSize=tail+7;
                memcpy(frame,"bvxn",4);Put32(frame+4,(uint32_t)wanted);Put32(frame+8,(uint32_t)payloadSize);
                memcpy(payload+payloadSize,"bvx$",4);
                size_t decoded=compression_decode_buffer(output,sizeof(output),frame,12+payloadSize+4,scratch,COMPRESSION_LZFSE);
                if(decoded!=wanted)return CacheProblem("slot decoding",logical,decoded,wanted);
                bytes=output;
            }
            for(size_t off=0;off<wanted;) {
                if(digest>=digestCount)return CacheProblem("missing digest",logical+off,digest,digestCount);
                CacheDigest d=digests[digest++];uint8_t actual[CC_SHA256_DIGEST_LENGTH];
                size_t remaining=wanted-off,covered=(remaining+Block-1)&~(size_t)(Block-1);
                if(d.offset!=logical+off||!d.length||d.length>MIN(4*Block,covered))
                    return CacheProblem("digest geometry",logical+off,d.offset,d.length);
                if(d.length>remaining) {
                    // Signed file digests include zeros after the last
                    // partial 4 KiB logical page, not compressed slot bytes.
                    uint8_t tail[4*Block]={0};
                    memcpy(tail,bytes+off,remaining);
                    CC_SHA256(tail,d.length,actual);
                } else CC_SHA256(bytes+off,d.length,actual);
                if(memcmp(d.digest,actual,sizeof(actual)))return CacheProblem("signed bytes",logical+off,0,d.length);
                off+=d.length;
            }
        }
        return digest==digestCount||CacheProblem("extra digests",cache.logical,digest,digestCount);
    } @finally {free(scratch);}
}

static BOOL VerifyPlain(Cache stream,uint64_t blocks,BOOL (^read)(uint64_t,void *,size_t),BOOL (^cancelled)(void)) {
    size_t count=stream.extents.length/sizeof(CacheExtent),hashes=stream.digests.length/sizeof(CacheDigest);
    if(!count||!hashes||!stream.logical||stream.logical>UINT64_MAX-(Block-1))return NO;
    CacheExtent *extents=stream.extents.mutableBytes;
    CacheDigest *digests=stream.digests.mutableBytes;
    qsort(extents,count,sizeof(*extents),CompareOffset);
    qsort(digests,hashes,sizeof(*digests),CompareOffset);
    uint64_t covered=0,total=(stream.logical+Block-1)&~(uint64_t)(Block-1);
    for(size_t i=0;i<count;i++) {
        CacheExtent e=extents[i];
        if(e.logical!=covered||!e.length||(e.length&(Block-1))||!e.physical||e.physical>=blocks||
           e.length/Block>blocks-e.physical||e.length>UINT64_MAX-covered)return NO;
        covered+=e.length;
    }
    if(covered<total)return NO;
    size_t extent=0;uint64_t position=0;
    for(size_t i=0;i<hashes;i++) {
        if(!(i&1023)&&cancelled&&cancelled())return NO;
        CacheDigest d=digests[i];uint8_t bytes[4*Block]={0},actual[CC_SHA256_DIGEST_LENGTH];
        if(d.offset!=position||!d.length||(d.length&(Block-1))||d.length>sizeof(bytes)||d.length>total-position)return NO;
        size_t take=(size_t)MIN(d.length,stream.logical-position);
        if(!ReadFork(extents,count,&extent,position,bytes,take,read))return NO;
        CC_SHA256(bytes,d.length,actual);
        if(memcmp(d.digest,actual,sizeof(actual)))return CacheProblem("signed file bytes",position,stream.stream,d.length);
        position+=d.length;
    }
    return position==total;
}

BOOL VIMVerifyBigSurSignedFileData(uint64_t blocks,
    BOOL (^visitTree)(VIMBigSurSealRecord),BOOL (^visitExtents)(VIMBigSurExtentVisitor),
    BOOL (^read)(uint64_t,void *,size_t),BOOL (^cancelled)(void),NSError **error) {
    if(!blocks||!visitTree||!visitExtents||!read)return NO;
    NSMutableDictionary<NSNumber *,NSMutableData *> *digests=[NSMutableDictionary dictionary],*extents=[NSMutableDictionary dictionary];
    NSMutableDictionary<NSNumber *,NSNumber *> *plain=[NSMutableDictionary dictionary];
    NSMutableDictionary<NSNumber *,NSArray<NSNumber *> *> *forks=[NSMutableDictionary dictionary],*compressed=[NSMutableDictionary dictionary];
    const uint64_t mask=(UINT64_C(1)<<60)-1,offsetMask=(UINT64_C(1)<<56)-1;
    BOOL listed=visitTree(^BOOL(const uint8_t *key,size_t k,const uint8_t *value,size_t v){
        if(k<8)return NO;
        uint64_t tag=U64(key),id=tag&mask,kind=tag>>60;
        if(kind==13) {
            if(k!=16||v!=35||value[2]!=32||U64(key+8)>>56!=1||!U16(value)||U16(value)>4)return NO;
            CacheDigest digest={.offset=U64(key+8)&offsetMask,.length=(uint32_t)U16(value)*Block};
            memcpy(digest.digest,value+3,sizeof(digest.digest));
            if(!digests[@(id)])digests[@(id)]=[NSMutableData data];
            [digests[@(id)] appendBytes:&digest length:sizeof(digest)];
        } else if(kind==3) {
            // j_inode_val_t followed by its aligned optional xfields.
            if(v==92)return YES;
            if(v<96)return NO;
            size_t count=U16(value+92),cursor=96+count*4;
            if(cursor>v)return NO;
            for(size_t i=0;i<count;i++) {
                size_t length=U16(value+96+i*4+2);
                if(length>v-cursor)return NO;
                if(value[96+i*4]==8) {
                    if(length!=40)return NO;
                    plain[@(U64(value+8))]=@(U64(value+cursor));
                }
                cursor+=(length+7)&~(size_t)7;
            }
        } else if(kind==4) {
            static const char resource[]="com.apple.ResourceFork",decmpfs[]="com.apple.decmpfs";
            if(k==10+sizeof(resource)&&!memcmp(key+10,resource,sizeof(resource))&&(U16(value)&3)==1) {
                if(v!=52)return NO;
                forks[@(U64(value+4))]=@[@(id),@(U64(value+12))];
            } else if(k==10+sizeof(decmpfs)&&!memcmp(key+10,decmpfs,sizeof(decmpfs))) {
                if(v<20||(U16(value)&3)!=2||U16(value+2)!=v-4||memcmp(value+4,"fpmc",4))return NO;
                compressed[@(id)]=@[@(U32(value+8)),@(U64(value+12))];
            }
        }
        return YES;
    });
    if(listed&&digests.count)listed=visitExtents(^BOOL(uint64_t stream,uint64_t logical,uint64_t length,uint64_t physical){
        NSNumber *id=@(stream);
        if(digests[id]) {
            if(!extents[id])extents[id]=[NSMutableData data];
            CacheExtent extent={logical,length,physical};
            [extents[id] appendBytes:&extent length:sizeof(extent)];
        }
        return YES;
    });else listed=NO;
    if(!listed)return Failure(error,cancelled&&cancelled()?ECANCELED:EBADMSG,@"Could not read the signed System file data records.");
    // Follow physical order, rather than turning the verification into random
    // disk seeks. No mounted VFS, original-VM files or new manifests are used.
    NSArray<NSNumber *> *order=[digests.allKeys sortedArrayUsingComparator:^NSComparisonResult(NSNumber *a,NSNumber *b){
        const CacheExtent *x=extents[a].bytes,*y=extents[b].bytes;
        uint64_t p=x?x->physical:0,q=y?y->physical:0;
        return p<q?NSOrderedAscending:p>q?NSOrderedDescending:NSOrderedSame;
    }];
    BOOL valid=YES;
    for(NSNumber *id in order) @autoreleasepool {
        if(cancelled&&cancelled()){valid=NO;break;}
        NSArray *fork=forks[id],*compression=fork?compressed[fork[0]]:nil;
        Cache stream={.stream=id.unsignedLongLongValue,.digests=digests[id],.extents=extents[id],
            .logical=[plain[id] unsignedLongLongValue]};
        if(compression) {
            stream.compressed=[fork[1] unsignedLongLongValue];stream.logical=[compression[1] unsignedLongLongValue];
            valid=[compression[0] unsignedIntValue]==8&&VerifyCache(stream,blocks,read,cancelled);
        } else valid=VerifyPlain(stream,blocks,read,cancelled);
        if(!valid) {
            fprintf(stderr,"Signed System data stream 0x%llx could not be authenticated.\n",(unsigned long long)stream.stream);
            break;
        }
    }
    // Publish NSError outside the per-file pool: an autoreleasing out
    // parameter must survive until ARC performs the caller's writeback.
    if(!valid)return Failure(error,cancelled&&cancelled()?ECANCELED:EBADMSG,
        @"The installed system’s file data could not be verified against Apple’s signed original.");
    return YES;
}

BOOL VIMVerifyBigSurIntelCaches(NSURL *mtree,uint64_t blocks,
    BOOL (^visitTree)(VIMBigSurSealRecord),BOOL (^visitExtents)(VIMBigSurExtentVisitor),
    BOOL (^read)(uint64_t,void *,size_t),BOOL (^cancelled)(void),NSError **error) {
    if(!mtree||!blocks||!visitTree||!visitExtents||!read)return NO;
    static const char *const names[]={
        "System/Library/dyld/dyld_shared_cache_x86_64","System/Library/dyld/dyld_shared_cache_x86_64h"
    };
    // An actual array outside the block keeps the two borrowed buffers alive
    // for the bounded traversal, without allocating per unrelated record.
    __attribute__((objc_precise_lifetime)) NSArray *digests=@[[NSMutableData data],[NSMutableData data]];
    __attribute__((objc_precise_lifetime)) NSArray *extents=@[[NSMutableData data],[NSMutableData data]];
    Cache storage[CacheCount]={0};Cache *caches=storage;
    for(size_t i=0;i<CacheCount;i++){caches[i].digests=digests[i];caches[i].extents=extents[i];}
    uint64_t stamp=0;
    if(!VIMBigSurVisitCanonicalTree(mtree,^BOOL(NSData *path,VIMBigSurCanonicalEntry entry){
        for(size_t i=0;i<CacheCount;i++) {
            size_t length=strlen(names[i]);
            if(path.length!=length||memcmp(path.bytes,names[i],length))continue;
            if(caches[i].owner||entry.directoryType!=8||!entry.inode||!entry.externalStream)return NO;
            caches[i].owner=entry.inode;caches[i].stream=entry.externalStream;
        }
        return YES;
    },&stamp,error))return NO;
    if(!caches[0].owner||!caches[1].owner||caches[0].owner==caches[1].owner||caches[0].stream==caches[1].stream)
        return Failure(error,EBADMSG,@"The signed Big Sur System does not describe both Intel caches.");
    const uint64_t mask=(UINT64_C(1)<<60)-1,offsetMask=(UINT64_C(1)<<56)-1;
    BOOL listed=visitTree(^BOOL(const uint8_t *key,size_t k,const uint8_t *value,size_t v){
        if(k<8)return NO;
        uint64_t tag=U64(key),id=tag&mask,kind=tag>>60;
        for(size_t i=0;i<CacheCount;i++) {
            Cache *cache=&caches[i];
            if(kind==13&&id==cache->stream) {
                if(k!=16||v!=35||value[2]!=32||U64(key+8)>>56!=1||!U16(value)||U16(value)>4)return NO;
                CacheDigest digest={.offset=U64(key+8)&offsetMask,.length=(uint32_t)U16(value)*Block};
                memcpy(digest.digest,value+3,sizeof(digest.digest));
                [cache->digests appendBytes:&digest length:sizeof(digest)];
            } else if(kind==4&&id==cache->owner) {
                static const char resource[]="com.apple.ResourceFork",decmpfs[]="com.apple.decmpfs";
                if(k==10+sizeof(resource)&&!memcmp(key+10,resource,sizeof(resource))) {
                    if(cache->compressed||v!=52||(U16(value)&3)!=1||U64(value+4)!=cache->stream)return NO;
                    cache->compressed=U64(value+12);
                } else if(k==10+sizeof(decmpfs)&&!memcmp(key+10,decmpfs,sizeof(decmpfs))) {
                    if(cache->logical||v!=20||(U16(value)&3)!=2||U16(value+2)!=16||memcmp(value+4,"fpmc",4)||U32(value+8)!=8)return NO;
                    cache->logical=U64(value+12);
                }
            }
        }
        return YES;
    });
    if(listed)listed=visitExtents(^BOOL(uint64_t stream,uint64_t logical,uint64_t length,uint64_t physical) {
        for(size_t i=0;i<CacheCount;i++)if(caches[i].stream==stream) {
            CacheExtent extent={logical,length,physical};
            [caches[i].extents appendBytes:&extent length:sizeof(extent)];
        }
        return YES;
    });
    if(!listed)return Failure(error,cancelled&&cancelled()?ECANCELED:EBADMSG,@"Could not read the authenticated Big Sur cache extents.");
    for(size_t i=0;i<CacheCount;i++)if(!VerifyCache(caches[i],blocks,read,cancelled))
        return Failure(error,cancelled&&cancelled()?ECANCELED:EBADMSG,
            [NSString stringWithFormat:@"Big Sur’s %s cache does not match the signed file digests.",strrchr(names[i],'/')+1]);
    return YES;
}
