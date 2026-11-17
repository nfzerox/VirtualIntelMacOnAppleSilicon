#import "VIMApplePeriodUpdate.h"
#import <CommonCrypto/CommonDigest.h>
#import <Security/Security.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Public libarchive API; the macOS SDK provides its library but not headers.
struct archive;
struct archive_entry;
extern struct archive *archive_read_new(void);
extern int archive_read_support_format_tar(struct archive *);
extern int archive_read_open_fd(struct archive *,int,size_t);
extern int archive_read_next_header(struct archive *,struct archive_entry **);
extern ssize_t archive_read_data(struct archive *,void *,size_t);
extern int archive_read_free(struct archive *);
extern const char *archive_entry_pathname(struct archive_entry *);
extern const char *archive_entry_hardlink(struct archive_entry *);
extern unsigned int archive_entry_filetype(struct archive_entry *);
extern int64_t archive_entry_size(struct archive_entry *);

static BOOL Fail(NSError **error,int code,NSString *message) {
    if(error)*error=[NSError errorWithDomain:NSPOSIXErrorDomain code:code userInfo:@{NSLocalizedDescriptionKey:message}];
    return NO;
}

static int HexDigit(uint8_t c) {
    if(c>='0'&&c<='9')return c-'0';
    c|=0x20;return c>='a'&&c<='f'?c-'a'+10:-1;
}

static NSData *Signature(NSData *member,NSString *package) {
    NSData *prefix=[[package stringByAppendingString:@"\n"] dataUsingEncoding:NSUTF8StringEncoding];
    const uint8_t *bytes=member.bytes;
    NSUInteger n=prefix.length,expected=n+512;
    if((member.length!=expected&&(member.length!=expected+1||bytes[expected]!='\n'))||
       memcmp(bytes,prefix.bytes,n))return nil;
    uint8_t signature[256];
    for(NSUInteger i=0;i<sizeof(signature);i++) {
        int hi=HexDigit(bytes[n+2*i]),lo=HexDigit(bytes[n+2*i+1]);
        if(hi<0||lo<0)return nil;
        signature[i]=(uint8_t)((hi<<4)|lo);
    }
    return [NSData dataWithBytes:signature length:sizeof(signature)];
}

BOOL VIMStageApplePeriodUpdate(NSURL *source,NSString *packageMember,NSData *publicKey,
    uint64_t maxPackageBytes,NSURL *destination,BOOL (^cancelled)(void),NSError **error) {
    if(!source.isFileURL||!destination.isFileURL||![packageMember hasSuffix:@".pkg.tar"]||
       ![packageMember isEqual:packageMember.lastPathComponent]||packageMember.length>128||
       !publicKey.length||!maxPackageBytes)
        return Fail(error,EINVAL,@"Invalid Apple update recipe.");
    SecKeyRef key=SecKeyCreateWithData((__bridge CFDataRef)publicKey,(__bridge CFDictionaryRef)@{
        (__bridge id)kSecAttrKeyType:(__bridge id)kSecAttrKeyTypeRSA,
        (__bridge id)kSecAttrKeyClass:(__bridge id)kSecAttrKeyClassPublic},NULL);
    if(!key||SecKeyGetBlockSize(key)!=256) {
        if(key)CFRelease(key);
        return Fail(error,EINVAL,@"Could not read Tiger’s original Apple update key.");
    }
    int input=-1,output=-1;BOOL seenPackage=NO;struct archive *reader=NULL;
    NSData *signature=nil;uint8_t buffer[64*1024],digest[CC_SHA1_DIGEST_LENGTH];
    @try {
        struct stat info;
        input=open(source.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
        if(input<0||fstat(input,&info)||!S_ISREG(info.st_mode))return Fail(error,EINVAL,@"Could not open the Apple update download.");
        reader=archive_read_new();
        if(!reader||archive_read_support_format_tar(reader)||archive_read_open_fd(reader,input,sizeof(buffer)))
            return Fail(error,EINVAL,@"Could not read the Apple update archive.");
        for(;;) {
            struct archive_entry *entry=NULL;
            int status=archive_read_next_header(reader,&entry);
            if(status==1)break;
            if(cancelled&&cancelled())return Fail(error,ECANCELED,@"Installation cancelled.");
            const char *name=status==0?archive_entry_pathname(entry):NULL;
            if(!name||archive_entry_filetype(entry)!=S_IFREG||archive_entry_hardlink(entry))
                return Fail(error,EINVAL,@"Unexpected member in the Apple update archive.");
            int64_t length=archive_entry_size(entry);
            if(!strcmp(name,"signature")&&!signature&&length>0&&length<=1024) {
                NSMutableData *text=[NSMutableData dataWithCapacity:(NSUInteger)length];
                for(ssize_t n;(n=archive_read_data(reader,buffer,sizeof(buffer)))>0;)[text appendBytes:buffer length:(NSUInteger)n];
                if(text.length!=(NSUInteger)length||!(signature=Signature(text,packageMember)))
                    return Fail(error,EINVAL,@"The Apple update signature record is invalid.");
            } else if(!strcmp(name,packageMember.UTF8String)&&!seenPackage&&length>0&&(uint64_t)length<=maxPackageBytes) {
                seenPackage=YES;
                output=open(destination.fileSystemRepresentation,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
                if(output<0)return Fail(error,errno,@"Could not stage the Apple update package.");
                // SHA-1 is the algorithm of the original Apple signature,
                // not a new signing policy for modern installers.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                CC_SHA1_CTX sha;CC_SHA1_Init(&sha);
                ssize_t n=0;uint64_t written=0;
                while((n=archive_read_data(reader,buffer,sizeof(buffer)))>0) {
                    if(cancelled&&cancelled())return Fail(error,ECANCELED,@"Installation cancelled.");
                    CC_SHA1_Update(&sha,buffer,(CC_LONG)n);
                    for(ssize_t offset=0;offset<n;) {
                        ssize_t m=write(output,buffer+offset,(size_t)(n-offset));
                        if(m<0&&errno==EINTR)continue;
                        if(m<=0)return Fail(error,m<0?errno:EIO,@"Could not write the staged Apple update package.");
                        offset+=m;
                    }
                    written+=(uint64_t)n;
                }
                CC_SHA1_Final(digest,&sha);
#pragma clang diagnostic pop
                if(n<0||written!=(uint64_t)length)return Fail(error,EIO,@"The Apple update package is incomplete.");
            } else return Fail(error,EINVAL,@"The Apple update archive does not match its recipe.");
        }
        if(!seenPackage||!signature||!SecKeyVerifySignature(key,kSecKeyAlgorithmRSASignatureDigestPKCS1v15SHA1,
            (__bridge CFDataRef)[NSData dataWithBytes:digest length:sizeof(digest)],(__bridge CFDataRef)signature,NULL))
            return Fail(error,EINVAL,@"Apple’s original signature did not verify this update.");
        int fd=output;output=-1;
        if(close(fd)) {int problem=errno;unlink(destination.fileSystemRepresentation);return Fail(error,problem,@"Could not finish writing the Apple update package.");}
        return YES;
    } @finally {
        if(output>=0) {close(output);unlink(destination.fileSystemRepresentation);}
        if(reader)archive_read_free(reader);
        if(input>=0)close(input);
        CFRelease(key);
    }
}
