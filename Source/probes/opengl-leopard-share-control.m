// Run the real production ShareHome routine in an isolated native directory
// context. Only reassert the existing local account's already published home
// share in our VM; no account creation, credentials, journal or service toggle.
#define main VIMAccountProgramMain
#include "../tools/opengl-create-account.m"
#undef main
int main(int argc,char **argv){@autoreleasepool{
    if(argc!=2||geteuid()!=0||!VIMOpenGLGuestMachine())return 77;
    NSString *name=[NSString stringWithUTF8String:argv[1]];
    struct passwd *user=getpwnam(argv[1]);
    NSString *home=[@"/Users/" stringByAppendingString:name];
    if(!user||user->pw_uid<501||strcmp(user->pw_dir,[home fileSystemRepresentation]))return 77;
    VIMAccountNode *node=VIMAccountLocalNode();
    // Keep the wrapper alive across ShareHome just like the provisioner.
    // Its native reference must NOT remain open across a record-type change.
    VIMAccountRecord *account=[node recordWithRecordType:kODRecordTypeUsers name:name attributes:nil error:NULL];
    if(!account)return 77;
    NSArray *shares=HomeShares(node,name);
    if([shares count]!=1||![First([shares objectAtIndex:0],@"dsAttrTypeNative:directory_path") isEqual:home])return 77;
    BOOL result=ShareHome(node,name,home);
    if(![First(account,kODAttributeTypeNFSHomeDirectory) isEqual:home])return 1;
    printf("production-share-home-fresh-context=%d\n",result);
    return result?0:1;
}}
