#import "VIMNetworkDefaults.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
@import vmnet;
@import SystemConfiguration;

NSString *VIMNetworkMode(NSDictionary *configuration) {
    NSString *mode=configuration[@"NetworkMode"];
    return [@[@"shared",@"host",@"bridged"] containsObject:mode]?mode:@"shared";
}
BOOL VIMNetworkUsableIPv4(NSString *address) {
    struct in_addr ip;if(![address isKindOfClass:NSString.class]||inet_pton(AF_INET,address.UTF8String,&ip)!=1)return NO;
    uint32_t bits=ntohl(ip.s_addr);
    return bits&&((bits>>24)!=127)&&((bits>>16)!=0xa9fe)&&bits<0xe0000000;
}
NSArray *VIMNetworkBridgeInterfaces(void) {
    xpc_object_t list=vmnet_copy_shared_interface_list();NSMutableArray *result=[NSMutableArray new];
    NSArray *interfaces=CFBridgingRelease(SCNetworkInterfaceCopyAll());
    if(list)xpc_array_apply(list,^bool(size_t index,xpc_object_t item){
        if(xpc_get_type(item)!=XPC_TYPE_STRING)return true;
        NSString *name=@(xpc_string_get_string_ptr(item)),*label=name;
        for(id candidate in interfaces) {
            SCNetworkInterfaceRef interface=(__bridge SCNetworkInterfaceRef)candidate;
            if([(__bridge NSString *)SCNetworkInterfaceGetBSDName(interface) isEqual:name]) {
                NSString *display=(__bridge NSString *)SCNetworkInterfaceGetLocalizedDisplayName(interface);
                if(display.length)label=[NSString stringWithFormat:@"%@ (%@)",display,name];break;
            }
        }
        [result addObject:@{@"Name":label,@"Identifier":name}];return true;
    });return result;
}
NSString *VIMNetworkDefaultBridge(void) {
    SCDynamicStoreRef store=SCDynamicStoreCreate(NULL,CFSTR("VirtualIntelMac.Network"),NULL,NULL);
    NSDictionary *global=store?CFBridgingRelease(SCDynamicStoreCopyValue(store,CFSTR("State:/Network/Global/IPv4"))):nil;
    if(store)CFRelease(store);
    NSArray *choices=VIMNetworkBridgeInterfaces();NSString *primary=global[@"PrimaryInterface"];
    for(NSDictionary *choice in choices)if([choice[@"Identifier"] isEqual:primary])return primary;
    return choices.firstObject[@"Identifier"];
}
NSString *VIMNetworkHostAddress(NSString *interface) {
    struct ifaddrs *list=NULL;NSString *address=nil;
    if(!interface.length||getifaddrs(&list))return nil;
    for(struct ifaddrs *item=list;item;item=item->ifa_next)if(item->ifa_addr&&item->ifa_addr->sa_family==AF_INET&&[interface isEqual:@(item->ifa_name)]) {
        char text[INET_ADDRSTRLEN];if(inet_ntop(AF_INET,&((struct sockaddr_in *)item->ifa_addr)->sin_addr,text,sizeof(text))&&VIMNetworkUsableIPv4(@(text))){address=@(text);break;}
    }freeifaddrs(list);return address;
}

