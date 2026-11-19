#import "VIMAPFSFirmlinks.h"
#import "VIMDiskImageIO.h"
#import "VIMBigSurCacheStorage.h"
#import "VIMBigSurFixups.h"
#import "VIMBigSurMtree.h"
#import "VIMBigSurPhysicalTree.h"
#import "VIMBigSurSealTree.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// Apple File System Reference: 4 KiB, single-store APFS; no encrypted or
// sealed fsroot trees. All integers on disk are little endian.
enum { Block=4096, Inode=3, Xattr=4, SiblingLink=5, SiblingMap=12, FirmlinkFlag=0x00800000 };
static const char ReservedName[]="com.apple.fs.firmlinj";
static const char FirmlinkName[]="com.apple.fs.firmlink";
static const uint64_t OIDMask=(UINT64_C(1)<<60)-1;

static uint16_t U16(const uint8_t *p) {
    return (uint16_t)p[0]|(uint16_t)p[1]<<8;
}
static uint32_t U32(const uint8_t *p) {
    return (uint32_t)U16(p)|(uint32_t)U16(p+2)<<16;
}
static uint64_t U64(const uint8_t *p) {
    return (uint64_t)U32(p)|(uint64_t)U32(p+4)<<32;
}
static void Set32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(i*8));
}
static void Set64(uint8_t *p,uint64_t value) {
    Set32(p,(uint32_t)value);Set32(p+4,(uint32_t)(value>>32));
}
static uint64_t Checksum(const uint8_t *data) {
    const uint64_t mod=UINT32_MAX;
    uint64_t a=0,b=0;
    for(unsigned i=8;i<Block;i+=4){a+=U32(data+i);b+=a;}
    uint64_t first=mod-(a+b)%mod,second=mod-(a+first)%mod;
    return first|(second<<32);
}

typedef struct {__strong VIMDiskImageIO *image;uint64_t offset,blocks;} Container;
static BOOL ReadBlock(Container c,uint64_t address,uint8_t *bytes) {
    if(address>=c.blocks)return NO;
    return [c.image readAt:c.offset+address*Block bytes:bytes length:Block];
}
static BOOL WriteBlock(Container c,uint64_t address,const uint8_t *bytes) {
    return address<c.blocks&&[c.image writeAt:c.offset+address*Block bytes:bytes length:Block];
}
static BOOL ValidObject(const uint8_t *block,uint16_t type) {
    return U16(block+24)==type&&U64(block)&&U64(block)==Checksum(block);
}
static BOOL ReadObject(Container c,uint64_t address,uint16_t type,uint8_t *block) {
    return ReadBlock(c,address,block)&&ValidObject(block,type);
}

static BOOL ReadActiveSuperblock(Container c,uint8_t *super) {
    if(!ReadObject(c,0,1,super)||memcmp(super+32,"NXSB",4)||U32(super+36)!=Block||U64(super+40)!=c.blocks)return NO;
    uint64_t start=U64(super+112);uint32_t count=U32(super+104);
    if(!count||count>65536||!start||start>=c.blocks||count>c.blocks-start)return NO;
    uint8_t block[Block];
    for(uint64_t i=start;i<start+count;i++) {
        if(!ReadBlock(c,i,block))return NO;
        if(!memcmp(block+32,super+32,8)&&!memcmp(block+72,super+72,16)&&
            U64(block+16)>U64(super+16)&&ValidObject(block,1))memcpy(super,block,Block);
    }
    return YES;
}

typedef uint64_t (^ResolveObject)(uint64_t);
typedef BOOL (^ReadRecord)(uint8_t *,uint64_t,BOOL,uint8_t *,size_t,uint8_t *,size_t);
static BOOL WalkTree(Container c,uint64_t root,ResolveObject resolve,uint16_t subtype,BOOL fixed,
                      ReadRecord record,void (^finish)(uint8_t *,uint64_t)) {
    NSMutableArray<NSNumber *> *pending=[NSMutableArray arrayWithObject:@(root)];
    NSMutableSet<NSNumber *> *seen=[NSMutableSet set];
    while(pending.count) @autoreleasepool {
        uint64_t oid=pending.lastObject.unsignedLongLongValue;[pending removeLastObject];
        uint64_t address=resolve?resolve(oid):oid;NSNumber *key=@(address);
        if(!address||[seen containsObject:key])return NO;
        [seen addObject:key];
        uint8_t node[Block];
        if(!ReadBlock(c,address,node)||!(ValidObject(node,2)||ValidObject(node,3))||U16(node+28)!=subtype)return NO;
        uint16_t flags=U16(node+32),level=U16(node+34),tableLength=U16(node+42);
        uint32_t count=U32(node+36),table=56+U16(node+40),keys=table+tableLength,values=Block-(flags&1?40:0);
        BOOL leaf=(flags&2)!=0;uint32_t stride=fixed?4:8;
        if(((flags&4)!=0)!=fixed||leaf!=(level==0)||keys>values||count>tableLength/stride)return NO;
        for(uint32_t i=0;i<count;i++) {
            const uint8_t *slot=node+table+i*stride;
            uint32_t k=keys+U16(slot),endOffset=U16(slot+(fixed?2:4));
            size_t keyLength=fixed?16:U16(slot+2),valueLength=fixed?(leaf?16:8):U16(slot+6);
            if(k>values||endOffset>values-keys||!keyLength||keyLength>values-k)return NO;
            uint32_t v=values-endOffset;
            if(!valueLength||valueLength>values-v||(!leaf&&valueLength!=8))return NO;
            if(!leaf)[pending addObject:@(U64(node+v))];
            if(!record(node,address,leaf,node+k,keyLength,node+v,valueLength)) {
                // Failure-only structure context; no file names or contents.
                fprintf(stderr,"Detached APFS: rejected tree record block=%llu subtype=%u leaf=%d key=%zu value=%zu tag=0x%llx\n",
                    address,subtype,leaf,keyLength,valueLength,keyLength>=8?U64(node+k):0);
                return NO;
            }
        }
        if(finish)finish(node,address);
    }
    return YES;
}

typedef struct {uint64_t xid,address;uint32_t flags,size;} MapRecord;
static ResolveObject ReadObjectMapWithMode(Container c,uint64_t address,uint64_t xid,BOOL headerless) {
    uint8_t map[Block];
    if(!ReadObject(c,address,11,map))return nil;
    NSMutableDictionary<NSNumber *,NSData *> *entries=[NSMutableDictionary dictionary];
    if(!WalkTree(c,U64(map+48),nil,11,YES,
        ^BOOL(__unused uint8_t *node,__unused uint64_t where,BOOL leaf,uint8_t *key,size_t keyLength,uint8_t *value,size_t valueLength){
            if(!leaf)return YES;
            if(keyLength!=16||valueLength!=16)return NO;
            uint64_t time=U64(key+8);
            if(time>xid)return YES;
            NSNumber *oid=@(U64(key));NSData *existing=entries[oid];
            MapRecord previous={0};if(existing)[existing getBytes:&previous length:sizeof(previous)];
            if(!existing||time>previous.xid) {
                MapRecord record={.xid=time,.address=U64(value+8),.flags=U32(value),.size=U32(value+4)};
                entries[oid]=[NSData dataWithBytes:&record length:sizeof(record)];
            }
            return YES;
        },nil))return nil;
    return ^uint64_t(uint64_t oid){
        MapRecord record={0};[entries[@(oid)] getBytes:&record length:sizeof(record)];
        return (record.flags&13)==(headerless?8U:0U)&&record.size==Block?record.address:0;
    };
}

