#pragma once
#import <OpenDirectory/OpenDirectory.h>

// Cold, authorized first-account setup only. El Capitan's native SMB NTLM
// service uses the local KDC even when the shadow record contains an NT hash.
// Match Setup Assistant's native authority; let OD generate its own keys.
static int VIMOpenGLPrepareLocalKerberos(ODNode *node,ODRecord *record,
    NSString *name,NSString *password,NSError **error)
{
    ODRecord *config=[node recordWithRecordType:@"dsRecTypeStandard:Config"
        name:@"KerberosKDC" attributes:[NSArray arrayWithObject:kODAttributeTypeFullName] error:error];
    NSArray *realms=[config valuesForAttribute:kODAttributeTypeFullName error:error];
    if(!config||![realms count])return 75; // Native first-boot KDC not published yet.
    NSString *realm=[realms count]==1?[realms objectAtIndex:0]:nil;
    if(![realm isKindOfClass:[NSString class]]||![realm hasPrefix:@"LKDC:SHA1."]||
       [realm rangeOfString:@";"].location!=NSNotFound)return 1;
    NSString *authority=[NSString stringWithFormat:@";Kerberosv5;;%@@%@;%@",name,realm,realm];
    NSArray *authorities=[record valuesForAttribute:kODAttributeTypeAuthenticationAuthority error:error];
    BOOL registered=[authorities containsObject:authority]||
        [authorities containsObject:[authority stringByAppendingString:@";"]];
    NSArray *keys=[record valuesForAttribute:@"dsAttrTypeNative:KerberosKeys" error:NULL];
    if(registered&&[keys count])return 0;
    if(!registered&&![record addValue:authority toAttribute:kODAttributeTypeAuthenticationAuthority error:error])return 1;
    if(![record changePassword:password toPassword:password error:error]||
       ![record synchronizeAndReturnError:error])return 1;
    // synchronize refreshes cached attributes as well as committing changes.
    // Without it a successful password operation can look like missing keys.
    keys=[record valuesForAttribute:@"dsAttrTypeNative:KerberosKeys" error:error];
    if(![keys count]){
        // High Sierra's PlistFile module elides same-password/same-hash
        // updates even when this newly provisioned authority has no keys.
        // Use the SAME system key generator as PlistFile, then publish through
        // OD. No password rotation, donated keys, raw shadow-file edits or
        // changes to preexisting accounts. The caller has verified ownership
        // and the requested password before entering this cold setup routine.
        CFURLRef URL=CFURLCreateWithFileSystemPath(NULL,CFSTR("/System/Library/PrivateFrameworks/HeimODAdmin.framework"),kCFURLPOSIXPathStyle,true);
        CFBundleRef bundle=URL?CFBundleCreate(NULL,URL):NULL;
        if(URL)CFRelease(URL);
        typedef CFArrayRef (*DefaultEnctypes)(CFErrorRef *);
        typedef CFArrayRef (*ModifyKeys)(CFArrayRef,CFStringRef,CFArrayRef,CFTypeRef,unsigned long,CFErrorRef *);
        DefaultEnctypes defaults=bundle?(DefaultEnctypes)CFBundleGetFunctionPointerForName(bundle,CFSTR("HeimODCopyDefaultEnctypes")):NULL;
        ModifyKeys modify=bundle?(ModifyKeys)CFBundleGetFunctionPointerForName(bundle,CFSTR("HeimODModifyKeys")):NULL;
        CFArrayRef standard=defaults?defaults(NULL):NULL;
        CFMutableArrayRef enctypes=standard?CFArrayCreateMutableCopy(NULL,0,standard):NULL;
        if(standard)CFRelease(standard);
        // PlistFile adds this enctype when the account's SMB-NT hash is on;
        // default AES-only keys cannot authenticate native NTLMv2 clients.
        if(enctypes&&!CFArrayContainsValue(enctypes,CFRangeMake(0,CFArrayGetCount(enctypes)),CFSTR("arcfour-hmac-md5")))
            CFArrayAppendValue(enctypes,CFSTR("arcfour-hmac-md5"));
        NSString *principal=[NSString stringWithFormat:@"%@@%@",name,realm];
        CFArrayRef generated=enctypes&&modify?modify(NULL,(CFStringRef)principal,enctypes,(CFStringRef)password,0,NULL):NULL;
        BOOL saved=generated&&CFArrayGetCount(generated)>0&&
            [record setValue:(NSArray *)generated forAttribute:@"dsAttrTypeNative:KerberosKeys" error:error]&&
            [record synchronizeAndReturnError:error];
        if(generated)CFRelease(generated);
        if(enctypes)CFRelease(enctypes);
        if(bundle)CFRelease(bundle);
        if(!saved)return 1;
        keys=[record valuesForAttribute:@"dsAttrTypeNative:KerberosKeys" error:error];
    }
    return [keys count]&&[record verifyPassword:password error:error]?0:1;
}
