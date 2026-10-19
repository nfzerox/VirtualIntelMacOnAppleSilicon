#import "VIMSMBService.h"
#import "VIMMachine.h"
#include <dns_sd.h>
#include <arpa/inet.h>

static NSString *Instance(VIMMachine *machine) {
    // Foundation URLComponents rejects spaces in hostnames; Unicode names
    // may also be converted to IDNA, no longer matching the DNS-SD instance.
    // Keep the advertised instance itself URL-safe, not merely a manually
    // percent-escaped URL that only works in smbutil's separate URL parser.
    NSMutableString *name=[NSMutableString new];
    NSCharacterSet *letters=[NSCharacterSet characterSetWithCharactersInString:@"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"];
    for(NSUInteger i=0;i<machine.name.length&&name.length<26;i++) {
        unichar c=[machine.name characterAtIndex:i];
        if([letters characterIsMember:c])[name appendFormat:@"%C",c];
        else if(name.length&&![name hasSuffix:@"-"])[name appendString:@"-"];
    }
    if(!name.length)[name appendString:@"VirtualMac"];
    if([name hasSuffix:@"-"])[name deleteCharactersInRange:NSMakeRange(name.length-1,1)];
    // Library bundle names are unique. Use the actual human-readable name,
    // not an entire UUID, in the address users type. NoAutoRename below fails
    // closed on a conflicting local service rather than connecting another VM.
    // Keep a short VM-scoped namespace, separate from the guest's own Bonjour
    // computer/service name (which can remain cached after a bridged boot).
    return [name stringByAppendingString:@"-VM"];
}
@interface VIMSMBService () {
    DNSServiceRef _addressConnection;
    DNSServiceRef _service;
}
@property NSURL *URL;
@property NSError *error;
@property NSString *instance;
@property NSString *hostname;
@property uint16_t port;
@end
static void Registered(DNSServiceRef service,DNSServiceFlags flags,DNSServiceErrorType error,
                       const char *name,const char *type,const char *domain,void *context) {
    VIMSMBService *owner=(__bridge VIMSMBService *)context;
    if(error)owner.error=[NSError errorWithDomain:@"VirtualIntelMac.FileSharing" code:error userInfo:@{NSLocalizedDescriptionKey:@"File Sharing’s local address couldn’t be published. Restart the virtual Mac and try again."}];
}
static void AddressRegistered(DNSServiceRef connection,DNSRecordRef record,DNSServiceFlags flags,DNSServiceErrorType error,void *context) {
    Registered(connection,flags,error,NULL,NULL,NULL,context);
}
@implementation VIMSMBService
+ (NSURL *)URLForMachine:(VIMMachine *)machine {
    NSURLComponents *url=[NSURLComponents new];url.scheme=@"smb";
    url.host=[Instance(machine) stringByAppendingString:@"._smb._tcp.local"];
    return url.URL;
}
- (instancetype)initWithMachine:(VIMMachine *)machine port:(uint16_t)port {
    if((self=[super init])) {
        _URL=[VIMSMBService URLForMachine:machine];_instance=Instance(machine);_port=port;
        _hostname=[NSString stringWithFormat:@"vim-%@.local.",[machine.configuration[@"Identifier"] lowercaseString]];
    }return self;
}
- (BOOL)start:(NSError **)error {
    NSAssert(NSThread.isMainThread,@"Local sharing is main-thread owned");
    [self stop];self.error=nil;
    struct in_addr loopback={.s_addr=htonl(INADDR_LOOPBACK)};DNSRecordRef record=NULL;
    DNSServiceErrorType status=DNSServiceCreateConnection(&_addressConnection);
    if(!status)status=DNSServiceSetDispatchQueue(_addressConnection,dispatch_get_main_queue());
    if(!status)status=DNSServiceRegisterRecord(_addressConnection,&record,kDNSServiceFlagsUnique,
        kDNSServiceInterfaceIndexLocalOnly,self.hostname.UTF8String,kDNSServiceType_A,kDNSServiceClass_IN,
        sizeof(loopback),&loopback,120,AddressRegistered,(__bridge void *)self);
    if(!status)status=DNSServiceRegister(&_service,kDNSServiceFlagsNoAutoRename,
        kDNSServiceInterfaceIndexLocalOnly,self.instance.UTF8String,"_smb._tcp.","local.",
        self.hostname.UTF8String,htons(self.port),0,NULL,Registered,(__bridge void *)self);
    if(!status)status=DNSServiceSetDispatchQueue(_service,dispatch_get_main_queue());
    if(status) {
        [self stop];self.error=[NSError errorWithDomain:@"VirtualIntelMac.FileSharing" code:status userInfo:@{NSLocalizedDescriptionKey:@"File Sharing’s local address couldn’t be published. Restart the virtual Mac and try again."}];
        if(error)*error=self.error;return NO;
    }return YES;
}
- (void)stop {
    if(_service){DNSServiceRefDeallocate(_service);_service=NULL;}
    if(_addressConnection){DNSServiceRefDeallocate(_addressConnection);_addressConnection=NULL;}
}
- (void)dealloc {[self stop];}
@end
