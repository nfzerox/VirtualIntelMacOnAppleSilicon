// Small, typed control messages for the USB/network helpers. Data transport
// remains on the existing file descriptors, never serialized through XPC.
// Public APIs only, including message-bound code identity on macOS 11.
#pragma once
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#import <xpc/xpc.h>
#import <os/log.h>
#include <fcntl.h>

static inline OSStatus VIMHelperMessageStatus(xpc_object_t message,NSString *requirement) {
    if(xpc_get_type(message)!=XPC_TYPE_DICTIONARY||!requirement.length)return errSecCSInvalidObjectRef;
    SecCodeRef code=NULL;SecRequirementRef policy=NULL;
    OSStatus status=SecCodeCreateWithXPCMessage(message,kSecCSDefaultFlags,&code);
    if(!status)status=SecRequirementCreateWithString((__bridge CFStringRef)requirement,kSecCSDefaultFlags,&policy);
    if(!status)status=SecCodeCheckValidity(code,kSecCSDefaultFlags,policy);
    if(code)CFRelease(code);if(policy)CFRelease(policy);
    return status;
}
static inline BOOL VIMHelperMessageMatches(xpc_object_t message,NSString *requirement) {
    return VIMHelperMessageStatus(message,requirement)==errSecSuccess;
}

static inline void VIMHelperSetPayload(xpc_object_t message,NSDictionary *payload) {
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:payload?:@{} format:NSPropertyListBinaryFormat_v1_0 options:0 error:NULL];
    if(data)xpc_dictionary_set_data(message,"payload",data.bytes,data.length);
}
static inline NSDictionary *VIMHelperPayload(xpc_object_t message) {
    size_t size=0;const void *bytes=xpc_dictionary_get_data(message,"payload",&size);
    if(!bytes||size>65536)return nil;
    id result=[NSPropertyListSerialization propertyListWithData:[NSData dataWithBytes:bytes length:size] options:NSPropertyListImmutable format:NULL error:NULL];
    return [result isKindOfClass:NSDictionary.class]?result:nil;
}
static inline NSFileHandle *VIMHelperCopyHandle(xpc_object_t message,const char *key) {
    int descriptor=xpc_dictionary_dup_fd(message,key);
    if(descriptor<0)return nil;
    fcntl(descriptor,F_SETFD,FD_CLOEXEC);
    return [[NSFileHandle alloc] initWithFileDescriptor:descriptor closeOnDealloc:YES];
}
static inline void VIMHelperReply(xpc_object_t request,NSDictionary *payload,NSFileHandle *socket,NSError *error) {
    xpc_object_t response=xpc_dictionary_create_reply(request);
    if(!response)return;
    VIMHelperSetPayload(response,payload);
    if(socket)xpc_dictionary_set_fd(response,"socket",socket.fileDescriptor);
    if(error) {
        xpc_dictionary_set_string(response,"error-domain",error.domain.UTF8String);
        xpc_dictionary_set_int64(response,"error-code",error.code);
        xpc_dictionary_set_string(response,"error",error.localizedDescription.UTF8String);
    }
    xpc_connection_t peer=xpc_dictionary_get_remote_connection(request);
    if(peer)xpc_connection_send_message(peer,response);
}
static inline NSError *VIMHelperReplyError(xpc_object_t response) {
    const char *description=xpc_dictionary_get_string(response,"error");
    if(!description)return nil;
    const char *domain=xpc_dictionary_get_string(response,"error-domain");
    return [NSError errorWithDomain:domain?@(domain):@"VirtualIntelMac.Helper" code:xpc_dictionary_get_int64(response,"error-code")
        userInfo:@{NSLocalizedDescriptionKey:@(description)}];
}

@protocol VIMHelperService <NSObject>
- (void)handleMessage:(xpc_object_t)message;
- (void)connectionInvalidated;
@end
static inline xpc_connection_t VIMHelperCreateListener(NSString *name,NSString *requirement,id<VIMHelperService> (^createClient)(void)) {
    xpc_connection_t listener=xpc_connection_create_mach_service(name.UTF8String,NULL,XPC_CONNECTION_MACH_SERVICE_LISTENER);
    xpc_connection_set_event_handler(listener,^(xpc_object_t event){
        if(xpc_get_type(event)!=XPC_TYPE_CONNECTION)return;
        xpc_connection_t peer=event;
        id<VIMHelperService> client=createClient();
        xpc_connection_set_event_handler(peer,^(xpc_object_t message){@autoreleasepool {
            if(xpc_get_type(message)==XPC_TYPE_ERROR){[client connectionInvalidated];return;}
            OSStatus status=VIMHelperMessageStatus(message,requirement);
            if(xpc_connection_get_euid(peer)==0||status) {
                os_log_error(OS_LOG_DEFAULT,"Helper rejected client identity (status %d)",(int)status);
                xpc_connection_cancel(peer);return;
            }
            [client handleMessage:message];
        }});
        xpc_connection_resume(peer);
    });
    xpc_connection_resume(listener);return listener;
}
