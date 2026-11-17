// Finite component fixture: the actual production extractor, never a VM/disk.
#import "../tools/HostInstall/VIMCPIOPayload.h"
#include <stdio.h>

int main(int argc,const char **argv) {
    if(argc<3||argc>4)return 64;
    @autoreleasepool {
        NSURL *payload=[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]];
        NSURL *root=[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[2]]];
        uint64_t cancelAt=argc==4?strtoull(argv[3],NULL,10):UINT64_MAX;
        NSMutableArray *observations=[NSMutableArray array];NSError *error=nil;
        BOOL ok=VIMExtractCPIOPayload(payload,root,^BOOL(uint64_t input){
            [observations addObject:@(input)];return input<cancelAt;
        },&error);
        NSDictionary *result=@{@"Success":@(ok),@"Progress":observations,
            @"ErrorCode":@(error.code),@"ErrorDomain":error.domain?:@""};
        NSData *data=[NSJSONSerialization dataWithJSONObject:result options:0 error:nil];
        fwrite(data.bytes,1,data.length,stdout);fputc('\n',stdout);
        return 0;
    }
}