static ResolveObject ReadObjectMap(Container c,uint64_t address,uint64_t xid) {
    return ReadObjectMapWithMode(c,address,xid,NO);
}

static int Compare(const uint8_t *a,size_t length,const char *b,size_t size) {
    int order=memcmp(a,b,MIN(length,size));if(order)return order;
    return length>size?1:length<size?-1:0;
}

static BOOL FindVolume(Container c,const uint8_t nx[Block],NSUUID *identity,uint16_t role,uint8_t volume[Block]) {
    uuid_t uuid;[identity getUUIDBytes:uuid];
    ResolveObject resolve=ReadObjectMap(c,U64(nx+160),U64(nx+16));
    if(!resolve)return NO;
    for(unsigned index=0;index<100;index++) {
        uint64_t oid=U64(nx+184+index*8);if(!oid)continue;
        if(!ReadObject(c,resolve(oid),13,volume)||memcmp(volume+32,"APSB",4))return NO;
        if(U16(volume+0x3c4)==role&&!memcmp(volume+240,uuid,sizeof(uuid)))return YES;
    }
    return NO;
}

static ResolveObject ReadUnsealedVolume(Container c,NSUUID *identity,uint16_t role,uint8_t volume[Block]) {
    uint8_t nx[Block];
    if(!ReadActiveSuperblock(c,nx)||!FindVolume(c,nx,identity,role,volume)||
       U64(volume+216)||(U64(volume+56)&0x20))return nil;
    return ReadObjectMap(c,U64(volume+128),U64(volume+16));
}

static BOOL Finalize(Container c,NSUUID *system,uint16_t role,NSDictionary<NSNumber *,NSData *> *destinations,
                     NSDictionary<NSNumber *,NSArray<NSNumber *> *> *permissions,BOOL signedFlags) {
    uint8_t volume[Block];
    ResolveObject resolve=ReadUnsealedVolume(c,system,role,volume);
    if(!resolve){fprintf(stderr,"Detached APFS metadata: cannot read unsealed volume role=0x%x\n",role);return NO;}
    NSMutableSet<NSNumber *> *inodes=[NSMutableSet set],*attributes=[NSMutableSet set];
    NSMutableSet<NSNumber *> *assigned=[NSMutableSet set];
    NSMutableSet<NSNumber *> *reservedRootless=[NSMutableSet set],*rootlessRange=[NSMutableSet set];
    NSMutableDictionary<NSNumber *,NSData *> *edits=[NSMutableDictionary dictionary];
    __block BOOL dirty=NO;
    BOOL walked=WalkTree(c,U64(volume+136),resolve,14,NO,
        ^BOOL(__unused uint8_t *node,__unused uint64_t address,BOOL leaf,uint8_t *key,size_t keyLength,uint8_t *value,size_t valueLength){
            if(keyLength<8)return NO;
            uint64_t tag=U64(key);NSNumber *oid=@(tag&OIDMask);NSData *expected=destinations[oid];
            NSArray<NSNumber *> *identity=permissions[oid];
            if(!expected&&!identity)return YES;
            uint64_t type=tag>>60;
            if(type==Xattr) {
                static const char reserved[]="com.apple.rootlesr",original[]="com.apple.rootless";
                if(keyLength<11||U16(key+8)!=keyLength-10)return NO;
                uint8_t *name=key+10;size_t length=keyLength-10;
                int relative=Compare(name,length,reserved,sizeof(reserved));
                // Keep B-tree ordering and reject a colliding native key.
                if(relative>0&&Compare(name,length,original,sizeof(original))<=0)[rootlessRange addObject:oid];
                if(!relative){[reservedRootless addObject:oid];memcpy(name,original,sizeof(original));dirty=YES;}
            }
            if(type==Inode&&leaf) {
                if(valueLength<82)return NO;
                if(identity) {
                    uint16_t mode=identity[2].unsignedShortValue;
                    uint32_t uid=identity[0].unsignedIntValue,gid=identity[1].unsignedIntValue;
                    if((U16(value+80)&S_IFMT)!=(mode&S_IFMT)||[assigned containsObject:oid]) {
                        fprintf(stderr,"Detached APFS metadata: inode=%llu role=0x%x mode=%o expected=%o duplicate=%d\n",
                            oid.unsignedLongLongValue,role,U16(value+80),mode,[assigned containsObject:oid]);return NO;
                    }
                    if(signedFlags) {
                        uint32_t previous=U32(value+68),flags=identity[3].unsignedIntValue;
                        // Compression must have been encoded by the filesystem
                        // tool already. Never set a flag to fake its presence.
                        if(((flags&FirmlinkFlag)!=0)!=(expected!=nil)||((previous^flags)&UF_COMPRESSED)||
                            (previous&FirmlinkFlag)) {
                            fprintf(stderr,"Detached APFS metadata: inode=%llu role=0x%x flags=0x%x expected=0x%x firmlink=%d\n",
                                oid.unsignedLongLongValue,role,previous,flags,expected!=nil);return NO;
                        }
                        if(previous!=flags){Set32(value+68,flags);dirty=YES;}
                    }
                    if(U32(value+72)!=uid||U32(value+76)!=gid||U16(value+80)!=mode) {
                        Set32(value+72,uid);Set32(value+76,gid);value[80]=(uint8_t)mode;value[81]=(uint8_t)(mode>>8);
                        dirty=YES;
                    }
                    [assigned addObject:oid];
                }
                if(expected) {
                    if(!S_ISDIR(U16(value+80))||[inodes containsObject:oid]||
                        ((U32(value+68)&FirmlinkFlag)!=0)!=signedFlags)return NO;
                    if(!signedFlags)Set32(value+68,U32(value+68)|FirmlinkFlag);
                    [inodes addObject:oid];dirty=YES;
                }
            }
            if(type!=Xattr||!expected)return YES;
            if(keyLength<11||U16(key+8)!=keyLength-10)return NO;
            uint8_t *name=key+10;size_t length=keyLength-10;
            int relative=Compare(name,length,ReservedName,sizeof(ReservedName));
            if(relative>0&&Compare(name,length,FirmlinkName,sizeof(FirmlinkName))<=0)return NO;
            if(relative!=0)return YES;
            if(leaf) {
                if([attributes containsObject:oid]||valueLength!=4+expected.length||U16(value)!=2||
                   U16(value+2)!=expected.length||memcmp(value+4,expected.bytes,expected.length))return NO;
                [attributes addObject:oid];
            }
            // Index separators can repeat the minimum key in a child.
            memcpy(name,FirmlinkName,sizeof(FirmlinkName));dirty=YES;return YES;
        },^(uint8_t *node,uint64_t address){
            if(!dirty)return;
            Set64(node,Checksum(node));edits[@(address)]=[NSData dataWithBytes:node length:Block];dirty=NO;
        });
    NSSet *requested=[NSSet setWithArray:destinations.allKeys?:@[]];
    [reservedRootless intersectSet:rootlessRange];
    if(!walked||![inodes isEqual:requested]||![attributes isEqual:requested]||
       reservedRootless.count||![assigned isEqual:[NSSet setWithArray:permissions.allKeys?:@[]]]) {
        fprintf(stderr,"Detached APFS metadata: role=0x%x walked=%d firmlinks=%lu/%lu attributes=%lu assigned=%lu/%lu rootless-collisions=%lu\n",
            role,walked,(unsigned long)inodes.count,(unsigned long)requested.count,
            (unsigned long)attributes.count,(unsigned long)assigned.count,(unsigned long)permissions.count,
            (unsigned long)reservedRootless.count);return NO;
    }
    for(NSNumber *address in edits)if(!WriteBlock(c,address.unsignedLongLongValue,edits[address].bytes)) {
        fprintf(stderr,"Detached APFS metadata: role=0x%x block=%llu write error=%d\n",role,address.unsignedLongLongValue,errno);return NO;
    }
    BOOL synced=[c.image synchronize];
    if(!synced)fprintf(stderr,"Detached APFS metadata: role=0x%x sync error=%d\n",role,errno);
    return synced;
}

