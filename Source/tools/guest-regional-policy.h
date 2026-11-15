#import <Foundation/Foundation.h>

/* Pure policy: no preferences, accounts, environment, or filesystem changes.
 * nil requests mean preserve guest defaults and return nil without an error.
 * Normalization validates syntax rather than the host or guest ISO registry;
 * only the resolver's native capability inventories establish support.
 * Result language values are canonical IDs; *NativeLanguage values retain the
 * exact corresponding capability entry (including period .lproj names).
 * Optional unverified settings are omitted. Warnings and Skipped are arrays.
 */
NSDictionary *VIMRegionalNormalizeRequest(id request, NSError **error);
NSDictionary *VIMRegionalResolve(NSDictionary *request, NSDictionary *capabilities, NSError **error);
NSString *VIMRegionalCanonicalLanguageIdentifier(id value);
BOOL VIMRegionalCompatibleLanguageIdentifiers(NSString *left, NSString *right);
