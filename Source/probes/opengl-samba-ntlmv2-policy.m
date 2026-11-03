// Reversible research control for the native Darwin9 standalone Samba server.
// Never edits accounts, hashes, host settings, or an unrelated preexisting file.
#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <stdio.h>
static BOOL Owned(NSString *path,BOOL directory){
    struct stat st;return !lstat(path.fileSystemRepresentation,&st)&&st.st_uid==0&&
        (directory?S_ISDIR(st.st_mode):(S_ISREG(st.st_mode)&&st.st_nlink==1))&&!(st.st_mode&0022);
}
int main(int argc,const char **argv){@autoreleasepool{
    if(argc!=2||(strcmp(argv[1],"--enable")&&strcmp(argv[1],"--restore")))return 64;
    char release[64]={0};size_t size=sizeof(release);
    if(geteuid()!=0||sysctlbyname("kern.osrelease",release,&size,NULL,0)||strncmp(release,"9.",2))return 77;
    io_service_t vm=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2"));
    if(!vm)return 77;IOObjectRelease(vm);
    NSString *directory=@"/var/root/VIMSMBPolicy95",*backup=[directory stringByAppendingPathComponent:@"Original.conf"];
    NSString *target=@"/etc/smb.conf";
    if(!Owned(target,NO))return 77;
    BOOL restore=!strcmp(argv[1],"--restore");
    if(![[NSFileManager defaultManager] fileExistsAtPath:directory]){
        if(restore)return 0;
        if(mkdir(directory.fileSystemRepresentation,0700))return 1;
    }
    if(!Owned(directory,YES))return 77;
    NSData *current=[NSData dataWithContentsOfFile:target];if(!current)return 1;
    NSData *original=[NSData dataWithContentsOfFile:backup];
    if(original&&!Owned(backup,NO))return 77;
    if(!original){
        if(restore)return 1;
        original=current;
        if(![original writeToFile:backup atomically:YES]||chmod(backup.fileSystemRepresentation,0600))return 1;
    }
    NSMutableData *candidate=[NSMutableData dataWithData:original];
    [candidate appendData:[@"\n; Virtual Intel Mac reversible native NTLMv2-only research control\n[global]\n    use spnego = no\n    ntlm auth = no\n    lanman auth = no\n" dataUsingEncoding:NSUTF8StringEncoding]];
    if(![current isEqual:original]&&![current isEqual:candidate]){fputs("Configuration changed independently; not overwritten.\n",stderr);return 77;}
    NSData *desired=restore?original:candidate;
    if(![desired writeToFile:target atomically:YES]||chmod(target.fileSystemRepresentation,0644)||
       ![[NSData dataWithContentsOfFile:target] isEqual:desired])return 1;
    puts(restore?"Original native Samba configuration restored.":"Native NTLMv2-only profile staged; reload the native service separately.");
    return 0;
}}