static BOOL FinalizeAPFSVolume(NSURL *raw,uint64_t offset,uint64_t size,NSUUID *volume,uint16_t role,
                              NSDictionary<NSNumber *,NSData *> *destinations,
                              NSDictionary<NSNumber *,NSArray<NSNumber *> *> *permissions,BOOL signedFlags,NSError **error) {
    VIMDiskImageIO *image=[[VIMDiskImageIO alloc] initWithURL:raw writable:YES error:error];
    if(!image){fprintf(stderr,"Detached APFS metadata: role=0x%x image open error=%d\n",role,errno);return NO;}
    BOOL ok=image&&volume&&(destinations.count||permissions.count)&&
        (!destinations.count||role==VIMAPFSSystemRole)&&
        !(offset%Block)&&size&&!(size%Block)&&offset<=image.size&&size<=image.size-offset;
    if(ok)for(NSData *data in destinations.allValues) {
        if(![data isKindOfClass:NSData.class]||!data.length||data.length>UINT16_MAX||
            ((const uint8_t *)data.bytes)[data.length-1]){ok=NO;break;}
    }
    if(ok)for(NSArray *values in permissions.allValues)if(values.count!=(signedFlags?4:3)){ok=NO;break;}
    if(ok)ok=Finalize((Container){image,offset,size/Block},volume,role,destinations,permissions,signedFlags);
    if(!ok&&error)*error=[NSError errorWithDomain:@"VirtualIntelMac.APFSVolume" code:1 userInfo:@{
        NSLocalizedDescriptionKey:@"Could not finalize the guest’s APFS volume metadata."}];
    return ok;
}

BOOL VIMFinalizeAPFSVolume(NSURL *raw,uint64_t offset,uint64_t size,NSUUID *volume,uint16_t role,
                           NSDictionary<NSNumber *,NSData *> *destinations,
                           NSDictionary<NSNumber *,NSArray<NSNumber *> *> *permissions,NSError **error) {
    return FinalizeAPFSVolume(raw,offset,size,volume,role,destinations,permissions,NO,error);
}

BOOL VIMFinalizeAPFSFirmlinks(NSURL *raw,uint64_t offset,uint64_t size,NSUUID *system,
                              NSDictionary<NSNumber *,NSData *> *destinations,NSError **error) {
    return VIMFinalizeAPFSVolume(raw,offset,size,system,VIMAPFSSystemRole,destinations,nil,error);
}

// Inspect native directory and xattr records, not VFS reads (which hide
// decmpfs and return decompressed bytes). Apple seals the actual resource
// fork geometry as well as the logical file content.
@interface VIMCatalogPath : NSObject <NSCopying>
- (instancetype)initWithData:(NSData *)bytes;
@end
@implementation VIMCatalogPath {
    NSData *_data;
    NSUInteger _fingerprint;
}
- (instancetype)initWithData:(NSData *)bytes {
    if((self=[super init])){
        _data=[bytes copy];
        // Hash the complete raw path. Most System files share long initial
        // prefixes, which made generic NSData hash buckets very expensive.
        uint64_t hash=UINT64_C(14695981039346656037);
        const uint8_t *p=_data.bytes;
        for(NSUInteger i=0;i<_data.length;i++)hash=(hash^p[i])*UINT64_C(1099511628211);
        _fingerprint=(NSUInteger)hash;
    }
    return self;
}
- (NSUInteger)hash {return _fingerprint;}
- (BOOL)isEqual:(id)object {
    return object==self||([object isKindOfClass:VIMCatalogPath.class]&&[_data isEqual:((VIMCatalogPath *)object)->_data]);
}
- (id)copyWithZone:(__unused NSZone *)zone {return self;}
@end

// j_drec_val_t's optional xfields begin after the fixed 18-byte record.
// Locate DREC_EXT_TYPE_SIBLING_ID without interpreting other fields.
static BOOL DirectorySiblingField(const uint8_t *value,size_t size,size_t *offset) {
    *offset=0;
    if(size==18)return YES;
    if(size<22)return NO;
    const size_t fields=U16(value+18),table=22+4*fields;
    if(table>size)return NO;
    size_t cursor=table;
    for(size_t i=0;i<fields;i++) {
        const uint8_t *field=value+22+4*i;
        const size_t length=U16(field+2),aligned=(length+7)&~(size_t)7;
        if(aligned>size-cursor)return NO;
        if(field[0]==1) {
            if(*offset||length!=8||!U64(value+cursor)||U64(value+cursor)>OIDMask)return NO;
            *offset=cursor;
        }
        cursor+=aligned;
    }
    return cursor==size;
}

static BOOL AttributeLayout(const uint8_t *value,size_t size,uint16_t *form,size_t *length) {
    if(size<4)return NO;
    *form=U16(value)&3;
    // APFS Reference: xdata_len describes embedded bytes only. A stream
    // attribute carries j_xattr_dstream_t regardless of that ignored field.
    if(*form==1)*length=48;
    else if(*form==2)*length=U16(value+2);
    else return NO;
    return size==4+*length;
}

static BOOL ReadCompressionHeaders(Container c,uint64_t root,ResolveObject resolve,
    NSDictionary<NSNumber *,NSArray<NSNumber *> *> *owners,NSMutableDictionary *types) {
    NSMutableSet *pending=[NSMutableSet setWithArray:owners.allKeys];
    BOOL walked=WalkTree(c,root,resolve,14,NO,
        ^BOOL(__unused uint8_t *node,__unused uint64_t address,BOOL leaf,uint8_t *key,size_t k,uint8_t *value,size_t v) {
            if(!leaf||k<8||(U64(key)>>60)!=8)return YES;
            NSNumber *stream=@(U64(key)&OIDMask);
            NSArray<NSNumber *> *files=owners[stream];
            if(!files)return YES;
            if(k!=16||v!=24)return NO;
            if(U64(key+8))return YES;
            uint64_t bytes=U64(value)&((UINT64_C(1)<<56)-1),physical=U64(value+8);
            uint8_t header[16];
            if(![pending containsObject:stream]||bytes<sizeof(header)||!physical||physical>=c.blocks||
               ![c.image readAt:c.offset+physical*Block bytes:header length:sizeof(header)]||memcmp(header,"fpmc",4))return NO;
            for(NSNumber *file in files)types[file]=@(U32(header+4));
            [pending removeObject:stream];
            return YES;
        },nil);
    return walked&&!pending.count;
}

