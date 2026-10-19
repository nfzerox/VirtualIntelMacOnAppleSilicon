#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
/// Suggestions only: QEMU's eventual bind remains authoritative. Existing
/// settings are never reassigned behind the user's back.
NSDictionary<NSString *,NSNumber *> *VIMSuggestedForwardPorts(NSURL *library, NSURL *excludingMachine);
/// Fill missing defaults without changing explicit opt-outs or assigned ports.
/// Returns nil if an unassigned local port cannot be allocated.
NSDictionary *_Nullable VIMNetworkConfigurationWithDefaults(NSDictionary *configuration, NSURL *library, NSURL *machine);
/// Stable default, or the validated user override. Does not change on rename.
NSString *VIMNetworkMACAddress(NSDictionary *configuration);
BOOL VIMNetworkMACAddressIsValid(NSString *address);
NSString *VIMNetworkMode(NSDictionary *configuration);
NSArray<NSDictionary<NSString *,NSString *> *> *VIMNetworkBridgeInterfaces(void);
NSString *_Nullable VIMNetworkDefaultBridge(void);
NSString *_Nullable VIMNetworkHostAddress(NSString *interface);
BOOL VIMNetworkUsableIPv4(NSString *address);
NS_ASSUME_NONNULL_END
