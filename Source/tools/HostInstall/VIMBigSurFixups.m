#import "VIMBigSurFixups.h"
#import "VIMBigSurArchive.h"
#include <AppleArchive/AppleArchive.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

static BOOL Failure(NSError **error,NSInteger code,NSString *message) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurFixups" code:code userInfo:@{NSLocalizedDescriptionKey:message}];
    return NO;
}

static BOOL SafePath(NSString *name) {
    if(!name||name.isAbsolutePath)return NO;
    if(!name.length||[name isEqual:@"."])return YES;
    for(NSString *part in [name componentsSeparatedByString:@"/"])
        if(!part.length||[part isEqual:@"."]||[part isEqual:@".."])return NO;
    return YES;
}

static BOOL AddSealFields(AAArchiveStream archive,AAHeader header,NSString *name,
    NSMutableSet *fastPromote,NSMutableDictionary *rootless,NSMutableDictionary *alternateLinks) {
    if([name isEqual:@"."])name=@"";
    const AAFieldKey internal={.skey="FLI"},attrs={.skey="XAT"};
    uint64_t flags=0,length=0,offset=0;
    if(AAHeaderGetFieldUIntWithKey(header,internal,&flags)<0||
       AAHeaderGetFieldBlobWithKey(header,attrs,&length,&offset)<0||length>16U*1024U*1024U)return NO;
    if(flags&0x20000)[fastPromote addObject:name];
    if(!length)return YES;
    NSMutableData *bytes=[NSMutableData dataWithLength:(NSUInteger)length];
    if(AAArchiveStreamReadBlob(archive,attrs,bytes.mutableBytes,bytes.length)<0)return NO;
    AAEntryXATBlob blob=AAEntryXATBlobCreateWithEncodedData(bytes.bytes,bytes.length);
    if(!blob)return NO;
    BOOL valid=YES;
    for(uint32_t i=0,count=AAEntryXATBlobGetEntryCount(blob);i<count;i++) {
        size_t keyLength=0,size=0;
        if(AAEntryXATBlobGetEntry(blob,i,0,NULL,&keyLength,0,NULL,&size)<0){valid=NO;break;}
        char actual[256];
        if(keyLength>=sizeof(actual))continue;
        if(AAEntryXATBlobGetEntry(blob,i,sizeof(actual),actual,NULL,0,NULL,NULL)<0){valid=NO;break;}
        NSMutableDictionary *destination=!strcmp(actual,"com.apple.rootless")?rootless:
            !strcmp(actual,"com.apple.fs.altlink")?alternateLinks:nil;
        if(!destination)continue;
        NSMutableData *value=[NSMutableData dataWithLength:size];
        if(AAEntryXATBlobGetEntry(blob,i,0,NULL,NULL,size,value.mutableBytes,NULL)<0||
           (destination[name]&&![destination[name] isEqual:value])){valid=NO;break;}
        destination[name]=value;
    }
    AAEntryXATBlobDestroy(blob);
    return valid;
}

