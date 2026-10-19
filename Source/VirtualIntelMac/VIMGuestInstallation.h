#import <Foundation/Foundation.h>
@class VIMGuestAgent;
NS_ASSUME_NONNULL_BEGIN
@interface VIMGuestInstallation : NSObject
/// Check the prior outcome without retiring its boot/display evidence while
/// new files are only being prepared. No system writes or archival.
+ (void)checkForRepairWithAgent:(VIMGuestAgent *)agent journal:(NSURL *)hostJournal
                    completion:(void (^)(NSError *_Nullable))completion;
/// A same-volume newer OS can supersede a proven completed installation even
/// when the update deleted its legacy temporary verifier. No guest writes.
+ (void)checkForRepairWithAgent:(VIMGuestAgent *)agent journal:(NSURL *)hostJournal
                 updatedSystem:(nullable NSDictionary *)report completion:(void (^)(NSError *_Nullable))completion;
/// Explicit repair only: confirm a terminal guest outcome, then retain the
/// complete host record in history. Never changes guest files or receipts.
+ (void)archiveForRepairWithAgent:(VIMGuestAgent *)agent journal:(NSURL *)hostJournal
                     completion:(void (^)(NSError *_Nullable))completion;
+ (void)archiveForRepairWithAgent:(VIMGuestAgent *)agent journal:(NSURL *)hostJournal
                   updatedSystem:(nullable NSDictionary *)report completion:(void (^)(NSError *_Nullable))completion;
/// Explicit once-only dispatch. Uncertain outcomes require status inspection,
/// never automatic reapplication. Does not restart or remove installer media.
+ (void)applyWithAgent:(VIMGuestAgent *)agent preparation:(NSDictionary *)preparation
              journal:(NSURL *)hostJournal completion:(void (^)(NSDictionary *_Nullable,NSError *_Nullable))completion;
/// Read the existing guest transaction; optionally verify after a user/app
/// restart. Never reapplies files, reboots, marks ready, or deletes media.
+ (void)reconcileWithAgent:(VIMGuestAgent *)agent journal:(NSURL *)hostJournal
         verifyAfterReboot:(BOOL)verify
                completion:(void (^)(NSDictionary *_Nullable,NSError *_Nullable))completion;
@end
NS_ASSUME_NONNULL_END
