// Mojave's AppKit admits root layer backing through this per-application
// preference. Aperture predates that default and otherwise presents through
// the incompatible legacy direct-window GL surface path. No injection,
// executable patch, global AppKit preference, or per-frame work is needed.
static void PrepareAperturePolicy(NSString *input,NSString *output) {
    NSMutableDictionary *policy=[NSMutableDictionary dictionary];
    struct stat st;
    if(lstat(input.fileSystemRepresentation,&st)==0) {
        Check(S_ISREG(st.st_mode)&&st.st_size>0&&st.st_size<=1024*1024,
              @"Aperture compatibility preferences have an unsafe type or size");
        NSDictionary *original=[NSDictionary dictionaryWithContentsOfFile:input];
        Check(original!=nil,@"Aperture compatibility preferences are invalid; no preferences were replaced");
        [policy addEntriesFromDictionary:original];
    } else Check(errno==ENOENT,@"Cannot read Aperture compatibility preferences");
    policy[@"NSViewAllowsRootLayerBacking"]=@YES;
    NSError *error=nil;
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:policy
        format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
    Check(data&&[data writeToFile:output options:NSDataWritingAtomic error:&error],error.localizedDescription);
    Check(chmod(output.fileSystemRepresentation,0644)==0,@"Cannot secure Aperture compatibility preferences");
}
