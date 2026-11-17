// Finite preparation of the production decoder from trusted installer media.
// No disk image creation, OS installation, account or host-system changes.
#define main VIMHostInstallWorkerMain
#include "../tools/HostInstall/install-worker.m"
#undef main

int main(int argc,char **argv) { @autoreleasepool {
    if(argc!=4)return 64;
    @try {
        NSURL *attempt=[NSURL fileURLWithPath:@(argv[3])];Directory(attempt);
        NSURL *decoder=PrepareAssistantCryptexDecoder([NSURL fileURLWithPath:@(argv[1])],
            [NSURL fileURLWithPath:@(argv[2])],attempt);
        puts(decoder.fileSystemRepresentation);return 0;
    } @catch(NSException *failure) {
        fprintf(stderr,"%s\n",failure.reason.UTF8String);return 1;
    }
}}