static NSDictionary *BigSurCatalog(Container c,NSUUID *identity,BOOL canonical) {
    uint8_t volume[Block];
    ResolveObject resolve=ReadUnsealedVolume(c,identity,VIMAPFSSystemRole,volume);
    if(!resolve)return nil;
    BOOL hashed=(U64(volume+56)&9)!=0;
    NSMutableDictionary<NSNumber *,NSMutableArray<NSArray *> *> *children=[NSMutableDictionary dictionary];
    NSMutableDictionary<NSNumber *,NSNumber *> *types=[NSMutableDictionary dictionary],*forks=[NSMutableDictionary dictionary];
    NSMutableSet<NSNumber *> *overallocatedForks=[NSMutableSet set];
    NSMutableDictionary<NSNumber *,NSMutableArray<NSNumber *> *> *streams=[NSMutableDictionary dictionary];
    NSMutableDictionary<NSNumber *,NSMutableArray<NSNumber *> *> *compressionStreams=[NSMutableDictionary dictionary];
    NSMutableSet<NSNumber *> *separate=[NSMutableSet set];
    BOOL walked=WalkTree(c,U64(volume+136),resolve,14,NO,
        ^BOOL(__unused uint8_t *node,__unused uint64_t address,BOOL leaf,uint8_t *key,size_t k,uint8_t *value,size_t v) {
            if(!leaf)return YES;
            if(k<8)return NO;
            uint64_t tag=U64(key),type=tag>>60;
            if(type==9) {
                size_t nameOffset=hashed?12:10;
                if(k<=nameOffset||v<18)return NO;
                size_t length=hashed?U32(key+8)&0x3ff:U16(key+8);
                const uint8_t *name=key+nameOffset;
                if(length<2||length!=k-nameOffset||name[length-1]||
                    memchr(name,0,length-1)||memchr(name,'/',length-1))return NO;
                NSData *component=[NSData dataWithBytes:name length:length-1];
                NSNumber *parent=@(tag&OIDMask);
                if(!children[parent])children[parent]=[NSMutableArray array];
                size_t sibling=0;
                if(canonical&&!DirectorySiblingField(value,v,&sibling))return NO;
                NSArray *child=@[component,@(U64(value)),@((U16(value+16)&15)==4)];
                if(sibling)child=[child arrayByAddingObject:@(U64(value+sibling))];
                [children[parent] addObject:child];
            } else if(type==Inode&&canonical) {
                if(v<16)return NO;
                uint64_t oid=tag&OIDMask;
                if(U64(value+8)!=oid)[separate addObject:@(oid)];
            } else if(type==Xattr) {
                if(k<=10||v<4||U16(key+8)!=k-10)return NO;
                uint16_t form;size_t length;
                if(!AttributeLayout(value,v,&form,&length))return NO;
                if(canonical) {
                    if(form==1) {
                        NSNumber *oid=@(tag&OIDMask);
                        if(!streams[oid])streams[oid]=[NSMutableArray array];
                        [streams[oid] addObject:@(U64(value+4))];
                    }
                    return YES;
                }
                const char decmpfs[]="com.apple.decmpfs",resource[]="com.apple.ResourceFork";
                BOOL compressed=k==10+sizeof(decmpfs)&&!memcmp(key+10,decmpfs,sizeof(decmpfs));
                BOOL fork=k==10+sizeof(resource)&&!memcmp(key+10,resource,sizeof(resource));
                if(!compressed&&!fork)return YES;
                NSNumber *oid=@(tag&OIDMask);
                if(compressed) {
                    if(types[oid])return NO;
                    if(form==1) {
                        NSNumber *stream=@(U64(value+4));
                        if(!stream.unsignedLongLongValue||U64(value+12)<16)return NO;
                        if(!compressionStreams[stream])compressionStreams[stream]=[NSMutableArray array];
                        [compressionStreams[stream] addObject:oid];types[oid]=@0;
                    } else {
                        if(length<16||memcmp(value+4,"fpmc",4))return NO;
                        types[oid]=@(U32(value+8));
                    }
                } else {
                    if(forks[oid])return NO;
                    forks[oid]=@(form==1?U64(value+12):length);
                    if(form==1) {
                        uint64_t bytes=U64(value+12),blocks=bytes/Block+(bytes%Block!=0);
                        if(U64(value+20)/Block>blocks)[overallocatedForks addObject:oid];
                    }
                }
            }
            return YES;
        },nil);
    if(!walked)return nil;
    // Older host APFS can spill compression metadata itself into an xattr
    // stream. Read just each header, not all of its compressed file bytes.
    if(compressionStreams.count&&!ReadCompressionHeaders(c,U64(volume+136),resolve,compressionStreams,types))return nil;
    NSData *root=NSData.data;
    NSMutableDictionary<VIMCatalogPath *,NSNumber *> *paths=[NSMutableDictionary dictionaryWithObject:@2
        forKey:[[VIMCatalogPath alloc] initWithData:root]];
    NSMutableDictionary<VIMCatalogPath *,NSNumber *> *siblings=[NSMutableDictionary dictionary];
    NSMutableArray<NSArray *> *pending=[NSMutableArray arrayWithObject:@[root,@2]];
    while(pending.count) @autoreleasepool {
        NSArray *item=pending.lastObject;[pending removeLastObject];
        NSData *parent=item[0];NSNumber *oid=item[1];
        NSArray *entries=children[oid];[children removeObjectForKey:oid];
        for(NSArray *child in entries) {
            NSMutableData *path=[parent mutableCopy];
            if(parent.length)[path appendBytes:"/" length:1];
            [path appendData:child[0]];
            VIMCatalogPath *key=[[VIMCatalogPath alloc] initWithData:path];
            if(paths[key])return nil;
            paths[key]=child[1];
            if(child.count==4)siblings[key]=child[3];
            if([child[2] boolValue])[pending addObject:@[path,child[1]]];
        }
    }
    return canonical?@{@"Paths":paths,@"Streams":streams,@"SeparateFileStreams":separate,@"Siblings":siblings}:
        @{@"Paths":paths,@"CompressionTypes":types,@"ResourceForkLengths":forks,@"OverallocatedForks":overallocatedForks};
}

static NSDictionary *ReadBigSurCatalog(NSURL *raw,uint64_t offset,uint64_t size,NSUUID *system,BOOL canonical,NSError **error) {
    VIMDiskImageIO *image=[[VIMDiskImageIO alloc] initWithURL:raw writable:NO error:error];
    BOOL valid=image&&system&&!(offset%Block)&&size&&!(size%Block)&&offset<=image.size&&size<=image.size-offset;
    NSDictionary *catalog=valid?BigSurCatalog((Container){image,offset,size/Block},system,canonical):nil;
    if(!catalog) {
        if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurCatalog" code:EBADMSG userInfo:@{
            NSLocalizedDescriptionKey:@"Could not read the detached Big Sur System file records."}];
    }
    return catalog;
}

