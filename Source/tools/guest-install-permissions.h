// Normalize only private, prepared replacement trees before fingerprinting.
// Never follow symlinks or change the existing installed files here.
static void NormalizeReplacement(NSString *path,BOOL rootOwned) {
    struct stat st;Check(lstat(path.fileSystemRepresentation,&st)==0,@"Prepared replacement disappeared");
    if(S_ISLNK(st.st_mode)) {
        if(rootOwned)Check(lchown(path.fileSystemRepresentation,0,0)==0,@"Cannot set replacement symlink ownership");
        return;
    }
    Check(S_ISREG(st.st_mode)||S_ISDIR(st.st_mode),@"Unsupported replacement file type");
    int fd=open(path.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    Check(fd>=0,@"Cannot open prepared replacement for permissions");
    struct stat opened;BOOL same=fstat(fd,&opened)==0&&opened.st_dev==st.st_dev&&opened.st_ino==st.st_ino;
    mode_t mode=S_ISDIR(st.st_mode)||(st.st_mode&0111)?0755:0644;
    BOOL ok=same&&(!rootOwned||fchown(fd,0,0)==0)&&fchmod(fd,mode)==0;close(fd);
    Check(ok,@"Cannot normalize prepared replacement permissions");
    if(S_ISDIR(st.st_mode)) {
        NSArray *names=[NSFileManager.defaultManager contentsOfDirectoryAtPath:path error:NULL];
        Check(names!=nil,@"Cannot inspect prepared replacement directory");
        for(NSString *name in names)NormalizeReplacement([path stringByAppendingPathComponent:name],rootOwned);
    }
}
