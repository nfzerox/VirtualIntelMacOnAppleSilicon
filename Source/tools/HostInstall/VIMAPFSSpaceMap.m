#import "VIMAPFSSpaceMap.h"
#import "VIMAPFSCheckpoint.h"

enum { Block=4096, ChunkSize=32 };
static uint16_t U16(const uint8_t *p) {return (uint16_t)p[0]|(uint16_t)p[1]<<8;}
static uint32_t U32(const uint8_t *p) {return U16(p)|(uint32_t)U16(p+2)<<16;}
static uint64_t U64(const uint8_t *p) {return U32(p)|(uint64_t)U32(p+4)<<32;}
static void Put16(uint8_t *p,uint16_t v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void Put32(uint8_t *p,uint32_t v) {Put16(p,(uint16_t)v);Put16(p+2,(uint16_t)(v>>16));}
static void Put64(uint8_t *p,uint64_t v) {Put32(p,(uint32_t)v);Put32(p+4,(uint32_t)(v>>32));}
static uint64_t Checksum(const uint8_t *p) {
    uint64_t a=0,b=0;
    for(unsigned i=8;i<Block;i+=4){a+=U32(p+i);b+=a;}
    uint64_t first=UINT32_MAX-(a+b)%UINT32_MAX,second=UINT32_MAX-(a+first)%UINT32_MAX;
    return first|(second<<32);
}
static BOOL Bit(const uint8_t *p,uint32_t n) {return (p[n>>3]&(1U<<(n&7)))!=0;}
static void SetBit(uint8_t *p,uint32_t n) {p[n>>3]|=(uint8_t)(1U<<(n&7));}
static id Invalid(NSError **error) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.APFSSpaceMap" code:1
        userInfo:@{NSLocalizedDescriptionKey:@"Could not plan space for the detached APFS boot or snapshot metadata."}];
    return nil;
}

typedef struct {uint64_t first,length,best,bestLength;} FreeRuns;
static void Run(FreeRuns *s,uint64_t first,uint64_t length) {
    if(s->length&&s->first+s->length==first)s->length+=length;
    else {s->first=first;s->length=length;}
    if(s->length>s->bestLength){s->best=s->first;s->bestLength=s->length;}
}

static NSData *ReadCIB(VIMAPFSCheckpoint *disk,uint64_t address,uint32_t index) {
    NSData *block=[disk readBlock:address];const uint8_t *p=block.bytes;
    if(block.length!=Block||(U32(p+24)&0xffff)!=7||U64(p)!=Checksum(p)||U32(p+32)!=index)return nil;
    uint32_t count=U32(p+36)&0xfffff;
    return count&&count<=(Block-40)/ChunkSize?block:nil;
}

typedef struct {uint32_t cursor,capacity;uint64_t base;uint8_t *bitmap;} InternalPool;
static uint64_t AllocateInternal(InternalPool *pool,uint64_t old,NSMutableArray<NSNumber *> *retired) {
    while(pool->cursor<pool->capacity&&Bit(pool->bitmap,pool->cursor))pool->cursor++;
    if(pool->cursor==pool->capacity)return 0;
    if(old) {
        if(old<pool->base||old-pool->base>=pool->capacity||!Bit(pool->bitmap,(uint32_t)(old-pool->base)))return 0;
        [retired addObject:@(old)];
    }
    SetBit(pool->bitmap,pool->cursor);
    return pool->base+pool->cursor++;
}

