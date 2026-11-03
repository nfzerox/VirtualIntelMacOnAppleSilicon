// QGA's 0xff fence resets ANY partial request, not just leading fence bytes
// on an otherwise complete line. See vendored qga/qapi-schema.json's
// guest-sync-delimited contract. Required after a host timeout/disconnect.
// No polling, per-packet logging, or change to command authorization/dispatch.
#import <Foundation/Foundation.h>
#include <string.h>
static inline BOOL VIMOpenGLAgentAppend(NSMutableData *pending,const unsigned char *bytes,NSUInteger length){
    const unsigned char *cursor=bytes,*fence=NULL,*end=bytes+length;
    while(cursor<end){
        const unsigned char *next=memchr(cursor,0xff,(size_t)(end-cursor));
        if(!next)break;fence=next;cursor=next+1;
    }
    if(fence){[pending setLength:0];bytes=fence+1;length=(NSUInteger)(end-bytes);}
    const NSUInteger maximum=1024*1024;
    if([pending length]>maximum||length>maximum-[pending length])return NO;
    [pending appendBytes:bytes length:length];return YES;
}
