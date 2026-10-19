#import "VIMFileTransaction.h"
#include <CommonCrypto/CommonDigest.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>

// Compiled only into the isolated fixture, never into a shipped guest tool.
static void InterruptionPoint(void) {
#ifdef VIM_TRANSACTION_TESTING
    static unsigned point;
    const char *requested=getenv("VIM_TRANSACTION_INTERRUPT");
    if(requested&&++point==strtoul(requested,NULL,10))_exit(77);
#endif
}

static void Require(BOOL ok,NSString *message){if(!ok)@throw [NSException exceptionWithName:@"FileTransaction" reason:message userInfo:nil];}
static BOOL Exists(NSString *path){struct stat s;return lstat(path.fileSystemRepresentation,&s)==0;}
static NSString *FileHash(NSString *path) {
    int fd=open(path.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW);Require(fd>=0,@"Cannot read transaction input");
    CC_SHA256_CTX context;CC_SHA256_Init(&context);uint8_t buffer[65536],digest[CC_SHA256_DIGEST_LENGTH];
    for(;;){ssize_t n=read(fd,buffer,sizeof(buffer));if(n<0&&errno==EINTR)continue;if(n<=0){close(fd);Require(n==0,@"Transaction hash read failed");break;}CC_SHA256_Update(&context,buffer,(CC_LONG)n);}
    CC_SHA256_Final(digest,&context);NSMutableString *hash=[NSMutableString string];for(NSUInteger i=0;i<sizeof(digest);i++)[hash appendFormat:@"%02x",digest[i]];return hash;
}
static NSDictionary *Fingerprint(NSString *path) {
    NSFileManager *fm=NSFileManager.defaultManager;struct stat s;
    Require(lstat(path.fileSystemRepresentation,&s)==0,@"Transaction image disappeared");
    if(S_ISREG(s.st_mode))return @{@"Type":@"file",@"SHA256":FileHash(path),@"Mode":@(s.st_mode&0777)};
    if(S_ISLNK(s.st_mode)){NSString *target=[fm destinationOfSymbolicLinkAtPath:path error:NULL];Require(target!=nil,@"Cannot read transaction symlink");return @{@"Type":@"link",@"Target":target};}
    Require(S_ISDIR(s.st_mode),@"Unsupported transaction file type");
    NSArray *names=[fm contentsOfDirectoryAtPath:path error:NULL];Require(names!=nil,@"Cannot inspect transaction directory");
    NSMutableDictionary *children=[NSMutableDictionary dictionary];
    for(NSString *name in names)children[name]=Fingerprint([path stringByAppendingPathComponent:name]);
    return @{@"Type":@"directory",@"Mode":@(s.st_mode&0777),@"Children":children};
}
static void Same(NSString *path,NSDictionary *expected){Require(Exists(path)&&[Fingerprint(path) isEqual:expected],[@"Transaction file changed: " stringByAppendingString:path]);}
static void Flush(NSString *path,BOOL full) {
    int fd=open(path.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    Require(fd>=0,@"Cannot open transaction data for flushing");
    int result=fsync(fd);
    if(result==0&&full)result=fcntl(fd,F_FULLFSYNC);
    close(fd);Require(result==0,[@"Cannot durably flush " stringByAppendingString:path]);
}
static void FlushTree(NSString *path) {
    struct stat st;Require(lstat(path.fileSystemRepresentation,&st)==0,@"Staged tree disappeared");
    if(S_ISLNK(st.st_mode))return; // Its containing directory persists the link.
    if(S_ISDIR(st.st_mode)) {
        NSArray *names=[NSFileManager.defaultManager contentsOfDirectoryAtPath:path error:NULL];
        Require(names!=nil,@"Cannot flush staged directory");
        for(NSString *name in names)FlushTree([path stringByAppendingPathComponent:name]);
    } else Require(S_ISREG(st.st_mode),@"Unsupported staged file");
    Flush(path,S_ISREG(st.st_mode));
}
static void MakeDirectory(NSString *path){NSError *error=nil;Require([NSFileManager.defaultManager createDirectoryAtPath:path withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:&error],error.localizedDescription);Flush(path,NO);Flush(path.stringByDeletingLastPathComponent,NO);}
static void Move(NSString *from,NSString *to){Require(!Exists(to),@"Refusing to overwrite retained transaction files");Require(rename(from.fileSystemRepresentation,to.fileSystemRepresentation)==0,@"Same-volume transaction rename failed");Flush(to.stringByDeletingLastPathComponent,NO);Flush(from.stringByDeletingLastPathComponent,NO);InterruptionPoint();}
static BOOL Fail(NSException *exception,NSError **error){if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.FileTransaction" code:1 userInfo:@{NSLocalizedDescriptionKey:exception.reason?:@"Transaction failed"}];return NO;}
static void ValidateRecord(NSDictionary *record) {
    Require([record isKindOfClass:NSDictionary.class]&&[record[@"FormatVersion"] isEqual:@1]&&[record[@"Ready"] isEqual:@NO],@"Invalid transaction journal");
    NSString *root=record[@"Root"],*payload=record[@"Payload"],*identity=record[@"ID"];
    for(id path in @[root?:@0,payload?:@0]) Require([path isKindOfClass:NSString.class]&&[path isAbsolutePath]&&[path isEqual:[path stringByResolvingSymlinksInPath]],@"Transaction root changed or is invalid");
    Require([identity isKindOfClass:NSString.class]&&[[NSUUID alloc] initWithUUIDString:identity]!=nil,@"Invalid transaction identity");
    Require([@[@"planned",@"publishing",@"files-published",@"rolling-back",@"rolled-back"] containsObject:record[@"Phase"]],@"Invalid transaction phase");
    NSArray *entries=record[@"Entries"];
    Require([entries isKindOfClass:NSArray.class]&&entries.count>0&&entries.count<=128,@"Invalid transaction entries");
    NSMutableSet *seen=[NSMutableSet set];NSUInteger index=0;
    for(NSDictionary *entry in entries) {
        Require([entry isKindOfClass:NSDictionary.class],@"Invalid transaction entry");
        NSString *relative=entry[@"RelativePath"];
        Require([relative isKindOfClass:NSString.class]&&relative.length&&!relative.isAbsolutePath&&![relative.pathComponents containsObject:@".."]&&![relative.pathComponents containsObject:@"."]&&[relative isEqual:relative.stringByStandardizingPath],@"Invalid relative target");
        for(NSString *other in seen) Require(![other isEqual:relative]&&![other hasPrefix:[relative stringByAppendingString:@"/"]]&&![relative hasPrefix:[other stringByAppendingString:@"/"]],@"Overlapping targets");
        [seen addObject:relative];
        NSString *target=[root stringByAppendingPathComponent:relative],*source=[payload stringByAppendingPathComponent:relative];
        NSString *parent=target.stringByDeletingLastPathComponent.stringByResolvingSymlinksInPath;
        Require([root isEqual:@"/"]||[parent isEqual:root]||[parent hasPrefix:[root stringByAppendingString:@"/"]],@"Target parent escapes root");
        NSString *holding=[parent stringByAppendingPathComponent:[NSString stringWithFormat:@".virtualintelmac-%@-%lu",identity,(unsigned long)index++]];
        Require([entry[@"Target"] isEqual:target]&&[entry[@"Source"] isEqual:source]&&[entry[@"Holding"] isEqual:holding],@"Journal paths do not match target identity");
        struct stat st;
        if(lstat(holding.fileSystemRepresentation,&st)==0) Require(S_ISDIR(st.st_mode),@"Holding path is not a real directory");
        Require([entry[@"OriginalPresent"] isKindOfClass:NSNumber.class]&&[entry[@"Original"] isKindOfClass:NSDictionary.class]&&[entry[@"Replacement"] isKindOfClass:NSDictionary.class],@"Invalid transaction fingerprints");
        Require([@[@"planned",@"staging",@"staged",@"publishing",@"published",@"rolling-back",@"rolled-back"] containsObject:entry[@"Phase"]],@"Invalid entry phase");
    }
}
@interface VIMFileTransaction ()
@property NSURL *journal;
@property NSMutableDictionary *record;
@end
@implementation VIMFileTransaction
- (int)lockOperation {
    NSString *path=[self.journal.path stringByAppendingString:@".lock"];
    int fd=open(path.fileSystemRepresentation,O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
    Require(fd>=0,@"Cannot open transaction lock");
    if(flock(fd,LOCK_EX|LOCK_NB)!=0){close(fd);Require(NO,@"Transaction is already in use");}
    NSDictionary *disk=[NSDictionary dictionaryWithContentsOfURL:self.journal];
    if(![disk isEqual:self.record]){close(fd);Require(NO,@"Transaction changed; reload before continuing");}
    return fd;
}
- (NSDictionary *)state{return [self.record copy];}
- (void)save {
    self.record[@"UpdatedAt"]=NSDate.date;NSError *error=nil;
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:self.record format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
    Require(data&&[data writeToURL:self.journal options:NSDataWritingAtomic error:&error],error.localizedDescription?:@"Cannot save transaction journal");
    Flush(self.journal.path,YES);
    Flush(self.journal.URLByDeletingLastPathComponent.path,NO);
    InterruptionPoint();
}
+ (instancetype)createAtURL:(NSURL *)journal root:(NSURL *)root payload:(NSURL *)payload targets:(NSArray<NSString *> *)targets metadata:(NSDictionary *)metadata error:(NSError **)error {
    int creationLock=-1;
    @try {
        Require(journal.isFileURL,@"Journal must be a local file");
        MakeDirectory(journal.URLByDeletingLastPathComponent.path);
        creationLock=open([journal.path stringByAppendingString:@".lock"].fileSystemRepresentation,O_CREAT|O_RDWR|O_NOFOLLOW|O_CLOEXEC,0600);
        Require(creationLock>=0&&flock(creationLock,LOCK_EX|LOCK_NB)==0,@"Transaction creation is already in use");
        Require(!Exists(journal.path),@"Transaction journal already exists");
        NSString *destination=root.URLByResolvingSymlinksInPath.path,*source=payload.URLByResolvingSymlinksInPath.path;
        Require(destination.isAbsolutePath&&source.isAbsolutePath&&![destination isEqual:source],@"Invalid transaction roots");
        NSString *identity=NSUUID.UUID.UUIDString;NSMutableArray *entries=[NSMutableArray array];NSMutableSet *paths=[NSMutableSet set];
        Require(targets.count>0&&targets.count<=128,@"Invalid transaction target count");
        for(NSString *relative in targets) {
            Require([relative isKindOfClass:NSString.class]&&!relative.isAbsolutePath&&relative.length&&
                ![relative.pathComponents containsObject:@".."]&&![relative.pathComponents containsObject:@"."]&&
                [relative isEqual:relative.stringByStandardizingPath],@"Invalid transaction target");
            for(NSString *existing in paths)Require(![relative isEqual:existing]&&![relative hasPrefix:[existing stringByAppendingString:@"/"]]&&![existing hasPrefix:[relative stringByAppendingString:@"/"]],@"Overlapping transaction targets");
            [paths addObject:relative];NSString *target=[destination stringByAppendingPathComponent:relative],*input=[source stringByAppendingPathComponent:relative];
            NSString *parent=target.stringByDeletingLastPathComponent.stringByResolvingSymlinksInPath;
            Require([destination isEqual:@"/"]||[parent isEqual:destination]||[parent hasPrefix:[destination stringByAppendingString:@"/"]],@"Target parent escapes root");
            Require([input.stringByResolvingSymlinksInPath hasPrefix:[source stringByAppendingString:@"/"]],@"Payload input escapes root");
            BOOL present=Exists(target);
            NSString *holding=[parent stringByAppendingPathComponent:[NSString stringWithFormat:@".virtualintelmac-%@-%lu",identity,(unsigned long)entries.count]];
            Require(!Exists(holding),@"Transaction holding directory already exists");
            [entries addObject:[@{@"RelativePath":relative,@"Target":target,@"Source":input,@"Holding":holding,
                @"OriginalPresent":@(present),@"Original":present?Fingerprint(target):@{},@"Replacement":Fingerprint(input),@"Phase":@"planned"} mutableCopy]];
        }
        VIMFileTransaction *transaction=[self new];transaction.journal=journal;
        transaction.record=[@{@"FormatVersion":@1,@"ID":identity,@"Root":destination,@"Payload":source,@"Metadata":metadata,
                              @"Entries":entries,@"Phase":@"planned",@"Ready":@NO} mutableCopy];
        MakeDirectory(journal.URLByDeletingLastPathComponent.path);[transaction save];return transaction;
    } @catch(NSException *exception){Fail(exception,error);return nil;}
    @finally {if(creationLock>=0)close(creationLock);}
}
+ (instancetype)loadURL:(NSURL *)journal error:(NSError **)error {
    NSData *data=[NSData dataWithContentsOfURL:journal options:0 error:error];if(!data)return nil;
    id record=[NSPropertyListSerialization propertyListWithData:data options:NSPropertyListMutableContainersAndLeaves format:NULL error:error];
    if(![record isKindOfClass:NSMutableDictionary.class]||![record[@"FormatVersion"] isEqual:@1]||![record[@"Entries"] isKindOfClass:NSMutableArray.class]) {
        if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.FileTransaction" code:1 userInfo:@{NSLocalizedDescriptionKey:@"Invalid transaction journal"}];return nil;
    }
    @try { ValidateRecord(record); }
    @catch(NSException *exception){Fail(exception,error);return nil;}
    VIMFileTransaction *transaction=[self new];transaction.journal=journal;transaction.record=record;return transaction;
}
- (BOOL)verifyPublished:(NSError **)error {
    int lock=-1;
    @try {
        lock=[self lockOperation];ValidateRecord(self.record);
        Require([self.record[@"Phase"] isEqual:@"files-published"],@"Transaction has not completed publication");
        for(NSDictionary *entry in self.record[@"Entries"])Same(entry[@"Target"],entry[@"Replacement"]);
        return YES;
    } @catch(NSException *exception){Fail(exception,error);return NO;}
    @finally {if(lock>=0)close(lock);}
}
- (BOOL)publish:(NSError **)error {
    int lock=-1;
    @try {
        lock=[self lockOperation];
        ValidateRecord(self.record);
        Require(![@[@"rolling-back",@"rolled-back"] containsObject:self.record[@"Phase"]],@"A rollback cannot be republished");
        // Copy and validate all replacement trees before changing any original.
        for(NSMutableDictionary *entry in self.record[@"Entries"]) {
            if([@[@"staged",@"publishing",@"published"] containsObject:entry[@"Phase"]])continue;
            NSString *holding=entry[@"Holding"],*new=[holding stringByAppendingPathComponent:@"new"];
            MakeDirectory(holding);entry[@"Phase"]=@"staging";[self save];
            if(Exists(new))Move(new,[holding stringByAppendingPathComponent:[@"incomplete-" stringByAppendingString:NSUUID.UUID.UUIDString]]);
            Same(entry[@"Source"],entry[@"Replacement"]);NSError *copyError=nil;
            Require([NSFileManager.defaultManager copyItemAtPath:entry[@"Source"] toPath:new error:&copyError],copyError.localizedDescription);
            Same(new,entry[@"Replacement"]);FlushTree(new);Flush(holding,NO);entry[@"Phase"]=@"staged";[self save];
        }
        self.record[@"Phase"]=@"publishing";[self save];
        for(NSMutableDictionary *entry in self.record[@"Entries"]) {
            NSString *target=entry[@"Target"],*holding=entry[@"Holding"],*new=[holding stringByAppendingPathComponent:@"new"],*original=[holding stringByAppendingPathComponent:@"original"];
            if([entry[@"Phase"] isEqual:@"published"]){Same(target,entry[@"Replacement"]);continue;}
            entry[@"Phase"]=@"publishing";[self save];BOOL present=[entry[@"OriginalPresent"] boolValue];
            if(Exists(new)) {
                Same(new,entry[@"Replacement"]);
                if(present&&!Exists(original)){Same(target,entry[@"Original"]);Move(target,original);}
                if(present)Same(original,entry[@"Original"]);
                Require(!Exists(target),@"Unexpected target during publication");Move(new,target);
            }
            Same(target,entry[@"Replacement"]);
            if(present)Same(original,entry[@"Original"]);
            entry[@"Phase"]=@"published";[self save];
        }
        self.record[@"Phase"]=@"files-published";[self save];return YES;
    } @catch(NSException *exception){return Fail(exception,error);}
    @finally {if(lock>=0)close(lock);}
}
- (BOOL)rollback:(NSError **)error {
    int lock=-1;
    @try {
        lock=[self lockOperation];
        ValidateRecord(self.record);
        self.record[@"Phase"]=@"rolling-back";[self save];
        for(NSMutableDictionary *entry in [self.record[@"Entries"] reverseObjectEnumerator]) {
            NSString *target=entry[@"Target"],*holding=entry[@"Holding"],*original=[holding stringByAppendingPathComponent:@"original"],*failed=[holding stringByAppendingPathComponent:@"withdrawn"];
            BOOL present=[entry[@"OriginalPresent"] boolValue];
            entry[@"Phase"]=@"rolling-back";[self save];
            if(Exists(original)) {
                Same(original,entry[@"Original"]);
                if(Exists(target)){Same(target,entry[@"Replacement"]);Move(target,failed);}
                Move(original,target);
            } else if(!present&&Exists(target)) {
                Same(target,entry[@"Replacement"]);Move(target,failed);
            }
            if(present)Same(target,entry[@"Original"]);else Require(!Exists(target),@"Unexpected restored target");
            entry[@"Phase"]=@"rolled-back";[self save];
        }
        self.record[@"Phase"]=@"rolled-back";[self save];return YES;
    } @catch(NSException *exception){return Fail(exception,error);}
    @finally {if(lock>=0)close(lock);}
}
@end
