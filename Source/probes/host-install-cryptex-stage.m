#import "../tools/HostInstall/VIMAssistantCryptex.h"

int main(int argc,char **argv) { @autoreleasepool {
    NSError *error=nil;
    if(argc==3&&!strcmp(argv[1],"--verify-intel-caches")) {
        BOOL valid=VIMVerifyAssistantIntelCryptexCaches([NSURL fileURLWithPath:@(argv[2])],&error);
        if(!valid)fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);
        return valid?0:1;
    }
    if(argc!=7) {
        fprintf(stderr,"usage: host-install-cryptex-stage original.zip /scratch-preboot /scratch-patches DATA-UUID BUILD /installer-framework/Resources/cryptex-decoder\n");
        return 64;
    }
    BOOL ok=VIMStageAssistantIntelCryptex([NSURL fileURLWithPath:@(argv[1])],
        [NSURL fileURLWithPath:@(argv[2])],[NSURL fileURLWithPath:@(argv[3])],
        [[NSUUID alloc] initWithUUIDString:@(argv[4])],@(argv[5]),[NSURL fileURLWithPath:@(argv[6])],nil,&error);
    if(!ok)fprintf(stderr,"%s\n",error.localizedDescription.UTF8String?:"Could not stage the original Ventura Cryptex.");
    return ok?0:1;
}}
