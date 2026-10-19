// Copy only hash-identified, read-only support media into private staging.
// No mounted code is executed and no system file is installed here.
#include <sys/mount.h>

static void VIMMediaCheck(BOOL ok, NSString *message) {
    if(!ok)@throw [NSException exceptionWithName:@"SupportMedia" reason:message userInfo:nil];
}
static NSString *VIMMediaDigest(int fd, int output, uint64_t expected) {
    struct stat st;
    VIMMediaCheck(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>=0&&(uint64_t)st.st_size==expected,
                  @"A support file has an unexpected type or size.");
    CC_SHA256_CTX context;CC_SHA256_Init(&context);
    uint8_t buffer[262144],digest[CC_SHA256_DIGEST_LENGTH];uint64_t total=0;
    for(;;) {
        ssize_t n=read(fd,buffer,sizeof(buffer));if(n<0&&errno==EINTR)continue;
        VIMMediaCheck(n>=0,@"Could not read a support file.");if(!n)break;
        total+=(uint64_t)n;VIMMediaCheck(total<=expected,@"A support file changed while being read.");
        CC_SHA256_Update(&context,buffer,(CC_LONG)n);
        for(ssize_t offset=0;output>=0&&offset<n;) {
            ssize_t written=write(output,buffer+offset,(size_t)(n-offset));
            if(written<0&&errno==EINTR)continue;
            VIMMediaCheck(written>0,@"Could not write the private support copy.");offset+=written;
        }
    }
    VIMMediaCheck(total==expected,@"A support file was truncated.");
    CC_SHA256_Final(digest,&context);NSMutableString *hash=[NSMutableString string];
    for(NSUInteger i=0;i<sizeof(digest);i++)[hash appendFormat:@"%02x",digest[i]];
    return hash;
}
static NSData *VIMMediaReadManifest(int directory, NSString *name) {
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
static NSDictionary *VIMStageSupportMedia(NSString *manifestName,NSString *destination,
                                        NSString *volumes,BOOL requireReadOnly) {
    VIMMediaCheck([manifestName isEqual:@"support-manifest.plist"],@"Invalid support manifest name.");
    int target=open(destination.fileSystemRepresentation,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    VIMMediaCheck(target>=0,@"Could not open private support staging.");
    int volume=-1;
    @try {
        struct stat targetStat;
        VIMMediaCheck(!fstat(target,&targetStat)&&targetStat.st_uid==geteuid()&&
                      (targetStat.st_mode&0777)==0700,@"Support staging must be private and owned by the guest agent.");
        NSData *trusted=VIMMediaReadManifest(target,manifestName);
        NSDictionary *manifest=trusted?[NSPropertyListSerialization propertyListWithData:trusted options:0 format:NULL error:NULL]:nil;
        NSDictionary *files=[manifest isKindOfClass:NSDictionary.class]?manifest[@"Files"]:nil;
        VIMMediaCheck([files isKindOfClass:NSDictionary.class],@"The trusted support manifest is invalid.");
        BOOL xz=files[@"payload.tar.xz"]!=nil,gzip=files[@"payload.tar.gz"]!=nil;
        VIMMediaCheck(xz!=gzip,@"The support manifest must identify exactly one payload archive.");
        NSSet *required=[NSSet setWithArray:@[@"guest-macho",@"guest-image-patch",@"guest-prepare",@"guest-install",@"guest-progress",@"codesign_allocate",@"SupportIcon.png",xz?@"payload.tar.xz":@"payload.tar.gz"]];
        VIMMediaCheck([manifest[@"FormatVersion"] isEqual:@1]&&
                      [@[@"mojave",@"catalina",@"bigsur"] containsObject:manifest[@"Guest"]]&&
                      [files isKindOfClass:NSDictionary.class]&&[required isEqual:[NSSet setWithArray:files.allKeys]],
                      @"The trusted support manifest is invalid.");
        NSCharacterSet *bad=[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"] invertedSet];
        uint64_t total=0;
        for(NSString *name in required) {
            NSDictionary *file=files[name];NSString *hash=[file isKindOfClass:NSDictionary.class]?file[@"SHA256"]:nil;
            NSNumber *size=[file isKindOfClass:NSDictionary.class]?file[@"Size"]:nil;
            VIMMediaCheck([hash isKindOfClass:NSString.class]&&hash.length==64&&
                          [hash rangeOfCharacterFromSet:bad].location==NSNotFound&&
                          [size isKindOfClass:NSNumber.class]&&size.longLongValue>0&&size.unsignedLongLongValue<=512ULL*1024*1024,
                          @"The support file manifest is invalid.");
            total+=size.unsignedLongLongValue;
        }
        VIMMediaCheck(total<=1024ULL*1024*1024,@"The support media is too large.");
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
                NSData *marker=allowed?VIMMediaReadManifest(candidate,@"Support.plist"):nil;
                if([marker isEqual:trusted]){volume=candidate;break;}
                close(candidate);
            }
            if(volume<0)usleep(100000);
        }
        VIMMediaCheck(volume>=0,@"The read-only support disc did not become available. Nothing was installed.");
        NSMutableArray *records=[NSMutableArray array];
        for(NSString *name in [files.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
            NSDictionary *file=files[name];uint64_t size=[file[@"Size"] unsignedLongLongValue];
            int existing=openat(target,name.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
            if(existing>=0) {
                @try {VIMMediaCheck([VIMMediaDigest(existing,-1,size) isEqual:file[@"SHA256"]],@"A staged support file conflicts with this app build.");}
                @finally {close(existing);}
            } else {
                VIMMediaCheck(errno==ENOENT,@"Could not inspect a staged support file.");
                NSString *temporary=[@".copy-" stringByAppendingString:NSUUID.UUID.UUIDString];
                int input=openat(volume,name.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC),output=-1;
                @try {
                    VIMMediaCheck(input>=0,@"A support disc file is missing.");
                    output=openat(target,temporary.fileSystemRepresentation,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
                    VIMMediaCheck(output>=0,@"Could not create a private support copy.");
                    VIMMediaCheck([VIMMediaDigest(input,output,size) isEqual:file[@"SHA256"]],@"Support file verification failed. Nothing was installed.");
                    VIMMediaCheck(!fsync(output),@"Could not finish the private support copy.");
                    // Publish without replacing an existing file, even after a race.
                    VIMMediaCheck(!linkat(target,temporary.fileSystemRepresentation,target,name.fileSystemRepresentation,0),@"Could not publish the verified support copy.");
                } @finally {
                    if(input>=0)close(input);if(output>=0)close(output);
                    unlinkat(target,temporary.fileSystemRepresentation,0);
                }
            }
            [records addObject:@{@"Name":name,@"Size":@(size),@"SHA256":file[@"SHA256"]}];
        }
        struct statfs filesystem;
        VIMMediaCheck(!fstatfs(volume,&filesystem),@"Could not identify the support disc for safe removal.");
        return @{@"Directory":destination,@"Files":records,@"SHA256Verified":@YES,
                 @"MediaDevice":@(filesystem.f_mntfromname),@"Transport":@"read-only-support-media"};
    } @finally {if(volume>=0)close(volume);close(target);}
}
