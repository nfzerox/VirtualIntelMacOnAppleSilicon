#import "VIMCPIOPayload.h"
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static BOOL ExtractionError(NSError **error,NSInteger code,NSString *message) {
    if(error)*error=[NSError errorWithDomain:NSCocoaErrorDomain code:code
        userInfo:@{NSLocalizedDescriptionKey:message}];
    return NO;
}

BOOL VIMExtractCPIOPayload(NSURL *payload,NSURL *root,
                          BOOL (^progress)(uint64_t),NSError **error) {
    // Preserve the former pax/file-path behavior for readable payload links.
    int fd=open(payload.fileSystemRepresentation,O_RDONLY|O_CLOEXEC);
    if(fd<0) {
        if(error)*error=[NSError errorWithDomain:NSPOSIXErrorDomain code:errno userInfo:nil];
        return NO;
    }
    NSFileHandle *input=[[NSFileHandle alloc] initWithFileDescriptor:fd closeOnDealloc:YES];
    uint8_t signature[6];ssize_t length;
    do {length=pread(fd,signature,sizeof(signature),0);} while(length<0&&errno==EINTR);
    if(length<0) {
        int problem=errno;[input closeFile];
        if(error)*error=[NSError errorWithDomain:NSPOSIXErrorDomain code:problem userInfo:nil];
        return NO;
    }
    BOOL gzip=length>=2&&signature[0]==0x1f&&signature[1]==0x8b;
    BOOL bzip=length>=3&&!memcmp(signature,"BZh",3);
    BOOL plain=length==6&&(!memcmp(signature,"070707",6)||!memcmp(signature,"070701",6)||!memcmp(signature,"070702",6));
    if(!gzip&&!bzip&&!plain) {
        [input closeFile];
        return ExtractionError(error,NSFileReadCorruptFileError,@"The installer package format wasn’t recognized.");
    }
    // dup2 in NSTask shares the open-file description. Observe its offset;
    // do not seek elsewhere, reread compressed data or materialize a CPIO.
    int observer=fcntl(fd,F_DUPFD_CLOEXEC,0);
    NSTask *extractor=[NSTask new],*decoder=nil;
    NSPipe *pipe=nil;
    extractor.executableURL=[NSURL fileURLWithPath:@"/bin/pax"];
    extractor.arguments=gzip?@[@"-r",@"-z",@"-p",@"p"]:@[@"-r",@"-p",@"p"];
    extractor.currentDirectoryURL=root;
    extractor.standardInput=input;
    if(bzip) {
        pipe=[NSPipe pipe];decoder=[NSTask new];
        decoder.executableURL=[NSURL fileURLWithPath:@"/usr/bin/bzip2"];
        decoder.arguments=@[@"-dc"];decoder.standardInput=input;
        decoder.standardOutput=pipe.fileHandleForWriting;
        extractor.standardInput=pipe.fileHandleForReading;
    }
    dispatch_semaphore_t exited=dispatch_semaphore_create(0);
    extractor.terminationHandler=^(__unused NSTask *finished){dispatch_semaphore_signal(exited);};
    BOOL decoderLaunched=NO,extractorLaunched=NO,cancelled=NO,readClosed=NO,writeClosed=NO;
    @try {
        if(progress&&!progress(0)) {
            cancelled=YES;
        } else {
            if(decoder) {
                decoderLaunched=[decoder launchAndReturnError:error];
                [pipe.fileHandleForWriting closeFile];writeClosed=YES;
                if(!decoderLaunched)return NO;
            }
            extractorLaunched=[extractor launchAndReturnError:error];
            if(pipe){[pipe.fileHandleForReading closeFile];readClosed=YES;}
            if(!extractorLaunched)return NO;
            for(;;) {
                long waiting=dispatch_semaphore_wait(exited,dispatch_time(DISPATCH_TIME_NOW,500*NSEC_PER_MSEC));
                off_t position=observer>=0?lseek(observer,0,SEEK_CUR):-1;
                // Offset reporting is optional; losing it must not reject an
                // otherwise readable installer. Cancellation still gets a tick.
                if(progress&&!progress(position>=0?(uint64_t)position:0)) {
                    cancelled=YES;
                    if(extractor.running)[extractor terminate];
                    if(decoder.running)[decoder terminate];
                    break;
                }
                if(!waiting)break;
            }
            [extractor waitUntilExit];
            if(decoderLaunched)[decoder waitUntilExit];
        }
        if(cancelled)return ExtractionError(error,NSUserCancelledError,@"Installation preparation was cancelled.");
        if(extractor.terminationStatus||(decoderLaunched&&decoder.terminationStatus))
            return ExtractionError(error,NSFileReadCorruptFileError,@"The installer package couldn’t be extracted.");
        return YES;
    } @finally {
        // A launch/callback failure must not leave an extractor writing into
        // a disk image after the install worker begins its normal cleanup.
        if(pipe){if(!readClosed)[pipe.fileHandleForReading closeFile];if(!writeClosed)[pipe.fileHandleForWriting closeFile];}
        if(extractorLaunched){if(extractor.running)[extractor terminate];[extractor waitUntilExit];}
        if(decoderLaunched){if(decoder.running)[decoder terminate];[decoder waitUntilExit];}
        extractor.terminationHandler=nil;
        if(observer>=0)close(observer);
        [input closeFile];
    }
}
