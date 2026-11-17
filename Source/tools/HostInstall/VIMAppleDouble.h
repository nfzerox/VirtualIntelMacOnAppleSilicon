#import <Foundation/Foundation.h>

// Native pax merges AppleDouble entries into filesystem metadata. Some Apple
// packages contain only the sidecar: copyfile creates the empty data fork too.
// Normalize that ledger entry to the actual file, retaining source ownership
// and the mode chosen by the extractor. Missing ordinary files remain errors.
void VIMNormalizeAppleDoubleMetadata(NSURL *root,
    NSMutableDictionary<NSString *,NSArray<NSNumber *> *> *metadata);
