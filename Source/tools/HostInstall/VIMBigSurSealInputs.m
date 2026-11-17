#import "VIMBigSurSealInputs.h"
#include "../host-install-bigsur-seal.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static BOOL Failure(NSError **error,NSInteger code,NSString *message) {
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.BigSurSealInputs" code:code
        userInfo:@{NSLocalizedDescriptionKey:message}];
    return NO;
}

BOOL VIMBigSurExtractSealInputs(NSURL *intelMTree,NSURL *directory,VIMArchiveProgress progress,void *context,NSError **error) {
    int input=open(intelMTree.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    int output=-1,reader=-1,writer=-1;struct stat info;
    @try {
        if(input<0||fstat(input,&info)||!S_ISREG(info.st_mode))return Failure(error,EINVAL,@"The original Big Sur Intel seal archive is unavailable.");
        output=open(directory.fileSystemRepresentation,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(output<0)return Failure(error,errno,@"The new Big Sur seal staging directory is unavailable.");
        int fds[2];
        if(pipe(fds))return Failure(error,errno,@"Could not open the Big Sur seal stream.");
        reader=fds[0];writer=fds[1];
        if(fcntl(writer,F_SETNOSIGPIPE,1)<0||fcntl(reader,F_SETFD,FD_CLOEXEC)<0||fcntl(writer,F_SETFD,FD_CLOEXEC)<0)
            return Failure(error,errno,@"Could not configure the Big Sur seal stream.");
        __block int extractError=0;
        dispatch_group_t group=dispatch_group_create();
        int consume=reader;reader=-1;
        dispatch_group_async(group,dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
            if(VIMExtractIntelSealArchive(consume,output,NULL,NULL))extractError=errno;
            close(consume);
        });
        int decodeError=0;
        @try {if(VIMDecodeIntelMTree(input,writer,progress,context))decodeError=errno;}
        @finally {close(writer);writer=-1;dispatch_group_wait(group,DISPATCH_TIME_FOREVER);}
        if(decodeError==ECANCELED)return Failure(error,ECANCELED,@"Installation cancelled.");
        if(decodeError||extractError)return Failure(error,extractError?:decodeError,@"Could not extract the original Big Sur Intel seal metadata.");
        return YES;
    } @finally {
        if(input>=0)close(input);if(output>=0)close(output);
        if(reader>=0)close(reader);if(writer>=0)close(writer);
    }
}
