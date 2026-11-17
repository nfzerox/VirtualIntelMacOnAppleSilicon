#import "VIMBigSurSnapshot.h"
#import "VIMBigSurPhysicalTree.h"
#import "VIMBigSurSealTree.h"
#import "VIMAPFSCheckpoint.h"
#import "VIMAPFSFirmlinks.h"
#import "VIMAPFSFreeQueue.h"
#import "VIMAPFSSpaceMap.h"
#import "VIMHFSCatalog.h"
#import "VIMDiskImageIO.h"
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

enum { Block=4096,Physical=0x40000000,OMAP=11,ExtentRef=15,SnapshotMetadata=16 };
static const uint64_t Mask=(UINT64_C(1)<<60)-1;
static uint32_t Get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t Get64(const uint8_t *p) {return Get32(p)|(uint64_t)Get32(p+4)<<32;}
static void Put16(uint8_t *p,uint16_t n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void Put32(uint8_t *p,uint32_t n) {Put16(p,(uint16_t)n);Put16(p+2,(uint16_t)(n>>16));}
static void Put64(uint8_t *p,uint64_t n) {Put32(p,(uint32_t)n);Put32(p+4,(uint32_t)(n>>32));}
static NSData *Checksummed(NSMutableData *block) {
    uint8_t *p=block.mutableBytes;uint64_t a=0,b=0;
    for(size_t i=8;i<Block;i+=4){a+=Get32(p+i);b+=a;}
    uint64_t first=UINT32_MAX-(a+b)%UINT32_MAX,second=UINT32_MAX-(a+first)%UINT32_MAX;
    Put64(p,first|(second<<32));return block;
}
static id Failure(NSError **error,NSString *message) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurSnapshot" code:EBADMSG userInfo:@{
        NSLocalizedDescriptionKey:message}];
    return nil;
}
static NSMutableData *Object(uint64_t oid,uint64_t xid,uint32_t type) {
    NSMutableData *block=[NSMutableData dataWithLength:Block];uint8_t *p=block.mutableBytes;
    Put64(p+8,oid);Put64(p+16,xid);Put32(p+24,type);return block;
}
static NSData *VariableLeaf(uint64_t address,uint64_t xid,uint32_t subtype,NSArray<NSArray<NSData *> *> *rows) {
    NSMutableData *block=Object(address,xid,Physical|2);uint8_t *p=block.mutableBytes;
    Put32(p+28,subtype);size_t count=rows.count,table=MAX(8,(count+7)&~(size_t)7)*8;
    size_t keys=56+table,values=Block-40,keyUsed=0,valueUsed=0,maxKey=0,maxValue=0;
    if(keys>values)return nil;
    for(size_t i=0;i<count;i++) {
        NSData *k=rows[i][0],*v=rows[i][1];valueUsed+=v.length;
        if(keys+keyUsed+k.length+valueUsed>values)return nil;
        uint8_t *slot=p+56+8*i;Put16(slot,(uint16_t)keyUsed);Put16(slot+2,(uint16_t)k.length);
        Put16(slot+4,(uint16_t)valueUsed);Put16(slot+6,(uint16_t)v.length);
        memcpy(p+keys+keyUsed,k.bytes,k.length);memcpy(p+values-valueUsed,v.bytes,v.length);
        keyUsed+=k.length;maxKey=MAX(maxKey,k.length);maxValue=MAX(maxValue,v.length);
    }
    Put16(p+32,3);Put32(p+36,(uint32_t)count);Put16(p+42,(uint16_t)table);Put16(p+44,(uint16_t)keyUsed);
    Put16(p+46,(uint16_t)(values-keys-keyUsed-valueUsed));Put16(p+48,UINT16_MAX);Put16(p+52,UINT16_MAX);
    Put32(p+values,0x52);Put32(p+values+4,Block);Put32(p+values+16,(uint32_t)maxKey);Put32(p+values+20,(uint32_t)maxValue);
    Put64(p+values+24,count);Put64(p+values+32,1);
    return Checksummed(block);
}
static NSData *ObjectMap(uint64_t address,uint64_t xid,uint64_t root,uint32_t flags,uint64_t snapshot) {
    NSMutableData *block=Object(address,xid,Physical|OMAP);uint8_t *p=block.mutableBytes;
    Put32(p+32,flags);Put32(p+36,snapshot!=0);Put32(p+40,Physical|2);Put32(p+44,Physical|2);
    Put64(p+48,root);Put64(p+56,snapshot);Put64(p+64,snapshot?xid:0);
    return Checksummed(block);
}
static void AppendMap(NSMutableData *rows,uint64_t id,uint64_t xid,uint64_t address,uint32_t flags) {
    uint8_t row[32]={0};Put64(row,id);Put64(row+8,xid);Put32(row+16,flags);Put32(row+20,Block);Put64(row+24,address);
    [rows appendBytes:row length:sizeof(row)];
}
static int OrderMap(const void *a,const void *b) {
    uint64_t x=Get64(a),y=Get64(b);
    if(x==y){x=Get64((const uint8_t *)a+8);y=Get64((const uint8_t *)b+8);}
    return x<y?-1:x>y?1:0;
}
static NSData *ReadTreeBlock(int fd,uint64_t relative) {
    NSMutableData *block=[NSMutableData dataWithLength:Block];uint8_t *p=block.mutableBytes;
    off_t offset=(off_t)(relative*Block);
    for(size_t done=0;done<Block;) {
        ssize_t n=pread(fd,p+done,Block-done,offset+done);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)return nil;
        done+=(size_t)n;
    }
    return block;
}
static NSArray *SnapshotRecords(uint64_t xid,uint64_t frozen,uint64_t extentRef,uint64_t stamp,uint64_t inum,NSData *rootHash) {
    NSMutableString *name=[NSMutableString stringWithString:@"com.apple.os.update-"];
    const uint8_t *hash=rootHash.bytes;
    for(NSUInteger i=0;i<rootHash.length;i++)[name appendFormat:@"%02X",hash[i]];
    const char *text=name.UTF8String;size_t length=strlen(text)+1;
    uint8_t idKey[8],namedKey[10];Put64(idKey,(UINT64_C(1)<<60)|xid);
    Put64(namedKey,(UINT64_C(11)<<60)|Mask);Put16(namedKey+8,(uint16_t)length);
    uint8_t metadata[50]={0},idValue[8];Put64(idValue,xid);
    Put64(metadata,extentRef);Put64(metadata+8,frozen);Put64(metadata+16,stamp);
    Put64(metadata+24,stamp);Put64(metadata+32,inum);Put32(metadata+40,Physical|2);Put16(metadata+48,(uint16_t)length);
    NSMutableData *byID=[NSMutableData dataWithBytes:metadata length:sizeof(metadata)];
    NSMutableData *byName=[NSMutableData dataWithBytes:namedKey length:sizeof(namedKey)];
    [byID appendBytes:text length:length];[byName appendBytes:text length:length];
    return @[@[[NSData dataWithBytes:idKey length:8],byID],@[byName,[NSData dataWithBytes:idValue length:8]]];
}