BOOL VIMNetworkMACAddressIsValid(NSString *address) {
    if(![address isKindOfClass:NSString.class]||address.length!=17)return NO;
    unsigned bytes[6];int end=0;
    if(sscanf(address.UTF8String,"%2x:%2x:%2x:%2x:%2x:%2x%n",&bytes[0],&bytes[1],&bytes[2],&bytes[3],&bytes[4],&bytes[5],&end)!=6||end!=17)return NO;
    for(NSUInteger i=0;i<17;i++) {
        unichar c=[address characterAtIndex:i];
        if(i%3==2?c!=':':!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return NO;
    }
    return !(bytes[0]&1)&&(bytes[0]|bytes[1]|bytes[2]|bytes[3]|bytes[4]|bytes[5]);
}
NSString *VIMNetworkMACAddress(NSDictionary *configuration) {
    if(VIMNetworkMACAddressIsValid(configuration[@"MACAddress"]))return [configuration[@"MACAddress"] uppercaseString];
    NSString *identity=[configuration[@"Identifier"] stringByReplacingOccurrencesOfString:@"-" withString:@""];
    if(identity.length<8)return @"52:54:00:00:00:01";
    NSMutableString *mac=[NSMutableString stringWithString:@"52:54"];
    for(NSUInteger i=0;i<4;i++)[mac appendFormat:@":%@",[identity substringWithRange:NSMakeRange(i*2,2)]];
    return mac.uppercaseString;
}

static BOOL Available(uint16_t port) {
    int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return NO;
    struct sockaddr_in address={0};address.sin_len=sizeof(address);
    address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    address.sin_port=htons(port);
    BOOL available=bind(fd,(struct sockaddr *)&address,sizeof(address))==0;
    close(fd);return available;
}
static NSDictionary<NSString *,NSNumber *> *SuggestedPorts(NSURL *library,NSURL *excludingMachine,NSSet *additional) {
    NSMutableSet *reserved=[additional mutableCopy];
    NSArray *entries=[NSFileManager.defaultManager contentsOfDirectoryAtURL:library
        includingPropertiesForKeys:nil options:NSDirectoryEnumerationSkipsHiddenFiles error:nil];
    for(NSURL *entry in entries) {
        if(![entry.pathExtension isEqual:@"vintelmac"]||
           [entry.URLByResolvingSymlinksInPath isEqual:excludingMachine.URLByResolvingSymlinksInPath])continue;
        NSDictionary *config=[NSDictionary dictionaryWithContentsOfURL:[entry URLByAppendingPathComponent:@"Machine.plist"]];
        for(NSString *key in @[@"SSHPort",@"FileSharingPort"])
            if([config[key] isKindOfClass:NSNumber.class])[reserved addObject:config[key]];
    }
    NSMutableDictionary *result=[NSMutableDictionary dictionary];
    for(NSString *key in @[@"SSHPort",@"FileSharingPort"]) {
        NSUInteger first=[key isEqual:@"SSHPort"]?2222:1445;
        // Bound work on the UI thread. Zero is visibly invalid rather than
        // silently suggesting a known-conflicting port on an exhausted host.
        result[key]=@0;
        for(NSUInteger port=first;port<first+256;port++) {
            if([reserved containsObject:@(port)]||!Available(port))continue;
            result[key]=@(port);[reserved addObject:@(port)];break;
        }
    }
    return result;
}
NSDictionary<NSString *,NSNumber *> *VIMSuggestedForwardPorts(NSURL *library,NSURL *excludingMachine) {
    return SuggestedPorts(library,excludingMachine,[NSSet set]);
}
NSDictionary *VIMNetworkConfigurationWithDefaults(NSDictionary *configuration,NSURL *library,NSURL *machine) {
    NSMutableDictionary *result=[configuration mutableCopy];BOOL needsPorts=NO;
    NSMutableSet *reserved=[NSMutableSet set];
    for(NSString *key in @[@"SSHPort",@"FileSharingPort"])if(result[key])[reserved addObject:result[key]];
    for(NSString *key in @[@"SSH",@"FileSharing"]) {
        NSString *enabled=[key stringByAppendingString:@"Enabled"],*port=[key stringByAppendingString:@"Port"];
        if(!result[enabled])result[enabled]=@YES;
        if(!result[port])needsPorts=YES;
    }
    if(needsPorts) {
        NSDictionary *ports=SuggestedPorts(library,machine,reserved);
        for(NSString *key in @[@"SSH",@"FileSharing"]) {
            NSString *port=[key stringByAppendingString:@"Port"];
            if(!result[port]) {
                if([ports[port] unsignedIntegerValue]<1024)return nil;
                result[port]=ports[port];
            }
        }
    }
    return result;
}
