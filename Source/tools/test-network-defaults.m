#import <Foundation/Foundation.h>
#import "VIMNetworkDefaults.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
int main(void) {@autoreleasepool {
    NSURL *root=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]];
    NSFileManager *fm=NSFileManager.defaultManager;
    NSCAssert([fm createDirectoryAtURL:root withIntermediateDirectories:NO attributes:nil error:nil],@"fixture");
    NSURL *self=[root URLByAppendingPathComponent:@"Self.vintelmac"];
    NSDictionary *first=VIMSuggestedForwardPorts(root,self);
    NSCAssert([first[@"SSHPort"] integerValue]>1023&&[first[@"FileSharingPort"] integerValue]>1023,@"ports");
    NSDictionary *defaults=VIMNetworkConfigurationWithDefaults(@{},root,self);
    NSCAssert([defaults[@"SSHEnabled"] boolValue]&&[defaults[@"FileSharingEnabled"] boolValue]&&
        defaults[@"SSHPort"]&&defaults[@"FileSharingPort"]&&![defaults[@"SSHPort"] isEqual:defaults[@"FileSharingPort"]],@"Both local services are ready from the first boot");
    NSDictionary *optOut=@{@"SSHEnabled":@NO,@"FileSharingEnabled":@NO,@"SSHPort":@32001,@"FileSharingPort":@32002};
    NSCAssert([VIMNetworkConfigurationWithDefaults(optOut,root,self) isEqual:optOut],@"Never override explicit opt-outs or custom ports");
    NSDictionary *disabled=VIMNetworkConfigurationWithDefaults(@{@"SSHEnabled":@NO,@"FileSharingEnabled":@NO},root,self);
    NSCAssert(![disabled[@"SSHEnabled"] boolValue]&&![disabled[@"FileSharingEnabled"] boolValue]&&disabled[@"SSHPort"]&&disabled[@"FileSharingPort"],@"Opt-outs remain off but gain stable port values for later setup");
    NSDictionary *custom=VIMNetworkConfigurationWithDefaults(@{@"FileSharingPort":first[@"SSHPort"]},root,self);
    NSCAssert([custom[@"FileSharingPort"] isEqual:first[@"SSHPort"]]&&![custom[@"SSHPort"] isEqual:custom[@"FileSharingPort"]],@"A missing default port must not collide with this VM's other custom port");
    NSURL *other=[root URLByAppendingPathComponent:@"Other.vintelmac"];
    [fm createDirectoryAtURL:other withIntermediateDirectories:NO attributes:nil error:nil];
    NSCAssert([first writeToURL:[other URLByAppendingPathComponent:@"Machine.plist"] atomically:YES],@"manifest");
    NSDictionary *next=VIMSuggestedForwardPorts(root,self);
    NSCAssert(![next[@"SSHPort"] isEqual:first[@"SSHPort"]]&&![next[@"FileSharingPort"] isEqual:first[@"FileSharingPort"]],@"reserved sibling");
    NSDictionary *excluded=VIMSuggestedForwardPorts(root,other);
    NSCAssert([excluded isEqual:first],@"exclude current package");
    int fd=socket(AF_INET,SOCK_STREAM,0);
    struct sockaddr_in address={0};address.sin_len=sizeof(address);address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons([next[@"SSHPort"] unsignedShortValue]);
    NSCAssert(fd>=0&&bind(fd,(struct sockaddr *)&address,sizeof(address))==0&&listen(fd,1)==0,@"reserve live port");
    NSDictionary *busy=VIMSuggestedForwardPorts(root,self);
    NSCAssert(![busy[@"SSHPort"] isEqual:next[@"SSHPort"]],@"live listener excluded");
    close(fd);
    NSCAssert([fm removeItemAtURL:root error:nil],@"remove owned fixture");
    puts("PASS: enabled defaults, explicit opt-outs, custom-port preservation, sibling reservations, current-package and live-listener exclusion");
}}
