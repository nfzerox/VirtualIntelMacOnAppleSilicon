#include <sys/mount.h>

static int OpenInstallLock(NSURL *root) {
#ifdef VIM_INSTALL_PLAN_TESTING
    Check(![root.path isEqual:@"/"],@"Fixture lock cannot target the live system");
    NSURL *directory=root;
#else
    Check([root.path isEqual:@"/"]&&geteuid()==0,@"Installation lock requires the live guest and root");
    NSURL *directory=[NSURL fileURLWithPath:@"/private/var/db/VirtualIntelMac"];
    if(mkdir(directory.fileSystemRepresentation,0700)!=0)Check(errno==EEXIST,@"Cannot create private installer state directory");
#endif
    int parent=open(directory.fileSystemRepresentation,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    Check(parent>=0,@"Cannot open installer state directory");
    struct stat st;
    BOOL safe=fstat(parent,&st)==0&&S_ISDIR(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&0022);
    if(!safe){close(parent);Check(NO,@"Installer state directory has unsafe ownership or permissions");}
    int fd=openat(parent,"install.lock",O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);close(parent);
    Check(fd>=0,@"Cannot open installation lock");
    safe=fstat(fd,&st)==0&&S_ISREG(st.st_mode)&&st.st_nlink==1&&st.st_uid==geteuid()&&!(st.st_mode&0077);
    if(!safe){close(fd);Check(NO,@"Installation lock has unsafe ownership, permissions or links");}
    return fd;
}

static BOOL MakeGuestSystemWritable(NSURL *root,BOOL mojave) {
#ifdef VIM_INSTALL_PLAN_TESTING
    Check(![root.path isEqual:@"/"],@"Fixture must never remount the host");
    return NO;
#else
    // Repeat admission at the mutation boundary, even if an earlier caller
    // already checked it. Never expose a generic host remount operation.
    NSDictionary *admission=LiveAdmission();
    Check([root.path isEqual:@"/"]&&[admission[@"ProductVersion"] hasPrefix:mojave?@"10.14.":@"10.15."],@"System volume does not match the guest release");
    struct statfs before,after;
    Check(statfs("/",&before)==0&&!strcmp(before.f_mntonname,"/"),@"Cannot identify the mounted guest system");
    if(!(before.f_flags&MNT_RDONLY))return NO;
    Check(!mojave&&!strcmp(before.f_fstypename,"apfs"),@"Unsupported read-only guest system volume");
    NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:@"/sbin/mount"];
    task.arguments=@[@"-uw",@"/"];task.standardOutput=NSFileHandle.fileHandleWithStandardError;
    task.standardError=NSFileHandle.fileHandleWithStandardError;NSError *error=nil;
    Check([task launchAndReturnError:&error],error.localizedDescription);
    [task waitUntilExit];
    Check(task.terminationReason==NSTaskTerminationReasonExit&&task.terminationStatus==0,@"Could not make Catalina's system volume writable");
    Check(statfs("/",&after)==0&&!(after.f_flags&MNT_RDONLY)&&
          !memcmp(&before.f_fsid,&after.f_fsid,sizeof(before.f_fsid))&&
          !strcmp(before.f_mntfromname,after.f_mntfromname),@"Guest system volume changed or remains read-only");
    return YES;
#endif
}
