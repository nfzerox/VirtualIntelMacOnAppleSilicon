// Preference-only guest compatibility. No NVRAM, TCC, host settings or kernel
// changes. The caller admits the live guest and authenticates its identity.
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
static NSString *const BootPolicyTarget=@"Library/Preferences/com.apple.security.libraryvalidation.plist";
static NSDictionary *ReadLibraryValidationPolicy(NSURL *root) {
    BOOL live=[root.path isEqual:@"/"];
    NSURL *parent=root;
    for(NSString *part in @[@"Library",@"Preferences"]) {
        parent=[parent URLByAppendingPathComponent:part];struct stat st;
        Check(lstat(parent.fileSystemRepresentation,&st)==0&&S_ISDIR(st.st_mode)&&!(st.st_mode&0022)&&(!live||st.st_uid==0),
              @"Guest Library Validation preference directory is unsafe");
    }
    NSURL *file=[root URLByAppendingPathComponent:BootPolicyTarget];
    int fd=open(file.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0){Check(errno==ENOENT,@"Cannot read the guest Library Validation preference");return @{};}
    struct stat st;
    BOOL valid=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_nlink==1&&!(st.st_mode&0022)&&
        (!live||st.st_uid==0)&&st.st_size>0&&st.st_size<=1024*1024;
    if(!valid){close(fd);Check(NO,@"Guest Library Validation preference has unsafe ownership, permissions or size");}
    NSMutableData *data=[NSMutableData dataWithLength:(NSUInteger)st.st_size];NSUInteger offset=0;
    while(offset<data.length) {
        ssize_t count=read(fd,(uint8_t *)data.mutableBytes+offset,data.length-offset);
        if(count<0&&errno==EINTR)continue;
        if(count<=0){close(fd);Check(NO,@"Guest Library Validation preference changed while reading");}
        offset+=(NSUInteger)count;
    }close(fd);
    id value=[NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:NULL error:NULL];
    Check([value isKindOfClass:NSDictionary.class],@"Guest Library Validation preference is not a valid dictionary");return value;
}
static BOOL LibraryValidationDisabled(NSDictionary *policy) {
    id value=policy[@"DisableLibraryValidation"];
    return value&&CFGetTypeID((__bridge CFTypeRef)value)==CFBooleanGetTypeID()&&[value boolValue];
}
static NSDictionary *ConfigureLibraryValidationPolicy(NSURL *root,NSURL *journal) {
    NSDictionary *original=ReadLibraryValidationPolicy(root);
    if(LibraryValidationDisabled(original))return @{@"PreferenceVerified":@YES,@"Changed":@NO};
    Check(![NSFileManager.defaultManager fileExistsAtPath:journal.path],@"A boot-policy transaction already exists; inspect its status instead of retrying it");
    NSURL *payload=[journal.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"BootPolicyPayload"];
    NSURL *replacement=[payload URLByAppendingPathComponent:BootPolicyTarget];NSError *error=nil;
    Check(![NSFileManager.defaultManager fileExistsAtPath:payload.path],@"Boot-policy staging already exists; no write was repeated");
    Check([NSFileManager.defaultManager createDirectoryAtURL:replacement.URLByDeletingLastPathComponent
        withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:&error],error.localizedDescription);
    NSMutableDictionary *policy=[original mutableCopy];policy[@"DisableLibraryValidation"]=@YES;
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:policy format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
    Check(data&&[data writeToURL:replacement options:NSDataWritingAtomic error:&error],error.localizedDescription);
    Check(chmod(replacement.fileSystemRepresentation,0644)==0,@"Cannot secure the staged Library Validation preference");
    VIMFileTransaction *transaction=[VIMFileTransaction createAtURL:journal root:root payload:payload targets:@[BootPolicyTarget]
        metadata:@{@"Operation":@"guest-library-validation-preference",@"PolicyVersion":@1} error:&error];
    Check(transaction!=nil,error.localizedDescription);
    // Preserve originals using the same durable publication layer as graphics
    // setup. Do not rerun or silently roll back an uncertain publication.
    Check([transaction publish:&error]&&[transaction verifyPublished:&error],error.localizedDescription);
    Check(LibraryValidationDisabled(ReadLibraryValidationPolicy(root)),@"Published guest compatibility preference did not verify");
    return @{@"PreferenceVerified":@YES,@"Changed":@YES,@"TransactionID":transaction.state[@"ID"],@"GuestJournal":journal.path};
}