NSDictionary *VIMPlanBigSurSnapshot(NSURL *raw,NSUUID *system,NSUUID *snapshot,
    NSURL *authenticatedTree,uint64_t nodeCount,NSData *rootHash,
    NSDictionary<NSNumber *,NSNumber *> *ids,BOOL (^cancelled)(void),NSError **error) {
    if(!system||!snapshot||rootHash.length!=32||!nodeCount||nodeCount>INT64_MAX/Block)
        return Failure(error,@"The authenticated Big Sur snapshot input is incomplete.");
    uint64_t offset=0,size=0;
    if(!VIMGetAPFSPartition(raw,&offset,&size,error))return nil;
    VIMAPFSCheckpoint *checkpoint=[[VIMAPFSCheckpoint alloc] initWithRawImage:raw error:error];
    if(!checkpoint)return nil;
    NSDictionary *source=VIMReadBigSurSnapshotSource(raw,offset,size,system,error);
    if(!source)return nil;
    NSData *extents=VIMReadBigSurExtentRecords(raw,offset,size,system,ids,error);
    if(!extents)return nil;
    uint64_t xid=checkpoint.nextTransactionID,rootOID=Get64((const uint8_t *)checkpoint.superblock.bytes+88);
    if(xid>Mask||rootOID>Mask-nodeCount-2||[source[@"Transaction"] unsignedLongLongValue]!=checkpoint.transactionID)
        return Failure(error,@"The Big Sur snapshot source changed or exhausted its object IDs.");
    const uint8_t *old=[source[@"System"] bytes];
    NSMutableData *nxRows=[source[@"ContainerRows"] mutableCopy];
    uint64_t fext=VIMBigSurPhysicalTreeBlockCount(extents.length/32),vmap=VIMBigSurPhysicalTreeBlockCount(nodeCount+2),
        nmap=VIMBigSurPhysicalTreeBlockCount(nxRows.length/32+1);
    uint64_t count=nodeCount+fext*2+vmap+nmap+9;
    NSDictionary *allocation=VIMPlanAPFSAllocation(checkpoint,count,error);
    if(!allocation)return nil;
    uint64_t start=[allocation[@"Start"] unsignedLongLongValue],retired=[source[@"RetiredSystemCount"] unsignedLongLongValue];
    uint64_t added=count-nmap-3;
    if(!start||count>checkpoint.blockCount-start||retired>Get64(old+88)||
        Get64(old+88)-retired>UINT64_MAX-added||Get64(old+224)>UINT64_MAX-added||Get64(old+232)>UINT64_MAX-retired||
        Get64(old+176)==UINT64_MAX)return Failure(error,@"The planned Big Sur snapshot allocation is invalid.");
    NSMutableArray<NSData *> *blocks=[NSMutableArray arrayWithCapacity:(NSUInteger)count];
    uint64_t (^append)(NSData *)=^uint64_t(NSData *block) {
        if(block.length!=Block||blocks.count>=count)return 0;
        uint64_t address=start+blocks.count;[blocks addObject:block];return address;
    };
    BOOL (^sink)(uint64_t,NSData *)=^BOOL(uint64_t address,NSData *block) {
        return address==start+blocks.count&&append(block)==address;
    };
    NSMutableData *vmRows=[NSMutableData dataWithCapacity:(NSUInteger)(nodeCount+2)*32];
    int fd=open(authenticatedTree.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    struct stat file;
    @try {
        if(fd<0||fstat(fd,&file)||!S_ISREG(file.st_mode)||(uint64_t)file.st_size!=nodeCount*Block)
            return Failure(error,@"The authenticated Big Sur filesystem tree is not available.");
        for(uint64_t relative=0;relative<nodeCount;relative++) @autoreleasepool {
            if(!(relative&1023)&&cancelled&&cancelled())return Failure(error,@"Installation cancelled.");
            uint64_t block=append(ReadTreeBlock(fd,relative));
            if(!block)return Failure(error,@"Could not read the authenticated Big Sur tree.");
            AppendMap(vmRows,rootOID+relative,xid,block,8);
        }
    } @finally {if(fd>=0)close(fd);}
    uint64_t integrityID=rootOID+nodeCount,extensionID=integrityID+1;
    NSMutableData *integrity=Object(integrityID,xid,30);uint8_t *integ=integrity.mutableBytes;
    Put32(integ+32,2);Put32(integ+40,1);Put32(integ+44,128);memcpy(integ+128,rootHash.bytes,32);
    AppendMap(vmRows,integrityID,xid,append(Checksummed(integrity)),0);
    NSMutableData *extension=Object(extensionID,xid,29);uint8_t *ext=extension.mutableBytes;
    Put32(ext+32,1);Put64(ext+40,xid);[snapshot getUUIDBytes:ext+48];
    AppendMap(vmRows,extensionID,xid,append(Checksummed(extension)),0);
    uint64_t frozenFext=VIMBigSurWritePhysicalTree(extents,start+blocks.count,xid,31,sink);
    uint64_t liveFext=frozenFext?VIMBigSurWritePhysicalTree(extents,start+blocks.count,xid,31,sink):0;
    uint64_t vRoot=liveFext?VIMBigSurWritePhysicalTree(vmRows,start+blocks.count,xid,OMAP,sink):0;
    if(!vRoot)return Failure(error,@"Could not place the Big Sur extent and object trees.");
    uint64_t snapMap=append(VIMBigSurSnapshotMapBlock(start+blocks.count,xid));
    uint64_t vMap=append(ObjectMap(start+blocks.count,xid,vRoot,0,snapMap));
    uint64_t liveRef=append(VariableLeaf(start+blocks.count,xid,ExtentRef,@[]));
    if(!snapMap||!vMap||!liveRef)return Failure(error,@"Could not place the Big Sur snapshot support blocks.");
    NSMutableData *live=[source[@"System"] mutableCopy];uint8_t *l=live.mutableBytes;
    Put64(l+16,xid);Put64(l+56,Get64(old+56)|0x20);Put64(l+88,Get64(old+88)-retired+added);
    Put64(l+128,vMap);Put64(l+136,rootOID);Put64(l+216,1);
    Put64(l+224,Get64(old+224)+added);Put64(l+232,Get64(old+232)+retired);
    Put64(l+0x3e8,extensionID);Put64(l+0x400,integrityID);Put64(l+0x408,liveFext);
    NSMutableData *frozen=[live mutableCopy];uint8_t *f=frozen.mutableBytes;
    Put64(f+8,start+blocks.count);Put32(f+24,Physical|13);Put32(f+28,0);
    Put64(f+128,0);Put64(f+144,0);Put64(f+152,0);Put64(f+0x408,frozenFext);
    uint64_t frozenAddress=append(Checksummed(frozen)),inum=Get64(old+176);
    uint64_t snapRoot=append(VariableLeaf(start+blocks.count,xid,SnapshotMetadata,
        SnapshotRecords(xid,frozenAddress,Get64(old+144),Get64(old+256),inum,rootHash)));
    if(!frozenAddress||!snapRoot)return Failure(error,@"Could not place the frozen Big Sur System header.");
    Put64(l+144,liveRef);Put64(l+152,snapRoot);Put64(l+176,inum+1);Put64(l+0x3c8,xid);
    uint64_t liveAddress=append(Checksummed(live));
    if(!liveAddress)return Failure(error,@"Could not place the live Big Sur System header.");
    AppendMap(nxRows,Get64(old+8),xid,liveAddress,0);
    qsort(nxRows.mutableBytes,nxRows.length/32,32,OrderMap);
    uint64_t nxRoot=VIMBigSurWritePhysicalTree(nxRows,start+blocks.count,xid,OMAP,sink);
    uint64_t nextMap=nxRoot?append(ObjectMap(start+blocks.count,xid,nxRoot,[source[@"ContainerMapFlags"] unsignedIntValue],0)):0;
    if(!nextMap||blocks.count!=count)return Failure(error,@"The Big Sur transaction did not fit its reserved APFS blocks.");
    NSMutableData *nx=[checkpoint.superblock mutableCopy];uint8_t *super=nx.mutableBytes;
    Put64(super+88,extensionID+1);Put64(super+160,nextMap);
    NSArray *commit=VIMAPFSRetirementWrites(checkpoint,allocation,source[@"RetiredRanges"],nx,error);
    if(!commit)return nil;
    return @{@"Start":@(start),@"Blocks":blocks,@"CheckpointWrites":commit,@"OldTransaction":@(checkpoint.transactionID),
        @"Transaction":@(xid),@"VolumeUUID":system.UUIDString,@"SnapshotUUID":snapshot.UUIDString,@"RootHash":rootHash,
        @"RootObject":@(rootOID),@"NodeCount":@(nodeCount)};
}

static BOOL WriteTreeBlock(VIMDiskImageIO *image,uint64_t offset,NSData *block) {
    return block.length==Block&&[image writeAt:offset bytes:block.bytes length:Block];
}

BOOL VIMPublishBigSurSnapshot(NSURL *raw,NSUUID *system,NSUUID *snapshot,
    NSURL *authenticatedTree,uint64_t nodeCount,NSData *rootHash,NSURL *mtree,
    NSDictionary<NSNumber *,NSNumber *> *ids,BOOL (^cancelled)(void),NSError **error) {
    VIMDiskImageIO *image=[[VIMDiskImageIO alloc] initWithURL:raw writable:YES error:error];
    if(!image)return NO;
    uint64_t base=0,size=0;
    if(!VIMGetAPFSPartition(raw,&base,&size,error))return NO;
    NSDictionary *plan=VIMPlanBigSurSnapshot(raw,system,snapshot,authenticatedTree,nodeCount,rootHash,ids,cancelled,error);
    if(!plan)return NO;
    NSArray<NSData *> *blocks=plan[@"Blocks"];
    NSArray<NSDictionary *> *checkpoint=plan[@"CheckpointWrites"];
    uint64_t start=[plan[@"Start"] unsignedLongLongValue],limit=size/Block;
    if(!start||start>=limit||blocks.count>limit-start||checkpoint.count<2||
       ![checkpoint.lastObject[@"Block"] isEqual:@0]) {
        Failure(error,@"The planned Big Sur snapshot does not fit the detached partition.");return NO;
    }
    NSMutableSet<NSNumber *> *seen=[NSMutableSet set];
    for(NSDictionary *write in checkpoint) {
        NSNumber *address=write[@"Block"];
        uint64_t block=address.unsignedLongLongValue;
        if(block>=limit||(block>=start&&block-start<blocks.count)||[seen containsObject:address]||
           [write[@"Bytes"] length]!=Block) {
            Failure(error,@"The Big Sur snapshot checkpoint overlaps another write.");return NO;
        }
        [seen addObject:address];
    }
    BOOL written=YES;
    for(NSUInteger i=0;i<blocks.count;i++) {
        if(!(i&1023)&&cancelled&&cancelled()) {
            Failure(error,@"Installation cancelled.");return NO;
        }
        if(blocks[i].length!=Block||!WriteTreeBlock(image,base+(start+i)*Block,blocks[i])){written=NO;break;}
    }
    if(cancelled&&cancelled()){Failure(error,@"Installation cancelled.");return NO;}
    // Do not cancel after writing the successor ring. The old checkpoint
    // remains authoritative until its replacement is fully committed.
    for(NSUInteger i=0;written&&i<checkpoint.count;i++) {
        if(i+2>=checkpoint.count&&![image synchronize]){written=NO;break;}
        NSDictionary *write=checkpoint[i];
        written=WriteTreeBlock(image,base+[write[@"Block"] unsignedLongLongValue]*Block,write[@"Bytes"]);
    }
    if(!written||![image synchronize]) {
        Failure(error,@"Could not commit the detached Big Sur snapshot checkpoint.");return NO;
    }
    return VIMVerifyBigSurPublishedSnapshot(raw,base,size,system,snapshot,[plan[@"Transaction"] unsignedLongLongValue],
        nodeCount,rootHash,mtree,cancelled,error);
}
