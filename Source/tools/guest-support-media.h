// Copy the identified read-only support disc into private staging.
// No mounted code is executed and no system file is installed here.
#include <sys/mount.h>

static void VIMMediaCheck(BOOL ok, NSString *message) {
    if(!ok)@throw [NSException exceptionWithName:@"SupportMedia" reason:message userInfo:nil];
}
static void VIMMediaCopy(int fd, int output, uint64_t expected) {
    struct stat st;
    VIMMediaCheck(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>=0&&(uint64_t)st.st_size==expected,
                  @"A support file has an unexpected type or size.");
    uint8_t buffer[262144];uint64_t total=0;
    for(;;) {
        ssize_t n=read(fd,buffer,sizeof(buffer));if(n<0&&errno==EINTR)continue;
        VIMMediaCheck(n>=0,@"Could not read a support file.");if(!n)break;
        total+=(uint64_t)n;VIMMediaCheck(total<=expected,@"A support file changed while being read.");
        for(ssize_t offset=0;output>=0&&offset<n;) {
            ssize_t written=write(output,buffer+offset,(size_t)(n-offset));
            if(written<0&&errno==EINTR)continue;
            VIMMediaCheck(written>0,@"Could not write the private support copy.");offset+=written;
        }
    }
    VIMMediaCheck(total==expected,@"A support file was truncated.");
}
static NSData *VIMMediaReadMarker(int directory, NSString *name) {
    int fd=openat(directory,name.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return nil;
    @try {
        struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>65536)return nil;
        NSMutableData *data=[NSMutableData dataWithLength:(NSUInteger)st.st_size];size_t offset=0;
        while(offset<data.length) {
            ssize_t n=read(fd,(uint8_t *)data.mutableBytes+offset,data.length-offset);
            if(n<0&&errno==EINTR)continue;if(n<=0)return nil;offset+=(size_t)n;
        }
        return data;
    } @finally {close(fd);}
}
static NSDictionary *VIMStageSupportMedia(NSString *guest,NSString *destination,
                                        NSString *volumes,BOOL requireReadOnly) {
    VIMMediaCheck([@[@"mojave",@"catalina",@"bigsur"] containsObject:guest],@"Invalid support OS.");
    int target=open(destination.fileSystemRepresentation,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    VIMMediaCheck(target>=0,@"Could not open private support staging.");
    int volume=-1;
    @try {
        struct stat targetStat;
        VIMMediaCheck(!fstat(target,&targetStat)&&targetStat.st_uid==geteuid()&&
                      (targetStat.st_mode&0777)==0700,@"Support staging must be private and owned by the guest agent.");
        // Automount can lag behind insertion. Inspect read-only mounted volumes,
        // not a guessed /Volumes label (which can be changed or collide).
        for(unsigned attempt=0;attempt<300&&volume<0;attempt++) {
            NSArray *names=[NSFileManager.defaultManager contentsOfDirectoryAtPath:volumes error:NULL];
            VIMMediaCheck(names.count<=256,@"Too many mounted volumes to identify support media safely.");
            for(NSString *name in names) {
                NSString *path=[volumes stringByAppendingPathComponent:name];
                int candidate=open(path.fileSystemRepresentation,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
                if(candidate<0)continue;
                struct stat st;struct statfs fs;
                BOOL allowed=!fstat(candidate,&st)&&(!requireReadOnly||
                    (st.st_dev!=targetStat.st_dev&&!fstatfs(candidate,&fs)&&(fs.f_flags&MNT_RDONLY)));
                NSData *data=allowed?VIMMediaReadMarker(candidate,@"Support.plist"):nil;
                id marker=data?[NSPropertyListSerialization propertyListWithData:data options:0 format:NULL error:NULL]:nil;
                if([marker isKindOfClass:NSDictionary.class]&&[marker[@"FormatVersion"] isEqual:@1]&&
                   [marker[@"Kind"] isEqual:@"VirtualIntelMacSupport"]&&[marker[@"Guest"] isEqual:guest]){volume=candidate;break;}
                close(candidate);
            }
            if(volume<0)usleep(100000);
        }
        VIMMediaCheck(volume>=0,@"The read-only support disc did not become available. Nothing was installed.");
        struct stat xzInfo,gzipInfo;
        BOOL xz=!fstatat(volume,"payload.tar.xz",&xzInfo,AT_SYMLINK_NOFOLLOW),gzip=!fstatat(volume,"payload.tar.gz",&gzipInfo,AT_SYMLINK_NOFOLLOW);
        VIMMediaCheck(xz!=gzip,@"The support disc must contain exactly one payload archive.");
        NSString *archive=xz?@"payload.tar.xz":@"payload.tar.gz";
        NSSet *required=[NSSet setWithArray:@[@"guest-macho",@"guest-image-patch",@"guest-prepare",@"guest-install",@"guest-progress",@"codesign_allocate",@"SupportIcon.png",archive]];
        uint64_t total=0;
        NSMutableArray *records=[NSMutableArray array];
        for(NSString *name in [required.allObjects sortedArrayUsingSelector:@selector(compare:)]) {
            int input=openat(volume,name.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
            struct stat info;
            VIMMediaCheck(input>=0,@"A support disc file is missing.");
            BOOL valid=!fstat(input,&info)&&S_ISREG(info.st_mode)&&info.st_size>0&&info.st_size<=512LL*1024*1024;
            if(!valid){close(input);VIMMediaCheck(NO,@"A support file has an invalid type or size.");}
            uint64_t size=(uint64_t)info.st_size;total+=size;
            if(total>1024ULL*1024*1024){close(input);VIMMediaCheck(NO,@"The support media is too large.");}
            int existing=openat(target,name.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
            if(existing>=0) {
                struct stat owned;
                BOOL safe=!fstat(existing,&owned)&&S_ISREG(owned.st_mode)&&owned.st_uid==geteuid()&&owned.st_nlink==1;
                close(existing);
                if(!safe){close(input);VIMMediaCheck(NO,@"A staged support file is not privately owned.");}
            }else if(errno!=ENOENT){close(input);VIMMediaCheck(NO,@"Could not inspect a staged support file.");}
            NSString *temporary=[@".copy-" stringByAppendingString:NSUUID.UUID.UUIDString];
            int output=-1;
            @try {
                output=openat(target,temporary.fileSystemRepresentation,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
                VIMMediaCheck(output>=0,@"Could not create a private support copy.");
                VIMMediaCopy(input,output,size);
                if(![name hasPrefix:@"payload.tar."]&&![name isEqual:@"SupportIcon.png"])
                    VIMMediaCheck(!fchmod(output,0700),@"Could not prepare a staged support tool.");
                VIMMediaCheck(!fsync(output),@"Could not finish the private support copy.");
                // Private staging belongs to this operation. Replace a
                // bootstrap tool atomically, never truncate a running image.
                VIMMediaCheck(!renameat(target,temporary.fileSystemRepresentation,target,name.fileSystemRepresentation),@"Could not publish the support copy.");
            } @finally {
                if(input>=0)close(input);if(output>=0)close(output);
                unlinkat(target,temporary.fileSystemRepresentation,0);
            }
            [records addObject:@{@"Name":name,@"Size":@(size)}];
        }
        struct statfs filesystem;
        VIMMediaCheck(!fstatfs(volume,&filesystem),@"Could not identify the support disc for safe removal.");
        return @{@"Directory":destination,@"Files":records,@"FilesStaged":@YES,
                 @"Archive":archive,@"MediaDevice":@(filesystem.f_mntfromname),@"Transport":@"read-only-support-media"};
    } @finally {if(volume>=0)close(volume);close(target);}
}
