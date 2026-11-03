// Unprivileged read-only control for the actual native account adapter.
// Optional --verify-password privately checks the current user's supplied
// password. Never creates an account or changes a service/permission/password.
// --share reads one exact native share. --flush-share additionally requests
// the adapter's root/VM-guarded record flush, without changing its attributes.
#import "../tools/opengl-leopard-directory.h"
#include <pwd.h>
#include <unistd.h>
#include <stdio.h>
int main(int argc,char **argv){
    if(argc==3&&(!strcmp(argv[2],"--share")||!strcmp(argv[2],"--flush-share"))){
        // Report native SharePoints errors/values, not passwords, user records
        // or the account transaction journal. Flushing is explicit below.
        NSAutoreleasePool *pool=[NSAutoreleasePool new];NSError *error=nil;
        VIMLeopardDirectory *node=[VIMLeopardDirectory localDirectory:&error];
        NSArray *shares=[node recordsOfType:@kDSStdRecordTypeSharePoints
            named:[NSString stringWithUTF8String:argv[1]] error:&error];
        printf("share-count=%ld error=%ld\n",shares?(long)[shares count]:-1L,(long)[error code]);
        if([shares count]==1){
            VIMLeopardRecord *record=[shares objectAtIndex:0];
            for(NSString *key in [NSArray arrayWithObjects:@"directory_path",@"smb_name",@"smb_shared",@"smb_guestaccess",nil]){
                error=nil;NSArray *values=[record valuesForAttribute:[@"dsAttrTypeNative:" stringByAppendingString:key] error:&error];
                printf("%s=%s error=%ld\n",[key UTF8String],[[values description] UTF8String],(long)[error code]);
            }
            if(!strcmp(argv[2],"--flush-share")){
                error=nil;BOOL flushed=[record synchronizeAndReturnError:&error];
                printf("share-flush=%d error=%ld\n",flushed,(long)[error code]);
            }else printf("share-flush-not-invoked (read-only)\n");
        }
        [pool drain];return shares?0:1;
    }
    BOOL authenticate=argc==3&&!strcmp(argv[2],"--verify-password");
    if((argc!=2&&!authenticate)||geteuid()==0)return 64;
    NSAutoreleasePool *pool=[NSAutoreleasePool new];NSError *error=nil;
    VIMLeopardDirectory *node=[VIMLeopardDirectory localDirectory:&error];
    NSString *name=[NSString stringWithUTF8String:argv[1]];struct passwd *native=getpwnam(argv[1]);
    VIMLeopardRecord *record=[node recordWithRecordType:@kDSStdRecordTypeUsers name:name attributes:nil error:&error];
    NSArray *uids=[record valuesForAttribute:@kDS1AttrUniqueID error:&error];
    NSArray *homes=[record valuesForAttribute:@kDS1AttrNFSHomeDirectory error:&error];
    NSArray *guids=[record valuesForAttribute:@kDS1AttrGeneratedUID error:&error];
    CFUUIDRef guid=[guids count]==1?CFUUIDCreateFromString(NULL,(CFStringRef)[guids objectAtIndex:0]):NULL;
    BOOL read=record&&native&&[uids count]==1&&[homes count]==1&&guid&&
        [[uids objectAtIndex:0] isEqual:[NSString stringWithFormat:@"%u",native->pw_uid]]&&
        [[homes objectAtIndex:0] isEqual:[NSString stringWithUTF8String:native->pw_dir]];
    if(guid)CFRelease(guid);printf("native-record-read=%d error=%ld\n",read,(long)[error code]);
    error=nil;NSArray *matches=[node recordsOfType:@kDSStdRecordTypeUsers named:name error:&error];
    BOOL query=matches&&[matches count]==1;printf("native-exact-query=%d error=%ld\n",query,(long)[error code]);
    CFUUIDRef unique=CFUUIDCreate(NULL);NSString *absent=(NSString *)CFUUIDCreateString(NULL,unique);CFRelease(unique);
    error=nil;NSArray *missing=[node recordsOfType:@kDSStdRecordTypeUsers named:absent error:&error];[absent release];
    BOOL empty=missing&&![missing count];printf("native-absent-query=%d error=%ld\n",empty,(long)[error code]);
    // An unprivileged fixture may not mutate even if accidentally run in a
    // VM. This call must fail BEFORE the native write API is invoked.
    error=nil;BOOL refused=record&&[uids count]==1&&![record setValue:[uids objectAtIndex:0] forAttribute:@kDS1AttrUniqueID error:&error]&&
        [error code]==eDSPermissionError;
    printf("unprivileged-mutation-refused=%d\n",refused);
    BOOL verified=YES;
    if(authenticate){
        // Only the logged-in account, never an arbitrary authentication probe.
        if(!native||native->pw_uid!=getuid())verified=NO;
        else{
            char *input=getpass("Current user's password (not saved): ");
            NSString *password=input?[[NSString alloc] initWithUTF8String:input]:nil;
            if(input){volatile char *wipe=input;size_t count=strlen(input);while(count--)*wipe++=0;}
            error=nil;verified=password&&[record verifyPassword:password error:&error];[password release];
            printf("native-password-verification=%d error=%ld\n",verified,(long)[error code]);
        }
    }
    [pool drain];return read&&query&&empty&&refused&&verified?0:1;
}