NSDictionary *VIMAuditBigSurSystemMetadata(NSURL *raw,uint64_t offset,uint64_t size,NSUUID *system,NSURL *zip,NSError **error) {
    NSDictionary *catalog=ReadBigSurCatalog(raw,offset,size,system,NO,error);
    if(!catalog)return nil;
    NSMutableDictionary *paths=catalog[@"Paths"],*permissions=[NSMutableDictionary dictionary],*differences=[NSMutableDictionary dictionary];
    NSMutableArray<NSString *> *compressionPaths=[NSMutableArray array],*allocationPaths=[NSMutableArray array];
    NSDictionary *types=catalog[@"CompressionTypes"],*forks=catalog[@"ResourceForkLengths"];
    NSSet *overallocated=catalog[@"OverallocatedForks"];
    __block NSUInteger entries=0,compressed=0;
    __block NSString *unmatched=nil;
    BOOL matched=VIMBigSurVisitFixups(zip,^BOOL(NSString *path,VIMBigSurFixup expected,__unused NSString *owner){
        NSData *bytes=(!path.length||[path isEqual:@"."])?NSData.data:[path dataUsingEncoding:NSUTF8StringEncoding];
        VIMCatalogPath *key=[[VIMCatalogPath alloc] initWithData:bytes];
        NSNumber *oid=paths[key];
        if(![oid isKindOfClass:NSNumber.class]){unmatched=path;return NO;}
        paths[key]=NSNull.null;
        NSArray *identity=@[@(expected.uid),@(expected.gid),@(expected.mode),@(expected.flags)];
        NSArray *previous=permissions[oid];
        if(previous&&![previous isEqual:identity]){unmatched=path;return NO;}
        permissions[oid]=identity;entries++;
        uint32_t observed=[types[oid] unsignedIntValue];
        if(expected.compressed)compressed++;
        if(observed!=expected.compressionType||(expected.compressed&&[forks[oid] unsignedLongLongValue]!=expected.resourceLength)) {
            NSString *kind=observed==expected.compressionType?@"ResourceSize":
                [NSString stringWithFormat:@"%u->%u",observed,expected.compressionType];
            differences[kind]=@([differences[kind] unsignedIntegerValue]+1);
            [compressionPaths addObject:path];
        } else if(expected.compressed&&expected.resourceLength&&[overallocated containsObject:oid])
            [allocationPaths addObject:path];
        return YES;
    },NULL,NULL,error);
    if(!matched||entries!=paths.count) {
        if(error&&(unmatched||matched))*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurCatalog" code:EBADMSG userInfo:@{
            NSLocalizedDescriptionKey:unmatched?[NSString stringWithFormat:@"An original Big Sur catalog record does not match %@.",unmatched]:
                @"The detached Big Sur System contains paths outside the original manifest."}];
        return nil;
    }
    return @{@"Entries":@(entries),@"CompressedEntries":@(compressed),@"CompressionDifferences":differences,
        @"CompressionPaths":compressionPaths,@"OverallocatedResourcePaths":allocationPaths,@"Permissions":permissions};
}

BOOL VIMFinalizeBigSurSystemMetadata(NSURL *raw,uint64_t offset,uint64_t size,NSUUID *system,NSURL *zip,
    NSDictionary<NSNumber *,NSData *> *firmlinks,BOOL (^repair)(NSArray<NSString *> *,NSError **),NSError **error) {
    NSDictionary *audit=VIMAuditBigSurSystemMetadata(raw,offset,size,system,zip,error);
    if(!audit)return NO;
    NSArray<NSString *> *overallocated=audit[@"OverallocatedResourcePaths"];
    if(repair&&overallocated.count&&![audit[@"CompressionDifferences"] count]) {
        audit=nil;
        if(!repair(overallocated,error))return NO;
        audit=VIMAuditBigSurSystemMetadata(raw,offset,size,system,zip,error);
        if(!audit)return NO;
    }
    NSDictionary *differences=audit[@"CompressionDifferences"];
    if(differences.count) {
        if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurCatalog" code:EBADMSG userInfo:@{
            NSLocalizedDescriptionKey:[NSString stringWithFormat:@"Big Sur compression records differ from Apple’s original manifest: %@.",differences]}];
        return NO;
    }
    if([audit[@"OverallocatedResourcePaths"] count]) {
        if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurCatalog" code:EBADMSG userInfo:@{
            NSLocalizedDescriptionKey:@"The Big Sur resource forks still contain unused allocation blocks."}];
        return NO;
    }
    return FinalizeAPFSVolume(raw,offset,size,system,VIMAPFSSystemRole,firmlinks,audit[@"Permissions"],YES,error);
}

static BOOL BindID(NSMutableDictionary *map,NSMutableDictionary *inverse,NSNumber *actual,NSNumber *canonical) {
    if(!actual.unsignedLongLongValue||actual.unsignedLongLongValue>OIDMask||
        !canonical.unsignedLongLongValue||canonical.unsignedLongLongValue>OIDMask)return NO;
    NSNumber *current=map[actual],*source=inverse[canonical];
    if((current&&![current isEqual:canonical])||(source&&![source isEqual:actual]))return NO;
    map[actual]=canonical;inverse[canonical]=actual;return YES;
}

