// Maintainer-only utility. Select exactly one identity, never export a Keychain.
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#import <CommonCrypto/CommonDigest.h>

static void Check(OSStatus status) {
    if(status==errSecSuccess)return;
    NSString *message=CFBridgingRelease(SecCopyErrorMessageString(status,NULL));
    fprintf(stderr,"Signing Keychain operation failed: %s (%d)\n",message.UTF8String,(int)status);
    exit(1);
}
static NSString *Fingerprint(SecCertificateRef certificate) {
    NSData *data=CFBridgingRelease(SecCertificateCopyData(certificate));
    unsigned char digest[CC_SHA1_DIGEST_LENGTH];
    CC_SHA1(data.bytes,(CC_LONG)data.length,digest);
    NSMutableString *result=[NSMutableString new];
    for(unsigned i=0;i<sizeof(digest);i++)[result appendFormat:@"%02X",digest[i]];
    return result;
}
int main(int argc,const char **argv) { @autoreleasepool {
    if(argc!=4)return 2;
    NSString *operation=@(argv[1]),*expected=@(argv[2]),*path=@(argv[3]);
    NSString *password=NSProcessInfo.processInfo.environment[@"VIM_KEY_EXPORT_PASSWORD"];
    if(!password.length)return 2;
    SecItemImportExportKeyParameters parameters={.version=SEC_KEY_IMPORT_EXPORT_PARAMS_VERSION,
        .passphrase=(__bridge CFStringRef)password};
    if([operation isEqual:@"export"]) {
        CFTypeRef matches=NULL;
        Check(SecItemCopyMatching((__bridge CFDictionaryRef)@{(__bridge id)kSecClass:(__bridge id)kSecClassIdentity,
            (__bridge id)kSecMatchLimit:(__bridge id)kSecMatchLimitAll,(__bridge id)kSecReturnRef:@YES},&matches));
        NSArray *identities=CFBridgingRelease(matches);
        for(id object in identities) {
            SecIdentityRef identity=(__bridge SecIdentityRef)object;
            SecCertificateRef certificate=NULL;Check(SecIdentityCopyCertificate(identity,&certificate));
            BOOL wanted=[Fingerprint(certificate) isEqual:expected];
            if(wanted) {
                NSData *der=CFBridgingRelease(SecCertificateCopyData(certificate));
                CFRelease(certificate);
                CFDataRef archive=NULL;
                Check(SecItemExport(identity,kSecFormatPKCS12,0,&parameters,&archive));
                NSData *data=CFBridgingRelease(archive);
                // A private output directory is created by the Python caller.
                if(![data writeToFile:path options:NSDataWritingWithoutOverwriting error:NULL] ||
                   ![der writeToFile:[path stringByAppendingString:@".cer"] options:NSDataWritingWithoutOverwriting error:NULL])return 1;
                return 0;
            }
            CFRelease(certificate);
        }
        fprintf(stderr,"The requested signing identity was not found.\n");return 1;
    }
    if([operation isEqual:@"import"]||[operation isEqual:@"verify"]) {
        // Verify the archive's identity before mutating the user's Keychain.
        NSData *data=[NSData dataWithContentsOfFile:path];if(!data)return 1;
        CFArrayRef decoded=NULL;
        if(!@available(macOS 15.0,*))return 2;
        Check(SecPKCS12Import((__bridge CFDataRef)data,(__bridge CFDictionaryRef)@{
            (__bridge id)kSecImportExportPassphrase:password,(__bridge id)kSecImportToMemoryOnly:@YES},&decoded));
        NSArray *items=CFBridgingRelease(decoded);
        if(items.count!=1)return 1;
        SecIdentityRef identity=(__bridge SecIdentityRef)items.firstObject[(__bridge id)kSecImportItemIdentity];
        SecCertificateRef certificate=NULL;Check(SecIdentityCopyCertificate(identity,&certificate));
        BOOL wanted=[Fingerprint(certificate) isEqual:expected];CFRelease(certificate);
        if(!wanted){fprintf(stderr,"Signing archive fingerprint mismatch.\n");return 1;}
        if([operation isEqual:@"verify"])return 0;
        SecTrustedApplicationRef codesign=NULL;Check(SecTrustedApplicationCreateFromPath("/usr/bin/codesign",&codesign));
        SecAccessRef access=NULL;
        Check(SecAccessCreate(CFSTR("Virtual Intel Mac Local Development"),(__bridge CFArrayRef)@[(__bridge id)codesign],&access));
        CFRelease(codesign);parameters.accessRef=access;
        SecKeychainRef keychain=NULL;Check(SecKeychainCopyDefault(&keychain));
        SecExternalFormat format=kSecFormatPKCS12;SecExternalItemType type=kSecItemTypeAggregate;
        Check(SecItemImport((__bridge CFDataRef)data,NULL,&format,&type,0,&parameters,keychain,NULL));
        CFRelease(keychain);CFRelease(access);return 0;
    }
    return 2;
} }
