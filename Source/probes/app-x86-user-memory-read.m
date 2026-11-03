/* Development-only read-only x86 page-table walk through the visible app's
 * EXISTING QMP owner. No second client, CR3/register changes, guest pause,
 * privilege elevation or guest writes. Never package/link into the app.
 *
 * This is for an explicitly selected diagnostic process/range, not a scan of
 * user memory. The output can contain private data: keep it in scratch, never
 * commit the dump. Validate the exact task/pmap and saved-register ABI before
 * passing the physical CR3 and user address. 4-level x86 paging only; upper
 * address-space/canonical checks and present/user bits fail closed. Supports
 * ordinary 4-KiB, 2-MiB and 1-GiB pages, with a 16-KiB per-call bound.
 */
#import <Foundation/Foundation.h>
#include <stdint.h>
#include <stdio.h>
@interface VIMQMPConnection : NSObject
- (void)execute:(NSString *)command arguments:(NSDictionary *)arguments completion:(void (^)(id,NSError *))completion;
@end
@interface VIMX86UserRead : NSObject
@property VIMQMPConnection *control;
@property NSString *destination;
@property NSMutableArray *pages;
@property uint64_t cr3;
@property uint64_t address;
@property uint64_t remaining;
- (void)walk:(uint64_t)table shift:(unsigned)shift;
@end
@implementation VIMX86UserRead
- (void)finish:(NSString *)error {
    NSMutableDictionary *result=[@{@"Pages":self.pages,@"Remaining":@(self.remaining)} mutableCopy];
    if(error)result[@"Error"]=error;
    [result writeToFile:self.destination atomically:YES];
}
- (void)readPage:(uint64_t)physical bytes:(uint64_t)available {
    uint64_t length=MIN(self.remaining,available),address=self.address;
    NSString *command=[NSString stringWithFormat:@"xp /%llubx 0x%llx",(unsigned long long)length,(unsigned long long)physical];
    [self.control execute:@"human-monitor-command" arguments:@{@"command-line":command} completion:^(id text,NSError *error){
        if(error||![text isKindOfClass:NSString.class]){[self finish:error.localizedDescription?:@"No physical memory result"];return;}
        [self.pages addObject:@{@"VirtualAddress":@(address),@"PhysicalAddress":@(physical),@"Length":@(length),@"Memory":text}];
        self.address+=length;self.remaining-=length;
        if(self.remaining)[self walk:self.cr3 shift:39];else [self finish:nil];
    }];
}
- (void)walk:(uint64_t)table shift:(unsigned)shift {
    uint64_t entry=table+((self.address>>shift)&511)*8;
    NSString *command=[NSString stringWithFormat:@"xp /1gx 0x%llx",(unsigned long long)entry];
    [self.control execute:@"human-monitor-command" arguments:@{@"command-line":command} completion:^(id text,NSError *error){
        unsigned long long actual=0,pte=0;
        if(error||![text isKindOfClass:NSString.class]||sscanf([text UTF8String],"%llx: %llx",&actual,&pte)!=2||actual!=entry){
            [self finish:error.localizedDescription?:@"Unexpected physical PTE result"];return;
        }
        if((pte&5)!=5){[self finish:@"Unmapped or supervisor-only user page"];return;}
        if(shift==12||(shift!=39&&(pte&128))){
            uint64_t mask=(1ULL<<shift)-1;
            uint64_t physical=(pte&0x000ffffffffff000ULL&~mask)+(self.address&mask);
            [self readPage:physical bytes:(mask+1)-(self.address&mask)];
        }else [self walk:pte&0x000ffffffffff000ULL shift:shift-9];
    }];
}
@end
__attribute__((visibility("default")))
void VIMReadX86UserMemory(VIMQMPConnection *control,uint64_t cr3,uint64_t address,uint64_t length,const char *output){
    if(!control||!output||!length||length>16384||!cr3||(cr3&4095)||cr3>=0x10000000000000ULL||
       address>=0x800000000000ULL||length>0x800000000000ULL-address)return;
    NSString *path=[NSString stringWithUTF8String:output];
    if(!path.isAbsolutePath||[NSFileManager.defaultManager fileExistsAtPath:path])return;
    VIMX86UserRead *read=[VIMX86UserRead new];read.control=control;read.destination=path;
    read.pages=[NSMutableArray array];read.cr3=cr3;read.address=address;read.remaining=length;
    [read walk:cr3 shift:39];
}