static NSDictionary *PlanCanonicalMap(NSURL *raw,uint64_t offset,uint64_t size,NSUUID *system,NSURL *mtree,
                                       NSDictionary *policy,NSError **error) {
    NSDictionary *catalog=ReadBigSurCatalog(raw,offset,size,system,YES,error);
    if(!catalog)return nil;
    NSMutableDictionary *paths=catalog[@"Paths"],*map=[NSMutableDictionary dictionary],*inverse=[NSMutableDictionary dictionary];
    NSDictionary *streams=catalog[@"Streams"];NSSet *separate=catalog[@"SeparateFileStreams"];
    NSDictionary *siblings=catalog[@"Siblings"];
    NSSet *promotePaths=policy[@"FastPromote"];NSMutableSet *promote=[NSMutableSet set];
    NSDictionary *rootlessPaths=policy[@"Rootless"];NSMutableDictionary *rootless=[NSMutableDictionary dictionary];
    NSDictionary *alternatePaths=policy[@"AlternateLinks"];NSMutableDictionary *alternate=[NSMutableDictionary dictionary];
    __block NSUInteger entries=0,external=0;
    __block NSUInteger promotedPaths=0,protectedPaths=0,alternateCount=0;
    __block NSData *failedPath=nil;__block NSString *reason=nil;
    uint64_t timestamp=0;
    BOOL resolved=VIMBigSurVisitCanonicalTree(mtree,^BOOL(NSData *path,VIMBigSurCanonicalEntry entry){
        VIMCatalogPath *key=[[VIMCatalogPath alloc] initWithData:path];
        NSNumber *actual=paths[key];
        NSArray *observed=streams[actual?:@0];
        NSNumber *sibling=siblings[key];
        if(![actual isKindOfClass:NSNumber.class])reason=@"missing or repeated path";
        else if([separate containsObject:actual])reason=@"separate file stream";
        else if(observed.count!=(entry.externalStream!=0))reason=@"external attribute count";
        else if((sibling!=nil)!=(entry.siblingID!=0)||
                (sibling&&!BindID(map,inverse,sibling,@(entry.siblingID))))reason=@"hard link sibling ID";
        else if(!BindID(map,inverse,actual,@(entry.inode))||
            (entry.externalStream&&!BindID(map,inverse,observed[0],@(entry.externalStream))))reason=@"conflicting object ID";
        if(reason){failedPath=path;return NO;}
        if(policy) {
            NSString *name=path.length?[[NSString alloc] initWithData:path encoding:NSUTF8StringEncoding]:@"";
            if(!name){failedPath=path;reason=@"invalid path encoding";return NO;}
            if([promotePaths containsObject:name]){[promote addObject:actual];promotedPaths++;}
            NSData *attribute=rootlessPaths[name];
            if(attribute){rootless[actual]=attribute;protectedPaths++;}
            attribute=alternatePaths[name];
            if(attribute){alternate[actual]=attribute;alternateCount++;}
        }
        paths[key]=NSNull.null;entries++;if(entry.externalStream)external++;
        return YES;
    },&timestamp,error);
    if(!resolved||entries!=paths.count||promotedPaths!=promotePaths.count||protectedPaths!=rootlessPaths.count||
        alternateCount!=alternatePaths.count) {
        if(error&&(failedPath||resolved))*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurCatalog" code:EBADMSG userInfo:@{
            NSLocalizedDescriptionKey:failedPath?
                [NSString stringWithFormat:@"Big Sur’s original canonical IDs do not match %@ (%@).",
                    [[NSString alloc] initWithData:failedPath encoding:NSUTF8StringEncoding]?:@"the System volume",reason]:
                @"The detached Big Sur System and its signed sealing policy have different paths."}];
        return nil;
    }
    return @{@"ObjectIDs":map,@"ChangeTime":@(timestamp),@"Entries":@(entries),@"ExternalStreams":@(external),
        @"FastPromote":promote,@"Rootless":rootless,@"AlternateLinks":alternate};
}

NSDictionary *VIMPlanBigSurCanonicalMap(NSURL *raw,uint64_t offset,uint64_t size,NSUUID *system,NSURL *mtree,NSError **error) {
    return PlanCanonicalMap(raw,offset,size,system,mtree,nil,error);
}

static uint64_t CanonicalID(NSDictionary<NSNumber *,NSNumber *> *ids,uint64_t original) {
    NSNumber *translated=ids[@(original)];
    return translated?translated.unsignedLongLongValue:original;
}

static BOOL CanonicalInode(uint8_t *value,size_t size,NSDictionary *ids,uint64_t stamp,BOOL promote) {
    if(size<92||size==93||size==94||size==95)return NO;
    Set64(value,CanonicalID(ids,U64(value)));
    Set64(value+8,CanonicalID(ids,U64(value+8)));
    for(size_t date=16;date<48;date+=8)Set64(value+date,stamp);
    Set64(value+48,(U64(value+48)&~UINT64_C(0x20000))|(promote?UINT64_C(0x20000):0));
    Set32(value+60,0);Set32(value+64,1);
    if(size==92)return YES;
    size_t fields=U16(value+92),payload=96+4*fields;
    if(payload>size)return NO;
    for(size_t i=0;i<fields;i++) {
        const uint8_t *field=value+96+4*i;
        size_t length=U16(field+2),aligned=(length+7)&~(size_t)7;
        if(aligned>size-payload)return NO;
        if(field[0]==8) {
            if(length!=40)return NO;
            Set64(value+payload+24,1);Set64(value+payload+32,1);
        }
        payload+=aligned;
    }
    return YES;
}

static BOOL EmitCanonicalFilesystem(Container c,NSUUID *system,NSDictionary *plan,
                                    BOOL (^visit)(NSData *,NSData *)) {
    uint8_t volume[Block];
    ResolveObject resolve=ReadUnsealedVolume(c,system,VIMAPFSSystemRole,volume);
    if(!resolve)return NO;
    NSDictionary *ids=plan[@"ObjectIDs"],*rootless=plan[@"Rootless"],*alternate=plan[@"AlternateLinks"];
    NSSet *promote=plan[@"FastPromote"];
    uint64_t stamp=[plan[@"ChangeTime"] unsignedLongLongValue];
    static const char rootlessName[]="com.apple.rootless";
    static const char alternateName[]="com.apple.fs.altlink";
    // A host app launched from a download (quarantined, translocated) has
    // every file it writes tagged with com.apple.provenance by the host
    // kernel. Apple's signed System tree has no such attribute; omit it.
    static const char provenanceName[]="com.apple.provenance";
    BOOL walked=WalkTree(c,U64(volume+136),resolve,14,NO,
        ^BOOL(__unused uint8_t *node,__unused uint64_t address,BOOL leaf,uint8_t *key,size_t k,uint8_t *value,size_t v) {
            if(!leaf)return YES;
            if(k<8)return NO;
            uint64_t tag=U64(key),kind=tag>>60,oid=tag&OIDMask;
            // Physical extents have a separate tree in the sealed snapshot.
            if(kind==8)return YES;
            if(kind!=Inode&&kind!=Xattr&&kind!=6&&kind!=9&&kind!=SiblingLink&&kind!=SiblingMap)return NO;
            if(kind==Xattr&&k==10+sizeof(rootlessName)&&!memcmp(key+10,rootlessName,sizeof(rootlessName))&&rootless[@(oid)])
                return YES; // Emit the original signed value below.
            if(kind==Xattr&&k==10+sizeof(alternateName)&&!memcmp(key+10,alternateName,sizeof(alternateName))&&alternate[@(oid)])
                return YES;
            if(kind==Xattr&&k==10+sizeof(provenanceName)&&!memcmp(key+10,provenanceName,sizeof(provenanceName)))
                return YES;
            Set64(key,(kind<<60)|CanonicalID(ids,oid));
            if(kind==Inode) {
                if(!CanonicalInode(value,v,ids,stamp,[promote containsObject:@(oid)]))return NO;
            } else if(kind==Xattr) {
                if(v<4)return NO;
                if((U16(value)&3)==1) {
                    if(v!=52)return NO;
                    Set64(value+4,CanonicalID(ids,U64(value+4)));
                    Set64(value+36,1);Set64(value+44,1);
                }
            } else if(kind==9) {
                size_t sibling=0;
                if(!DirectorySiblingField(value,v,&sibling))return NO;
                Set64(value,CanonicalID(ids,U64(value)));Set64(value+8,stamp);
                if(sibling)Set64(value+sibling,CanonicalID(ids,U64(value+sibling)));
            } else if(kind==SiblingLink) {
                // The key joins the file ID and sibling ID; the value names
                // the parent of that specific directory entry.
                if(k!=16||v<=10||U16(value+8)!=v-10||value[v-1]||!ids[@(U64(key+8))])return NO;
                Set64(key+8,CanonicalID(ids,U64(key+8)));
                Set64(value,CanonicalID(ids,U64(value)));
            } else if(kind==SiblingMap) {
                if(k!=8||v!=8||!ids[@(oid)]||!ids[@(U64(value))])return NO;
                Set64(value,CanonicalID(ids,U64(value)));
            }
            return visit([NSData dataWithBytes:key length:k],[NSData dataWithBytes:value length:v]);
        },nil);
    if(!walked)return NO;
    NSArray *attributes=@[rootless?:@{},alternate?:@{}];
    NSArray<NSData *> *names=@[[NSData dataWithBytes:rootlessName length:sizeof(rootlessName)],
        [NSData dataWithBytes:alternateName length:sizeof(alternateName)]];
    for(NSUInteger i=0;i<attributes.count;i++) {
        NSDictionary *values=attributes[i];NSData *name=names[i];
        for(NSNumber *actual in values) @autoreleasepool {
            NSData *attribute=values[actual];if(attribute.length>UINT16_MAX)return NO;
            NSMutableData *key=[NSMutableData dataWithLength:10+name.length];uint8_t *bytes=key.mutableBytes;
            Set64(bytes,((uint64_t)Xattr<<60)|CanonicalID(ids,actual.unsignedLongLongValue));
            bytes[8]=(uint8_t)name.length;bytes[9]=(uint8_t)(name.length>>8);memcpy(bytes+10,name.bytes,name.length);
            // Both attributes are embedded records (flags=2) in the original
            // authenticated Intel System tree, including Ventura's altlink.
            uint8_t header[4]={2,0,(uint8_t)attribute.length,(uint8_t)(attribute.length>>8)};
            NSMutableData *value=[NSMutableData dataWithBytes:header length:sizeof(header)];[value appendData:attribute];
            if(!visit(key,value))return NO;
        }
    }
    return YES;
}

