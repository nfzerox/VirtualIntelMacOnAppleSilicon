#import <Cocoa/Cocoa.h>
NS_ASSUME_NONNULL_BEGIN
/// Edits first-install intent, never an already installed guest's preferences.
@interface VIMRegionalSettingsView : NSView
@property(nonatomic) BOOL enabled;
@property(nonatomic) BOOL frozen;
- (instancetype)initWithRequest:(nullable NSDictionary *)request;
- (nullable NSDictionary *)requestWithError:(NSError **)error;
- (NSDictionary *)draftState;
- (void)restoreDraftState:(NSDictionary *)state;
- (void)restoreRequest:(nullable NSDictionary *)request;
@end
NS_ASSUME_NONNULL_END
