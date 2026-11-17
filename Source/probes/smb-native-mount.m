// One-shot host-side SMB qualification through the public NetFS mount API
// used by macOS. Password comes from stdin, never argv, logs or Keychain.
// Mount only; the caller owns the empty mount directory and normal unmount.
#import <Foundation/Foundation.h>
#import <NetFS/NetFS.h>
#include <sys/mount.h>

int main(int argc,const char *argv[]) { @autoreleasepool {
    if(argc!=4){fputs("usage: smb-native-mount SMB_URL USER EMPTY_MOUNT_DIRECTORY < password\n",stderr);return 64;}
    setvbuf(stdout,NULL,_IONBF,0);
    NSURL *server=[NSURL URLWithString:@(argv[1])];
    NSURL *directory=[NSURL fileURLWithPath:@(argv[3]) isDirectory:YES];
    if(![server.scheme isEqual:@"smb"]||server.password.length)return 64;
    NSData *input=[[NSFileHandle fileHandleWithStandardInput] readDataToEndOfFile];
    NSString *password=[[NSString alloc] initWithData:input encoding:NSUTF8StringEncoding];
    if([password hasSuffix:@"\n"])password=[password substringToIndex:password.length-1];
    if(!password)return 65;
    NSMutableDictionary *open=[@{(__bridge NSString *)kNAUIOptionKey:(__bridge NSString *)kNAUIOptionNoUI,
        (__bridge NSString *)kNetFSForceNewSessionKey:@YES,
        (__bridge NSString *)kNetFSAllowLoopbackKey:@YES} mutableCopy];
    NSMutableDictionary *mount=[@{(__bridge NSString *)kNetFSMountAtMountDirKey:@YES,
        (__bridge NSString *)kNetFSSoftMountKey:@YES,
        (__bridge NSString *)kNetFSMountFlagsKey:@(MNT_NOSUID|MNT_NODEV)} mutableCopy];
    CFArrayRef points=NULL;
    puts("Connecting through native NetFS without authentication UI");
    int status=NetFSMountURLSync((__bridge CFURLRef)server,(__bridge CFURLRef)directory,
        (__bridge CFStringRef)@(argv[2]),(__bridge CFStringRef)password,
        (__bridge CFMutableDictionaryRef)open,(__bridge CFMutableDictionaryRef)mount,&points);
    printf("NetFSMountURLSync status=%d\n",status);
    if(points)CFRelease(points);
    if(status)return 1;
    struct statfs fs;
    if(statfs(directory.fileSystemRepresentation,&fs)||strcmp(fs.f_fstypename,"smbfs"))return 1;
    puts("Verified native authenticated smbfs mount");return 0;
} }
