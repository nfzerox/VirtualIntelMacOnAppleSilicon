#import "VIMAPFSCheckpoint.h"
#import "VIMHFSCatalog.h"
#import "VIMDiskImageIO.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

enum { Block=4096, MapEntry=40, MapStart=40, MapCapacity=(Block-MapStart)/MapEntry };
static uint32_t U32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t U64(const uint8_t *p) {return U32(p)|(uint64_t)U32(p+4)<<32;}
static void Put32(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static void Put64(uint8_t *p,uint64_t v) {Put32(p,(uint32_t)v);Put32(p+4,(uint32_t)(v>>32));}
static uint64_t Checksum(const uint8_t *p) {
    uint64_t a=0,b=0;
    for(unsigned i=8;i<Block;i+=4){a+=U32(p+i);b+=a;}
    uint64_t first=UINT32_MAX-(a+b)%UINT32_MAX,second=UINT32_MAX-(a+first)%UINT32_MAX;
    return first|(second<<32);
}
static BOOL Object(NSData *block,uint32_t type) {
    const uint8_t *p=block.bytes;
    return block.length==Block&&(U32(p+24)&0xffff)==type&&U64(p)&&U64(p)==Checksum(p);
}
static id Invalid(NSError **error) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.APFSCheckpoint" code:1
        userInfo:@{NSLocalizedDescriptionKey:@"The detached APFS checkpoint has an unsupported or inconsistent layout."}];
    return nil;
}

typedef struct {uint64_t base;uint32_t count,next,index,length;} Ring;
static BOOL ValidRing(Ring ring,uint64_t blocks,uint32_t minimum) {
    return ring.base&&ring.base<blocks&&ring.count<=blocks-ring.base&&ring.next<ring.count&&
        ring.index<ring.count&&ring.length>=minimum&&ring.length<=ring.count;
}
static uint32_t RingSlot(Ring ring,uint32_t delta) {return (uint32_t)(((uint64_t)ring.index+delta)%ring.count);}
static BOOL NewWindow(Ring ring,uint32_t length,uint32_t *start) {
    if(!length||length>ring.count-ring.length)return NO;
    *start=length<=ring.count-ring.next?ring.next:0;
    for(uint32_t i=0;i<ring.length;i++) {
        uint32_t slot=RingSlot(ring,i);
        if(slot>=*start&&slot-*start<length)return NO;
    }
    return YES;
}
static NSData *Sealed(NSMutableData *block) {
    Put64(block.mutableBytes,Checksum(block.bytes));return block;
}

@implementation VIMAPFSCheckpoint {
    VIMDiskImageIO *_image;
    uint64_t _offset;
    Ring _descriptors,_objects;
    NSMutableDictionary<NSArray<NSNumber *> *,NSData *> *_mappings;
    NSMutableArray<NSArray<NSNumber *> *> *_order;
}