NSDictionary *VIMVisitBigSurCanonicalRecords(NSURL *raw,uint64_t offset,uint64_t size,NSUUID *system,
    NSURL *mtree,NSURL *zip,BOOL (^visit)(NSData *,NSData *),NSError **error) {
    NSDictionary *policy=VIMBigSurReadSealPolicy(zip,error);
    if(!policy)return nil;
    NSDictionary *plan=PlanCanonicalMap(raw,offset,size,system,mtree,policy,error);
    if(!plan)return nil;
    VIMDiskImageIO *image=[[VIMDiskImageIO alloc] initWithURL:raw writable:NO error:error];
    BOOL valid=visit&&image&&offset<=image.size&&size<=image.size-offset&&
        EmitCanonicalFilesystem((Container){image,offset,size/Block},system,plan,visit);
    if(valid)return plan;
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurCatalog" code:EBADMSG userInfo:@{
        NSLocalizedDescriptionKey:@"Could not derive the authenticated Big Sur filesystem records."}];
    return nil;
}

NSData *VIMReadBigSurExtentRecords(NSURL *raw,uint64_t offset,uint64_t size,NSUUID *system,
    NSDictionary<NSNumber *,NSNumber *> *ids,NSError **error) {
    VIMDiskImageIO *image=[[VIMDiskImageIO alloc] initWithURL:raw writable:NO error:error];
    BOOL valid=image&&system&&ids&&!(offset%Block)&&size&&!(size%Block)&&offset<=image.size&&size<=image.size-offset;
    NSMutableData *physical=[NSMutableData data];
    Container c={image,offset,size/Block};
    if(valid) {
        uint8_t volume[Block];ResolveObject resolve=ReadUnsealedVolume(c,system,VIMAPFSSystemRole,volume);
        valid=resolve&&WalkTree(c,U64(volume+136),resolve,14,NO,
            ^BOOL(__unused uint8_t *node,__unused uint64_t address,BOOL leaf,uint8_t *key,size_t k,uint8_t *value,size_t v) {
                if(!leaf)return YES;
                if(k<8)return NO;
                uint64_t tag=U64(key);
                if(tag>>60!=8)return YES;
                if(k!=16||v!=24||U64(value+16)||!(tag&OIDMask))return NO;
                VIMBigSurExtent extent={CanonicalID(ids,tag&OIDMask),U64(key+8),U64(value),U64(value+8)};
                if(!extent.length||extent.length>=UINT64_C(1)<<56||!extent.physical||
                    extent.physical>=c.blocks||extent.length/Block>c.blocks-extent.physical)return NO;
                [physical appendBytes:&extent length:sizeof(extent)];return YES;
            },nil);
    }
    NSData *records=valid?VIMBigSurEncodeExtentRecords(physical):nil;
    if(records.length)return records;
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurCatalog" code:EBADMSG userInfo:@{
        NSLocalizedDescriptionKey:@"Could not read the detached Big Sur System’s file extents."}];
    return nil;
}

static NSDictionary *SnapshotSource(Container c,NSUUID *system) {
    uint8_t nx[Block],volume[Block],map[Block],systemMap[Block];
    if(!ReadActiveSuperblock(c,nx))return nil;
    ResolveObject sys=ReadUnsealedVolume(c,system,VIMAPFSSystemRole,volume);
    if(!sys)return nil;
    uint64_t volumeID=U64(volume+8),nxMap=U64(nx+160),vMap=U64(volume+128);
    if(!ReadObject(c,nxMap,11,map)||U32(map+36)||!ReadObject(c,vMap,11,systemMap))return nil;
    ResolveObject container=ReadObjectMap(c,nxMap,U64(nx+16));
    uint64_t volumeBlock=container?container(volumeID):0;
    if(!volumeBlock)return nil;
    NSMutableIndexSet *retiredContainer=[NSMutableIndexSet indexSetWithIndex:(NSUInteger)nxMap],
        *retiredSystem=[NSMutableIndexSet indexSetWithIndex:(NSUInteger)vMap];
    [retiredContainer addIndex:(NSUInteger)volumeBlock];
    NSMutableData *rows=[NSMutableData data];
    if(!WalkTree(c,U64(map+48),nil,11,YES,
        ^BOOL(__unused uint8_t *node,__unused uint64_t address,BOOL leaf,uint8_t *key,size_t k,uint8_t *value,size_t v) {
            if(!leaf)return YES;
            if(k!=16||v!=16)return NO;
            if(U64(key)!=volumeID){[rows appendBytes:key length:k];[rows appendBytes:value length:v];}
            return YES;
        },^(__unused uint8_t *node,uint64_t address){[retiredContainer addIndex:(NSUInteger)address];}))return nil;
    const ReadRecord countOnly=^BOOL(__unused uint8_t *node,__unused uint64_t address,__unused BOOL leaf,
        __unused uint8_t *key,__unused size_t k,__unused uint8_t *value,__unused size_t v){return YES;};
    void (^retire)(__unused uint8_t *,uint64_t)=^(__unused uint8_t *node,uint64_t address){[retiredSystem addIndex:(NSUInteger)address];};
    if(!WalkTree(c,U64(volume+136),sys,14,NO,countOnly,retire)||
       !WalkTree(c,U64(systemMap+48),nil,11,YES,countOnly,retire)||
       !WalkTree(c,U64(volume+152),nil,16,NO,countOnly,retire)||
       [retiredSystem intersectsIndexesInRange:NSMakeRange((NSUInteger)nxMap,1)])return nil;
    NSUInteger systemCount=retiredSystem.count;
    [retiredSystem addIndexes:retiredContainer];
    if(retiredSystem.count!=systemCount+retiredContainer.count)return nil;
    NSMutableArray *retired=[NSMutableArray array];
    [retiredSystem enumerateRangesUsingBlock:^(NSRange range,__unused BOOL *stop){
        [retired addObject:@[@(range.location),@(range.length)]];
    }];
    return @{@"Transaction":@(U64(nx+16)),@"System":[NSData dataWithBytes:volume length:Block],
        @"ContainerMapFlags":@(U32(map+32)),@"ContainerRows":rows,@"RetiredSystemCount":@(systemCount),@"RetiredRanges":retired};
}

