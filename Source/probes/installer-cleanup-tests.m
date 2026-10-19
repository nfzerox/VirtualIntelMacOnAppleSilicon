#import <Foundation/Foundation.h>
#import "VIMMachine.h"
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
int main(void){@autoreleasepool {
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *root=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]];
    NSURL *vm=[root URLByAppendingPathComponent:@"test.vintelmac"];
    for(NSString *dir in @[@"State",@"Installation",@"Firmware",@"Disks"])
        NSCAssert([fm createDirectoryAtURL:[vm URLByAppendingPathComponent:dir] withIntermediateDirectories:YES attributes:nil error:NULL],@"fixture directory");
    NSDictionary *config=@{@"FormatVersion":@1,@"Identifier":NSUUID.UUID.UUIDString,@"Name":@"Test",@"Guest":@"catalina",@"CPUCount":@4,@"MemoryMiB":@4096,
      @"Disk":@"Disks/System.qcow2",@"Firmware":@"Firmware/CODE.fd",@"NVRAM":@"Firmware/NVRAM.fd",@"BootMedia":@"Firmware/EFI.img",@"BootMediaKind":@"efi-disk",@"InstallerMedia":@"Installation/Installer.iso",@"InstallState":@"installing"};
    for(NSString *key in @[@"Disk",@"Firmware",@"NVRAM",@"BootMedia",@"InstallerMedia"])
        [@"test" writeToURL:[vm URLByAppendingPathComponent:config[key]] atomically:YES encoding:NSUTF8StringEncoding error:NULL];
    [config writeToURL:[vm URLByAppendingPathComponent:@"Machine.plist"] atomically:YES];
    [@{@"FormatVersion":@1,@"MachineIdentifier":config[@"Identifier"],@"Media":config[@"InstallerMedia"],@"Size":@4} writeToURL:[vm URLByAppendingPathComponent:@"Installation/Ownership.plist"] atomically:YES];
    NSString *transaction=NSUUID.UUID.UUIDString;
    NSDictionary *evidence=@{@"TransactionID":transaction,@"VerifiedAfterReboot":@YES,@"MetalCommandVerified":@YES,@"VolumeUUID":@"volume",@"ProductBuildVersion":@"19H15",@"BootSessionUUID":@"boot"};
    NSDictionary *report=@{@"Guest":@"catalina",@"InstalledSystemCandidate":@YES,@"DesktopRunning":@YES,@"LiveRoot":@YES,@"KernelMatchesSystem":@YES,@"VolumeUUID":@"volume",@"ProductBuildVersion":@"19H15",@"BootSessionUUID":@"boot"};
    VIMMachine *machine=[VIMMachine loadURL:vm error:NULL];NSCAssert(machine,@"fixture load");
    NSCAssert(machine.requiresInstallerDisplay,@"fresh installer uses basic display");
    NSMutableDictionary *setup=[report mutableCopy];setup[@"InstalledSystemCandidate"]=@NO;
    NSCAssert(![machine claimAutomaticInstallationWithGuestReport:setup error:NULL],@"unfinished setup cannot start patching");
    setup=[report mutableCopy];setup[@"DesktopRunning"]=@NO;
    NSCAssert(![machine claimAutomaticInstallationWithGuestReport:setup error:NULL],@"setup without a usable desktop cannot start patching");
    setup=[report mutableCopy];setup[@"Guest"]=@"mojave";
    NSCAssert(![machine claimAutomaticInstallationWithGuestReport:setup error:NULL],@"wrong guest cannot start patching");
    NSCAssert([machine claimAutomaticInstallationWithGuestReport:report error:NULL],@"completed setup claims automatic install");
    NSCAssert(![machine claimAutomaticInstallationWithGuestReport:report error:NULL],@"automatic attempt must not repeat");
    VIMMachine *reopened=[VIMMachine loadURL:vm error:NULL];
    NSCAssert(![reopened claimAutomaticInstallationWithGuestReport:report error:NULL],@"relaunch must not repeat an uncertain attempt");
    NSMutableDictionary *publication=[@{@"TransactionID":transaction,@"Phase":@"awaiting-reboot",@"FilesPublished":@YES,@"RebootRequired":@YES,@"ProductBuildVersion":@"19H15",@"VolumeUUID":@"volume"} mutableCopy];
    NSMutableDictionary *installation=[@{@"FormatVersion":@1,@"Phase":@"awaiting-reboot",@"TransactionID":transaction,@"GuestResult":publication,@"Preparation":@{@"TransactionID":transaction,@"GuestReport":report}} mutableCopy];
    NSURL *journal=[vm URLByAppendingPathComponent:@"State/GuestInstallation.plist"];
    [installation writeToURL:journal atomically:YES];
    NSCAssert(!machine.requiresInstallerDisplay,@"completed publication boots PVG with media retained");
    NSCAssert(machine.configuration[@"InstallerMedia"],@"display selection must not remove media");
    for(NSString *phase in @[@"dispatching",@"rebuilding-cache",@"rolled-back",@"outcome-unknown"]) {
        installation[@"Phase"]=phase;[installation writeToURL:journal atomically:YES];
        NSCAssert(machine.requiresInstallerDisplay,@"incomplete or uncertain publication retains basic display");
    }
    installation[@"Phase"]=@"graphics-verified";[installation writeToURL:journal atomically:YES];
    NSCAssert(!machine.requiresInstallerDisplay,@"verified installation keeps PVG");
    publication[@"TransactionID"]=NSUUID.UUID.UUIDString;[installation writeToURL:journal atomically:YES];
    NSCAssert(machine.requiresInstallerDisplay,@"foreign transaction cannot enable PVG");
    NSCAssert(![machine finishInstallationWithGuestReport:report error:NULL],@"must require verification");
    [@{@"Phase":@"graphics-verified",@"TransactionID":transaction,@"PostBootVerification":evidence} writeToURL:[vm URLByAppendingPathComponent:@"State/GuestInstallation.plist"] atomically:YES];
    int lock=open([vm URLByAppendingPathComponent:@"State/Session.lock"].fileSystemRepresentation,O_RDWR);
    NSCAssert(lock>=0&&flock(lock,LOCK_EX|LOCK_NB)==0,@"lock fixture");
    NSCAssert(![machine finishInstallationWithGuestReport:report error:NULL],@"running guest must prevent cleanup");close(lock);
    NSError *failure=nil;BOOL finished=[machine finishInstallationWithGuestReport:report error:&failure];
    NSCAssert(finished,@"cleanup should succeed: %@",failure);
    NSCAssert(!machine.configuration[@"InstallerMedia"],@"boot routing must be retired");
    NSCAssert(!machine.requiresInstallerDisplay,@"installed VM uses PVG without media");
    NSCAssert(![fm fileExistsAtPath:[vm URLByAppendingPathComponent:@"Installation/Installer.iso"].path],@"owned media should be removed");
    NSCAssert([machine finishInstallationWithGuestReport:report error:NULL],@"cleanup must be idempotent");
    // Reconstruct a crash after routing was saved but before ISO deletion.
    [@"test" writeToURL:[vm URLByAppendingPathComponent:@"Installation/Installer.iso"] atomically:YES encoding:NSUTF8StringEncoding error:NULL];
    [@{@"Phase":@"cleanup-pending",@"Identifier":config[@"Identifier"],@"TransactionID":transaction,@"GuestReport":report} writeToURL:[vm URLByAppendingPathComponent:@"State/Install.plist"] atomically:YES];
    reopened=[VIMMachine loadURL:vm error:NULL];
    NSCAssert([reopened retryInstallerCleanup:NULL],@"restart must finish pending cleanup");
    NSCAssert(![fm fileExistsAtPath:[vm URLByAppendingPathComponent:@"Installation/Installer.iso"].path],@"retry removes surplus owned media");
    NSCAssert([reopened retryInstallerCleanup:NULL],@"completed cleanup retry is a no-op");
    NSCAssert([fm fileExistsAtPath:[vm URLByAppendingPathComponent:@"Disks/System.qcow2"].path],@"system disk must remain");
    [fm removeItemAtURL:root error:NULL];
    puts("PASS: no-verification and active-session refusal, boot-route retirement, owned media deletion, idempotent retry, system disk retained");
}return 0;}