- (instancetype)initWithRawImage:(NSURL *)image error:(NSError **)error {
    self=[super init];if(!self)return nil;
    uint64_t size=0;
    if(!VIMGetAPFSPartition(image,&_offset,&size,error))return nil;
    _image=[[VIMDiskImageIO alloc] initWithURL:image writable:NO error:error];
    if(!_image||_offset>_image.size||size>_image.size-_offset||size%Block)return Invalid(error);
    _blockCount=size/Block;
    NSData *anchor=[self readBlock:0];const uint8_t *p=anchor.bytes;
    if(!Object(anchor,1)||memcmp(p+32,"NXSB\0\x10\0\0",8)||U64(p+40)!=_blockCount)return Invalid(error);
    uint32_t count=U32(p+104);uint64_t base=U64(p+112);
    if(!count||count>65536||!base||base>=_blockCount||count>_blockCount-base)return Invalid(error);
    NSData *latest=anchor;
    for(uint64_t block=base;block<base+count;block++) {
        NSData *candidate=[self readBlock:block];const uint8_t *data=candidate.bytes;
        if(!candidate)return Invalid(error);
        if(!memcmp(data+32,p+32,8)&&!memcmp(data+72,p+72,16)&&
            U64(data+16)>U64((const uint8_t *)latest.bytes+16)&&Object(candidate,1))latest=candidate;
    }
    _superblock=latest;p=latest.bytes;
    _transactionID=U64(p+16);_nextTransactionID=U64(p+96);
    _descriptors=(Ring){U64(p+112),U32(p+104),U32(p+128),U32(p+136),U32(p+140)};
    _objects=(Ring){U64(p+120),U32(p+108),U32(p+132),U32(p+144),U32(p+148)};
    if(_descriptors.count!=count||_descriptors.base!=base||_nextTransactionID<=_transactionID||
        !ValidRing(_descriptors,_blockCount,2)||!ValidRing(_objects,_blockCount,1))return Invalid(error);
    _mappings=[NSMutableDictionary dictionary];_order=[NSMutableArray array];
    NSMutableSet *unmapped=[NSMutableSet set];
    for(uint32_t i=0;i<_objects.length;i++)[unmapped addObject:@(_objects.base+RingSlot(_objects,i))];
    for(uint32_t i=0;i<_descriptors.length-1;i++) {
        NSData *map=[self readBlock:_descriptors.base+RingSlot(_descriptors,i)];const uint8_t *m=map.bytes;
        if(!Object(map,12)||U64(m+16)!=_transactionID||U32(m+32)!=(i==_descriptors.length-2)||U32(m+36)>MapCapacity)return Invalid(error);
        for(uint32_t j=0;j<U32(m+36);j++) {
            const uint8_t *entry=m+MapStart+j*MapEntry;
            NSArray *key=@[@(U64(entry+16)),@(U64(entry+24))];NSNumber *block=@(U64(entry+32));
            if(U32(entry+8)!=Block||_mappings[key]||![unmapped containsObject:block])return Invalid(error);
            [unmapped removeObject:block];[_order addObject:key];_mappings[key]=[NSData dataWithBytes:entry length:MapEntry];
        }
    }
    NSData *ring=[self readBlock:_descriptors.base+RingSlot(_descriptors,_descriptors.length-1)];
    const uint8_t *r=ring.bytes;
    if(unmapped.count||!Object(ring,1)||U64(r+16)!=_transactionID||memcmp(r+32,p+32,0x3d8-32))return Invalid(error);
    return self;
}

- (NSData *)readBlock:(uint64_t)address {
    if(address>=_blockCount)return nil;
    NSMutableData *result=[NSMutableData dataWithLength:Block];uint8_t *p=result.mutableBytes;
    return [_image readAt:_offset+address*Block bytes:p length:Block]?result:nil;
}

- (NSData *)readEphemeral:(uint64_t)object volume:(uint64_t)volume {
    NSData *entry=_mappings[@[@(volume),@(object)]];const uint8_t *map=entry.bytes;
    if(!map)return nil;
    NSData *block=[self readBlock:U64(map+32)];const uint8_t *p=block.bytes;
    return Object(block,U32(map)&0xffff)&&U64(p+8)==object&&U64(p+16)==_transactionID&&
        U32(p+24)==U32(map)&&U32(p+28)==U32(map+4)?block:nil;
}

