#import <Foundation/Foundation.h>
#import "../tools/VIMFileTransaction.h"
#define VIM_BOOT_POLICY_FIXTURE 1
static void Check(BOOL ok,NSString *message){if(!ok)@throw [NSException exceptionWithName:@"PolicyFixture" reason:message userInfo:nil];}
#include "../tools/guest-boot-policy.h"
static void Rejected(void (^operation)(void)) {BOOL rejected=NO;@try{operation();}@catch(NSException *exception){rejected=YES;}NSCAssert(rejected,@"Unsafe or repeated operation must fail");}
int main(void){@autoreleasepool {
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *base=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]];
    NSURL *root=[base URLByAppendingPathComponent:@"root"],*journal=[base URLByAppendingPathComponent:@"stage/Policy.plist"];
    NSURL *file=[root URLByAppendingPathComponent:BootPolicyTarget];
    [fm createDirectoryAtURL:file.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0755} error:nil];
    NSDictionary *original=@{@"DisableLibraryValidation":@NO,@"PreserveThisSetting":@[@1,@"untouched"]};
    [original writeToURL:file atomically:YES];chmod(file.fileSystemRepresentation,0644);
    NSDictionary *result=ConfigureLibraryValidationPolicy(root,journal);
    NSCAssert([result[@"PreferenceVerified"] isEqual:@YES]&&[result[@"Changed"] isEqual:@YES],@"Preference published and verified");
    NSCAssert([[ReadLibraryValidationPolicy(root) objectForKey:@"PreserveThisSetting"] isEqual:original[@"PreserveThisSetting"]],@"Existing keys preserved");
    NSData *first=[NSData dataWithContentsOfURL:file];
    NSCAssert([ConfigureLibraryValidationPolicy(root,journal)[@"Changed"] isEqual:@NO]&&[first isEqual:[NSData dataWithContentsOfURL:file]],@"Already-correct preference is a no-op");
    NSError *error=nil;VIMFileTransaction *transaction=[VIMFileTransaction loadURL:journal error:&error];
    NSCAssert([transaction rollback:&error]&&[ReadLibraryValidationPolicy(root) isEqual:original],@"Original preference/keys recoverable");
    Rejected(^{ConfigureLibraryValidationPolicy(root,journal);});
    chmod(file.fileSystemRepresentation,0666);Rejected(^{ReadLibraryValidationPolicy(root);});chmod(file.fileSystemRepresentation,0644);
    NSURL *outside=[base URLByAppendingPathComponent:@"outside.plist"];[original writeToURL:outside atomically:YES];
    [fm removeItemAtURL:file error:nil];[fm createSymbolicLinkAtURL:file withDestinationURL:outside error:nil];
    Rejected(^{ReadLibraryValidationPolicy(root);});NSCAssert([[NSDictionary dictionaryWithContentsOfURL:outside] isEqual:original],@"Symlink target unchanged");
    [fm removeItemAtURL:file error:nil];[@"not a plist" writeToURL:file atomically:YES encoding:NSUTF8StringEncoding error:nil];chmod(file.fileSystemRepresentation,0644);
    Rejected(^{ReadLibraryValidationPolicy(root);});
    [fm removeItemAtURL:base error:nil];
    puts("PASS: guest preference-only publication, existing-key preservation, no-op, rollback, repeat/symlink/permissions/malformed-file rejection; no live system touched");
}return 0;}
