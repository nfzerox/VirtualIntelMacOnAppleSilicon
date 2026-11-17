// Bounded native check of the production archive/redaction path; no VM needed.
#import "../VirtualIntelMac/VIMDiagnostics.m"
#import "../VirtualIntelMac/VIMLibraryLocation.m"
void VIMPresentError(NSError *error,NSWindow *window) { abort(); }
void VIMPresentAlert(NSAlert *alert,NSWindow *window,void (^completion)(NSModalResponse)) { abort(); }
int main(int argc,const char **argv) { @autoreleasepool {
    if(argc!=2)return 2;
    NSURL *scratch=[NSURL fileURLWithPath:@(argv[1]) isDirectory:YES];
    NSURL *library=[scratch URLByAppendingPathComponent:@"Library"];
    NSURL *state=[library URLByAppendingPathComponent:@"Private Name.vintelmac/State"];
    NSFileManager *fm=NSFileManager.defaultManager;NSError *error=nil;
    if(![fm createDirectoryAtURL:state withIntermediateDirectories:YES attributes:nil error:&error])return 1;
    NSURL *machine=[state URLByDeletingLastPathComponent];
    NSString *privatePath=[@"/Users/" stringByAppendingString:@"private-person/item"];
    [@{@"CPUCount":@4,@"Guest":@"mojave",@"SSHUsername":@"NeverExportThisAccount",
        @"Disk":@"NeverExportThisDisk"} writeToURL:[machine URLByAppendingPathComponent:@"Machine.plist"] atomically:YES];
    [@{@"Phase":@"failed",@"Error":[NSString stringWithFormat:@"Cannot read %@ at 10.0.0.42",privatePath]}
        writeToURL:[state URLByAppendingPathComponent:@"GuestInstallation.plist"] atomically:YES];
    NSString *fault=@"qemu-system-x86_64: hvdbt: monitor fault: esr 0x2000000 far 0xffffff7f9fad6880 elr 0xf00000af1a6c arm-word 0x00000000";
    NSString *retry=@"hvdbt: vCPU 2 synchronizing translated block entry after Unknown instruction; arm-pc 0xf000008157f0 x86-rip 0xffffff8000123456 retry 1";
    NSString *startup=[@"" stringByPaddingToLength:280000 withString:@"ordinary QEMU startup data\n" startingAtIndex:0];
    if(![[NSString stringWithFormat:@"%@\nRuntime %@ and /Volumes/PrivateDrive/VM; mac=52:54:00:12:34:56 password=secret-value\n%@\n%@\n[Virtual Intel Mac] QEMU state: internal-error\n",startup,privatePath,retry,fault]
        writeToURL:[state URLByAppendingPathComponent:@"session.log"] atomically:YES encoding:NSUTF8StringEncoding error:&error])return 1;
    NSURL *output=[scratch URLByAppendingPathComponent:@"Diagnostics.zip"];
    if(![VIMDiagnostics writeArchiveToURL:output library:library error:&error]) {
        fprintf(stderr,"%s\n",error.description.UTF8String);return 1;
    }
    // Read the real archive member using a fixed pattern; no file extraction.
    NSTask *unzip=[NSTask new];NSPipe *result=[NSPipe pipe];
    unzip.executableURL=[NSURL fileURLWithPath:@"/usr/bin/unzip"];
    unzip.arguments=@[@"-p",output.path,@"*/Mac 1/session.log"];
    unzip.standardOutput=result;
    if(![unzip launchAndReturnError:&error])return 1;
    NSData *bytes=[result.fileHandleForReading readDataToEndOfFile];[unzip waitUntilExit];
    NSString *log=[[NSString alloc] initWithData:bytes encoding:NSUTF8StringEncoding];
    if(unzip.terminationStatus||bytes.length>262144||![log containsString:fault]||![log containsString:retry]||
        ![log containsString:@"QEMU state: internal-error"]||![log containsString:@"<redacted>/item"]||
        [log containsString:privatePath]||[log containsString:@"secret-value"])return 1;
    // Property-list XML stays valid after redaction (no raw '<redacted>' tags).
    NSURL *record=[scratch URLByAppendingPathComponent:@"Redaction.plist"];
    if(!WriteRecord(@{@"Error":privatePath},record,&error))return 1;
    NSDictionary *restored=ReadRecord(record);
    if(![restored[@"Error"] isEqual:@"<redacted>/item"])return 1;
    puts(output.path.UTF8String);return 0;
} }
