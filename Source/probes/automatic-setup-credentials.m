// Finite host-only checks; uses random test IDs, never a real VM credential.
#import <Foundation/Foundation.h>
#import "../VirtualIntelMac/VIMAutomaticSetup.h"
#include <sys/stat.h>
#include <unistd.h>
static void Require(BOOL condition,const char *label){if(!condition){fprintf(stderr,"FAIL %s\n",label);exit(1);}printf("PASS %s\n",label);}
int main(void){@autoreleasepool{
    NSString *identifier=NSUUID.UUID.UUIDString;NSError *error=nil;
    NSString *value=@"credential-probe-é-水";
    Require([VIMAutomaticSetup savePassword:value identifier:identifier error:&error],"private save without Keychain authorization");
    Require([[VIMAutomaticSetup passwordForIdentifier:identifier error:&error] isEqual:value],"UTF-8 round trip");
    NSURL *support=[NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
    NSURL *directory=[support URLByAppendingPathComponent:@"Virtual Intel Mac/Setup Credentials" isDirectory:YES];
    NSURL *file=[directory URLByAppendingPathComponent:identifier];struct stat st;
    Require(!lstat(directory.fileSystemRepresentation,&st)&&st.st_uid==getuid()&&(st.st_mode&0777)==0700,"directory owner/mode");
    Require(!lstat(file.fileSystemRepresentation,&st)&&st.st_uid==getuid()&&(st.st_mode&0777)==0600&&st.st_nlink==1,"credential owner/mode/link count");
    Require([VIMAutomaticSetup savePassword:@"" identifier:identifier error:&error],"empty password supported");
    Require([[VIMAutomaticSetup passwordForIdentifier:identifier error:&error] isEqual:@""],"empty password survives resume");
    Require(!chmod(file.fileSystemRepresentation,0644),"prepare own unsafe-mode fixture");error=nil;
    Require([VIMAutomaticSetup passwordForIdentifier:identifier error:&error]==nil&&error!=nil,"reject nonprivate credential");
    Require(!chmod(file.fileSystemRepresentation,0600),"restore own fixture mode");
    Require([VIMAutomaticSetup removePasswordForIdentifier:identifier error:&error],"retire credential");
    Require(![NSFileManager.defaultManager fileExistsAtPath:file.path],"no credential remains");
    Require(!symlink("/dev/null",file.fileSystemRepresentation),"prepare own symlink fixture");error=nil;
    Require([VIMAutomaticSetup passwordForIdentifier:identifier error:&error]==nil&&error!=nil,"reject credential symlink");
    Require(!unlink(file.fileSystemRepresentation),"remove own symlink fixture");error=nil;
    Require(!mkfifo(file.fileSystemRepresentation,0600),"prepare own nonregular fixture");
    Require([VIMAutomaticSetup passwordForIdentifier:identifier error:&error]==nil&&error!=nil,"reject FIFO without blocking");
    Require(!unlink(file.fileSystemRepresentation),"remove own FIFO fixture");error=nil;
    Require(![VIMAutomaticSetup savePassword:value identifier:@"../invalid" error:&error]&&error!=nil,"reject path traversal");
    puts("All private setup-credential checks passed; no VM or real credential changed.");return 0;
}}