static int ParseStream(AAByteStream source,VIMBigSurFixupVisitor visit,
                       NSMutableSet *fastPromote,NSMutableDictionary *rootless,NSMutableDictionary *alternateLinks) {
    if(!source)return EIO;
    AAArchiveStream archive=AADecodeArchiveInputStreamOpen(source,NULL,NULL,0,1);
    AAHeader header=NULL;int problem=archive?0:EBADMSG;NSUInteger entries=0;
    NSMutableDictionary<NSNumber *,NSArray *> *hardLinks=[NSMutableDictionary dictionary];
    const AAFieldKey path={.skey="PAT"},type={.skey="TYP"},uid={.skey="UID"},gid={.skey="GID"},
        mode={.skey="MOD"},flags={.skey="FLG"},compression={.skey="AFT"},resource={.skey="AFR"},
        cluster={.skey="HLC"},ordinal={.skey="HLO"};
    while(!problem) @autoreleasepool {
        int status=AAArchiveStreamReadHeader(archive,&header);
        if(!status)break;
        char name[4096];size_t length;uint64_t t,u=0,g=0,m=0,f=0,kind=0,fork=0,id=0,index=0;
        if(status<0||AAHeaderGetFieldStringWithKey(header,path,sizeof(name),name,&length)!=1||length>=sizeof(name)||strlen(name)!=length||
            AAHeaderGetFieldUIntWithKey(header,type,&t)!=1){problem=EBADMSG;break;}
        mode_t kindBits=t=='D'?S_IFDIR:t=='L'?S_IFLNK:t=='F'||t=='H'?S_IFREG:0;
        NSString *relative=@(name);
        int hasCluster=AAHeaderGetFieldUIntWithKey(header,cluster,&id);
        int hasOrdinal=AAHeaderGetFieldUIntWithKey(header,ordinal,&index);
        if(!kindBits||!SafePath(relative)||hasCluster<0||hasOrdinal<0||(hasOrdinal&&!hasCluster)||
            (hasCluster&&kindBits!=S_IFREG)){problem=EBADMSG;break;}
        int hasUID=AAHeaderGetFieldUIntWithKey(header,uid,&u),hasGID=AAHeaderGetFieldUIntWithKey(header,gid,&g);
        int hasMode=AAHeaderGetFieldUIntWithKey(header,mode,&m),hasFlags=AAHeaderGetFieldUIntWithKey(header,flags,&f);
        if(!hasUID&&!hasGID&&!hasMode&&!hasFlags&&hasOrdinal&&index) {
            // Monterey puts inode metadata on ordinal zero only. Each alias
            // still needs its own path in the detached catalog audit. Seal
            // fields belong to the original inode, not to every alias.
            NSArray *original=hardLinks[@(id)];
            if(!original){problem=EBADMSG;break;}
            VIMBigSurFixup record;
            [original[0] getValue:&record size:sizeof(record)];
            if(visit&&!visit(relative,record,original[1])){problem=ECANCELED;break;}
            entries++;continue;
        }
        int hasCompression=AAHeaderGetFieldUIntWithKey(header,compression,&kind);
        if(hasUID!=1||hasGID!=1||hasMode!=1||hasFlags!=1||u>UINT32_MAX||g>UINT32_MAX||m>07777||f>UINT32_MAX||
           hasCompression<0||kind>UINT32_MAX||
           (hasCompression&&AAHeaderGetFieldUIntWithKey(header,resource,&fork)!=1)){problem=EBADMSG;break;}
        VIMBigSurFixup record={.uid=(uint32_t)u,.gid=(uint32_t)g,.flags=(uint32_t)f,.mode=(uint16_t)(kindBits|m),
            .compressed=hasCompression!=0,.compressionType=(uint32_t)kind,.resourceLength=fork};
        if(hasOrdinal&&!index) {
            NSNumber *key=@(id);
            if(hardLinks[key]){problem=EBADMSG;break;}
            hardLinks[key]=@[[NSValue valueWithBytes:&record objCType:@encode(VIMBigSurFixup)],relative];
        }
        if(fastPromote&&!AddSealFields(archive,header,relative,fastPromote,rootless,alternateLinks)){problem=EBADMSG;break;}
        if(visit&&!visit(relative,record,nil)){problem=ECANCELED;break;}
        entries++;
    }
    if(header)AAHeaderDestroy(header);
    if(archive&&AAArchiveStreamClose(archive)&&!problem)problem=EBADMSG;
    if(AAByteStreamClose(source)&&!problem)problem=EIO;
    return !problem&&!entries?EBADMSG:problem;
}

