#import "VIMDiskImageIO.h"
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

enum { Slots=16 };
typedef struct {uint64_t band,used;int fd;} Band;

@implementation VIMDiskImageIO {
    int _fd,_directory,_lock;
    uint64_t _bandSize,_clock;
    BOOL _writable;
    Band _cache[Slots];
    NSMutableSet<NSNumber *> *_dirty;
}

- (instancetype)initWithURL:(NSURL *)url writable:(BOOL)writable error:(NSError **)error {
    self=[super init];if(!self)return nil;
    _fd=_directory=_lock=-1;
    for(unsigned i=0;i<Slots;i++)_cache[i].fd=-1;
    _writable=writable;
    struct stat info;
    BOOL ok=!lstat(url.fileSystemRepresentation,&info);
    if(ok&&S_ISREG(info.st_mode)) {
        _fd=open(url.fileSystemRepresentation,(writable?O_RDWR:O_RDONLY)|O_NOFOLLOW|O_CLOEXEC);
        ok=_fd>=0&&!fstat(_fd,&info)&&S_ISREG(info.st_mode)&&info.st_size>0;
        if(ok&&writable)ok=info.st_nlink==1&&!flock(_fd,LOCK_EX|LOCK_NB);
        if(ok)_size=(uint64_t)info.st_size;
    } else if(ok&&S_ISDIR(info.st_mode)) {
        NSDictionary *properties=[NSDictionary dictionaryWithContentsOfURL:[url URLByAppendingPathComponent:@"Info.plist"]];
        _size=[properties[@"size"] unsignedLongLongValue];
        _bandSize=[properties[@"band-size"] unsignedLongLongValue];
        _directory=open([url URLByAppendingPathComponent:@"bands"].fileSystemRepresentation,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        ok=[properties[@"diskimage-bundle-type"] isEqual:@"com.apple.diskimage.sparsebundle"]&&
            _size&&_size<=INT64_MAX&&_bandSize&&!(_bandSize%512)&&_bandSize<=INT64_MAX&&_directory>=0;
        if(ok&&writable) {
            _lock=open([url URLByAppendingPathComponent:@"token"].fileSystemRepresentation,O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
            ok=_lock>=0&&!flock(_lock,LOCK_EX|LOCK_NB);
        }
        _dirty=[NSMutableSet set];
    } else ok=NO;
    if(ok)return self;
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.DiskImage" code:errno?:EINVAL userInfo:@{
        NSLocalizedDescriptionKey:@"The detached installation disk could not be opened."}];
    return nil;
}

- (void)dealloc {
    for(unsigned i=0;i<Slots;i++)if(_cache[i].fd>=0)close(_cache[i].fd);
    if(_fd>=0)close(_fd);
    if(_directory>=0)close(_directory);
    if(_lock>=0)close(_lock);
}

- (int)band:(uint64_t)number create:(BOOL)create {
    unsigned oldest=0;
    for(unsigned i=0;i<Slots;i++) {
        if(_cache[i].fd>=0&&_cache[i].band==number){_cache[i].used=++_clock;return _cache[i].fd;}
        if(_cache[i].used<_cache[oldest].used)oldest=i;
    }
    char name[32];snprintf(name,sizeof(name),"%llx",(unsigned long long)number);
    int fd=openat(_directory,name,(_writable?O_RDWR:O_RDONLY)|O_NOFOLLOW|O_CLOEXEC|(create?O_CREAT:0),0600);
    if(fd<0)return -1;
    if(_cache[oldest].fd>=0)close(_cache[oldest].fd);
    _cache[oldest]=(Band){number,++_clock,fd};return fd;
}

- (BOOL)transferAt:(uint64_t)offset bytes:(void *)bytes length:(size_t)length writing:(BOOL)writing {
    if(offset>_size||length>_size-offset||(writing&&!_writable))return NO;
    uint8_t *out=bytes;
    while(length) {
        uint64_t band=_bandSize?offset/_bandSize:0,position=_bandSize?offset%_bandSize:offset;
        size_t take=_bandSize?(size_t)MIN(length,_bandSize-position):length;
        int fd=_bandSize?[self band:band create:writing]:_fd;
        if(fd<0) {
            if(writing||!_bandSize||errno!=ENOENT)return NO;
            memset(out,0,take);
        } else {
            size_t done=0;
            while(done<take) {
                ssize_t n=writing?pwrite(fd,out+done,take-done,(off_t)(position+done)):
                    pread(fd,out+done,take-done,(off_t)(position+done));
                if(n<0&&errno==EINTR)continue;
                if(n<0)return NO;
                if(!n) {
                    if(writing||!_bandSize)return NO;
                    memset(out+done,0,take-done);break;
                }
                done+=(size_t)n;
            }
            if(writing&&_bandSize)[_dirty addObject:@(band)];
        }
        offset+=take;out+=take;length-=take;
    }
    return YES;
}

- (BOOL)readAt:(uint64_t)offset bytes:(void *)bytes length:(size_t)length {
    return [self transferAt:offset bytes:bytes length:length writing:NO];
}
- (BOOL)writeAt:(uint64_t)offset bytes:(const void *)bytes length:(size_t)length {
    return [self transferAt:offset bytes:(void *)bytes length:length writing:YES];
}
- (BOOL)synchronize {
    if(_fd>=0)return !fsync(_fd);
    for(NSNumber *number in _dirty) {
        int fd=[self band:number.unsignedLongLongValue create:NO];
        if(fd<0||fsync(fd))return NO;
    }
    [_dirty removeAllObjects];return YES;
}
@end
