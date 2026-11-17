#import "VIMTigerAccount.h"
#import <CommonCrypto/CommonDigest.h>
#import <Security/Security.h>

typedef struct { const uint8_t *bytes; NSUInteger left; BOOL bad; } Reader;
static uint32_t Word(Reader *r) {
    if(r->left<4){r->bad=YES;return 0;}
    const uint8_t *p=r->bytes;r->bytes+=4;r->left-=4;
    return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
}
static void Put(NSMutableData *data,uint32_t value) {
    uint32_t be=CFSwapInt32HostToBig(value);[data appendBytes:&be length:4];
}
static NSData *Atom(Reader *r) {
    const uint8_t *start=r->bytes;
    (void)Word(r);uint32_t length=Word(r);
    if(r->bad||length>r->left){r->bad=YES;return nil;}
    r->bytes+=length;r->left-=length;
    return [NSData dataWithBytes:start length:(NSUInteger)length+8];
}
static NSData *String(NSString *string) {
    NSData *bytes=[string dataUsingEncoding:NSUTF8StringEncoding];
    NSMutableData *data=[NSMutableData data];Put(data,6);Put(data,(uint32_t)bytes.length+1);
    [data appendData:bytes];uint8_t zero=0;[data appendBytes:&zero length:1];return data;
}
static NSMutableDictionary<NSData *,NSMutableArray<NSData *> *> *Attributes(Reader *r,NSMutableArray<NSData *> *order) {
    uint32_t count=Word(r);
    NSMutableDictionary *attributes=[NSMutableDictionary dictionary];
    if(count>r->left/12){r->bad=YES;return nil;}
    while(count--) {
        NSData *key=Atom(r);uint32_t values=Word(r);
        if(!key||values>r->left/8||attributes[key]){r->bad=YES;return nil;}
        NSMutableArray *array=[NSMutableArray array];
        while(values--){NSData *atom=Atom(r);if(!atom)return nil;[array addObject:atom];}
        [order addObject:key];attributes[key]=array;
    }
    return attributes;
}

@interface VIMNetInfoRecord : NSObject
@property uint32_t identifier,serial,version,parent;
@property NSMutableArray<NSNumber *> *children;
@property NSMutableArray<NSData *> *order;
@property NSMutableDictionary<NSData *,NSMutableArray<NSData *> *> *attributes;
@property NSData *metadata;
@end
@implementation VIMNetInfoRecord
- (instancetype)init {
    if((self=[super init])) {
        _children=[NSMutableArray array];_order=[NSMutableArray array];_attributes=[NSMutableDictionary dictionary];
        uint32_t zero=0;_metadata=[NSData dataWithBytes:&zero length:4];
    }
    return self;
}
- (void)append:(NSString *)value key:(NSString *)name {
    NSData *key=String(name),*atom=String(value);
    if(!self.attributes[key]){[self.order addObject:key];self.attributes[key]=[NSMutableArray array];}
    if(![self.attributes[key] containsObject:atom])[self.attributes[key] addObject:atom];
}
- (NSData *)encode {
    NSMutableData *data=[NSMutableData data];
    Put(data,self.identifier);Put(data,self.serial);Put(data,self.version);Put(data,self.parent);
    Put(data,(uint32_t)self.children.count);for(NSNumber *child in self.children)Put(data,child.unsignedIntValue);
    Put(data,(uint32_t)self.order.count);
    for(NSData *key in self.order) {
        [data appendData:key];NSArray *values=self.attributes[key];Put(data,(uint32_t)values.count);
        for(NSData *atom in values)[data appendData:atom];
    }
    [data appendData:self.metadata];return data;
}
@end

