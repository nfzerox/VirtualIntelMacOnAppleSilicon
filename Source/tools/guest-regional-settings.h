#import <Foundation/Foundation.h>
#include <sys/types.h>

NSDictionary *VIMGuestRegionalCapabilities(void);
/* Pure preflight: resolve guest resources and require/filter native setters. */
NSDictionary *VIMGuestRegionalPlan(NSDictionary *request, NSDictionary *capabilities, NSError **error);
BOOL VIMGuestApplyRegionalSettings(NSDictionary *request, NSString *user, uid_t uid,
    NSMutableDictionary *accountJournal, NSString *journalPath, NSError **error);

/* OS adapter seam: command execution is replaceable; planning, typed readback,
 * recovery and atomic journal persistence remain shared. The public entry point
 * enforces root. Embedders of this adapter entry point enforce their own privilege
 * boundary. Output is NSData, Status is an NSNumber; commands never use a shell. */
@protocol VIMGuestRegionalBackend <NSObject>
- (NSDictionary *)regionalCapabilities;
- (NSDictionary *)runExecutable:(NSString *)executable arguments:(NSArray *)arguments
    environment:(NSDictionary *)environment error:(NSError **)error;
@end
BOOL VIMGuestApplyRegionalSettingsWithBackend(NSDictionary *request, NSString *user, uid_t uid,
    NSMutableDictionary *accountJournal, NSString *journalPath, id<VIMGuestRegionalBackend> backend, NSError **error);
