#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN
/// Extract the original gzip/bzip2/plain CPIO stream with native system pax.
/// The callback observes compressed input consumption (including bounded tool
/// read-ahead), without reading/copying the payload again. NO cancels and reaps
/// the owned extraction processes before returning. There is no time limit.
BOOL VIMExtractCPIOPayload(NSURL *payload,NSURL *root,
                          BOOL (^progress)(uint64_t inputBytes),NSError **error);
NS_ASSUME_NONNULL_END
