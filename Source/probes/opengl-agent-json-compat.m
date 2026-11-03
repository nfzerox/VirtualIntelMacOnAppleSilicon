// Exercise the real service's JSON/dispatch path on a physical period OS,
// without opening a UART, requiring root, or passing the VM identity guard.
#define main VIMUnusedGuestServiceMain
#include "../tools/opengl-guest-agent.m"
#undef main
int main(void){@autoreleasepool{
    NSData *request=[@"{\"execute\":\"guest-sync-delimited\",\"arguments\":{\"id\":6364390369857758570},\"id\":1}"
        dataUsingEncoding:NSUTF8StringEncoding];
    NSMutableData *wire=[NSMutableData data];const unsigned char fence=0xff;
    [wire appendBytes:&fence length:1];[wire appendData:request];[wire appendBytes:"\n" length:1];
    for(NSUInteger split=0;split<=[wire length];split++){@autoreleasepool{
        NSMutableData *pending=[NSMutableData data];
        // A previous client disconnected in the middle of a request. The
        // next fence must discard it even when split across separate reads.
        [pending appendBytes:"{\"stale\":" length:9];
        if(!VIMOpenGLAgentAppend(pending,[wire bytes],split)||
           !VIMOpenGLAgentAppend(pending,(const unsigned char *)[wire bytes]+split,[wire length]-split))return 3;
        const unsigned char *start=[pending bytes],*newline=memchr(start,'\n',[pending length]);
        if(!newline)return 3;
        NSUInteger length=(NSUInteger)(newline-start);
        NSData *line=[NSData dataWithBytes:start length:length];
        [pending replaceBytesInRange:NSMakeRange(0,length+1) withBytes:NULL length:0];
        id parsed=[NSJSONSerialization JSONObjectWithData:line options:0 error:NULL];
        if(![parsed isKindOfClass:[NSDictionary class]]||[pending length])return 4;
    }}
    NSError *error=nil;id parsed=[NSJSONSerialization JSONObjectWithData:request options:0 error:&error];
    if(!parsed){fprintf(stderr,"JSON read: %s\n",[[error description] UTF8String]);return 1;}
    NSMutableDictionary *reply=[[Execute(parsed,[NSMutableDictionary dictionary],[NSMutableDictionary dictionary]) mutableCopy] autorelease];
    [reply setObject:[parsed objectForKey:@"id"] forKey:@"id"];
    NSData *output=[NSJSONSerialization dataWithJSONObject:reply options:0 error:&error];
    if(!output){fprintf(stderr,"JSON write: %s\n",[[error description] UTF8String]);return 2;}
    fwrite([output bytes],1,[output length],stdout);putchar('\n');return 0;
}}