static BOOL VisitFixups(NSURL *zip,VIMBigSurFixupVisitor visit,NSMutableSet *fastPromote,
    NSMutableDictionary *rootless,NSMutableDictionary *alternateLinks,VIMArchiveProgress progress,void *context,NSError **error) {
    int io[2];
    if(pipe(io))return Failure(error,errno,@"Could not open the Big Sur metadata stream.");
    int input=io[0],output=io[1];
    if(fcntl(output,F_SETNOSIGPIPE,1)<0||fcntl(input,F_SETFD,FD_CLOEXEC)<0||fcntl(output,F_SETFD,FD_CLOEXEC)<0) {
        int problem=errno;close(input);close(output);return Failure(error,problem,@"Could not configure the Big Sur metadata stream.");
    }
    __block int problem=0;
    dispatch_group_t reader=dispatch_group_create();
    dispatch_group_async(reader,dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{ @autoreleasepool {
        problem=ParseStream(AAFileStreamOpenWithFD(input,0),visit,fastPromote,rootless,alternateLinks);close(input);
    } });
    NSError *decodeError=nil;BOOL decoded=NO;
    @try {decoded=VIMBigSurDecodeFixupManifest(zip,output,progress,context,&decodeError);}
    @finally {close(output);dispatch_group_wait(reader,DISPATCH_TIME_FOREVER);}
    if(problem==ECANCELED||decodeError.code==ECANCELED)return Failure(error,ECANCELED,@"Installation cancelled.");
    if(problem)return Failure(error,problem,@"The original Apple installer contains an unreadable metadata row.");
    if(!decoded&&error)*error=decodeError;
    return decoded;
}

BOOL VIMBigSurVisitFixups(NSURL *zip,VIMBigSurFixupVisitor visit,
    VIMArchiveProgress progress,void *context,NSError **error) {
    if(!visit)return Failure(error,EINVAL,@"The Big Sur metadata visitor is missing.");
    return VisitFixups(zip,visit,nil,nil,nil,progress,context,error);
}

