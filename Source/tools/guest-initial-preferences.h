// One-time defaults for an account created by the native host installer.
// Called explicitly after its first desktop login, never by a read-only probe.
// -1: target desktop is not active yet; 0: already done; 1: changed.
#import <SystemConfiguration/SystemConfiguration.h>

static BOOL HostInstalledSMBDomainReady(void) {
    SCDynamicStoreRef store=SCDynamicStoreCreate(NULL,CFSTR("VirtualIntelMacFirstDesktop"),NULL,NULL);
    id value=store?CFBridgingRelease(SCDynamicStoreCopyValue(store,CFSTR("com.apple.smb"))):nil;
    if(store)CFRelease(store);
    id name=[value isKindOfClass:NSDictionary.class]?value[@"NetBIOSName"]:nil;
    return [name isKindOfClass:NSString.class]&&[name length]>0;
}

static NSInteger PrepareHostInstalledUserPreferences(NSDictionary *admission) {
    NSString *path=@"/private/var/db/VirtualIntelMac/LegacySetup/Account.plist";
    for(NSString *item in @[@"/private/var/db/VirtualIntelMac",path.stringByDeletingLastPathComponent,path]) {
        struct stat info;
        Check(!lstat(item.fileSystemRepresentation,&info)&&!S_ISLNK(info.st_mode)&&
            info.st_uid==0&&!(info.st_mode&0022),@"The host-created account preferences are unavailable");
    }
    struct stat file;
    Check(!lstat(path.fileSystemRepresentation,&file)&&S_ISREG(file.st_mode)&&file.st_nlink==1,
        @"The host-created account record is not a regular file");
    NSMutableDictionary *account=[[NSDictionary dictionaryWithContentsOfFile:path] mutableCopy];
    Check([account[@"FirstLoginPolicyPrepared"] isEqual:@YES]&&
        [account[@"InitialUserPreferencesPrepared"] isKindOfClass:NSNumber.class]&&
        [account[@"VolumeUUID"] isEqual:admission[@"VolumeUUID"]],@"No initial user-preference policy was staged for this system");
    // Do not overwrite preferences changed by the user after initial setup.
    if([account[@"InitialUserPreferencesPrepared"] boolValue])return NO;
    NSString *name=account[@"Username"];
    struct passwd *user=[name isKindOfClass:NSString.class]?getpwnam(name.UTF8String):NULL;
    struct stat console;
    Check(user&&user->pw_uid>=501&&user->pw_uid==[account[@"UID"] unsignedIntValue],
        @"The host-created account identity is unavailable");
    if(stat("/dev/console",&console)||console.st_uid!=user->pw_uid)return -1;
    // Native first-boot publication can precede the computer-name defaults.
    // Ventura then has an SMB listener and valid credentials, but digest-service
    // rejects NTLM because com.apple.smb lacks its live NetBIOSName. Let Apple's
    // publisher reconcile it after this account's first desktop is available;
    // don't invent a dynamic-store dictionary, reset credentials or enable a
    // service the user disabled. Already-completed account policies stay alone.
    if([account[@"HostStagedFileSharing"] isEqual:@YES]&&!HostInstalledSMBDomainReady()) {
        NSTask *sharing=[NSTask new];
        sharing.executableURL=[NSURL fileURLWithPath:@"/usr/libexec/smb-sync-preferences"];
        sharing.standardInput=NSFileHandle.fileHandleWithNullDevice;
        sharing.standardOutput=NSFileHandle.fileHandleWithNullDevice;
        sharing.standardError=NSFileHandle.fileHandleWithNullDevice;
        NSError *failure=nil;
        Check([sharing launchAndReturnError:&failure],failure.localizedDescription?:@"Could not prepare native file sharing");
        [sharing waitUntilExit];
        // Actual publication is authoritative: the stock tool can report an
        // already-loaded smbd without losing the successfully published domain.
        if(!HostInstalledSMBDomainReady())return -1;
    }
    // CFPreferences selects the guest's actual host identity and account home.
    // Never guess the ByHost filename from the VM bundle's UUID.
    CFStringRef domain=CFSTR("com.apple.screensaver"),owner=(__bridge CFStringRef)name;
    NSDictionary *values=@{@"idleTime":@0,@"askForPassword":@NO,@"askForPasswordDelay":@0};
    for(NSString *key in values)for(id host in @[(__bridge id)kCFPreferencesCurrentHost,(__bridge id)kCFPreferencesAnyHost])
        CFPreferencesSetValue((__bridge CFStringRef)key,(__bridge CFPropertyListRef)values[key],domain,owner,(__bridge CFStringRef)host);
    Check(CFPreferencesSynchronize(domain,owner,kCFPreferencesCurrentHost)&&
        CFPreferencesSynchronize(domain,owner,kCFPreferencesAnyHost),@"Could not save the initial screen-saver preferences");
    for(NSString *key in values) {
        id value=CFBridgingRelease(CFPreferencesCopyValue((__bridge CFStringRef)key,domain,owner,kCFPreferencesCurrentHost));
        Check([value isKindOfClass:NSNumber.class]&&[value doubleValue]==0,@"The initial screen-saver preferences did not take effect");
    }
    // pmset is the OS's own cold preference writer. No shell, background loop,
    // host preferences or private PowerManagement ABI is involved.
    NSTask *power=[NSTask new];power.executableURL=[NSURL fileURLWithPath:@"/usr/bin/pmset"];
    power.arguments=@[@"-a",@"sleep",@"0",@"displaysleep",@"0",@"disksleep",@"0"];
    power.standardInput=NSFileHandle.fileHandleWithNullDevice;
    power.standardOutput=NSFileHandle.fileHandleWithNullDevice;
    power.standardError=NSFileHandle.fileHandleWithNullDevice;
    NSError *error=nil;
    Check([power launchAndReturnError:&error],error.localizedDescription?:@"Could not prepare the initial energy settings");
    [power waitUntilExit];Check(power.terminationStatus==0,@"Could not save the initial energy settings");
    account[@"InitialUserPreferencesPrepared"]=@YES;
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:account format:NSPropertyListXMLFormat_v1_0 options:0 error:&error];
    Check(data&&[data writeToFile:path options:NSDataWritingAtomic error:&error],error.localizedDescription);
    Check(!chmod(path.fileSystemRepresentation,0600),@"Could not retain the private account-record permissions");
    return YES;
}
