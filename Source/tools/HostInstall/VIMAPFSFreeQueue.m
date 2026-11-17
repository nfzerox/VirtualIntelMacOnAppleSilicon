#import "VIMAPFSFreeQueue.h"
#import "VIMAPFSCheckpoint.h"

enum { Block=4096, QueueType=9, TableSize=((Block-56)/(4+16+8))*4, Fanout=120, Ghost=UINT16_MAX };
static uint16_t U16(const uint8_t *p) {return (uint16_t)p[0]|(uint16_t)p[1]<<8;}
static uint32_t U32(const uint8_t *p) {return U16(p)|(uint32_t)U16(p+2)<<16;}
static uint64_t U64(const uint8_t *p) {return U32(p)|(uint64_t)U32(p+4)<<32;}
static void Put16(uint8_t *p,uint16_t v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void Put32(uint8_t *p,uint32_t v) {Put16(p,(uint16_t)v);Put16(p+2,(uint16_t)(v>>16));}
static void Put64(uint8_t *p,uint64_t v) {Put32(p,(uint32_t)v);Put32(p+4,(uint32_t)(v>>32));}
static id Invalid(NSError **error) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.APFSFreeQueue" code:1
        userInfo:@{NSLocalizedDescriptionKey:@"The detached APFS free queue could not be updated safely."}];
    return nil;
}

typedef NSArray<NSNumber *> QueueRow;
static BOOL ReadQueue(VIMAPFSCheckpoint *disk,uint64_t oid,BOOL root,int expectedLevel,
                      NSMutableSet<NSNumber *> *seen,NSMutableArray<QueueRow *> *rows) {
    NSNumber *object=@(oid);
    if(!oid||[seen containsObject:object])return NO;
    [seen addObject:object];
    NSData *node=[disk readEphemeral:oid volume:0];const uint8_t *p=node.bytes;
    if(!p||U32(p+28)!=QueueType)return NO;
    uint16_t flags=U16(p+32),level=U16(p+34),offset=U16(p+40),length=U16(p+42);
    uint32_t count=U32(p+36),keys=56+offset+length,end=Block-(root?40:0);
    if(level>16||(expectedLevel>=0&&level!=expectedLevel)||(flags&7)!=(4|(level?0:2)|root)||
        keys>end||count>length/4||(level&&!count))return NO;
    for(uint32_t i=0;i<count;i++) {
        const uint8_t *entry=p+56+offset+i*4;
        uint16_t key=U16(entry),value=U16(entry+2);
        if(key+16U>end-keys||(value!=Ghost&&(value<8||value>end-keys)))return NO;
        if(level) {
            if(value==Ghost||!ReadQueue(disk,U64(p+end-value),NO,level-1,seen,rows))return NO;
        } else [rows addObject:@[@(U64(p+keys+key)),@(U64(p+keys+key+8)),@(value==Ghost?1:U64(p+end-value))]];
    }
    return YES;
}

static NSData *Node(uint64_t oid,uint64_t xid,uint16_t level,NSArray<QueueRow *> *rows,
                    BOOL root,uint64_t totalRows,uint64_t totalNodes) {
    NSMutableData *data=[NSMutableData dataWithLength:Block];uint8_t *p=data.mutableBytes;
    Put64(p+8,oid);Put64(p+16,xid);Put32(p+24,root?0x80000002:0x80000003);Put32(p+28,QueueType);
    const size_t keys=56+TableSize,end=Block-(root?40:0);
    size_t values=end,keyBytes=rows.count*16;
    if(rows.count>TableSize/4||keyBytes>end-keys)return nil;
    for(NSUInteger i=0;i<rows.count;i++) {
        QueueRow *row=rows[i];uint64_t value=[row[2] unsignedLongLongValue];
        Put64(p+keys+i*16,[row[0] unsignedLongLongValue]);Put64(p+keys+i*16+8,[row[1] unsignedLongLongValue]);
        uint16_t off=Ghost;
        if(level||value!=1) {
            if(values<keys+keyBytes+8)return nil;
            values-=8;Put64(p+values,value);off=(uint16_t)(end-values);
        }
        Put16(p+56+i*4,(uint16_t)(i*16));Put16(p+58+i*4,off);
    }
    Put16(p+32,4|(level?0:2)|root);Put16(p+34,level);Put32(p+36,(uint32_t)rows.count);
    Put16(p+42,TableSize);Put16(p+44,(uint16_t)keyBytes);Put16(p+46,(uint16_t)(values-keys-keyBytes));
    Put16(p+48,Ghost);Put16(p+52,Ghost);
    if(root) {
        Put32(p+end,0xe);Put32(p+end+4,Block);Put32(p+end+8,16);Put32(p+end+12,8);
        Put32(p+end+16,16);Put32(p+end+20,8);Put64(p+end+24,totalRows);Put64(p+end+32,totalNodes);
    }
    return data;
}

static NSComparisonResult OrderRows(QueueRow *a,QueueRow *b) {
    NSComparisonResult r=[a[0] compare:b[0]];
    if(r==NSOrderedSame)r=[a[1] compare:b[1]];
    return r==NSOrderedSame?[a[2] compare:b[2]]:r;
}