static int OpenLinkParent(int root,NSString *path,char relative[4096],char **leaf) {
    const char *bytes=path.UTF8String;
    if(!bytes||!path.length||[path isEqual:@"."]||!SafePath(path)||strlen(bytes)>=4096){errno=EINVAL;return -1;}
    strcpy(relative,bytes);
    int parent=fcntl(root,F_DUPFD_CLOEXEC,0);
    if(parent<0)return -1;
    char *component=relative;
    for(char *slash;(slash=strchr(component,'/'));) {
        *slash='\0';
        int child=openat(parent,component,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        int problem=errno;
        close(parent);parent=child;
        if(parent<0){errno=problem;return -1;}
        component=slash+1;
    }
    *leaf=component;
    return parent;
}

static BOOL StageHardLink(int root,NSString *owner,NSString *alias,BOOL detach) {
    char source[4096],target[4096],*original=NULL,*name=NULL;
    int from=OpenLinkParent(root,owner,source,&original);
    if(from<0)return NO;
    int to=OpenLinkParent(root,alias,target,&name);
    int problem=to<0?errno:0;
    struct stat src,dst,folder;
    BOOL exists=NO;
    if(!problem) {
        if(fstatat(from,original,&src,AT_SYMLINK_NOFOLLOW))problem=errno;
        else if(!S_ISREG(src.st_mode))problem=EINVAL;
        else if(!fstatat(to,name,&dst,AT_SYMLINK_NOFOLLOW)) {
            exists=YES;
            if(src.st_dev!=dst.st_dev||src.st_ino!=dst.st_ino||!S_ISREG(dst.st_mode))problem=EEXIST;
        } else if(errno!=ENOENT)problem=errno;
        if(!problem&&exists==detach) {
            if(fstat(to,&folder)) {
                problem=errno;goto done;
            }
            mode_t mode=folder.st_mode&07777;
            BOOL widened=!(mode&S_IWUSR);
            if(widened&&fchmod(to,mode|S_IWUSR))problem=errno;
            else {
                if(detach?unlinkat(to,name,0):linkat(from,original,to,name,0))problem=errno;
                else {
                    struct timespec times[2]={{.tv_nsec=UTIME_OMIT},folder.st_mtimespec};
                    if(futimens(to,times))problem=errno;
                }
                if(widened&&fchmod(to,mode)&&!problem)problem=errno;
            }
        }
    }
done:
    if(to>=0)close(to);
    close(from);
    if(problem)errno=problem;
    return !problem;
}

BOOL VIMBigSurDetachFixupHardLink(int root,NSString *owner,NSString *alias) {
    return StageHardLink(root,owner,alias,YES);
}

BOOL VIMBigSurStageFixupHardLink(int root,NSString *owner,NSString *alias) {
    if(StageHardLink(root,owner,alias,NO))return YES;
    int problem=errno;
    if(problem!=EPERM||!SafePath(owner)||!SafePath(alias))return NO;

    // macOS permits a link into an app bundle from a neutral source but
    // can deny linking from an app to another tree. Monterey's signed
    // metadata shares such an inode between apps and System frameworks.
    // On the unpublished disk only, briefly give the enclosing app a
    // neutral name. Preserve the original inode, contents and all links.
    NSArray<NSString *> *parts=[owner componentsSeparatedByString:@"/"];
    NSUInteger depth=0;
    while(depth+1<parts.count&&![parts[depth].pathExtension.lowercaseString isEqual:@"app"])depth++;
    if(depth+1>=parts.count)return NO;
    NSString *bundle=[[parts subarrayWithRange:NSMakeRange(0,depth+1)] componentsJoinedByString:@"/"];
    if([alias hasPrefix:[bundle stringByAppendingString:@"/"]])return NO;
    char relative[4096],*leaf=NULL;
    int parent=OpenLinkParent(root,bundle,relative,&leaf);
    if(parent<0)return NO;
    struct stat folder,app;
    if(fstat(parent,&folder)||fstatat(parent,leaf,&app,AT_SYMLINK_NOFOLLOW)||!S_ISDIR(app.st_mode)) {
        close(parent);errno=problem;return NO;
    }
    NSString *temporary=[@".vim-stage-link-" stringByAppendingString:NSUUID.UUID.UUIDString];
    mode_t mode=folder.st_mode&07777;
    BOOL widened=!(mode&S_IWUSR),renamed=NO,linked=NO;
    if(!widened||!fchmod(parent,mode|S_IWUSR)) {
        renamed=!renameatx_np(parent,leaf,parent,temporary.fileSystemRepresentation,RENAME_EXCL);
        if(renamed) {
            NSMutableArray<NSString *> *neutral=[parts mutableCopy];neutral[depth]=temporary;
            linked=StageHardLink(root,[neutral componentsJoinedByString:@"/"],alias,NO);
        }
    }
    int outcome=linked?0:errno;
    if(renamed) {
        if(renameatx_np(parent,temporary.fileSystemRepresentation,parent,leaf,RENAME_EXCL))outcome=errno;
        struct timespec times[2]={{.tv_nsec=UTIME_OMIT},folder.st_mtimespec};
        if(futimens(parent,times)&&!outcome)outcome=errno;
    }
    if(widened&&fchmod(parent,mode)&&!outcome)outcome=errno;
    close(parent);
    if(outcome)errno=outcome;
    return !outcome;
}

typedef struct {const uint8_t *bytes;size_t remaining;} PolicyInput;
static ssize_t ReadPolicy(void *opaque,void *out,size_t size) {
    PolicyInput *source=opaque;size_t count=MIN(source->remaining,size);
    memcpy(out,source->bytes,count);source->bytes+=count;source->remaining-=count;return (ssize_t)count;
}
static int ClosePolicy(__unused void *opaque) {return 0;}

NSDictionary *VIMBigSurReadSealPolicy(NSURL *zip,NSError **error) {
    NSMutableSet *fastPromote=[NSMutableSet set];NSMutableDictionary *rootless=[NSMutableDictionary dictionary];
    NSMutableDictionary *alternateLinks=[NSMutableDictionary dictionary];
    if(!VisitFixups(zip,nil,fastPromote,rootless,alternateLinks,NULL,NULL,error))return nil;
    NSData *payload=VIMBigSurReadSmallArchiveMember(zip,@"AssetData/payloadv2/firmlinks_payload",error);
    if(!payload)return nil;
    PolicyInput input={payload.bytes,payload.length};
    AAByteStream source=AACustomByteStreamOpen();
    if(source) {
        AACustomByteStreamSetData(source,&input);AACustomByteStreamSetReadProc(source,ReadPolicy);
        AACustomByteStreamSetCloseProc(source,ClosePolicy);
    }
    if(ParseStream(source,nil,fastPromote,rootless,alternateLinks)) {
        Failure(error,EBADMSG,@"Could not read Big Sur’s original firmlink sealing attributes.");return nil;
    }
    return @{@"FastPromote":fastPromote,@"Rootless":rootless,@"AlternateLinks":alternateLinks};
}

BOOL VIMBigSurStageDataProtection(NSURL *zip,NSURL *data,NSError **error) {
    NSDictionary *policy=VIMBigSurReadSealPolicy(zip,error);
    if(!policy)return NO;
    NSDictionary *rootless=policy[@"Rootless"];
    NSString *prefix=@"System/Library/Templates/Data/";
    for(NSString *path in rootless) {
        if(![path hasPrefix:prefix])continue;
        NSString *relative=[path substringFromIndex:prefix.length];
        NSURL *file=[data URLByAppendingPathComponent:relative];NSData *value=rootless[path];
        struct stat info;
        int problem=0;
        if(!relative.length||!SafePath(relative))problem=EINVAL;
        else if(lstat(file.fileSystemRepresentation,&info))problem=errno;
        if(problem)return Failure(error,problem,[NSString stringWithFormat:
            @"Could not open the original Data protection item %@: %s.",relative,strerror(problem)]);
        // The copied template retains read-only files and directories. xattrs
        // need write access even for their owner; change only this private
        // copy's mode, never follow a final symlink, and restore it on failure.
        mode_t mode=info.st_mode&07777;
        BOOL widened=(S_ISREG(info.st_mode)||S_ISDIR(info.st_mode))&&!(mode&S_IWUSR);
        if(widened&&chmod(file.fileSystemRepresentation,mode|S_IWUSR))problem=errno;
        else {
            // Some Data files (notably com.apple.Boot.plist) already have
            // their native label. Do not make a second B-tree key which
            // would collide when the detached finalizer renames our slot.
            ssize_t size=getxattr(file.fileSystemRepresentation,"com.apple.rootless",NULL,0,0,XATTR_NOFOLLOW);
            if(size>=0) {
                NSMutableData *native=[NSMutableData dataWithLength:(NSUInteger)size];
                if(getxattr(file.fileSystemRepresentation,"com.apple.rootless",native.mutableBytes,native.length,0,XATTR_NOFOLLOW)!=size)
                    problem=errno?:EIO;
                else if(![native isEqual:value])problem=EINVAL;
                else if(removexattr(file.fileSystemRepresentation,"com.apple.rootlesr",XATTR_NOFOLLOW)&&errno!=ENOATTR)
                    problem=errno;
            } else if(errno!=ENOATTR)problem=errno;
            else if(setxattr(file.fileSystemRepresentation,"com.apple.rootlesr",value.bytes,value.length,0,XATTR_NOFOLLOW))problem=errno;
            if(widened&&chmod(file.fileSystemRepresentation,mode)&&!problem)problem=errno;
        }
        if(problem)return Failure(error,problem,[NSString stringWithFormat:
            @"Could not retain the original Data protection attribute for %@: %s.",relative,strerror(problem)]);
    }
    return YES;
}
