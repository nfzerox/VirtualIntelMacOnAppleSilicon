#import "VIMBigSurTree.h"
#import <CommonCrypto/CommonDigest.h>

enum { Block=4096,Header=56,Slot=8,Footer=40,IndexValue=40 };
static const uint64_t ObjectMask=(UINT64_C(1)<<60)-1;

static uint32_t Get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t Get64(const uint8_t *p) {return Get32(p)|(uint64_t)Get32(p+4)<<32;}
static void Put16(uint8_t *p,uint16_t n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void Put32(uint8_t *p,uint32_t n) {Put16(p,(uint16_t)n);Put16(p+2,(uint16_t)(n>>16));}
static void Put64(uint8_t *p,uint64_t n) {Put32(p,(uint32_t)n);Put32(p+4,(uint32_t)(n>>32));}
static NSComparisonResult Numeric(uint64_t a,uint64_t b) {return a<b?NSOrderedAscending:a>b?NSOrderedDescending:NSOrderedSame;}

static BOOL ValidKey(NSData *key) {
    const uint8_t *bytes=key.bytes;
    if(key.length<8)return NO;
    switch(Get64(bytes)>>60) {
        case 3: case 6: case 12:return key.length==8;
        case 4:return key.length>10;
        case 9:return key.length>12;
        case 5: case 13:return key.length==16;
        default:return NO;
    }
}

NSComparisonResult VIMCompareBigSurRecordKeys(NSData *a,NSData *b) {
    const uint8_t *x=a.bytes,*y=b.bytes;
    if(!ValidKey(a)||!ValidKey(b))return Numeric(a.length,b.length);
    uint64_t first=Get64(x),second=Get64(y),kind=first>>60;
    NSComparisonResult order=Numeric(first&ObjectMask,second&ObjectMask);
    if(order!=NSOrderedSame)return order;
    order=Numeric(kind,second>>60);
    if(order!=NSOrderedSame)return order;
    if(kind==5||kind==13)return Numeric(Get64(x+8),Get64(y+8));
    size_t prefix=0;
    if(kind==9) {
        order=Numeric(Get32(x+8)>>10,Get32(y+8)>>10);
        if(order!=NSOrderedSame)return order;
        prefix=12;
    } else if(kind==4)prefix=10;
    else return NSOrderedSame;
    size_t n=a.length-prefix,m=b.length-prefix;
    int cmp=memcmp(x+prefix,y+prefix,MIN(n,m));
    return cmp<0?NSOrderedAscending:cmp>0?NSOrderedDescending:Numeric(n,m);
}

@interface VIMBigSurTreeNode : NSObject
@property uint64_t relative;
@property NSUInteger level;
@property NSData *key,*digest,*pivotHole;
@property NSMutableArray<VIMBigSurTreeNode *> *children;
@end
@implementation VIMBigSurTreeNode
@end

static NSUInteger Table(NSUInteger count) {return ((count+7)&~(NSUInteger)7)*Slot;}
static NSData *Hash(NSData *block) {
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(block.bytes,(CC_LONG)block.length,digest);
    return [NSData dataWithBytes:digest length:sizeof(digest)];
}
static void RecordSlot(uint8_t *slot,size_t key,size_t keySize,size_t value,size_t valueSize) {
    Put16(slot,(uint16_t)key);Put16(slot+2,(uint16_t)keySize);Put16(slot+4,(uint16_t)value);Put16(slot+6,(uint16_t)valueSize);
}
static void NodeHeader(uint8_t *node,uint16_t flags,uint16_t level,uint32_t count,uint16_t table,
    uint16_t freeOffset,uint16_t freeLength,uint16_t keyFree,uint16_t keyLength,uint16_t valueFree,uint16_t valueLength) {
    Put16(node+32,flags);Put16(node+34,level);Put32(node+36,count);
    Put16(node+42,table);Put16(node+44,freeOffset);Put16(node+46,freeLength);
    Put16(node+48,keyFree);Put16(node+50,keyLength);Put16(node+52,valueFree);Put16(node+54,valueLength);
}

@interface VIMBigSurTreeBuilder ()
@property VIMBigSurTreeNode *root;
@property NSMutableArray<NSArray<NSData *> *> *leaf;
@property NSData *firstLeaf,*previous;
@property(copy) BOOL (^sink)(uint64_t,NSData *);
@property uint64_t nextID,records;
@property NSUInteger leafBytes,maxKey,maxValue,lastLeafMax,incomingKeySize;
@property BOOL failed,finished;
@end

@implementation VIMBigSurTreeBuilder
- (instancetype)initWithSink:(BOOL (^)(uint64_t,NSData *))sink {
    if((self=[super init])){
        _root=[VIMBigSurTreeNode new];_leaf=[NSMutableArray array];_sink=[sink copy];_nextID=1;
    }
    return self;
}
- (VIMBigSurTreeNode *)nodeAtLevel:(NSUInteger)level key:(NSData *)key digest:(NSData *)digest
    children:(NSArray<VIMBigSurTreeNode *> *)children {
    VIMBigSurTreeNode *node=[VIMBigSurTreeNode new];node.relative=self.nextID++;
    node.level=level;node.key=key;node.digest=digest;node.children=children?[children mutableCopy]:[NSMutableArray array];
    return node;
}
- (VIMBigSurTreeNode *)rightmost:(NSUInteger)level {
    VIMBigSurTreeNode *node=self.root;
    while(node.level>level)node=node.children.lastObject;
    return node;
}
- (BOOL)roomIn:(VIMBigSurTreeNode *)node first:(NSUInteger)a second:(NSUInteger)b {
    NSUInteger count=node.children.count+2,used=Header+Table(count)+IndexValue*count+a+b;
    for(VIMBigSurTreeNode *child in node.children)used+=child.key.length;
    return used<=Block-(node==self.root?Footer:0);
}
- (void)split:(VIMBigSurTreeNode *)node {
    NSUInteger level=node.level;VIMBigSurTreeNode *pivot=node.children.lastObject;
    if(node.children.count<2){self.failed=YES;return;}
    if(node==self.root) {
        VIMBigSurTreeNode *right=[self nodeAtLevel:level key:pivot.key digest:nil children:@[pivot]];
        VIMBigSurTreeNode *left=[self nodeAtLevel:level key:node.key digest:nil
            children:[node.children subarrayWithRange:NSMakeRange(0,node.children.count-1)]];
        node.level++;node.children=[@[left,right] mutableCopy];
    } else {
        VIMBigSurTreeNode *parent=[self rightmost:level+1];
        if(![self roomIn:parent first:self.lastLeafMax second:self.incomingKeySize])[self split:parent];
        [node.children removeLastObject];node.pivotHole=pivot.key;
        VIMBigSurTreeNode *right=[self nodeAtLevel:level key:pivot.key digest:nil children:@[pivot]];
        [[[self rightmost:level+1] children] addObject:right];
    }
}
- (void)reserveUpper:(NSUInteger)length {
    self.maxKey=MAX(self.maxKey,length);
    if(self.root.level<2)return;
    VIMBigSurTreeNode *lower=[self rightmost:1],*upper=[self rightmost:2];
    if([self roomIn:lower first:self.maxKey second:self.maxKey])return;
    NSUInteger separator=length;
    for(VIMBigSurTreeNode *child in lower.children)separator=MAX(separator,child.key.length);
    if(![self roomIn:upper first:separator second:separator])[self split:upper];
}
- (BOOL)write:(uint64_t)relative bytes:(NSData *)block {return !self.sink||self.sink(relative,block);}
- (BOOL)flushLeaf {
    if(!self.leaf.count)return YES;
    uint8_t bytes[Block]={0};
    size_t table=Table(self.leaf.count),keyBase=Header+table,keys=keyBase,values=Block,index=0,maxKey=0;
    for(NSArray<NSData *> *record in self.leaf) {
        NSData *key=record[0],*value=record[1];values-=value.length;
        if(values<keys+key.length)return NO;
        RecordSlot(bytes+Header+index++*Slot,keys-keyBase,key.length,Block-values,value.length);
        memcpy(bytes+keys,key.bytes,key.length);memcpy(bytes+values,value.bytes,value.length);keys+=key.length;
        maxKey=MAX(maxKey,key.length);self.maxValue=MAX(self.maxValue,value.length);
    }
    NodeHeader(bytes,0x1a,0,(uint32_t)self.leaf.count,(uint16_t)table,(uint16_t)(keys-keyBase),
        (uint16_t)(values-keys),UINT16_MAX,0,UINT16_MAX,0);
    NSData *leaf=[NSData dataWithBytes:bytes length:sizeof(bytes)],*key=self.leaf.firstObject[0],*hash=Hash(leaf);
    self.records+=self.leaf.count;self.incomingKeySize=key.length;[self reserveUpper:key.length];
    if(!self.root.level&&!self.root.key) {
        self.root.key=key;self.root.digest=hash;self.firstLeaf=leaf;
        self.maxKey=MAX(self.maxKey,maxKey);
    } else {
        if(!self.root.level) {
            VIMBigSurTreeNode *right=[self nodeAtLevel:0 key:key digest:hash children:nil];
            VIMBigSurTreeNode *left=[self nodeAtLevel:0 key:self.root.key digest:self.root.digest children:nil];
            self.root.level=1;self.root.children=[@[left,right] mutableCopy];
            if(![self write:left.relative bytes:self.firstLeaf]||![self write:right.relative bytes:leaf])return NO;
            self.firstLeaf=nil;
        } else {
            VIMBigSurTreeNode *parent=[self rightmost:1];
            if(![self roomIn:parent first:key.length second:self.lastLeafMax])[self split:parent];
            VIMBigSurTreeNode *node=[self nodeAtLevel:0 key:key digest:hash children:nil];
            [[[self rightmost:1] children] addObject:node];
            if(![self write:node.relative bytes:leaf])return NO;
        }
        for(NSUInteger i=1;i<self.leaf.count;i++)[self reserveUpper:self.leaf[i][0].length];
    }
    self.lastLeafMax=maxKey;self.leafBytes=0;[self.leaf removeAllObjects];
    return !self.failed;
}
- (BOOL)appendKey:(NSData *)key value:(NSData *)value {
    if(self.finished||self.failed||!ValidKey(key)||!value.length||
       key.length+value.length>Block-Header-Table(1)-Footer||
       (self.previous&&VIMCompareBigSurRecordKeys(self.previous,key)!=NSOrderedAscending)) {
        self.failed=YES;return NO;
    }
    size_t maximum=Block-(self.root.key?0:Footer),record=key.length+value.length;
    if(self.leaf.count&&Header+Table(self.leaf.count+1)+self.leafBytes+record>maximum&&![self flushLeaf]) {
        self.failed=YES;return NO;
    }
    [self.leaf addObject:@[key,value]];self.leafBytes+=record;self.previous=key;
    return YES;
}
- (BOOL)writeIndex:(VIMBigSurTreeNode *)node {
    if(!node.level)return YES;
    for(VIMBigSurTreeNode *child in node.children)if(![self writeIndex:child])return NO;
    uint8_t bytes[Block]={0};
    BOOL root=node==self.root;NSData *hole=node.pivotHole;
    size_t count=node.children.count,table=Table(count+(hole.length!=0)),keyBase=Header+table;
    size_t end=Block-(root?Footer:0),keys=keyBase,values=end,index=0;
    for(VIMBigSurTreeNode *child in node.children) {
        if(values<IndexValue+keys+child.key.length||child.digest.length!=32)return NO;
        values-=IndexValue;
        RecordSlot(bytes+Header+index++*Slot,keys-keyBase,child.key.length,end-values,IndexValue);
        memcpy(bytes+keys,child.key.bytes,child.key.length);keys+=child.key.length;
        Put64(bytes+values,child.relative);memcpy(bytes+values+8,child.digest.bytes,32);
    }
    uint16_t freeKey=UINT16_MAX,freeValue=UINT16_MAX;
    if(hole.length) {
        if(hole.length<4||values<IndexValue+keys+hole.length)return NO;
        freeKey=(uint16_t)(keys-keyBase);
        memcpy(bytes+keys,hole.bytes,hole.length);
        Put16(bytes+keys,UINT16_MAX);Put16(bytes+keys+2,(uint16_t)hole.length);keys+=hole.length;
        values-=IndexValue;freeValue=(uint16_t)(end-values);
        Put16(bytes+values,UINT16_MAX);Put16(bytes+values+2,IndexValue);
        RecordSlot(bytes+Header+count*Slot,freeKey,hole.length,freeValue,IndexValue);
    }
    if(!count)return NO;
    NodeHeader(bytes,0x18|root,(uint16_t)node.level,(uint32_t)count,(uint16_t)table,(uint16_t)(keys-keyBase),
        (uint16_t)(values-keys),freeKey,(uint16_t)hole.length,freeValue,hole.length?IndexValue:0);
    if(root) {
        Put32(bytes+end,0x1c2);Put32(bytes+end+4,Block);
        Put32(bytes+end+16,(uint32_t)self.maxKey);Put32(bytes+end+20,(uint32_t)self.maxValue);
        Put64(bytes+end+24,self.records);Put64(bytes+end+32,self.nextID);
    }
    NSData *block=[NSData dataWithBytes:bytes length:sizeof(bytes)];
    node.digest=Hash(block);
    return [self write:node.relative bytes:block];
}
- (NSDictionary *)finishWithRootHash:(NSData *)expected error:(NSError **)error {
    BOOL ready=!self.failed&&!self.finished&&[self flushLeaf]&&self.root.level&&[self writeIndex:self.root];
    self.finished=YES;
    if(ready&&[self.root.digest isEqual:expected])
        return @{@"RootHash":self.root.digest,@"NodeCount":@(self.nextID),@"RecordCount":@(self.records)};
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurTree" code:EBADMSG userInfo:@{
        NSLocalizedDescriptionKey:ready?@"The rebuilt Big Sur tree does not match Apple’s original signed root.":
            @"Could not write the original Big Sur filesystem tree."}];
    return nil;
}
@end
