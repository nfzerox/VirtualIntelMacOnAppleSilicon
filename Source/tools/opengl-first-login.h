// Actual 13F34 HiCloudAppDelegate::init recognizes ~/.skipbuddy for MiniBuddy,
// publishes Apple's per-user completion state, then runs its own noninteractive
// prelogin/desktop transition. No injected code or synthesized button presses.
// Big Sur 20G1443 retains the same per-user marker in mbuseragent's
// MBUserConfigurator::userHasSkipBuddySet: completion block (0x10001c54e).
#import <Foundation/Foundation.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// Standalone, single-operation guest tools only. fchdir binds relative access
// to the verified home directory on 10.7-10.9, which lack public openat(). The
// original cwd is always restored before returning/calling any native service.
// operation: 0 inspect, 1 create/reuse authorized intent, 2 retire. Returns
// 0 absent/retired, 1 valid owned marker, -1 unsafe/error.
static inline int VIMOpenGLFirstLoginCookie(NSString *name,uid_t uid,int operation){
    if(geteuid()!=0||uid<501)return -1;
    NSString *home=[@"/Users/" stringByAppendingString:name];
    int cwd=open(".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    int directory=open([home fileSystemRepresentation],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    struct stat st;int result=-1;
    if(cwd<0||directory<0||fstat(directory,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=uid||fchdir(directory))goto done;
    if(lstat(".skipbuddy",&st)){
        if(errno!=ENOENT)goto done;
        if(operation!=1){result=0;goto done;}
        int fd=open(".skipbuddy",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd<0)goto done;
        BOOL valid=!fchmod(fd,0600)&&!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&st.st_nlink==1&&st.st_size==0;
        close(fd);if(!valid)goto done;
    }
    if(!S_ISREG(st.st_mode)||st.st_uid!=0||st.st_nlink!=1||st.st_size!=0||(st.st_mode&0077))goto done;
    if(operation==2){if(unlink(".skipbuddy"))goto done;result=0;}else result=1;
done:
    if(cwd>=0){if(fchdir(cwd))result=-1;close(cwd);}
    if(directory>=0)close(directory);return result;
}
