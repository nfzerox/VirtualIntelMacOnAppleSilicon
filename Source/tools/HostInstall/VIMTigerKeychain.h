#import <Foundation/Foundation.h>

// Build the certificate-only keychains Apple populated in Tiger's package
// postflight. The caller supplies DER certificates from authenticated media
// and a new destination in the private assembly workspace. This does not
// create the guest System.keychain or change any trust settings.
BOOL VIMCreateTigerCertificateKeychain(NSArray<NSData *> *certificates, NSURL *destination, NSError **error);
