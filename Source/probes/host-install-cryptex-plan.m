#import "../tools/HostInstall/VIMAssistantCryptex.h"

int main(int argc,char **argv) { @autoreleasepool {
    if(argc!=3) {
        fprintf(stderr,"usage: host-install-cryptex-plan archive-members.txt|- original-BuildManifest.plist\n");
        return 64;
    }
    NSError *error=nil;
    NSString *listing=!strcmp(argv[1],"-")?
        [[NSString alloc] initWithData:NSFileHandle.fileHandleWithStandardInput.readDataToEndOfFile encoding:NSUTF8StringEncoding]:
        [NSString stringWithContentsOfFile:@(argv[1]) encoding:NSUTF8StringEncoding error:&error];
    NSDictionary *manifest=[NSDictionary dictionaryWithContentsOfFile:@(argv[2])];
    NSDictionary *plan=listing&&manifest?
        VIMAssistantIntelCryptexPlan([listing componentsSeparatedByString:@"\n"],manifest,&error):nil;
    NSData *output=plan?[NSPropertyListSerialization dataWithPropertyList:plan format:NSPropertyListXMLFormat_v1_0 options:0 error:&error]:nil;
    if(!output) {
        fprintf(stderr,"%s\n",error.localizedDescription.UTF8String?:"Could not read the original Cryptex inputs.");
        return 1;
    }
    [NSFileHandle.fileHandleWithStandardOutput writeData:output];
    return 0;
}}
