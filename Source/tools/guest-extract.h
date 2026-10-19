// Restricted XZ/gzip ustar reader for package-app-guest-payloads.py output only.
// No PAX, GNU extensions, devices, hard links, ACLs or protected flags.
#include "guest-archive-stream.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

static uint64_t ArchiveOctal(const uint8_t *field,NSUInteger length) {
    uint64_t value=0;BOOL ended=NO,started=NO;
    for(NSUInteger i=0;i<length;i++) {
        uint8_t c=field[i];
        if(c==0||c==' '){if(started||c==0)ended=YES;continue;}
        Check(!ended&&c>='0'&&c<='7'&&value<=(UINT64_MAX-7)/8,@"Invalid archive numeric field");
        started=YES;value=value*8+c-'0';
    }return value;
}
static NSString *ArchiveString(const uint8_t *field,NSUInteger length) {
    NSUInteger n=0;while(n<length&&field[n])n++;
    NSString *s=[[NSString alloc] initWithBytes:field length:n encoding:NSUTF8StringEncoding];
    Check(s!=nil&&[s rangeOfCharacterFromSet:NSCharacterSet.controlCharacterSet].location==NSNotFound,@"Invalid archive path encoding");return s;
}
static NSArray<NSString *> *ArchiveParts(NSString *name) {
    Check(!name.isAbsolutePath,@"Absolute archive path");NSMutableArray *parts=[NSMutableArray array];
    for(NSString *part in [name componentsSeparatedByString:@"/"]) {
        if(!part.length)continue;
        Check(![part isEqual:@".."]&&![part isEqual:@"."]&&part.length<=255,@"Unsafe archive path");[parts addObject:part];
    }
    Check(parts.count>0&&[parts[0] isEqual:@"payload"],@"Archive entry outside payload root");return parts;
}
static int ArchiveParent(int root,NSArray<NSString *> *parts) {
    int current=dup(root);Check(current>=0,@"Cannot duplicate staging descriptor");
    @try {
        for(NSUInteger i=0;i+1<parts.count;i++) {
            const char *component=parts[i].fileSystemRepresentation;
            Check(mkdirat(current,component,0755)==0||errno==EEXIST,@"Cannot create payload directory");
            int next=openat(current,component,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            Check(next>=0,@"Payload parent is not a real directory");close(current);current=next;
        }return current;
    } @catch(NSException *error){close(current);@throw;}
}
static NSDictionary *ExtractPayload(NSString *archive,NSString *output) {
    Check(archive.isAbsolutePath&&output.isAbsolutePath,@"Extraction paths must be absolute");
    NSString *parent=output.stringByDeletingLastPathComponent.stringByResolvingSymlinksInPath;
    NSString *leaf=output.lastPathComponent;
    Check(leaf.length&&![leaf isEqual:@"."]&&![leaf isEqual:@".."],@"Invalid extraction directory");
    int parentFD=open(parent.fileSystemRepresentation,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    Check(parentFD>=0,@"Extraction parent is unavailable");
    int created=mkdirat(parentFD,leaf.fileSystemRepresentation,0700);
    if(created){close(parentFD);Check(NO,@"Extraction directory must not already exist");}
    int root=openat(parentFD,leaf.fileSystemRepresentation,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);close(parentFD);
    Check(root>=0,@"Cannot open extraction directory");
    VIMArchiveStream stream={0};VIMArchiveStream *file=&stream;
    uint64_t total=0;NSUInteger count=0;NSMutableSet *seen=[NSMutableSet set];BOOL payloadDirectory=NO;
    @try {
        ArchiveOpen(file,archive);
        for(;;) {
            uint8_t header[512];ArchiveRead(file,header,sizeof(header));BOOL zero=YES;
            for(NSUInteger i=0;i<sizeof(header);i++)zero&=header[i]==0;
            if(zero) {
                uint8_t second[512];ArchiveRead(file,second,sizeof(second));
                for(NSUInteger i=0;i<sizeof(second);i++)Check(second[i]==0,@"Invalid archive terminator");
                // Drain through the checked compression footer, not just the
                // tar terminator. Retain the existing bounded zero padding.
                NSUInteger padding=0;unsigned n;uint8_t tail[512];
                while((n=ArchiveReadSome(file,tail,sizeof(tail)))>0){padding+=n;Check(padding<=1024*1024,@"Excessive archive padding");for(unsigned i=0;i<n;i++)Check(tail[i]==0,@"Unexpected trailing archive data");}
                break;
            }
            Check(++count<=10000,@"Too many payload entries");
            Check(!memcmp(header+257,"ustar\0",6)&&!memcmp(header+263,"00",2),@"Unsupported archive format");
            uint64_t checksum=0;for(NSUInteger i=0;i<512;i++)checksum+=(i>=148&&i<156)?' ':header[i];
            Check(checksum==ArchiveOctal(header+148,8),@"Archive header checksum mismatch");
            NSString *name=ArchiveString(header,100),*prefix=ArchiveString(header+345,155);
            if(prefix.length)name=[prefix stringByAppendingFormat:@"/%@",name];
            NSArray *parts=ArchiveParts(name);NSString *key=[parts componentsJoinedByString:@"/"];
            Check(![seen containsObject:key],@"Duplicate payload entry");[seen addObject:key];
            uint64_t size=ArchiveOctal(header+124,12),mode=ArchiveOctal(header+100,8);
            uint8_t type=header[156];Check(type==0||type=='0'||type=='5'||type=='2',@"Unsupported archive entry type");
            Check(size<=512ULL*1024*1024&&total+size<=4ULL*1024*1024*1024,@"Payload exceeds extraction limit");total+=size;
            Check((type==0||type=='0')||size==0,@"Non-file archive entry has data");
            if(parts.count==1){Check(type=='5',@"Payload root must be a directory");payloadDirectory=YES;}
            int dir=ArchiveParent(root,parts);const char *last=[parts.lastObject fileSystemRepresentation];int fd=-1;
            @try {
                if(type=='5') {
                    Check(mkdirat(dir,last,0755)==0||errno==EEXIST,@"Cannot create archive directory");
                    fd=openat(dir,last,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);Check(fd>=0,@"Archive directory is unsafe");
                } else if(type=='2') {
                    NSString *target=ArchiveString(header+157,100);Check(target.length&&!target.isAbsolutePath,@"Unsafe archive symlink");
                    NSMutableArray *resolved=[[parts subarrayWithRange:NSMakeRange(0,parts.count-1)] mutableCopy];
                    for(NSString *part in [target componentsSeparatedByString:@"/"]) {
                        if(!part.length||[part isEqual:@"."])continue;
                        if([part isEqual:@".."]){Check(resolved.count>1,@"Archive symlink escapes payload");[resolved removeLastObject];}
                        else [resolved addObject:part];
                    }
                    Check(symlinkat(target.fileSystemRepresentation,dir,last)==0,@"Cannot create archive symlink");
                } else {
                    fd=openat(dir,last,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,(mode_t)(mode&0755));
                    Check(fd>=0,@"Cannot create new payload file");
                    uint64_t left=size;uint8_t bytes[65536];
                    while(left) {
                        unsigned chunk=(unsigned)MIN(left,sizeof(bytes));ArchiveRead(file,bytes,chunk);
                        unsigned offset=0;while(offset<chunk){ssize_t n=write(fd,bytes+offset,chunk-offset);if(n<0&&errno==EINTR)continue;Check(n>0,@"Payload write failed");offset+=n;}left-=chunk;
                    }
                    Check(fsync(fd)==0,@"Cannot flush payload file");
                    NSUInteger padding=(512-size%512)%512;
                    if(padding){ArchiveRead(file,bytes,(unsigned)padding);for(NSUInteger i=0;i<padding;i++)Check(bytes[i]==0,@"Nonzero archive file padding");}
                }
            } @finally {if(fd>=0)close(fd);close(dir);}
        }
        Check(payloadDirectory,@"Archive is missing its payload directory");
    } @finally {ArchiveClose(file);close(root);}
    return @{@"FormatVersion":@1,@"Extracted":@YES,@"Installed":@NO,@"EntryCount":@(count),@"UnpackedBytes":@(total),
             @"PayloadDirectory":[[parent stringByAppendingPathComponent:leaf] stringByAppendingPathComponent:@"payload"]};
}
