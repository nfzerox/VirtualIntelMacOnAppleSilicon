// Trust the guest agent's private staging boundary, not a supplied plist.
// A root-owned 0700 ancestor prevents unprivileged replacement during use.
static NSString *TrustedStagingPath(NSString *path,BOOL mayBeMissing,uid_t owner) {
    Check(path.isAbsolutePath&&![path.pathComponents containsObject:@".."]&&![path.pathComponents containsObject:@"."],@"Installer staging path is not absolute and normalized");
    if([path hasPrefix:@"/var/tmp/"]||[path hasPrefix:@"/var/db/"])path=[@"/private" stringByAppendingString:path];
    NSString *normalized=path.stringByStandardizingPath;
    if([normalized hasPrefix:@"/var/tmp/"]||[normalized hasPrefix:@"/var/db/"])normalized=[@"/private" stringByAppendingString:normalized];
    Check([path isEqual:normalized],@"Installer staging path is not normalized");
    // Read legacy temporary transactions, but new installations use /var/db
    // so updates cannot discard their journal, rollback files or verifier.
    NSString *base=[path hasPrefix:@"/private/var/db/"]?@"/private/var/db/":@"/private/var/tmp/";
    Check([path hasPrefix:base],@"Installer input is outside private guest staging");
    NSArray *parts=[[path substringFromIndex:base.length] pathComponents];
    Check(parts.count>0&&[parts[0] hasPrefix:@"VirtualIntelMac."]&&[parts[0] length]>16,@"Invalid guest staging directory");
    NSString *current=[base stringByAppendingPathComponent:parts[0]],*staging=current;
    for(NSUInteger index=0;index<parts.count;index++) {
        if(index)current=[current stringByAppendingPathComponent:parts[index]];
        struct stat st;
        if(lstat(current.fileSystemRepresentation,&st)!=0) {
            Check(mayBeMissing&&index==parts.count-1&&index>0&&errno==ENOENT,@"Installer staging input is missing");
            break;
        }
        Check(!S_ISLNK(st.st_mode)&&st.st_uid==owner&&!(st.st_mode&0022),@"Installer staging input has unsafe ownership, permissions or a symlink");
        if(index==0)Check(S_ISDIR(st.st_mode)&&!(st.st_mode&0077),@"Guest staging directory is not private");
        if(index<parts.count-1)Check(S_ISDIR(st.st_mode),@"Installer staging ancestor is not a directory");
        else Check(S_ISDIR(st.st_mode)||(S_ISREG(st.st_mode)&&st.st_nlink==1),@"Unsupported installer staging file or hard link");
    }
    return staging;
}
