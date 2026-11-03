// Cold native Darwin8/9 client loading. The original system smbfs.kext is never
// replaced, unloaded, or cache-patched. An already loaded different client
// remains in use until the ordinary setup restart. No resident process.
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
#include "opengl-boot-identity.h"
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
static NSString *const ClientBinary=@"tiger-smbfs",*const ClientInfo=@"TigerSMBClient-Info.plist",*const ClientRecord=@"TigerSMBClient.plist",*const ClientVersion=@"1.3.7";
#else
static NSString *const ClientBinary=@"leopard-smbfs",*const ClientInfo=@"LeopardSMBClient-Info.plist",*const ClientRecord=@"LeopardSMBClient.plist",*const ClientVersion=@"1.4.6";
#endif
static NSArray *ClientFiles(void){
    return [NSArray arrayWithObjects:ClientBinary,ClientInfo,
        @"SMB-COPYRIGHT.txt",@"SMB-VIM-LICENSE.txt",ClientRecord,nil];
}
static BOOL ClientPayload(NSString *directory){
    NSDictionary *record=[NSDictionary dictionaryWithContentsOfFile:[directory stringByAppendingPathComponent:ClientRecord]];
    NSDictionary *info=[NSDictionary dictionaryWithContentsOfFile:[directory stringByAppendingPathComponent:ClientInfo]];
    if(![[record objectForKey:@"Guest"] isEqual:Guest]||
       ![[record objectForKey:@"FormatVersion"] isEqual:[NSNumber numberWithInt:1]]||
       ![[record objectForKey:@"PolicyVersion"] isEqual:[NSNumber numberWithInt:2]]||
       ![[info objectForKey:@"CFBundleIdentifier"] isEqual:@"com.apple.filesystems.smbfs"]||
       ![[info objectForKey:@"CFBundleExecutable"] isEqual:@"smbfs"]||
       ![[info objectForKey:@"CFBundleVersion"] isEqual:ClientVersion])return NO;
    NSEnumerator *enumerator=[ClientFiles() objectEnumerator];NSString *name;
    while((name=[enumerator nextObject]))if(!File([directory stringByAppendingPathComponent:name],NO))return NO;
    return YES;
}
static NSString *ClientBoot(void){
    // The cold launch daemon can run before our serial agent. PID1's native
    // kernel birth record exists already and is not recomputed when a user
    // adjusts the wall clock. Reuse the measured Darwin8 process-info seam.
    VIMOpenGLProcessInfo process;
    if(!VIMOpenGLReadProcessBirth(1,&process)||process.pbi_uid)return nil;
    return [NSString stringWithFormat:@"%llu.%06llu",
        VIMOpenGLAgentStartSeconds(&process),VIMOpenGLAgentStartMicroseconds(&process)];
}
static BOOL ClientResident(void){
    int version=0;size_t size=sizeof(version);
    return !sysctlbyname("net.smb.fs.vim_client",&version,&size,NULL,0)&&size==sizeof(version)&&version==2;
}
static int RepairClient(NSDictionary *owner,NSString *build,BOOL *restart){
    NSString *root=[Tools stringByAppendingPathComponent:@"NativeSMBClient"];
    NSString *bundle=[root stringByAppendingPathComponent:@"smbfs.kext"];
    NSString *contents=[bundle stringByAppendingPathComponent:@"Contents"];
    NSString *executables=[contents stringByAppendingPathComponent:@"MacOS"];
    NSString *binary=[executables stringByAppendingPathComponent:@"smbfs"];
    NSString *info=[contents stringByAppendingPathComponent:@"Info.plist"];
    NSString *receiptPath=[State stringByAppendingPathComponent:@"FileSharingClient.plist"];
    NSDictionary *prior=File(receiptPath,YES)?[NSDictionary dictionaryWithContentsOfFile:receiptPath]:nil;
    if(prior&&!SameOwner(prior,owner))return Error("another transaction owns the native SMB client state");
    NSString *boot=ClientBoot(),*expected=Digest([Tools stringByAppendingPathComponent:ClientBinary]);
    if(!boot||!expected||!ClientPayload(Tools))return Error("native SMB client support is unavailable");
    if([prior objectForKey:@"LoadingBoot"]&&[[prior objectForKey:@"InstalledSHA256"] isEqual:expected])
        return Error("a previous native SMB client load was interrupted; its recovery state was retained");
    NSDictionary *native=[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/Extensions/smbfs.kext/Contents/Info.plist"];
    NSDictionary *candidate=[NSDictionary dictionaryWithContentsOfFile:[Tools stringByAppendingPathComponent:ClientInfo]];
    // Match the genuine installed client's ABI declaration, not a particular
    // OS build number. A changed native ABI remains untouched for diagnosis.
    if(![[native objectForKey:@"CFBundleIdentifier"] isEqual:[candidate objectForKey:@"CFBundleIdentifier"]]||
       ![[native objectForKey:@"CFBundleVersion"] isEqual:[candidate objectForKey:@"CFBundleVersion"]]||
       ![[native objectForKey:@"OSBundleLibraries"] isEqual:[candidate objectForKey:@"OSBundleLibraries"]])
        return Error("this system has a different native SMB client ABI");
    if(!Directory(root,0755)||!Directory(bundle,0755)||!Directory(contents,0755)||!Directory(executables,0755))
        return Error("the private native SMB client directory is unavailable");
    BOOL changed=![Digest(binary) isEqual:expected]||![Digest(info) isEqual:Digest([Tools stringByAppendingPathComponent:ClientInfo])];
    if(changed&&(!Copy([Tools stringByAppendingPathComponent:ClientBinary],binary,0644)||
                 !Copy([Tools stringByAppendingPathComponent:ClientInfo],info,0644)))
        return Error("the private native SMB client could not be published");
    // Validation uses the guest's own loader and original relocatable kernel
    // object. Modern codesign must not rewrite it into a later kext format.
    if(changed&&!Run(@"/sbin/kextload",[NSArray arrayWithObjects:@"-n",@"-t",bundle,nil]))
        return Error("the native loader rejected the private SMB client");
    NSData *loaded=Run(@"/usr/sbin/kextstat",[NSArray arrayWithObjects:@"-l",@"-b",@"com.apple.filesystems.smbfs",nil]);
    if(!loaded)return Error("native SMB client load state is unavailable");
    BOOL present=[loaded length]>0;
    BOOL ours=present&&ClientResident()&&[[prior objectForKey:@"LoadedBoot"] isEqual:boot]&&
        [[prior objectForKey:@"LoadedSHA256"] isEqual:expected];
    NSMutableDictionary *record=[NSMutableDictionary dictionaryWithObjectsAndKeys:
        [NSNumber numberWithInt:1],@"FormatVersion",Guest,@"Guest",build,@"ProductBuildVersion",
        [owner objectForKey:@"MachineIdentifier"],@"MachineIdentifier",
        [owner objectForKey:@"VolumeUUID"],@"VolumeUUID",expected,@"InstalledSHA256",nil];
    *restart=present&&!ours;
    if(*restart){
        NSString *deferred=[prior objectForKey:@"DeferredBoot"];
        if(deferred&&![deferred isEqual:boot]&&[[prior objectForKey:@"InstalledSHA256"] isEqual:expected])
            return Error("another SMB client still loads first after restart; the active filesystem was left untouched");
        [record setObject:boot forKey:@"DeferredBoot"];
    }
    if(!present){
        // A panic never clears this marker. Do not repeat a suspect load on
        // every boot; the untouched system client remains available instead.
        [record setObject:boot forKey:@"LoadingBoot"];
        if(!Save(record,receiptPath,0600))return Error("the native SMB client load record could not be saved");
        if(!Run(@"/sbin/kextload",[NSArray arrayWithObject:bundle])){
            [record removeObjectForKey:@"LoadingBoot"];Save(record,receiptPath,0600);
            return Error("the private native SMB client could not start");
        }
        loaded=Run(@"/usr/sbin/kextstat",[NSArray arrayWithObjects:@"-l",@"-b",@"com.apple.filesystems.smbfs",nil]);
        if(![loaded length]||!ClientResident())return Error("the private native SMB client did not become available; the active filesystem was left untouched");
        ours=YES;
    }
    [record removeObjectForKey:@"LoadingBoot"];
    if(ours){[record setObject:boot forKey:@"LoadedBoot"];[record setObject:expected forKey:@"LoadedSHA256"];}
    [record setObject:[NSNumber numberWithBool:*restart] forKey:@"RestartRequired"];
    if(!Save(record,receiptPath,0600))return Error("the native SMB client receipt could not be saved");
    return 0;
}
#endif
