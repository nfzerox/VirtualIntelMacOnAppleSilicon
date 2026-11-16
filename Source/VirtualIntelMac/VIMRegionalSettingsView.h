#import <Cocoa/Cocoa.h>
NS_ASSUME_NONNULL_BEGIN
typedef NS_ENUM(NSInteger, VIMRegionalConfigurationMode) {
    VIMRegionalConfigurationOff,
    VIMRegionalConfigurationHost,
    VIMRegionalConfigurationCustom
};
/// Edits first-install intent, never an already installed guest's preferences.
@interface VIMRegionalSettingsView : NSView
@property(nonatomic) BOOL enabled;
@property(nonatomic) BOOL frozen;
@property(nonatomic) VIMRegionalConfigurationMode configurationMode;
@property(nonatomic) BOOL showsModeControls;
@property(nonatomic,copy,nullable) void (^onChange)(void);
- (instancetype)initWithRequest:(nullable NSDictionary *)request;
- (nullable NSDictionary *)requestWithError:(NSError **)error;
/// Reads committed controls without ending editing or moving keyboard focus.
- (nullable NSDictionary *)committedRequestWithError:(NSError **)error;
- (NSDictionary *)draftState;
- (void)restoreDraftState:(NSDictionary *)state;
- (void)restoreRequest:(nullable NSDictionary *)request;
@end
NS_ASSUME_NONNULL_END
