// Guest diagnostic: default read-only comparison of a supplied password with
// native OD and this account's embedded SMB credential. The explicit staging
// option adds only a missing NTLM keyset. Print no password, digest or salt.
#import <Foundation/Foundation.h>
#import <OpenDirectory/OpenDirectory.h>
#import <CommonCrypto/CommonDigest.h>
#import "../tools/HostInstall/VIMSMBKeyset.h"
#include <pwd.h>
#include <unistd.h>

int main(int argc, const char **argv) { @autoreleasepool {
    BOOL stage = argc == 3 && !strcmp(argv[2], "--stage-nt-keyset");
    if ((argc != 2 && !stage) || geteuid() != 0 || !getpwnam(argv[1])) return 64;
    NSString *name = [NSString stringWithUTF8String:argv[1]];
    if (![name.lastPathComponent isEqual:name] || [name isEqual:@"."] || [name isEqual:@".."]) return 64;
    NSData *input = [[NSFileHandle fileHandleWithStandardInput] readDataToEndOfFile];
    if (input.length > 4096) return 64;
    NSString *password = [[[NSString alloc] initWithData:input encoding:NSUTF8StringEncoding] autorelease];
    if ([password hasSuffix:@"\n"]) password = [password substringToIndex:password.length - 1];
    if (!password) return 64;
    NSError *error = nil;
    ODNode *node = [ODNode nodeWithSession:ODSession.defaultSession type:kODNodeTypeLocalNodes error:&error];
    ODRecord *record = [node recordWithRecordType:kODRecordTypeUsers name:name attributes:nil error:&error];
    if (!record) { fprintf(stderr, "Native account lookup error %ld\n", (long)error.code); return 1; }
    BOOL verified = [record verifyPassword:password error:&error];
    NSString *path = [@"/private/var/db/dslocal/nodes/Default/users" stringByAppendingPathComponent:[name stringByAppendingPathExtension:@"plist"]];
    NSDictionary *local = [NSDictionary dictionaryWithContentsOfFile:path];
    NSArray *values = [local objectForKey:@"ShadowHashData"];
    NSDictionary *hashes = [values.count ? [NSPropertyListSerialization propertyListWithData:[values objectAtIndex:0]
        options:NSPropertyListImmutable format:NULL error:NULL] : nil retain];
    NSData *nt = [hashes objectForKey:@"NT"];
    if (nt && ![nt isKindOfClass:NSData.class]) { [hashes release]; return 1; }
    NSData *unicode = [password dataUsingEncoding:NSUTF16LittleEndianStringEncoding];
    unsigned char expected[CC_MD4_DIGEST_LENGTH];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    CC_MD4(unicode.bytes, (CC_LONG)unicode.length, expected);
#pragma clang diagnostic pop
    BOOL matches = nt.length == sizeof(expected) && !memcmp(nt.bytes, expected, sizeof(expected));
    NSArray *keys = [record valuesForAttribute:@"dsAttrTypeNative:KerberosKeys" error:NULL];
    NSUInteger before = keys.count;
    if (stage) {
        // Explicit repair of the tested account only, never overwrite existing
        // keys or rotate its password. Same encoder as host-side creation.
        if (!verified || !matches || before ||
            ![record setValue:@[VIMSMBLocalKeyset(nt)] forAttribute:@"dsAttrTypeNative:KerberosKeys" error:&error] ||
            ![record synchronizeAndReturnError:&error]) {
            fprintf(stderr,"Keyset staging refused or failed (%ld)\n",(long)error.code);
            [hashes release]; return 1;
        }
        keys = [record valuesForAttribute:@"dsAttrTypeNative:KerberosKeys" error:NULL];
    }
    NSDictionary *report = @{@"NativePasswordVerified":@(verified), @"NativeError":@(error.code),
        @"EmbeddedHashKinds":hashes.allKeys ?: @[], @"NTBytes":@(nt.length), @"NTMatchesPassword":@(matches),
        @"KerberosKeysetsBefore":@(before), @"KerberosKeysetsAfter":@(keys.count)};
    NSData *json = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:NULL];
    [[NSFileHandle fileHandleWithStandardOutput] writeData:json];
    [hashes release];
    return verified && matches ? 0 : 1;
} }
