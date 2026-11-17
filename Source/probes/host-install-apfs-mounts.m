// Exercise the shipping volume-group mount lifecycle on a new, empty image.
// No installer extraction, VM boot, existing disk edits or force unmounts.
#define main VIMHostInstallWorkerMain
#import "../tools/HostInstall/install-worker.m"
#undef main

static void MountState(NSDictionary *volume,NSString *label) {
    NSString *disk=DiskIdentifier(volume[@"DeviceIdentifier"]);
    NSDictionary *state=ToolPlist(Diskutil,@[@"info",@"-plist",disk]);
    fprintf(stderr,"%s: %s path=%s\n",label.UTF8String,disk.UTF8String,[state[@"MountPoint"] UTF8String]?:"-");
}

int main(int argc,const char *argv[]) { @autoreleasepool {
    BOOL hold=argc==3&&!strcmp(argv[2],"--hold");
    if(argc!=2&&!hold)return 64;
    uid_t owner=getuid();gid_t group=getgid();
    if(hold&&!owner) {
        const char *uid=getenv("SUDO_UID"),*gid=getenv("SUDO_GID");
        if(!uid||!gid||!atoi(uid))return 64;
        owner=(uid_t)atoi(uid);group=(gid_t)atoi(gid);
    }
    NSURL *attempt=[NSURL fileURLWithPath:@(argv[1]) isDirectory:YES];
    NSError *error=nil;
    if(![NSFileManager.defaultManager createDirectoryAtURL:attempt withIntermediateDirectories:NO
        attributes:@{NSFilePosixPermissions:@0700} error:&error])return 73;
    if(hold&&chown(attempt.fileSystemRepresentation,owner,group))return 73;
    NSURL *image=At(attempt,@"MountFixture.sparsebundle"),*data=At(attempt,@"Data"),*system=At(attempt,@"System");
    Directory(data);Directory(system);BOOL attached=NO;
    @try {
        CreateSplitAPFSGroup(image,nil,nil,attempt,8,YES,&attached);
        WithSplitAPFSDisk(image,&attached,^(NSString *whole,__unused NSArray<NSDictionary *> *parts){
            NSDictionary *named=SplitAPFSGroup([whole stringByAppendingString:@"s2"].lastPathComponent,attempt,YES);
            MountState(named[APFSData],@"Before Data");MountState(named[APFSSystem],@"Before System");
            WithAPFSSystemAndData(named,system,data,^{
                MountState(named[APFSData],@"Mounted Data");MountState(named[APFSSystem],@"Mounted System");
                if(hold) {
                    // Keep the exact production mount lifecycle while a
                    // separate, unprivileged archive probe uses this new disk.
                    // EOF also releases the mount; no force eject or polling.
                    Require(!chown(system.fileSystemRepresentation,owner,group),@"Could not assign the disposable archive root.");
                    fprintf(stderr,"READY %s owner=%u; send one byte to detach\n",system.fileSystemRepresentation,owner);
                    char done;read(STDIN_FILENO,&done,1);
                }
            });
        });
        puts("PASS independent System/Data mounting through the shipping helpers");return 0;
    } @catch(NSException *issue) {
        fprintf(stderr,"%s\n",issue.reason.UTF8String);return 1;
    }
} }
