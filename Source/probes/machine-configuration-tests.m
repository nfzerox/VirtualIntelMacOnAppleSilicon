#import <Foundation/Foundation.h>
#import "VIMMachine.h"
#import "VIMInstallerAssembly.h"
#import "VIMNetworkDefaults.h"
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>

static NSURL *Fixture(NSURL *root,NSString *name) {
    NSString *identifier=NSUUID.UUID.UUIDString;
    NSURL *URL=[root URLByAppendingPathComponent:[identifier stringByAppendingString:@".vintelmac"]];
    NSFileManager *fm=NSFileManager.defaultManager;
    for(NSString *dir in @[@"Disks",@"Firmware",@"State"])[fm createDirectoryAtURL:[URL URLByAppendingPathComponent:dir] withIntermediateDirectories:YES attributes:nil error:nil];
    for(NSString *file in @[@"Disks/System.qcow2",@"Firmware/NVRAM.fd"])[@"fixture" writeToURL:[URL URLByAppendingPathComponent:file] atomically:YES encoding:NSUTF8StringEncoding error:nil];
    [@{@"FormatVersion":@1,@"Identifier":identifier,@"Name":name,@"Guest":@"mojave",@"CPUCount":@4,@"MemoryMiB":@4096,
      @"Disk":@"Disks/System.qcow2",@"NVRAM":@"Firmware/NVRAM.fd",@"Firmware":@"Firmware/CODE.fd",@"BootMedia":@"Firmware/EFI.img",@"FirmwareSource":@"application"}
        writeToURL:[URL URLByAppendingPathComponent:@"Machine.plist"] atomically:YES];return URL;
}
static void CheckUpdateRepairs(NSURL *root) {
    NSFileManager *fm=NSFileManager.defaultManager;
    for(NSString *guest in @[@"mojave",@"catalina",@"bigsur"]) {
        NSURL *URL=Fixture(root,@"Update");
        NSURL *configuration=[URL URLByAppendingPathComponent:@"Machine.plist"];
        NSMutableDictionary *config=[[NSDictionary dictionaryWithContentsOfURL:configuration] mutableCopy];
        config[@"Guest"]=guest;config[@"InstallState"]=@"installed";[config writeToURL:configuration atomically:YES];
        VIMMachine *machine=[VIMMachine loadURL:URL error:nil];NSCAssert(machine,@"Update fixture loads");
        NSString *identity=NSUUID.UUID.UUIDString;
        NSDictionary *prior=@{@"FormatVersion":@1,@"TransactionID":identity,@"Phase":@"graphics-verified",
            @"Preparation":@{@"TransactionID":identity,@"GuestReport":@{@"Guest":guest,@"ProductBuildVersion":@"old-build",@"VolumeUUID":@"same-volume"}},
            @"GuestResult":@{@"TransactionID":identity,@"Phase":@"awaiting-reboot",@"FilesPublished":@YES,@"RebootRequired":@YES,
                @"ProductBuildVersion":@"old-build",@"VolumeUUID":@"same-volume"},
            @"PostBootVerification":@{@"TransactionID":identity,@"VerifiedAfterReboot":@YES,@"MetalCommandVerified":@YES,
                @"ProductBuildVersion":@"old-build",@"VolumeUUID":@"same-volume",@"BootSessionUUID":@"old-boot"}};
        NSDictionary *live=@{@"Guest":guest,@"InstalledSystemCandidate":@YES,@"DesktopRunning":@YES,
            @"LiveRoot":@YES,@"KernelMatchesSystem":@YES,@"ProductBuildVersion":@"new-build",
            @"VolumeUUID":@"same-volume",@"BootSessionUUID":@"new-boot"};
        NSURL *journal=[URL URLByAppendingPathComponent:@"State/GuestInstallation.plist"];
        NSURL *claims=[URL URLByAppendingPathComponent:@"State/UpdateRepairs"];
        for(NSUInteger test=0;test<16;test++) {
            NSMutableDictionary *state=[prior mutableCopy],*report=[live mutableCopy];
            switch(test) {
                case 0:report[@"ProductBuildVersion"]=@"old-build";break;
                case 1:report[@"VolumeUUID"]=@"different-volume";break;
                case 2:report[@"InstalledSystemCandidate"]=@NO;break;
                case 3:report[@"KernelMatchesSystem"]=@NO;break;
                case 4:report[@"LiveRoot"]=@NO;break;
                case 5:report[@"BootSessionUUID"]=@"old-boot";break;
                case 6:report[@"Guest"]=@"unsupported";break;
                case 7:report[@"ProductBuildVersion"]=@"../../outside";break;
                case 8:[state removeObjectForKey:@"PostBootVerification"];break;
                case 9:state[@"Phase"]=@"publishing";break;
                case 10:state[@"Phase"]=@"outcome-unknown";break;
                case 11:state[@"Phase"]=@"rolled-back";break;
                case 12:{NSMutableDictionary *v=[state[@"PostBootVerification"] mutableCopy];v[@"MetalCommandVerified"]=@NO;state[@"PostBootVerification"]=v;break;}
                case 13:state[@"TransactionID"]=@"not-a-uuid";break;
                case 14:state[@"Preparation"]=@"invalid";break;
                case 15:state[@"GuestResult"]=@{};break;
            }
            [state writeToURL:journal atomically:YES];NSError *error=nil;
            NSCAssert(![machine claimAutomaticUpdateRepairWithGuestReport:report error:&error]&&!error,@"Ineligible update case %lu rejected without a write: %@",test,error);
            NSCAssert(![fm fileExistsAtPath:claims.path],@"No claim for ineligible evidence");
        }
        [prior writeToURL:journal atomically:YES];NSError *error=nil;
        NSCAssert([machine claimAutomaticUpdateRepairWithGuestReport:live error:&error],@"New build on verified same-volume desktop claims repair: %@",error);
        NSDictionary *record=[NSDictionary dictionaryWithContentsOfURL:[claims URLByAppendingPathComponent:@"new-build.plist"]];
        NSCAssert([record[@"GuestReport"] isEqual:live]&&[record[@"PreviousTransactionID"] isEqual:identity]&&[record[@"MachineIdentifier"] isEqual:config[@"Identifier"]],@"Claim retains both sides of the update");
        NSCAssert([[NSDictionary dictionaryWithContentsOfURL:journal] isEqual:prior],@"Claim never archives or rewrites the old transaction");
        VIMMachine *reopened=[VIMMachine loadURL:URL error:nil];error=nil;
        NSCAssert(![reopened claimAutomaticUpdateRepairWithGuestReport:live error:&error]&&!error,@"Relaunch never repeats a claimed attempt: %@",error);
        NSMutableDictionary *missing=[prior mutableCopy];
        missing[@"PreviousPostBootVerification"]=missing[@"PostBootVerification"];
        [missing removeObjectForKey:@"PostBootVerification"];
        missing[@"Phase"]=@"outcome-unknown";missing[@"ReconciliationError"]=@"Old verifier removed by update";
        [missing writeToURL:journal atomically:YES];
        NSCAssert([reopened claimAutomaticUpdateRepairWithGuestReport:live error:&error],@"Recover pre-dispatch read failure of a verified old transaction");
        NSCAssert(![reopened claimAutomaticUpdateRepairWithGuestReport:live error:&error],@"Missing-verifier recovery is durable and runs at most once");
        NSMutableDictionary *newAttempt=[missing mutableCopy];newAttempt[@"TransactionID"]=NSUUID.UUID.UUIDString;
        [newAttempt writeToURL:journal atomically:YES];
        NSCAssert(![reopened claimAutomaticUpdateRepairWithGuestReport:live error:&error],@"An uncertain new write cannot reuse old verification or claim");
        // A failed read-only old-build verification must not erase the proven
        // earlier success needed to repair the newly installed guest build.
        NSMutableDictionary *reconciled=[prior mutableCopy];
        reconciled[@"PreviousPostBootVerification"]=reconciled[@"PostBootVerification"];
        [reconciled removeObjectForKey:@"PostBootVerification"];reconciled[@"Phase"]=@"awaiting-reboot";
        [reconciled writeToURL:journal atomically:YES];
        NSMutableDictionary *next=[live mutableCopy];next[@"ProductBuildVersion"]=@"another-build";next[@"BootSessionUUID"]=@"another-boot";
        NSCAssert([reopened claimAutomaticUpdateRepairWithGuestReport:next error:&error],@"Earlier read-only failure retains valid update evidence: %@",error);
        next[@"ProductBuildVersion"]=@"headless-build";next[@"DesktopRunning"]=@NO;
        NSCAssert([reopened claimAutomaticUpdateRepairWithGuestReport:next error:&error],@"A verified update can repair before WindowServer/Finder becomes usable: %@",error);
        NSCAssert([fm contentsOfDirectoryAtPath:claims.path error:nil].count==3,@"One claim per target build");
        NSURL *display=[URL URLByAppendingPathComponent:@"State/UpdateDisplay.plist"];
        [@{@"FormatVersion":@1,@"Guest":guest,@"PreviousTransactionID":identity,
           @"PreviousBuild":@"old-build",@"VolumeUUID":@"same-volume"} writeToURL:display atomically:YES];
        NSCAssert(machine.requiresInstallerDisplay,@"Software display persists even after updater clears NVRAM");
        NSString *fresh=NSUUID.UUID.UUIDString;
        NSMutableDictionary *repaired=[prior mutableCopy];repaired[@"Phase"]=@"awaiting-reboot";repaired[@"TransactionID"]=fresh;
        repaired[@"Preparation"]=@{@"TransactionID":fresh,@"GuestReport":live};
        repaired[@"GuestResult"]=@{@"TransactionID":fresh,@"Phase":@"awaiting-reboot",@"FilesPublished":@YES,@"RebootRequired":@YES,
            @"ProductBuildVersion":@"new-build",@"VolumeUUID":@"same-volume"};
        [repaired removeObjectForKey:@"PostBootVerification"];[repaired writeToURL:journal atomically:YES];
        NSCAssert(!machine.requiresInstallerDisplay,@"Fresh completed repair returns to PVG for postboot verification");
        repaired[@"Phase"]=@"publishing";[repaired writeToURL:journal atomically:YES];
        NSCAssert(machine.requiresInstallerDisplay,@"Interrupted publication never enables PVG");
    }
    puts("PASS: minor-update repair claims for Mojave/Catalina/Big Sur, evidence guards, old-journal preservation, relaunch deduplication");
}
int main(void) {@autoreleasepool {
    id previousKeep=[NSUserDefaults.standardUserDefaults objectForKey:@"VIMKeepInstallers"];
    [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"VIMKeepInstallers"];
    NSFileManager *fm=NSFileManager.defaultManager;NSURL *root=[[fm.temporaryDirectory URLByAppendingPathComponent:[@"vim-model-" stringByAppendingString:NSUUID.UUID.UUIDString]] URLByResolvingSymlinksInPath];
    [fm createDirectoryAtURL:root withIntermediateDirectories:NO attributes:nil error:nil];
    CheckUpdateRepairs(root);
    NSURL *original=Fixture(root,@"Mojave");NSError *error=nil;VIMMachine *machine=[VIMMachine loadURL:original error:&error];NSCAssert(machine,@"Fixture loads: %@",error);
    NSString *defaultMAC=VIMNetworkMACAddress(machine.configuration);
    NSCAssert(VIMNetworkMACAddressIsValid(defaultMAC),@"UUID default MAC is valid");
    for(id invalid in @[@"01:00:5E:00:00:01",@"00:00:00:00:00:00",@"52:54:00:12:34:GG",@"52:54:0:12:34:56",@"52:54:00:12:34:56,netdev=x",@1])
        NSCAssert(!VIMNetworkMACAddressIsValid(invalid),@"Reject malformed, multicast or injectable MAC");
    int fd=open([original URLByAppendingPathComponent:@"State/Session.lock"].fileSystemRepresentation,O_CREAT|O_RDWR,0600);NSCAssert(fd>=0&&flock(fd,LOCK_EX|LOCK_NB)==0,@"Own simulated live session");
    NSCAssert(([machine saveSettings:@{@"MACAddress":@"02:34:56:78:9A:BC",@"DisplayMode":@"space",@"NetworkConnected":@NO,@"AudioOutputDevice":@"default",@"AudioInputDevice":@"disabled"} error:&error]),@"New device settings queue: %@",error);
    NSCAssert([VIMNetworkMACAddress(machine.editableConfiguration) isEqual:@"02:34:56:78:9A:BC"],@"Custom MAC takes precedence");
    NSCAssert(![machine saveSettings:@{@"MACAddress":@1} error:nil]&&![machine saveSettings:@{@"DisplayMode":@"bogus"} error:nil],@"Reject invalid device settings");
    NSCAssert(([machine saveSettings:@{@"CPUCount":@8,@"Description":@"Personal Mac"} error:&error]),@"Live edits queue: %@",error);
    NSCAssert([machine.configuration[@"CPUCount"] isEqual:@4]&&[machine.editableConfiguration[@"CPUCount"] isEqual:@8],@"Launch snapshot remains unchanged");
    NSCAssert(!machine.configuration[@"SSHEnabled"]&&!machine.configuration[@"FileSharingEnabled"],@"Do not migrate ports in a live launch snapshot");
    NSCAssert([machine rename:@"Work Mac" error:&error]&&[machine.URL isEqual:original.URLByResolvingSymlinksInPath]&&[machine.pendingName isEqual:@"Work Mac"],@"Live package is not moved");
    NSCAssert(![machine saveSettings:@{@"CPUCount":@128} error:&error],@"Invalid pending values rejected");
    NSCAssert([fm fileExistsAtPath:[original URLByAppendingPathComponent:@"Disks/System.qcow2"].path],@"Live disk stays put");
    flock(fd,LOCK_UN);close(fd);
    NSCAssert([machine applyPendingChanges:&error],@"Stopped changes publish: %@",error);
    NSCAssert([machine.name isEqual:@"Work Mac"]&&[machine.URL.lastPathComponent isEqual:@"Work Mac.vintelmac"],@"Name is the package filename");
    NSCAssert(!machine.configuration[@"Name"]&&[machine.configuration[@"CPUCount"] isEqual:@8]&&!machine.pendingName,@"No duplicated name; changes persisted");
    NSCAssert([machine.configuration[@"SSHEnabled"] boolValue]&&[machine.configuration[@"FileSharingEnabled"] boolValue]&&
        machine.configuration[@"SSHPort"]&&machine.configuration[@"FileSharingPort"],@"Stopped legacy packages gain persistent local forwarding before launch");
    NSDictionary *networkSnapshot=machine.configuration;
    NSCAssert([machine applyPendingChanges:&error]&&[machine.configuration isEqual:networkSnapshot],@"Assigned ports remain stable on later starts");
    NSCAssert(([machine saveSettings:@{@"SSHEnabled":@NO,@"FileSharingEnabled":@NO} error:&error]),@"Users may opt out");
    NSCAssert([machine applyPendingChanges:&error]&&![machine.configuration[@"SSHEnabled"] boolValue]&&![machine.configuration[@"FileSharingEnabled"] boolValue],@"Later boots preserve explicit opt-outs");
    NSCAssert([machine.configuration[@"Description"] isEqual:@"Personal Mac"]&&[VIMMachine loadURL:machine.URL error:nil],@"Fresh reader accepts nameless configuration");
    NSCAssert([[NSString stringWithContentsOfURL:[machine.URL URLByAppendingPathComponent:@"Disks/System.qcow2"] encoding:NSUTF8StringEncoding error:nil] isEqual:@"fixture"],@"Disk contents untouched");
    VIMMachine *legacy=[VIMMachine loadURL:Fixture(root,@"Mojave") error:nil];NSCAssert([legacy applyPendingChanges:&error]&&[legacy.URL.lastPathComponent isEqual:@"Mojave.vintelmac"]&&!legacy.configuration[@"Name"],@"Legacy migration");
    VIMMachine *duplicate=[VIMMachine loadURL:Fixture(root,@"Mojave") error:nil];NSCAssert([duplicate applyPendingChanges:&error]&&[duplicate.name isEqual:@"Mojave 2"],@"Collision-safe migration");
    NSURL *app=[root URLByAppendingPathComponent:@"Install macOS Mojave.app"];
    [fm createDirectoryAtURL:[app URLByAppendingPathComponent:@"Contents/Resources"] withIntermediateDirectories:YES attributes:nil error:nil];
    [fm createDirectoryAtURL:[app URLByAppendingPathComponent:@"Contents/SharedSupport"] withIntermediateDirectories:YES attributes:nil error:nil];
    [@{@"CFBundleIdentifier":@"com.apple.InstallAssistant.Mojave"} writeToURL:[app URLByAppendingPathComponent:@"Contents/Info.plist"] atomically:YES];
    [@{@"System Image Info":@{@"version":@"10.14.6"}} writeToURL:[app URLByAppendingPathComponent:@"Contents/SharedSupport/InstallInfo.plist"] atomically:YES];
    for(NSString *file in @[@"Resources/createinstallmedia",@"SharedSupport/BaseSystem.dmg",@"SharedSupport/InstallESD.dmg"])[@"fixture" writeToURL:[app URLByAppendingPathComponent:[@"Contents/" stringByAppendingString:file]] atomically:YES encoding:NSUTF8StringEncoding error:nil];
    NSCAssert([VIMInstallerAssembly validateInstallerApplication:app guest:@"mojave" error:nil],@"Complete matching app accepted");
    NSCAssert([[VIMInstallerAssembly guestForInstallerApplication:app error:nil] isEqual:@"mojave"],@"Installer detection is independent of UI selection");
    NSCAssert(![VIMInstallerAssembly validateInstallerApplication:app guest:@"catalina" error:nil],@"Different OS rejected");
    [fm removeItemAtURL:[app URLByAppendingPathComponent:@"Contents/SharedSupport/InstallESD.dmg"] error:nil];
    NSCAssert(![VIMInstallerAssembly validateInstallerApplication:app guest:@"mojave" error:nil],@"Incomplete app rejected");
    NSCAssert(![VIMInstallerAssembly guestForInstallerApplication:app error:nil],@"Auto detection still refuses incomplete installers");
    // Simulate a verified guest whose app session disappeared before shutdown.
    NSURL *cleanup=Fixture(root,@"Cleanup");
    NSURL *media=[cleanup URLByAppendingPathComponent:@"Installation/Installer.iso"];
    [fm createDirectoryAtURL:media.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
    [@"fixture" writeToURL:media atomically:YES encoding:NSUTF8StringEncoding error:nil];
    NSMutableDictionary *config=[[NSDictionary dictionaryWithContentsOfURL:[cleanup URLByAppendingPathComponent:@"Machine.plist"]] mutableCopy];
    config[@"InstallerMedia"]=@"Installation/Installer.iso";config[@"InstallState"]=@"installing";
    [config writeToURL:[cleanup URLByAppendingPathComponent:@"Machine.plist"] atomically:YES];
    [@{@"FormatVersion":@1,@"MachineIdentifier":config[@"Identifier"],@"Media":@"Installation/Installer.iso",@"Size":@7}
        writeToURL:[cleanup URLByAppendingPathComponent:@"Installation/Ownership.plist"] atomically:YES];
    NSDictionary *report=@{@"InstalledSystemCandidate":@YES,@"LiveRoot":@YES,@"KernelMatchesSystem":@YES,@"Guest":@"mojave",
        @"VolumeUUID":@"volume",@"BootSessionUUID":@"boot",@"ProductBuildVersion":@"18G103"};
    NSMutableDictionary *verified=[report mutableCopy];verified[@"TransactionID"]=@"transaction";
    verified[@"VerifiedAfterReboot"]=@YES;verified[@"MetalCommandVerified"]=@YES;
    [@{@"Phase":@"graphics-verified",@"TransactionID":@"transaction",@"PostBootVerification":verified}
        writeToURL:[cleanup URLByAppendingPathComponent:@"State/GuestInstallation.plist"] atomically:YES];
    [@{@"Report":report} writeToURL:[cleanup URLByAppendingPathComponent:@"State/GuestStatus.plist"] atomically:YES];
    VIMMachine *recover=[VIMMachine loadURL:cleanup error:&error];NSCAssert(recover,@"Cleanup fixture loads: %@",error);
    fd=open([cleanup URLByAppendingPathComponent:@"State/Session.lock"].fileSystemRepresentation,O_CREAT|O_RDWR,0600);
    NSCAssert(fd>=0&&flock(fd,LOCK_EX|LOCK_NB)==0,@"Own live cleanup session");
    NSCAssert(![recover retryInstallerCleanup:&error]&&[fm fileExistsAtPath:media.path],@"Never delete live installer media");
    flock(fd,LOCK_UN);close(fd);
    NSMutableDictionary *wrong=[report mutableCopy];wrong[@"VolumeUUID"]=@"other-volume";
    [@{@"Report":wrong} writeToURL:[cleanup URLByAppendingPathComponent:@"State/GuestStatus.plist"] atomically:YES];
    NSCAssert(![recover retryInstallerCleanup:&error]&&[fm fileExistsAtPath:media.path],@"Reject mismatching persisted evidence");
    NSMutableDictionary *laterBoot=[report mutableCopy];laterBoot[@"BootSessionUUID"]=@"later-ordinary-boot";
    [@{@"Report":laterBoot} writeToURL:[cleanup URLByAppendingPathComponent:@"State/GuestStatus.plist"] atomically:YES];
    NSCAssert([recover retryInstallerCleanup:&error],@"Recover cleanup after another ordinary boot without in-memory session: %@",error);
    NSCAssert(![fm fileExistsAtPath:media.path]&&!recover.configuration[@"InstallerMedia"],@"Owned installer removed and detached");
    NSCAssert([recover retryInstallerCleanup:&error],@"Cleanup recovery is idempotent");
    [NSUserDefaults.standardUserDefaults setBool:YES forKey:@"VIMKeepInstallers"];
    [@"fixture" writeToURL:media atomically:YES encoding:NSUTF8StringEncoding error:nil];
    [config writeToURL:[cleanup URLByAppendingPathComponent:@"Machine.plist"] atomically:YES];
    VIMMachine *retaining=[VIMMachine loadURL:cleanup error:&error];
    NSCAssert([retaining finishInstallationWithGuestReport:report error:&error]&&[fm fileExistsAtPath:media.path]&&!retaining.configuration[@"InstallerMedia"],@"Retention still detaches the verified installer without deleting it");
    // Cache lookup is opt-in, OS-specific and requires an exact completed size.
    NSURL *attempt=[root URLByAppendingPathComponent:[@"Installers/" stringByAppendingString:NSUUID.UUID.UUIDString]];
    NSURL *cachedMedia=[attempt URLByAppendingPathComponent:@"Media/Installer.iso"];
    [fm createDirectoryAtURL:cachedMedia.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
    [@"fixture" writeToURL:cachedMedia atomically:YES encoding:NSUTF8StringEncoding error:nil];
    NSMutableDictionary *cacheReceipt=[@{@"FormatVersion":@1,@"Guest":@"mojave",@"Stage":@"media-created",@"Media":@"Installer.iso",@"Size":@7} mutableCopy];
    NSURL *cacheRecord=[cachedMedia.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"Media.plist"];
    [cacheReceipt writeToURL:cacheRecord atomically:YES];
    NSDictionary *retainedMedia=[VIMInstallerAssembly retainedInstallerForGuest:@"mojave" library:root];
    NSCAssert([[[retainedMedia[@"Media"] URLByResolvingSymlinksInPath] path] isEqual:cachedMedia.URLByResolvingSymlinksInPath.path],@"Reuse retained finished media: %@ expected %@",retainedMedia,cachedMedia);
    NSCAssert(![VIMInstallerAssembly retainedInstallerForGuest:@"catalina" library:root][@"Media"],@"Never reuse another OS");
    cacheReceipt[@"Size"]=@8;[cacheReceipt writeToURL:cacheRecord atomically:YES];
    NSCAssert(![VIMInstallerAssembly retainedInstallerForGuest:@"mojave" library:root][@"Media"],@"Reject truncated media");
    [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"VIMKeepInstallers"];
    cacheReceipt[@"Size"]=@7;[cacheReceipt writeToURL:cacheRecord atomically:YES];
    NSCAssert(![VIMInstallerAssembly retainedInstallerForGuest:@"mojave" library:root][@"Media"],@"Retention/reuse is opt-in");
    NSURL *download=[root URLByAppendingPathComponent:@"Downloads/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/Payload.pkg"];
    [fm createDirectoryAtURL:download.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil];
    [@"fixture" writeToURL:download atomically:YES encoding:NSUTF8StringEncoding error:nil];
    [@{@"FormatVersion":@1,@"SourceURL":@"https://swcdn.apple.com/fixture.pkg",@"Complete":@YES,@"Length":@7} writeToURL:[download.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"Download.plist"] atomically:YES];
    int downloadLock=open([download.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"Download.lock"].fileSystemRepresentation,O_CREAT|O_RDWR,0600);
    flock(downloadLock,LOCK_EX|LOCK_NB);
    [VIMInstallerAssembly retireCompletedDownloads:@{@"assistant":download}];
    NSCAssert([fm fileExistsAtPath:download.path],@"Active transfer cannot be retired");
    close(downloadLock);[VIMInstallerAssembly retireCompletedDownloads:@{@"assistant":download,@"user":cachedMedia}];
    NSCAssert(![fm fileExistsAtPath:download.path]&&[fm fileExistsAtPath:cachedMedia.path],@"Retire completed payload only, never caller-owned media");
    if(previousKeep)[NSUserDefaults.standardUserDefaults setObject:previousKeep forKey:@"VIMKeepInstallers"];else [NSUserDefaults.standardUserDefaults removeObjectForKey:@"VIMKeepInstallers"];
    [fm removeItemAtURL:root error:nil];puts("PASS: live staging, safe deferred rename, filename-only identity, legacy/collision migration, installer matching");
}return 0;}
