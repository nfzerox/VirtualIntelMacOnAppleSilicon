// Finite own-process controls for Tiger's actual Foundation/runtime contract.
// Only a fresh mkdtemp directory is written; no system/account/service changes.
#import "../tools/opengl-period-foundation.h"
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#define REQUIRE(test) do { if(!(test)){fprintf(stderr,"period control failed at line %d\n",__LINE__);return 1;} } while(0)
int main(void) {
    NSAutoreleasePool *pool=[NSAutoreleasePool new];
    NSMutableArray *array=[NSMutableArray array];NSMutableDictionary *map=[NSMutableDictionary dictionary];
    unsigned i;for(i=0;i<1100;i++){
        NSNumber *number=[NSNumber numberWithUnsignedInt:i];[array addObject:number];
        [map setObject:number forKey:[NSString stringWithFormat:@"item-%04u",i]];
    }
    NSDictionary *value=[NSDictionary dictionaryWithObjectsAndKeys:array,@"array",map,@"map",@"héllo\nTiger",@"text",nil];
    NSError *error=nil;
    NSData *encoded=[NSJSONSerialization dataWithJSONObject:value options:0 error:&error];
    REQUIRE(encoded&&!error);
    id decoded=[NSJSONSerialization JSONObjectWithData:encoded options:0 error:&error];
    REQUIRE([decoded isEqual:value]&&!error);
    NSString *pretty=[value JSONStringWithOptions:JKSerializeOptionPretty error:&error];
    REQUIRE(pretty&&[pretty length]&&[[NSJSONSerialization JSONObjectWithData:[pretty dataUsingEncoding:NSUTF8StringEncoding] options:0 error:&error] isEqual:value]);
    REQUIRE(![NSJSONSerialization JSONObjectWithData:[@"{broken" dataUsingEncoding:NSUTF8StringEncoding] options:0 error:&error]&&error);
    error=nil;
    NSData *plist=[NSPropertyListSerialization dataWithPropertyList:value format:NSPropertyListXMLFormat_v1_0 options:0 error:&error];
    REQUIRE(plist&&[[NSPropertyListSerialization propertyListWithData:plist options:0 format:NULL error:&error] isEqual:value]&&!error);
    NSData *bytes=[@"Tiger control" dataUsingEncoding:NSUTF8StringEncoding];
    NSData *roundtrip=[[[NSData alloc] initWithBase64Encoding:[bytes base64Encoding]] autorelease];
    REQUIRE([roundtrip isEqual:bytes]);
    REQUIRE(![[[NSData alloc] initWithBase64Encoding:@"A==="] autorelease]);
    char directory[]="/tmp/vim-tiger-period-XXXXXX";REQUIRE(mkdtemp(directory));
    NSString *root=[NSString stringWithUTF8String:directory];
    NSURL *url=[[NSURL fileURLWithPath:root isDirectory:YES] URLByAppendingPathComponent:@"first/second" isDirectory:YES];
    NSDictionary *attributes=[NSDictionary dictionaryWithObject:[NSNumber numberWithUnsignedInt:0700] forKey:NSFilePosixPermissions];
    REQUIRE([[NSFileManager defaultManager] createDirectoryAtURL:url withIntermediateDirectories:YES attributes:attributes error:&error]);
    struct stat st;REQUIRE(!stat([[url path] fileSystemRepresentation],&st)&&S_ISDIR(st.st_mode)&&(st.st_mode&0777)==0700);
    REQUIRE([[NSFileManager defaultManager] createDirectoryAtURL:url withIntermediateDirectories:YES attributes:attributes error:&error]);
    REQUIRE([[NSFileManager defaultManager] removeFileAtPath:root handler:nil]);
    printf("Tiger Foundation/JSON/plist/base64/directory controls passed; bits=%u\n",(unsigned)(sizeof(void*)*8));
    [pool drain];return 0;
}