static VIMNetInfoRecord *Decode(const void *bytes,NSUInteger length) {
    Reader r={bytes,length,NO};
    VIMNetInfoRecord *record=[VIMNetInfoRecord new];
    record.identifier=Word(&r);record.serial=Word(&r);record.version=Word(&r);record.parent=Word(&r);
    uint32_t children=Word(&r);
    if(children>r.left/4)return nil;
    while(children--)[record.children addObject:@(Word(&r))];
    record.attributes=Attributes(&r,record.order);
    const uint8_t *metadata=r.bytes;NSUInteger size=r.left;
    if(!Attributes(&r,[NSMutableArray array])||r.bad||r.left)return nil;
    record.metadata=[NSData dataWithBytes:metadata length:size];return record;
}
static NSMutableDictionary<NSNumber *,VIMNetInfoRecord *> *Load(NSURL *seed) {
    NSData *config=[NSData dataWithContentsOfURL:[seed URLByAppendingPathComponent:@"Config"]];
    if(config.length!=4||memcmp(config.bytes,"\0\0\0\1",4))return nil;
    NSData *index=[NSData dataWithContentsOfURL:[seed URLByAppendingPathComponent:@"Index"]];
    if(!index.length||index.length%16)return nil;
    Reader r={index.bytes,index.length,NO};
    NSMutableDictionary *records=[NSMutableDictionary dictionary],*stores=[NSMutableDictionary dictionary];
    while(r.left) {
        uint32_t identifier=Word(&r),version=Word(&r),size=Word(&r),slot=Word(&r);
        if(size<32||size%32||records[@(identifier)])return nil;
        NSData *store=stores[@(size)];
        if(!store) {
            store=[NSData dataWithContentsOfURL:[seed URLByAppendingPathComponent:[NSString stringWithFormat:@"Store.%u",size]]];
            if(!store)return nil;stores[@(size)]=store;
        }
        uint64_t offset=(uint64_t)slot*size;
        if(offset>store.length||size>store.length-offset)return nil;
        Reader entry={(const uint8_t *)store.bytes+offset,size,NO};
        uint32_t kind=Word(&entry),length=Word(&entry);
        if(kind!=256||length>entry.left)return nil;
        VIMNetInfoRecord *record=Decode(entry.bytes,length);
        if(!record||record.identifier!=identifier||record.version!=version)return nil;
        records[@(identifier)]=record;
    }
    return records;
}
static VIMNetInfoRecord *Named(NSDictionary *records,VIMNetInfoRecord *parent,NSString *name) {
    NSData *key=String(@"name"),*value=String(name);
    for(NSNumber *child in parent.children) {
        VIMNetInfoRecord *record=records[child];
        if([record.attributes[key] containsObject:value])return record;
    }
    return nil;
}
static BOOL Save(NSURL *seed,NSDictionary<NSNumber *,VIMNetInfoRecord *> *records,NSError **error) {
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *new=[seed.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"Prepared.nidb" isDirectory:YES];
    if(![fm createDirectoryAtURL:new withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:error])return NO;
    NSMutableDictionary<NSNumber *,NSMutableData *> *stores=[NSMutableDictionary dictionary];
    NSMutableData *index=[NSMutableData data],*config=[NSMutableData data];
    uint32_t checksum=0;Put(config,1);
    for(NSNumber *key in [records.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
        VIMNetInfoRecord *record=records[key];NSData *payload=[record encode];
        uint32_t size=((uint32_t)payload.length+8+31)&~31U;
        NSMutableData *store=stores[@(size)];
        if(!store)stores[@(size)]=store=[NSMutableData data];
        Put(index,record.identifier);Put(index,record.version);Put(index,size);Put(index,(uint32_t)(store.length/size));
        Put(store,256);Put(store,(uint32_t)payload.length);[store appendData:payload];
        [store increaseLengthBy:size-payload.length-8];
        checksum+=(record.identifier+1)*record.serial;
    }
    uint32_t clean=CFSwapInt32HostToLittle(checksum);
    if(![config writeToURL:[new URLByAppendingPathComponent:@"Config"] options:0 error:error]||
       ![index writeToURL:[new URLByAppendingPathComponent:@"Index"] options:0 error:error]||
       ![[NSData dataWithBytes:&clean length:4] writeToURL:[new URLByAppendingPathComponent:@"Clean"] options:0 error:error])return NO;
    for(NSNumber *size in stores)
        if(![stores[size] writeToURL:[new URLByAppendingPathComponent:[NSString stringWithFormat:@"Store.%@",size]] options:0 error:error])return NO;
    // The installer seed is inside a scratch disk owned by this worker.
    return [fm removeItemAtURL:seed error:error]&&[fm moveItemAtURL:new toURL:seed error:error];
}

uint32_t VIMCreateTigerAccount(NSURL *seed,NSString *name,NSString *guid,BOOL smb,NSError **error) {
    NSMutableDictionary<NSNumber *,VIMNetInfoRecord *> *records=Load(seed);
    VIMNetInfoRecord *users=Named(records,records[@0],@"users");
    VIMNetInfoRecord *admin=Named(records,Named(records,records[@0],@"groups"),@"admin");
    NSMutableIndexSet *used=[NSMutableIndexSet indexSet];
    VIMNetInfoRecord *record=nil;
    NSDictionary *properties=nil;
    if(!users||!admin||Named(records,users,name))goto invalid;
    uint32_t identifier=0,version=0,uid=501;
    for(VIMNetInfoRecord *record in records.objectEnumerator) {
        identifier=MAX(identifier,record.identifier);version=MAX(version,record.version);
    }
    for(NSNumber *child in users.children) {
        VIMNetInfoRecord *record=records[child];
        if(!record)goto invalid;
        for(NSData *atom in record.attributes[String(@"uid")]) {
            Reader value={atom.bytes,atom.length,NO};
            uint32_t type=Word(&value),length=Word(&value);
            if(type!=6||length!=value.left||!value.left||value.bytes[value.left-1])goto invalid;
            unsigned long number=strtoul((const char *)value.bytes,NULL,10);
            if(number<=UINT32_MAX)[used addIndex:number];
        }
    }
    while(uid<65534&&[used containsIndex:uid])uid++;
    if(uid==65534||identifier==UINT32_MAX||version>UINT32_MAX-3)goto invalid;
    identifier++;[users.children addObject:@(identifier)];
    [admin append:name key:@"users"];[admin append:guid key:@"groupmembers"];
    users.serial++;users.version=version+1;admin.serial++;admin.version=version+2;
    record=[VIMNetInfoRecord new];
    record.identifier=identifier;record.serial=1;record.version=version+3;record.parent=users.identifier;
    properties=@{@"name":name,@"realname":name,@"uid":@(uid).stringValue,@"gid":@"20",
        @"home":[@"/Users/" stringByAppendingString:name],@"shell":@"/bin/bash",@"passwd":@"********",
        @"generateduid":guid,@"authentication_authority":smb?@";ShadowHash;HASHLIST:<SALTED-SHA1,SMB-NT>":@";ShadowHash;",
        @"_writers_passwd":name,@"_writers_picture":name};
    for(NSString *key in [properties.allKeys sortedArrayUsingSelector:@selector(compare:)])[record append:properties[key] key:key];
    records[@(identifier)]=record;
    return Save(seed,records,error)?uid:0;
invalid:
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.HostInstall" code:2 userInfo:@{
        NSLocalizedDescriptionKey:@"Could not prepare the Tiger account from the original NetInfo seed."}];
    return 0;
}