#ifndef VIM_BOOT_POLICY_FIXTURE
static NSDictionary *BootPolicyOperation(NSString *requestPath,BOOL configure) {
    NSDictionary *live=LiveAdmissionWithPolicy(NO);
    int (*csrCheck)(uint32_t)=dlsym(RTLD_DEFAULT,"csr_check");
    Check(csrCheck&&csrCheck(1U<<2)==0,@"The guest SIP task-for-pid restriction must be disabled for its Library Validation compatibility policy");
    NSString *directory=TrustedStagingPath(requestPath,NO,0);
    Check([requestPath.lastPathComponent isEqual:@"BootPolicy.json"]&&
          [requestPath.stringByDeletingLastPathComponent isEqual:directory],@"Invalid boot-policy request location");
    int fd=open(requestPath.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat st;
    Check(fd>=0,@"Cannot open boot-policy request");
    BOOL valid=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_nlink==1&&st.st_uid==0&&!(st.st_mode&0077)&&st.st_size>0&&st.st_size<=4096;
    if(!valid){close(fd);Check(NO,@"Boot-policy request is not a small root-private file");}
    NSMutableData *data=[NSMutableData dataWithLength:st.st_size];ssize_t count=read(fd,data.mutableBytes,data.length);close(fd);
    Check(count==(ssize_t)data.length,@"Boot-policy request could not be read completely");
    id request=[NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
    Check([request isKindOfClass:NSDictionary.class]&&[request[@"ProtocolVersion"] isEqual:@1],@"Invalid boot-policy request");
    NSString *guest=[live[@"ProductVersion"] hasPrefix:@"10.14."]?@"mojave":([live[@"ProductVersion"] hasPrefix:@"10.15."]?@"catalina":@"bigsur");
    Check([request[@"Guest"] isEqual:guest]&&[request[@"VolumeUUID"] isEqual:live[@"VolumeUUID"]],@"Boot-policy request belongs to a different guest or volume");
    if(configure)for(NSString *key in @[@"ProductBuildVersion",@"BootSessionUUID"])
        Check([request[key] isEqual:live[key]],@"The guest boot/build changed; no compatibility preference was written");
    NSURL *root=[NSURL fileURLWithPath:@"/"],*journal=[NSURL fileURLWithPath:[directory stringByAppendingPathComponent:@"BootPolicyTransaction.plist"]];
    NSDictionary *outcome=configure?ConfigureLibraryValidationPolicy(root,journal):
        @{@"PreferenceVerified":@(LibraryValidationDisabled(ReadLibraryValidationPolicy(root))),@"Changed":@NO};
    NSMutableDictionary *result=[live mutableCopy];[result addEntriesFromDictionary:outcome];
    result[@"ProtocolVersion"]=@1;result[@"Guest"]=guest;result[@"TaskForPIDRestrictionDisabled"]=@YES;
    result[@"GuestJournal"]=journal.path;result[@"Operation"]=@"guest-library-validation-preference";
    if(!configure&&[NSFileManager.defaultManager fileExistsAtPath:journal.path]) {
        // Read-only inspection never publishes or rolls back an old attempt.
        NSError *error=nil;VIMFileTransaction *transaction=[VIMFileTransaction loadURL:journal error:&error];
        Check(transaction&&[transaction.state[@"Root"] isEqual:@"/"]&&
              [transaction.state[@"Metadata"][@"Operation"] isEqual:@"guest-library-validation-preference"]&&
              [transaction.state[@"Entries"] count]==1&&
              [transaction.state[@"Entries"][0][@"RelativePath"] isEqual:BootPolicyTarget],@"Saved boot-policy transaction is invalid");
        result[@"TransactionPhase"]=transaction.state[@"Phase"];result[@"TransactionID"]=transaction.state[@"ID"];
    }
    return result;
}
#endif
