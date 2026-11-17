// Finite control-transport check. Run as an ordinary launch agent, never root.
// Test both signed and differently identified clients against the same service.
#import <Foundation/Foundation.h>
#include <sys/socket.h>
#include <unistd.h>
#import "../compat/VIMHelperXPC.h"

@interface HelperControlProbe : NSObject <VIMHelperService>
@end
@implementation HelperControlProbe
- (void)connectionInvalidated {}
- (void)handleMessage:(xpc_object_t)message {
    fprintf(stderr,"probe: authenticated request received\n");
    NSFileHandle *handle=VIMHelperCopyHandle(message,"listener");
    NSDictionary *payload=VIMHelperPayload(message);
    if(![payload[@"Sequence"] isKindOfClass:NSNumber.class]||!handle){VIMHelperReply(message,nil,nil,[NSError errorWithDomain:@"Probe" code:1 userInfo:nil]);return;}
    VIMHelperReply(message,payload,handle,nil);
    fprintf(stderr,"probe: descriptor reply submitted\n");
}
@end
int main(int argc,const char **argv) {@autoreleasepool {
    if(argc!=4||getuid()==0)return 64;
    NSString *name=@(argv[2]),*requirement=@(argv[3]);
    if(!strcmp(argv[1],"--server")) {
        __attribute__((objc_precise_lifetime)) xpc_connection_t listener=VIMHelperCreateListener(name,requirement,^{return [HelperControlProbe new];});
        [[NSRunLoop currentRunLoop] run];(void)listener;return 0;
    }
    BOOL expectRejection=!strcmp(argv[1],"--reject");
    __attribute__((objc_precise_lifetime)) xpc_connection_t connection=xpc_connection_create_mach_service(name.UTF8String,NULL,0);
    xpc_connection_set_event_handler(connection,^(xpc_object_t event){(void)event;});
    xpc_connection_resume(connection);
    int descriptors[2];if(socketpair(AF_UNIX,SOCK_STREAM,0,descriptors))return 3;
    int receiver=descriptors[1];
    xpc_object_t message=xpc_dictionary_create(NULL,NULL,0);VIMHelperSetPayload(message,@{@"Sequence":@7});
    xpc_dictionary_set_fd(message,"listener",descriptors[0]);
    xpc_connection_send_message_with_reply(connection,message,dispatch_get_main_queue(),^(xpc_object_t reply){
        OSStatus identityStatus=VIMHelperMessageStatus(reply,requirement);
        BOOL accepted=xpc_get_type(reply)==XPC_TYPE_DICTIONARY&&!identityStatus;
        if(!accepted)fprintf(stderr,"probe: reply type %s, identity status %d\n",xpc_type_get_name(xpc_get_type(reply)),(int)identityStatus);
        if(expectRejection){printf("%s unapproved client\n",accepted?"FAIL":"PASS rejected");exit(accepted?1:0);}
        NSFileHandle *echo=accepted?VIMHelperCopyHandle(reply,"socket"):nil;
        const char marker[]="control-fd";char readback[sizeof(marker)]={0};
        BOOL passed=accepted&&[VIMHelperPayload(reply)[@"Sequence"] isEqual:@7]&&echo&&
            write(echo.fileDescriptor,marker,sizeof(marker))==sizeof(marker)&&
            read(receiver,readback,sizeof(readback))==sizeof(readback)&&!memcmp(marker,readback,sizeof(marker));
        printf("%s signed request, signed reply, payload and descriptor round-trip\n",passed?"PASS":"FAIL");exit(passed?0:1);
    });
    close(descriptors[0]);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC),dispatch_get_main_queue(),^{puts("FAIL probe timed out");exit(4);});
    [[NSRunLoop currentRunLoop] run];
}return 0;}