NSData *VIMClassicShadowHash(NSData *password,BOOL smb) {
    uint8_t slots[620]={0},hex[1240],*salt=slots+84;
    if(SecRandomCopyBytes(kSecRandomDefault,4,salt)!=errSecSuccess)return nil;
    NSMutableData *salted=[NSMutableData dataWithBytes:salt length:4];[salted appendData:password];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CC_SHA1(salted.bytes,(CC_LONG)salted.length,salt+4);
    if(smb) {
        NSString *text=[[NSString alloc] initWithData:password encoding:NSUTF8StringEncoding];
        NSData *unicode=[text dataUsingEncoding:NSUTF16LittleEndianStringEncoding];
        if(!unicode||!CC_MD4(unicode.bytes,(CC_LONG)unicode.length,slots))return nil;
    }
#pragma clang diagnostic pop
    const char alphabet[]="0123456789ABCDEF";
    for(unsigned i=0;i<sizeof(slots);i++){hex[2*i]=alphabet[slots[i]>>4];hex[2*i+1]=alphabet[slots[i]&15];}
    return [NSData dataWithBytes:hex length:sizeof(hex)];
}
NSData *VIMClassicAutoLoginPassword(NSData *password) {
    const uint8_t key[]={0x7d,0x89,0x52,0x23,0xd2,0xbc,0xdd,0xea,0xa3,0xb9,0x1f};
    NSUInteger length=(password.length/12+1)*12;
    NSMutableData *encoded=[NSMutableData dataWithLength:length];
    uint8_t *bytes=encoded.mutableBytes;
    if(SecRandomCopyBytes(kSecRandomDefault,length,bytes)!=errSecSuccess)return nil;
    memcpy(bytes,password.bytes,password.length);bytes[password.length]=0;
    for(NSUInteger i=0;i<length;i++)bytes[i]^=key[i%sizeof(key)];
    return encoded;
}