- (NSArray<NSDictionary *> *)successorWithObjects:(NSDictionary<NSArray<NSNumber *> *,NSData *> *)replacements
    discarding:(NSSet<NSArray<NSNumber *> *> *)removed superblock:(NSData *)superblock error:(NSError **)error {
    if(_nextTransactionID==UINT64_MAX)return Invalid(error);
    NSMutableArray *order=[_order mutableCopy];
    for(NSArray *key in removed) {
        if(!_mappings[key]||replacements[key])return Invalid(error);
        [order removeObject:key];
    }
    NSMutableArray *added=[NSMutableArray array];
    for(NSArray *key in replacements) {
        NSData *block=replacements[key],*old=_mappings[key];const uint8_t *bytes=block.bytes,*mapping=old.bytes;
        if(key.count!=2||block.length!=Block||U64(bytes+8)!=[key[1] unsignedLongLongValue]||U32(bytes+24)>>30!=2||
           (mapping&&(U32(mapping)!=U32(bytes+24)||U32(mapping+4)!=U32(bytes+28))))return Invalid(error);
        if(!old)[added addObject:key];
    }
    [added sortUsingComparator:^NSComparisonResult(NSArray *a,NSArray *b) {
        NSComparisonResult cmp=[a[0] compare:b[0]];return cmp==NSOrderedSame?[a[1] compare:b[1]]:cmp;
    }];
    [order addObjectsFromArray:added];
    if(!order.count||order.count>UINT32_MAX-MapCapacity)return Invalid(error);
    uint32_t objects=(uint32_t)order.count,maps=(objects+MapCapacity-1)/MapCapacity;
    uint32_t firstObject,firstMap;
    if(!NewWindow(_objects,objects,&firstObject)||!NewWindow(_descriptors,maps+1,&firstMap))return Invalid(error);
    NSMutableArray *writes=[NSMutableArray array];NSMutableArray<NSData *> *mapping=[NSMutableArray array];
    for(uint32_t i=0;i<objects;i++) {
        NSArray *key=order[i];NSData *prior=_mappings[key];
        NSData *bytes=replacements[key]?:[self readEphemeral:[key[1] unsignedLongLongValue] volume:[key[0] unsignedLongLongValue]];
        if(!bytes)return Invalid(error);
        NSMutableData *block=[bytes mutableCopy],*entry=prior?[prior mutableCopy]:[NSMutableData dataWithLength:MapEntry];
        uint8_t *p=block.mutableBytes,*m=entry.mutableBytes;
        uint64_t address=_objects.base+firstObject+i;
        if(!prior) {
            Put32(m,U32(p+24));Put32(m+4,U32(p+28));Put32(m+8,Block);
            Put64(m+16,[key[0] unsignedLongLongValue]);Put64(m+24,U64(p+8));
        }
        Put64(m+32,address);[mapping addObject:entry];
        Put64(p+16,_nextTransactionID);[writes addObject:@{@"Block":@(address),@"Bytes":Sealed(block)}];
    }
    for(uint32_t i=0;i<maps;i++) {
        uint64_t address=_descriptors.base+firstMap+i;
        NSMutableData *block=[NSMutableData dataWithLength:Block];uint8_t *p=block.mutableBytes;
        uint32_t first=i*MapCapacity,count=MIN(MapCapacity,objects-first);
        Put64(p+8,address);Put64(p+16,_nextTransactionID);Put32(p+24,0x4000000c);Put32(p+32,i+1==maps);Put32(p+36,count);
        for(uint32_t j=0;j<count;j++)memcpy(p+MapStart+j*MapEntry,mapping[first+j].bytes,MapEntry);
        [writes addObject:@{@"Block":@(address),@"Bytes":Sealed(block)}];
    }
    NSMutableData *next=[(superblock?:_superblock) mutableCopy];uint8_t *p=next.mutableBytes;
    if(next.length!=Block||memcmp(p+32,(const uint8_t *)_superblock.bytes+32,16)||
        memcmp(p+72,(const uint8_t *)_superblock.bytes+72,16))return Invalid(error);
    Put64(p+16,_nextTransactionID);Put64(p+96,_nextTransactionID+1);
    Put32(p+128,(firstMap+maps+1)%_descriptors.count);Put32(p+132,(firstObject+objects)%_objects.count);
    Put32(p+136,firstMap);Put32(p+140,maps+1);Put32(p+144,firstObject);Put32(p+148,objects);
    NSData *commit=Sealed(next);
    [writes addObject:@{@"Block":@(_descriptors.base+firstMap+maps),@"Bytes":commit}];
    [writes addObject:@{@"Block":@0,@"Bytes":commit}];
    return writes.copy;
}
@end
