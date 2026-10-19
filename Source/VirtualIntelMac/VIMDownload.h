#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
/// One persistent, app-owned transfer directory per Apple URL. Payload validity
/// here means complete HTTP transfer; installer signature validation is separate.
@interface VIMDownload : NSObject
@property(nonatomic, readonly) NSURL *directory;
@property(nonatomic, readonly) NSURL *sourceURL;
/// Main-thread callbacks. Progress uses -1 total when the server gives no size.
@property(nonatomic, copy, nullable) void (^progress)(int64_t received,int64_t total);
- (nullable instancetype)initWithSourceURL:(NSURL *)URL directory:(NSURL *)directory error:(NSError **)error;
- (void)startWithCompletion:(void (^)(NSURL *_Nullable file,NSError *_Nullable error,BOOL paused))completion;
- (void)pause;
@end
NS_ASSUME_NONNULL_END
