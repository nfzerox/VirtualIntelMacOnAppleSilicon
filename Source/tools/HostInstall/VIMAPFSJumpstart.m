#import "VIMAPFSJumpstart.h"
#import "VIMAPFSCheckpoint.h"
#import "VIMAPFSFreeQueue.h"
#import "VIMAPFSSpaceMap.h"
#import "VIMHFSCatalog.h"
#import "VIMDiskImageIO.h"
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

enum { Block=4096, JumpstartField=0x4f8 };
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
static BOOL Failure(NSError **error,NSString *message) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.APFSJumpstart" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];
    return NO;
}
BOOL VIMIsIntelAPFSDriver(NSData *driver) {
    const uint8_t *p=driver.bytes;
    if(driver.length<64||driver.length>(16<<20)||memcmp(p,"MZ",2))return NO;
    uint32_t pe=U32(p+0x3c);
    return pe>=64&&pe<=driver.length&&driver.length-pe>=94&&
        !memcmp(p+pe,"PE\0\0\x64\x86",6)&&!memcmp(p+pe+24,"\x0b\x02",2)&&!memcmp(p+pe+92,"\x0b\0",2);
}
static BOOL WriteBlock(VIMDiskImageIO *image,uint64_t position,NSData *data) {
    return data.length==Block&&[image writeAt:position bytes:data.bytes length:Block];
}

BOOL VIMPublishAPFSJumpstart(NSURL *raw,NSData *driver,NSError **error) {
    if(!VIMIsIntelAPFSDriver(driver))return Failure(error,@"The boot driver is not an Intel EFI boot-service image.");
    VIMDiskImageIO *image=[[VIMDiskImageIO alloc] initWithURL:raw writable:YES error:error];
    if(!image)return NO;
    uint64_t base=0,length=0;
    if(!VIMGetAPFSPartition(raw,&base,&length,error))return NO;
    VIMAPFSCheckpoint *disk=[[VIMAPFSCheckpoint alloc] initWithRawImage:raw error:error];
    if(!disk)return NO;
    if(U64((const uint8_t *)disk.superblock.bytes+JumpstartField))return Failure(error,@"The new APFS disk already has an EFI driver.");
    uint64_t count=(driver.length+Block-1)/Block;
    NSDictionary *allocation=VIMPlanAPFSAllocation(disk,count+1,error);
    if(!allocation)return NO;
    uint64_t start=[allocation[@"Start"] unsignedLongLongValue];
    NSMutableData *nx=[disk.superblock mutableCopy];Put64((uint8_t *)nx.mutableBytes+JumpstartField,start);
    NSArray<NSDictionary *> *plan=VIMAPFSRetirementWrites(disk,allocation,nil,nx,error);
    if(!plan)return NO;
    if(!start||start>=disk.blockCount||count>=disk.blockCount-start||length/Block!=disk.blockCount||plan.count<2||
        [plan.lastObject[@"Block"] unsignedLongLongValue]!=0)return Failure(error,@"The planned APFS boot-driver allocation is invalid.");
    NSMutableSet *seen=[NSMutableSet set];
    for(NSDictionary *write in plan) {
        NSNumber *address=write[@"Block"];uint64_t block=address.unsignedLongLongValue;
        if([seen containsObject:address]||block>=disk.blockCount||(block>=start&&block<=start+count)||[write[@"Bytes"] length]!=Block)
            return Failure(error,@"The planned APFS checkpoint overlaps another write.");
        [seen addObject:address];
    }

    NSMutableData *record=[NSMutableData dataWithLength:Block];uint8_t *p=record.mutableBytes;
    Put64(p+8,start);Put64(p+16,disk.nextTransactionID);Put32(p+24,0x40000014);
    memcpy(p+32,"JSDR",4);Put32(p+36,1);Put32(p+40,(uint32_t)driver.length);Put32(p+44,1);
    Put64(p+176,start+1);Put64(p+184,count);Put64(p,Checksum(p));
    BOOL ok=WriteBlock(image,base+start*Block,record);
    for(uint64_t i=0;ok&&i<count;i++) {
        NSMutableData *part=[NSMutableData dataWithLength:Block];size_t offset=(size_t)i*Block;
        memcpy(part.mutableBytes,(const uint8_t *)driver.bytes+offset,MIN(Block,driver.length-offset));
        ok=WriteBlock(image,base+(start+1+i)*Block,part);
    }
    for(NSUInteger i=0;ok&&i<plan.count;i++) {
        // Flush dependent writes before either of the superblocks.
        // A partially written scratch image is never published.
        if(i+2>=plan.count&&![image synchronize]){ok=NO;break;}
        NSDictionary *write=plan[i];
        ok=WriteBlock(image,base+[write[@"Block"] unsignedLongLongValue]*Block,write[@"Bytes"]);
    }
    if(!ok||![image synchronize])return Failure(error,@"Could not commit the detached APFS EFI checkpoint.");
    VIMAPFSCheckpoint *saved=[[VIMAPFSCheckpoint alloc] initWithRawImage:raw error:error];
    if(!saved)return NO;
    if(saved.transactionID!=disk.nextTransactionID||![VIMReadIntelAPFSJumpstart(raw,error) isEqualToData:driver])
        return Failure(error,@"The detached APFS boot-driver checkpoint did not pass readback.");
    return YES;
}
