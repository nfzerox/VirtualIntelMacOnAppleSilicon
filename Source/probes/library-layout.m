// Bounded native migration fixture. Uses only a newly supplied scratch folder.
#import "../VirtualIntelMac/VIMLibraryLocation.m"

int main(int argc,const char *argv[]) {@autoreleasepool {
    if(argc!=2)return 64;
    NSFileManager *fm=NSFileManager.defaultManager;NSError *error=nil;
    NSURL *root=[NSURL fileURLWithPath:@(argv[1]) isDirectory:YES];
    if([fm fileExistsAtPath:root.path])return 73;
    NSURL *machine=[root URLByAppendingPathComponent:@"Test.vintelmac"],*state=[machine URLByAppendingPathComponent:@"State"];
    NSCAssert([fm createDirectoryAtURL:state withIntermediateDirectories:YES attributes:nil error:&error],@"%@",error);
    NSString *attempt=[NSUUID.UUID.UUIDString stringByAppendingPathComponent:@"Media/Installer.img"];
    NSURL *media=[root URLByAppendingPathComponent:[@"Downloads/Preparation" stringByAppendingPathComponent:attempt]];
    NSCAssert([fm createDirectoryAtURL:media.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:&error],@"%@",error);
    NSData *bytes=[@"fixture disk, not a VM" dataUsingEncoding:NSUTF8StringEncoding];
    NSCAssert([bytes writeToURL:media options:NSDataWritingWithoutOverwriting error:&error],@"%@",error);
    NSCAssert(WriteLocationPlist(@{@"Identifier":NSUUID.UUID.UUIDString,@"InstallerMedia":media.path},[machine URLByAppendingPathComponent:@"Machine.plist"],&error),@"%@",error);
    int busy=open([state URLByAppendingPathComponent:@"Session.lock"].fileSystemRepresentation,O_CREAT|O_RDWR,0600);NSCAssert(busy>=0&&!flock(busy,LOCK_EX|LOCK_NB),@"lock");
    NSCAssert(!PrepareRoot(root,root,&error)&&error.code==EBUSY,@"running VM must defer");
    NSCAssert([fm fileExistsAtPath:machine.path]&&!Layout2(root),@"busy migration must not move anything");close(busy);
    error=nil;NSCAssert(PrepareRoot(root,root,&error),@"%@",error);
    NSURL *newMachine=[root URLByAppendingPathComponent:@"Machines/Test.vintelmac"],*newMedia=[root URLByAppendingPathComponent:[@"Installers" stringByAppendingPathComponent:attempt]];
    NSDictionary *config=[NSDictionary dictionaryWithContentsOfURL:[newMachine URLByAppendingPathComponent:@"Machine.plist"]];
    NSCAssert([config[@"InstallerMedia"] isEqual:newMedia.path],@"media path migration");
    NSCAssert([[NSData dataWithContentsOfURL:newMedia] isEqual:bytes],@"unchanged bytes");
    NSCAssert(![fm fileExistsAtPath:machine.path]&&Layout2(root),@"publish layout");
    NSCAssert(PrepareRoot(root,root,&error),@"idempotence");
    NSDictionary *overlap=@{@"/test/Downloads":@"/new/Downloads",@"/test/Downloads/Preparation":@"/new/Installers"};
    NSCAssert([RelocatedValue(@"/test/Downloads/Preparation/id/Media/file",overlap) isEqual:@"/new/Installers/id/Media/file"],@"longest prefix");
    puts("Library layout: busy deferral, rename, metadata, unchanged media, idempotence passed");return 0;
}}
