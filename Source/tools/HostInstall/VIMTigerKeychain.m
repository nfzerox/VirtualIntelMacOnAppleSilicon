#import "VIMTigerKeychain.h"
#import <Security/Security.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

BOOL VIMCreateTigerCertificateKeychain(NSArray<NSData *> *certificates,NSURL *destination,NSError **error) {
    NSString *name=destination.lastPathComponent;
    struct stat info;
    if(!destination.isFileURL||(![name isEqual:@"X509Anchors"]&&![name isEqual:@"X509Certificates"])||
       !certificates.count||!lstat(destination.fileSystemRepresentation,&info)||errno!=ENOENT) {
        if(error)*error=[NSError errorWithDomain:NSPOSIXErrorDomain code:EINVAL
            userInfo:@{NSLocalizedDescriptionKey:@"Choose a new Tiger certificate keychain in the installation workspace."}];
        return NO;
    }
    // Use an explicit, private database, not the login or system keychain.
    // Apple used these public labels as the certificate-only DB passwords.
    // We add certificate records, never call trust-setting or search-list APIs.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    SecKeychainRef keychain=NULL;
    const char *label=name.UTF8String;
    OSStatus status=SecKeychainCreate(destination.fileSystemRepresentation,(UInt32)strlen(label),label,false,NULL,&keychain);
    BOOL created=status==errSecSuccess;
    if(created)for(NSData *data in certificates) {
        SecCertificateRef certificate=SecCertificateCreateWithData(NULL,(__bridge CFDataRef)data);
        if(!certificate) {status=errSecDecode;break;}
        status=SecCertificateAddToKeychain(certificate,keychain);
        CFRelease(certificate);
        // Multiple Apple packages may supply the same certificate.
        if(status==errSecDuplicateItem)status=errSecSuccess;
        if(status!=errSecSuccess)break;
    }
    if(keychain)CFRelease(keychain);
#pragma clang diagnostic pop
    if(status==errSecSuccess)return YES;
    if(created)unlink(destination.fileSystemRepresentation);
    if(error)*error=[NSError errorWithDomain:NSOSStatusErrorDomain code:status userInfo:@{
        NSLocalizedDescriptionKey:[NSString stringWithFormat:@"Could not build Tiger’s %@ certificate keychain (%d).",name,(int)status]}];
    return NO;
}
