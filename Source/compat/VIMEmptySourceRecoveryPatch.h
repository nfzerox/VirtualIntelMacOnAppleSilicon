#pragma once
#import <Foundation/Foundation.h>
#import <CommonCrypto/CommonDigest.h>
#include <libkern/OSByteOrder.h>
#include <compression.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static inline ssize_t VIMRecoveryPatchRead(int fd,void *buffer,size_t length) {
    size_t done=0;
    while(done<length){
        ssize_t n=read(fd,(uint8_t *)buffer+done,length-done);
        if(n<0&&errno==EINTR)continue;
        if(n<0)return -1;
        if(!n)break;
        done+=(size_t)n;
    }
    return (ssize_t)done;
}

// Monterey and later package Intel's Recovery image as a BXDIFF50 patch
// against an empty source. The PBZX stream is complete replacement data.
// Verify Apple's declared result size and SHA-1, not just successful decode.
static inline BOOL VIMDecodeEmptySourceRecoveryPatch(NSURL *patch,NSURL *output,
    BOOL (^cancelled)(void),NSError **error) {
    int problem=EBADMSG,in=-1,out=-1;
    uint8_t *chunk=NULL,*plain=NULL;
    uint8_t header[0x48];
    struct stat info;
    if((in=open(patch.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC))<0){problem=errno;goto done;}
    if(fstat(in,&info)){problem=errno;goto done;}
    if(!S_ISREG(info.st_mode)||VIMRecoveryPatchRead(in,header,sizeof(header))!=(ssize_t)sizeof(header))goto done;
    uint64_t result=OSReadLittleInt64(header,0x10),capacity=OSReadBigInt64(header,0x40),written=0;
    if(memcmp(header,"BXDIFF50",8)||OSReadLittleInt64(header,8)||OSReadLittleInt64(header,0x18)||
       !result||memcmp(header+0x3c,"pbzx",4)||!capacity||capacity>(64ULL<<20))goto done;
    chunk=malloc((size_t)capacity+65536);plain=malloc((size_t)capacity);
    if(!chunk||!plain){problem=ENOMEM;goto done;}
    if((out=open(output.fileSystemRepresentation,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0644))<0){
        problem=errno;goto done;
    }
    CC_SHA1_CTX hash;CC_SHA1_Init(&hash);
    for(;;) {
        if(cancelled&&cancelled()){problem=ECANCELED;goto done;}
        uint8_t fields[16];ssize_t n=VIMRecoveryPatchRead(in,fields,sizeof(fields));
        if(n==0)break;
        if(n!=(ssize_t)sizeof(fields))goto done;
        uint64_t length=OSReadBigInt64(fields,0),stored=OSReadBigInt64(fields,8);
        if(!length||length>capacity||length>result-written||!stored||stored>capacity+65536||
           VIMRecoveryPatchRead(in,chunk,(size_t)stored)!=(ssize_t)stored)goto done;
        const uint8_t *bytes=chunk;
        if(stored!=length||(stored>=6&&!memcmp(chunk,"\xFD" "7zXZ\0",6))) {
            if(compression_decode_buffer(plain,(size_t)length,chunk,(size_t)stored,NULL,COMPRESSION_LZMA)!=length)goto done;
            bytes=plain;
        }
        for(size_t sent=0;sent<length;) {
            ssize_t count=write(out,bytes+sent,(size_t)length-sent);
            if(count<0&&errno==EINTR)continue;
            if(count<=0){problem=count<0?errno:EIO;goto done;}
            sent+=(size_t)count;
        }
        CC_SHA1_Update(&hash,bytes,(CC_LONG)length);written+=length;
    }
    unsigned char digest[CC_SHA1_DIGEST_LENGTH];CC_SHA1_Final(digest,&hash);
    if(written!=result||memcmp(digest,header+0x28,sizeof(digest)))goto done;
    problem=fsync(out)?errno:0;
done:
    free(chunk);free(plain);
    if(in>=0)close(in);
    if(out>=0){
        if(close(out)&&!problem)problem=errno;
        if(problem)unlink(output.fileSystemRepresentation);
    }
    if(problem&&error)*error=[NSError errorWithDomain:NSPOSIXErrorDomain code:problem userInfo:@{
        NSLocalizedDescriptionKey:problem==ECANCELED?@"Recovery preparation was cancelled.":
            @"The installer’s Recovery image couldn’t be reconstructed."}];
    return problem==0;
}
