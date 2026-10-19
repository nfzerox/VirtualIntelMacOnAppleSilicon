#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface VIMInstallerCatalog : NSObject
@property(nonatomic, readonly) NSArray<NSDictionary *> *supportedInstallers;
@property(nonatomic, readonly) NSUInteger revision;
+ (nullable instancetype)catalogWithData:(NSData *)data error:(NSError **)error;
+ (nullable instancetype)currentCatalogWithError:(NSError **)error;
/// Remote data can select approved assembly recipes, never executable commands.
+ (void)refreshWithCompletion:(void (^)(VIMInstallerCatalog *_Nullable catalog, NSError *_Nullable error))completion;
/// Debug override retains the exact Apple URL path on a loopback HTTP server.
+ (NSURL *)downloadURLForAppleURL:(NSURL *)URL;
@end
NS_ASSUME_NONNULL_END
