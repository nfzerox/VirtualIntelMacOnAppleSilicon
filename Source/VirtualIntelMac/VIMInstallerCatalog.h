#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface VIMInstallerCatalog : NSObject
@property(nonatomic, readonly) NSArray<NSDictionary *> *supportedInstallers;
@property(nonatomic, readonly) NSUInteger revision;
/// Where this catalog is refreshed from (the catalog's UpdateURL key).
@property(nonatomic, readonly, nullable) NSURL *updateURL;
+ (nullable instancetype)catalogWithData:(NSData *)data error:(NSError **)error;
+ (nullable instancetype)currentCatalogWithError:(NSError **)error;
+ (nullable NSDictionary *)fileForGuest:(NSString *)guest role:(NSString *)role;
+ (nullable NSDictionary *)fileForURL:(NSURL *)URL;
/// The catalog installer entry for a guest, or the entry owning a file URL.
+ (nullable NSDictionary *)installerForGuest:(NSString *)guest;
+ (nullable NSDictionary *)installerForURL:(NSURL *)URL;
/// Remote data can select approved assembly recipes, never executable commands.
+ (void)refreshWithCompletion:(void (^)(VIMInstallerCatalog *_Nullable catalog, NSError *_Nullable error))completion;
/// Debug override retains the exact Apple URL path on a loopback HTTP server.
+ (NSURL *)downloadURLForAppleURL:(NSURL *)URL;
/// Big Sur and later: Apple keeps only the newest full installer of each
/// release, so resolve it live from Apple's software update catalog when the
/// download starts. The result is an "assistant" file record: Role, URL,
/// Version, Build, Product and Size. Completion runs on the main queue.
/// A configured fixed full installer is used only if no live installer can be
/// resolved; that result has CatalogFallback=YES and may omit build/size.
+ (void)resolveAssistantForGuest:(NSString *)guest
                      completion:(void (^)(NSDictionary *_Nullable file, NSError *_Nullable error))completion;
/// Ordered optional update inputs, never a partial recipe. Callers keep the
/// verified base installer when a lookup or optional download is unavailable.
+ (nullable NSArray<NSDictionary *> *)hostUpdateFilesForRecipe:(NSDictionary *)recipe
    catalog:(NSDictionary *)catalog error:(NSError **)error;
+ (void)resolveHostUpdatesForGuest:(NSString *)guest
    completion:(void (^)(NSArray<NSDictionary *> *_Nullable files,NSError *_Nullable error))completion;
@end
NS_ASSUME_NONNULL_END
