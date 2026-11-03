// Read-only native directory metadata; never prints password/key material.
#import <Foundation/Foundation.h>
#import <OpenDirectory/OpenDirectory.h>
int main(int argc,const char **argv){@autoreleasepool{
    if(argc!=2)return 2;
    NSError *error=nil;
    ODNode *node=[ODNode nodeWithSession:[ODSession defaultSession] type:kODNodeTypeLocalNodes error:&error];
    ODRecord *record=[node recordWithRecordType:kODRecordTypeUsers name:[NSString stringWithUTF8String:argv[1]]
        attributes:[NSArray arrayWithObject:kODAttributeTypeAllAttributes] error:&error];
    if(!record){fprintf(stderr,"record error: %ld\n",(long)error.code);return 1;}
    NSArray *keys=[record valuesForAttribute:@"dsAttrTypeNative:KerberosKeys" error:&error];
    printf("fresh record: key values=%lu error=%ld\n",(unsigned long)keys.count,(long)error.code);
    NSArray *authorities=[record valuesForAttribute:kODAttributeTypeAuthenticationAuthority error:&error];
    NSUInteger kerberos=0;for(NSString *authority in authorities)if([authority hasPrefix:@";Kerberosv5;"])kerberos++;
    printf("Kerberos authorities=%lu\n",(unsigned long)kerberos);
    error=nil;BOOL synced=[record synchronizeAndReturnError:&error];
    keys=[record valuesForAttribute:@"dsAttrTypeNative:KerberosKeys" error:&error];
    printf("synchronized record: success=%d key values=%lu error=%ld\n",synced,(unsigned long)keys.count,(long)error.code);
    return 0;
}}