static BOOL QueueChanges(VIMAPFSCheckpoint *disk,uint8_t *header,NSArray<NSArray<NSNumber *> *> *ranges,uint64_t *next,
    NSMutableDictionary<NSArray<NSNumber *> *,NSData *> *writes,NSMutableSet<NSArray<NSNumber *> *> *discard) {
    uint64_t root=U64(header+8),xid=disk.nextTransactionID;
    uint16_t limit=U16(header+24);
    NSMutableSet<NSNumber *> *old=[NSMutableSet set];NSMutableArray<QueueRow *> *rows=[NSMutableArray array];
    if(!limit||!ReadQueue(disk,root,YES,-1,old,rows))return NO;
    uint64_t oldCount=0;
    for(QueueRow *row in rows) {
        uint64_t length=[row[2] unsignedLongLongValue];
        if(!length||length>UINT64_MAX-oldCount)return NO;
        oldCount+=length;
    }
    if(oldCount!=U64(header)||U64(header+16)!=[rows.firstObject.firstObject unsignedLongLongValue])return NO;
    for(NSArray<NSNumber *> *range in ranges) {
        if(range.count!=2)return NO;
        [rows addObject:@[@(xid),range[0],range[1]]];
    }
    [rows sortUsingComparator:^NSComparisonResult(QueueRow *a,QueueRow *b){return OrderRows(a,b);}];
    NSMutableArray<QueueRow *> *combined=[NSMutableArray array];uint64_t total=0;
    for(QueueRow *row in rows) {
        uint64_t when=[row[0] unsignedLongLongValue],start=[row[1] unsignedLongLongValue],length=[row[2] unsignedLongLongValue];
        if(!when||!start||!length||start>=disk.blockCount||length>disk.blockCount-start||length>UINT64_MAX-total)return NO;
        total+=length;
        QueueRow *last=combined.lastObject;
        if(when==[last[0] unsignedLongLongValue]) {
            uint64_t end=[last[1] unsignedLongLongValue]+[last[2] unsignedLongLongValue];
            if(start<end)return NO;
            if(start==end){[combined removeLastObject];[combined addObject:@[last[0],last[1],@([last[2] unsignedLongLongValue]+length)]];continue;}
        }
        [combined addObject:row];
    }
    uint64_t lastEnd=0;
    for(QueueRow *row in [combined sortedArrayUsingComparator:^NSComparisonResult(QueueRow *a,QueueRow *b){return [a[1] compare:b[1]];}]) {
        uint64_t start=[row[1] unsignedLongLongValue];
        if(start<lastEnd)return NO;
        lastEnd=start+[row[2] unsignedLongLongValue];
    }
    NSUInteger rootCapacity=Fanout;
    if(limit==1&&combined.count<=TableSize/4) {
        NSUInteger used=TableSize+combined.count*16;
        for(QueueRow *row in combined)if([row[2] unsignedLongLongValue]!=1)used+=8;
        if(used<=Block-56-40)rootCapacity=MAX(rootCapacity,combined.count);
    }
    [old removeObject:@(root)];
    NSArray<NSNumber *> *reuse=[[old allObjects] sortedArrayUsingSelector:@selector(compare:)];
    NSUInteger cursor=0,nodes=1;uint16_t level=0;
    NSArray<QueueRow *> *items=combined;
    while(items.count>(level?Fanout:rootCapacity)) {
        NSMutableArray<QueueRow *> *parents=[NSMutableArray array];
        for(NSUInteger i=0;i<items.count;i+=Fanout) {
            if(++nodes>limit)return NO;
            uint64_t oid;
            if(cursor<reuse.count)oid=reuse[cursor++].unsignedLongLongValue;
            else {if(*next==UINT64_MAX)return NO;oid=(*next)++;}
            NSArray *key=@[@0,@(oid)];
            NSData *node=Node(oid,xid,level,[items subarrayWithRange:NSMakeRange(i,MIN(Fanout,items.count-i))],NO,0,0);
            if(!node||writes[key])return NO;
            writes[key]=node;[parents addObject:@[items[i][0],items[i][1],@(oid)]];
        }
        level++;items=parents;
    }
    NSArray *rootKey=@[@0,@(root)];NSData *node=Node(root,xid,level,items,YES,combined.count,nodes);
    if(!node||writes[rootKey])return NO;
    writes[rootKey]=node;
    for(NSNumber *oid in old)if(!writes[@[@0,oid]])[discard addObject:@[@0,oid]];
    Put64(header,total);Put64(header+16,[combined.firstObject.firstObject unsignedLongLongValue]);
    return YES;
}

NSArray<NSDictionary *> *VIMAPFSRetirementWrites(VIMAPFSCheckpoint *disk,NSDictionary *allocation,
    NSArray<NSArray<NSNumber *> *> *retiredMain,NSData *superblock,NSError **error) {
    NSMutableData *sm=[allocation[@"Spaceman"] mutableCopy],*nx=[(superblock?:disk.superblock) mutableCopy];
    if(sm.length!=Block||nx.length!=Block||!allocation[@"SpacemanOID"])return Invalid(error);
    uint8_t *p=sm.mutableBytes,*n=nx.mutableBytes;uint64_t next=U64(n+88);
    NSMutableDictionary *replacements=[NSMutableDictionary dictionaryWithObject:sm forKey:@[@0,allocation[@"SpacemanOID"]]];
    NSMutableSet *discard=[NSMutableSet set];NSMutableArray *internal=[NSMutableArray array];
    for(NSNumber *block in allocation[@"RetiredInternal"])[internal addObject:@[block,@1]];
    if((internal.count&&!QueueChanges(disk,p+200,internal,&next,replacements,discard))||
       (retiredMain.count&&!QueueChanges(disk,p+240,retiredMain,&next,replacements,discard)))return Invalid(error);
    Put64(n+88,next);
    NSArray *successor=[disk successorWithObjects:replacements discarding:discard superblock:nx error:error];
    if(!successor)return nil;
    NSDictionary *writes=allocation[@"Writes"];
    NSMutableArray *plan=[NSMutableArray array];
    for(NSNumber *address in [writes.allKeys sortedArrayUsingSelector:@selector(compare:)])
        [plan addObject:@{@"Block":address,@"Bytes":writes[address]}];
    [plan addObjectsFromArray:successor];
    return plan.copy;
}
