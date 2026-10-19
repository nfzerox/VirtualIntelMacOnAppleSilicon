// Bounded native check of the production archive/redaction path; no VM needed.
#import "../VirtualIntelMac/VIMDiagnostics.m"
#import "../VirtualIntelMac/VIMLibraryLocation.m"
void VIMPresentError(NSError *error,NSWindow *window) { abort(); }
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
    [[NSString stringWithFormat:@"Runtime %@ and /Volumes/PrivateDrive/VM; mac=52:54:00:12:34:56 password=secret-value\n",privatePath]
        writeToURL:[state URLByAppendingPathComponent:@"session.log"] atomically:YES encoding:NSUTF8StringEncoding error:&error];
    NSURL *output=[scratch URLByAppendingPathComponent:@"Diagnostics.zip"];
    if(![VIMDiagnostics writeArchiveToURL:output library:library error:&error]) {
        fprintf(stderr,"%s\n",error.description.UTF8String);return 1;
    }
    // Property-list XML stays valid after redaction (no raw '<redacted>' tags).
    NSURL *record=[scratch URLByAppendingPathComponent:@"Redaction.plist"];
    if(!WriteRecord(@{@"Error":privatePath},record,&error))return 1;
    NSDictionary *restored=ReadRecord(record);
    if(![restored[@"Error"] isEqual:@"<redacted>/item"])return 1;
    puts(output.path.UTF8String);return 0;
} }
