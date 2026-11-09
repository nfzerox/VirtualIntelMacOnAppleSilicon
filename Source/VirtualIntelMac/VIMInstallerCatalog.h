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
@end
NS_ASSUME_NONNULL_END
