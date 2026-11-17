#import "VIMBigSurPhysicalTree.h"

enum {Block=4096,Header=56,Footer=40,LeafStep=112,InnerStep=140};
static void Put16(uint8_t *p,uint16_t n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void Put32(uint8_t *p,uint32_t n) {Put16(p,(uint16_t)n);Put16(p+2,(uint16_t)(n>>16));}
static void Put64(uint8_t *p,uint64_t n) {Put32(p,(uint32_t)n);Put32(p+4,(uint32_t)(n>>32));}
static uint32_t Get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t Checksum(const uint8_t *bytes) {
    uint64_t a=0,b=0;
    for(size_t i=8;i<Block;i+=4){a+=Get32(bytes+i);b+=a;}
    uint64_t first=UINT32_MAX-(a+b)%UINT32_MAX,second=UINT32_MAX-(a+first)%UINT32_MAX;
    return first|(second<<32);
}

static int CompareExtent(const void *lhs,const void *rhs) {
    const VIMBigSurExtent *a=lhs,*b=rhs;
    if(a->stream!=b->stream)return a->stream<b->stream?-1:1;
    if(a->offset!=b->offset)return a->offset<b->offset?-1:1;
    return 0;
}
static void AppendExtent(NSMutableData *output,VIMBigSurExtent extent) {
    uint8_t record[32];Put64(record,extent.stream);Put64(record+8,extent.offset);
    Put64(record+16,extent.length);Put64(record+24,extent.physical);
    [output appendBytes:record length:sizeof(record)];
}
NSData *VIMBigSurEncodeExtentRecords(NSMutableData *extents) {
    if(extents.length%sizeof(VIMBigSurExtent))return nil;
    size_t count=extents.length/sizeof(VIMBigSurExtent);
    VIMBigSurExtent *data=extents.mutableBytes,current={0};
    if(count)qsort(data,count,sizeof(*data),CompareExtent);
    NSMutableData *output=[NSMutableData dataWithCapacity:extents.length];
    for(size_t i=0;i<count;i++) {
        VIMBigSurExtent item=data[i];
        if(!item.stream||!item.physical||!item.length||((item.offset|item.length)&(Block-1))||
            item.length>=UINT64_C(1)<<56||item.offset>UINT64_MAX-item.length)return nil;
        if(current.stream==item.stream) {
            uint64_t end=current.offset+current.length;
            if(item.offset<end)return nil;
            if(item.offset==end&&item.physical==current.physical+current.length/Block){
                current.length+=item.length;
                if(current.length>=UINT64_C(1)<<56)return nil;
                continue;
            }
        }
        if(current.stream)AppendExtent(output,current);
        current=item;
    }
    if(current.stream)AppendExtent(output,current);
    return output;
}

static NSData *PackFixedNode(const uint8_t *rows,size_t count,uint64_t address,uint64_t xid,uint16_t level,bool root,
    uint32_t subtype,uint16_t keySize,uint16_t leafValueSize,uint32_t treeFlags,uint64_t records,uint64_t nodes) {
    size_t valueSize=level?8:leafValueSize;
    size_t table=(Block-Header)/(4+keySize+valueSize)*4,end=Block-(root?Footer:0),keys=Header+table;
    if(!address||!xid||keys>end||count>(end-keys)/(keySize+valueSize)||count>table/4)return nil;
    uint8_t block[Block]={0};
    Put64(block+8,address);Put64(block+16,xid);
    Put32(block+24,0x40000000|(root?2:3));Put32(block+28,subtype);
    Put16(block+32,4|(level?0:2)|root);Put16(block+34,level);Put32(block+36,(uint32_t)count);
    Put16(block+42,(uint16_t)table);Put16(block+44,(uint16_t)(keySize*count));
    Put16(block+46,(uint16_t)(end-keys-(keySize+valueSize)*count));
    Put16(block+48,UINT16_MAX);Put16(block+52,UINT16_MAX);
    for(size_t i=0;i<count;i++) {
        Put16(block+Header+i*4,(uint16_t)(i*keySize));
        Put16(block+Header+i*4+2,(uint16_t)((i+1)*valueSize));
        memcpy(block+keys+i*keySize,rows,keySize);
        memcpy(block+end-(i+1)*valueSize,rows+keySize,valueSize);
        rows+=keySize+valueSize;
    }
    if(root) {
        Put32(block+end,treeFlags);Put32(block+end+4,Block);Put32(block+end+8,keySize);
        Put32(block+end+12,leafValueSize);Put32(block+end+16,keySize);Put32(block+end+20,leafValueSize);
        Put64(block+end+24,records);Put64(block+end+32,nodes);
    }
    Put64(block,Checksum(block));
    return [NSData dataWithBytes:block length:sizeof(block)];
}

uint64_t VIMBigSurPhysicalTreeBlockCount(uint64_t rows) {
    uint64_t count=1;
    if(rows>LeafStep-1) {
        rows=rows/LeafStep+(rows%LeafStep!=0);count+=rows;
        while(rows>InnerStep){rows=rows/InnerStep+(rows%InnerStep!=0);count+=rows;}
    }
    return count;
}

uint64_t VIMBigSurWritePhysicalTree(NSData *records,uint64_t start,uint64_t xid,uint32_t subtype,
    BOOL (^sink)(uint64_t,NSData *)) {
    if(!records||records.length%32||!start||!xid||!sink)return 0;
    uint64_t address=start;size_t count=records.length/32,original=count;uint16_t level=0;
    if(VIMBigSurPhysicalTreeBlockCount(count)>UINT64_MAX-start)return 0;
    NSData *rows=records;
    while(count>(level?InnerStep:LeafStep-1)) {
        size_t step=level?InnerStep:LeafStep,stride=level?24:32;
        NSMutableData *parents=[NSMutableData dataWithCapacity:(count/step+1)*24];
        const uint8_t *bytes=rows.bytes;
        for(size_t first=0;first<count;first+=step) @autoreleasepool {
            const uint8_t *entry=bytes+first*stride;
            NSData *block=PackFixedNode(entry,MIN(step,count-first),address,xid,level,false,subtype,16,16,0x12,0,0);
            if(!block||!sink(address,block))return 0;
            uint8_t parent[24];memcpy(parent,entry,16);Put64(parent+16,address++);
            [parents appendBytes:parent length:sizeof(parent)];
        }
        rows=parents;count=parents.length/24;level++;
    }
    NSData *root=PackFixedNode(rows.bytes,count,address,xid,level,true,subtype,16,16,0x12,original,address-start+1);
    return root&&sink(address,root)?address:0;
}

NSData *VIMBigSurSnapshotMapBlock(uint64_t address,uint64_t xid) {
    uint8_t row[24]={0};Put64(row,xid);
    return PackFixedNode(row,1,address,xid,0,true,19,8,16,0x10,1,1);
}
