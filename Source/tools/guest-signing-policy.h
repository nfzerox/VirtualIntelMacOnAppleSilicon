// Capture signing policy BEFORE a Mach-O edit drops/invalidates its signature.
// --preserve-metadata alone cannot recover entitlements from an unsigned result.
#import <Security/Security.h>
static void RememberEntitlements(NSString *input,NSString *output,
                                 NSMutableDictionary *policies,NSString *work) {
    SecStaticCodeRef code=NULL;CFDictionaryRef information=NULL;
    OSStatus status=SecStaticCodeCreateWithPath((__bridge CFURLRef)[NSURL fileURLWithPath:input],kSecCSDefaultFlags,&code);
    if(status!=errSecSuccess)return; // Loose dsce dylibs may have no signature.
    status=SecCodeCopySigningInformation(code,kSecCSSigningInformation,&information);
    CFRelease(code);
    if(status!=errSecSuccess){if(information)CFRelease(information);return;}
    id policy=[(__bridge NSDictionary *)information objectForKey:(__bridge NSString *)kSecCodeInfoEntitlementsDict];
    if(policy)Check([policy isKindOfClass:NSDictionary.class],@"Source code-signing entitlements are invalid");
    if([policy count]) {
        NSString *file=[work stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".entitlements.plist"]];
        Check([policy writeToFile:file atomically:YES],@"Cannot preserve source code-signing entitlements");
        policies[output]=file;
    }
    CFRelease(information);
}