NSDictionary *VIMPlanAPFSAllocation(VIMAPFSCheckpoint *disk,uint64_t requested,NSError **error) {
    const uint8_t *nx=disk.superblock.bytes;
    if(!requested||!nx)return Invalid(error);
    uint64_t object=U64(nx+152),blocks=disk.blockCount,xid=disk.nextTransactionID;
    NSMutableData *spaceman=[[disk readEphemeral:object volume:0] mutableCopy];uint8_t *sm=spaceman.mutableBytes;
    if(!sm||U32(sm+32)!=Block||U64(sm+48)!=blocks||U32(sm+68)||U64(sm+96))return Invalid(error);
    uint32_t width=U32(sm+36),cibs=U32(sm+64),cibOffset=U32(sm+80);
    uint64_t totalFree=U64(sm+72);
    if(!width||width>Block*8||!cibs||cibs>Block/8||!cibOffset||cibOffset>Block-cibs*8||requested>totalFree)return Invalid(error);
    FreeRuns runs={0};uint64_t cursor=0,observedFree=0,observedChunks=0;
    for(uint32_t i=0;i<cibs;i++) {
        NSData *cib=ReadCIB(disk,U64(sm+cibOffset+i*8),i);const uint8_t *p=cib.bytes;
        if(!p)return Invalid(error);
        for(uint32_t j=0;j<(U32(p+36)&0xfffff);j++) {
            const uint8_t *chunk=p+40+j*ChunkSize;
            uint64_t first=U64(chunk+8),bitmap=U64(chunk+24);
            uint32_t count=U32(chunk+16),free=U32(chunk+20);
            if(first!=cursor||!count||count>width||count>blocks-cursor||free>count)return Invalid(error);
            if(!bitmap) {
                if(free!=count)return Invalid(error);
                Run(&runs,first,count);
            } else {
                NSData *bytes=[disk readBlock:bitmap];const uint8_t *bits=bytes.bytes;
                if(!bits)return Invalid(error);
                uint32_t local=0;
                for(uint32_t k=0;k<count;) {
                    if(Bit(bits,k)){k++;continue;}
                    uint32_t start=k;
                    while(k<count&&!Bit(bits,k))k++;
                    local+=k-start;Run(&runs,first+start,k-start);
                }
                if(local!=free)return Invalid(error);
            }
            cursor+=count;observedFree+=free;observedChunks++;
        }
    }
    if(cursor!=blocks||observedFree!=totalFree||observedChunks!=U64(sm+56)||!runs.best||runs.bestLength<requested)return Invalid(error);

    uint64_t poolCount=U64(sm+152),ring=U64(sm+168),base=U64(sm+176);
    uint32_t slots=U32(sm+164),xidOffset=U32(sm+324),indexOffset=U32(sm+328),nextOffset=U32(sm+332);
    uint16_t head=U16(sm+320),tail=U16(sm+322);
    if(U32(sm+160)!=1||!poolCount||poolCount>Block*8||!slots||slots>Block/2||
        head>=slots||tail>=slots||head==tail||!xidOffset||xidOffset>Block-8||!indexOffset||indexOffset>Block-2||
        !nextOffset||nextOffset>Block-2*slots||!ring||ring>=blocks||slots>blocks-ring||!base||base>=blocks||poolCount>blocks-base)return Invalid(error);
    uint16_t active=U16(sm+indexOffset),following=U16(sm+nextOffset+head*2);
    if(active==head||active==tail||active>=slots||following>=slots)return Invalid(error);
    NSMutableData *poolBytes=[[disk readBlock:ring+active] mutableCopy];
    if(!poolBytes)return Invalid(error);
    InternalPool pool={0,(uint32_t)poolCount,base,poolBytes.mutableBytes};
    NSMutableDictionary<NSNumber *,NSData *> *writes=[NSMutableDictionary dictionary];
    NSMutableArray<NSNumber *> *retired=[NSMutableArray array];
    uint64_t end=runs.best+requested;
    for(uint32_t i=0;i<cibs;i++) {
        uint64_t old=U64(sm+cibOffset+i*8);
        NSMutableData *cib=[ReadCIB(disk,old,i) mutableCopy];uint8_t *p=cib.mutableBytes;
        if(!p)return Invalid(error);
        BOOL changed=NO;
        for(uint32_t j=0;j<(U32(p+36)&0xfffff);j++) {
            uint8_t *chunk=p+40+j*ChunkSize;
            uint64_t first=U64(chunk+8),bitmap=U64(chunk+24);
            uint32_t count=U32(chunk+16),free=U32(chunk+20);
            if(first>=end||first+count<=runs.best)continue;
            uint32_t lower=(uint32_t)(MAX(runs.best,first)-first),upper=(uint32_t)(MIN(end,first+count)-first);
            if(upper-lower>free)return Invalid(error);
            NSMutableData *bits=bitmap?[[disk readBlock:bitmap] mutableCopy]:[NSMutableData dataWithLength:Block];
            if(!bits)return Invalid(error);
            uint8_t *map=bits.mutableBytes;
            for(uint32_t k=lower;k<upper;k++){if(Bit(map,k))return Invalid(error);SetBit(map,k);}
            uint64_t address=AllocateInternal(&pool,bitmap,retired);
            if(!address)return Invalid(error);
            writes[@(address)]=bits;
            Put64(chunk,xid);Put32(chunk+20,free-(upper-lower));Put64(chunk+24,address);changed=YES;
        }
        if(changed) {
            uint64_t address=AllocateInternal(&pool,old,retired);
            if(!address)return Invalid(error);
            Put64(p+8,address);Put64(p+16,xid);Put64(p,Checksum(p));writes[@(address)]=cib;
            Put64(sm+cibOffset+i*8,address);
        }
    }
    writes[@(ring+head)]=poolBytes;
    Put64(sm+72,totalFree-requested);Put64(sm+xidOffset,xid);Put16(sm+indexOffset,head);
    Put16(sm+320,following);Put16(sm+322,active);Put16(sm+nextOffset+head*2,UINT16_MAX);Put16(sm+nextOffset+tail*2,active);
    return @{@"Start":@(runs.best),@"Writes":writes.copy,@"SpacemanOID":@(object),@"Spaceman":spaceman,@"RetiredInternal":retired.copy};
}