NSDictionary *VIMReadBigSurSnapshotSource(NSURL *raw,uint64_t offset,uint64_t size,NSUUID *system,NSError **error) {
    VIMDiskImageIO *image=[[VIMDiskImageIO alloc] initWithURL:raw writable:NO error:error];
    BOOL valid=image&&system&&!(offset%Block)&&size&&!(size%Block)&&offset<=image.size&&size<=image.size-offset;
    NSDictionary *source=valid?SnapshotSource((Container){image,offset,size/Block},system):nil;
    if(source)return source;
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurCatalog" code:EBADMSG userInfo:@{
        NSLocalizedDescriptionKey:@"Could not inventory the detached Big Sur snapshot source."}];
    return nil;
}

static BOOL VerifySnapshot(Container c,NSUUID *system,NSUUID *snapshot,uint64_t transaction,
    uint64_t nodes,NSData *rootHash,NSURL *mtree,BOOL (^cancelled)(void),NSError **error) {
    uint8_t nx[Block],volume[Block],integrity[Block],extension[Block],frozen[Block];
    if(rootHash.length!=32||!transaction||!ReadActiveSuperblock(c,nx)||U64(nx+16)!=transaction||
       !FindVolume(c,nx,system,VIMAPFSSystemRole,volume)||U64(volume+16)!=transaction||
       !(U64(volume+56)&0x20)||U64(volume+216)!=1||U64(volume+0x3c8)!=transaction)return NO;
    uint64_t omap=U64(volume+128);
    ResolveObject ordinary=ReadObjectMap(c,omap,transaction);
    if(!ordinary||!ReadObject(c,ordinary(U64(volume+0x400)),30,integrity)||
       !ReadObject(c,ordinary(U64(volume+0x3e8)),29,extension))return NO;
    uint32_t hashOffset=U32(integrity+44);uuid_t uuid;[snapshot getUUIDBytes:uuid];
    if(U32(integrity+32)!=2||U32(integrity+36)||U32(integrity+40)!=1||U64(integrity+48)||
       hashOffset<56||hashOffset>Block-32||memcmp(integrity+hashOffset,rootHash.bytes,32)||
       U32(extension+32)!=1||U64(extension+40)!=transaction||memcmp(extension+48,uuid,sizeof(uuid)))return NO;
    NSMutableString *name=[NSMutableString stringWithString:@"com.apple.os.update-"];
    const uint8_t *hash=rootHash.bytes;
    for(size_t i=0;i<32;i++)[name appendFormat:@"%02X",hash[i]];
    size_t length=strlen(name.UTF8String)+1;
    NSData *snapshotName=[NSData dataWithBytes:name.UTF8String length:length];
    __block uint64_t frozenAddress=0;__block BOOL nameFound=NO;
    BOOL walked=WalkTree(c,U64(volume+152),nil,16,NO,
        ^BOOL(__unused uint8_t *node,__unused uint64_t address,BOOL leaf,uint8_t *key,size_t k,uint8_t *value,size_t v) {
            if(!leaf)return YES;
            if(k<8)return NO;
            uint64_t tag=U64(key);
            if(tag==((UINT64_C(1)<<60)|transaction)) {
                if(frozenAddress||k!=8||v!=50+length||U16(value+48)!=length||
                   memcmp(value+50,snapshotName.bytes,length))return NO;
                frozenAddress=U64(value+8);
            } else if(tag==((UINT64_C(11)<<60)|OIDMask)) {
                if(nameFound||k!=10+length||v!=8||U16(key+8)!=length||
                   memcmp(key+10,snapshotName.bytes,length)||U64(value)!=transaction)return NO;
                nameFound=YES;
            }
            return YES;
        },nil);
    if(!walked||!nameFound||!frozenAddress||!ReadObject(c,frozenAddress,13,frozen)||
       memcmp(frozen+32,"APSB",4)||memcmp(frozen+240,volume+240,16)||U64(frozen+16)!=transaction||
       !(U64(frozen+56)&0x20)||U64(frozen+136)!=U64(volume+136))return NO;
    ResolveObject headerless=ReadObjectMapWithMode(c,omap,transaction,YES);
    uint64_t root=U64(frozen+136);
    if(!headerless||!root||root>OIDMask||nodes>OIDMask-root)return NO;
    NSData *(^readNode)(uint64_t)=^NSData *(uint64_t relative){
        uint64_t block=headerless(root+relative);
        NSMutableData *data=[NSMutableData dataWithLength:Block];
        return block&&ReadBlock(c,block,data.mutableBytes)?data:nil;
    };
    if(!mtree)return VIMVerifyBigSurTreeNodes(nodes,rootHash,readNode,cancelled);
    const uint64_t fext=U64(frozen+0x408);
    if(!fext)return NO;
    return VIMVerifyBigSurSignedFileData(c.blocks,^BOOL(VIMBigSurSealRecord record){
        return VIMVisitBigSurAuthenticatedTree(nodes,rootHash,readNode,record,cancelled);
    },^BOOL(VIMBigSurExtentVisitor extent){
        return WalkTree(c,fext,nil,31,YES,
            ^BOOL(__unused uint8_t *node,__unused uint64_t address,BOOL leaf,uint8_t *key,size_t k,uint8_t *value,size_t v){
                return !leaf||(k==16&&v==16&&extent(U64(key),U64(key+8),U64(value),U64(value+8)));
            },nil);
    },^BOOL(uint64_t position,void *buffer,size_t length){
        uint64_t size=c.blocks*Block;
        if(position>size||length>size-position)return NO;
        return [c.image readAt:c.offset+position bytes:buffer length:length];
    },cancelled,error);
}

BOOL VIMVerifyBigSurPublishedSnapshot(NSURL *raw,uint64_t offset,uint64_t size,NSUUID *system,NSUUID *snapshot,
    uint64_t transaction,uint64_t nodes,NSData *rootHash,NSURL *mtree,BOOL (^cancelled)(void),NSError **error) {
    VIMDiskImageIO *image=[[VIMDiskImageIO alloc] initWithURL:raw writable:NO error:error];
    BOOL valid=image&&system&&snapshot&&!(offset%Block)&&size&&!(size%Block)&&offset<=image.size&&size<=image.size-offset;
    NSError *detail=nil;
    if(valid)valid=VerifySnapshot((Container){image,offset,size/Block},system,snapshot,transaction,nodes,rootHash,mtree,cancelled,&detail);
    if(!valid&&error)*error=detail?:[NSError errorWithDomain:@"VirtualIntelMac.BigSurSnapshot" code:EBADMSG userInfo:@{
        NSLocalizedDescriptionKey:@"The published Big Sur snapshot could not be authenticated through its APFS object maps."}];
    return valid;
}
