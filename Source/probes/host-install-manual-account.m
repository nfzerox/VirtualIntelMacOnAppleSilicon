// Exercise the shipping account-staging function, not a separate model of it.
#define main VIMHostInstallWorkerMain
#import "../tools/HostInstall/install-worker.m"
#undef main
#import "../compat/VIMHostUpdatePolicy.h"
#import "../tools/opengl-system-version.h"

static NSURL *ProbeUpdateInput(NSURL *root,NSDictionary *step) {
    NSURL *role=At(root,[step[@"Role"] stringByAppendingPathExtension:@"pkg"]);
    return [NSFileManager.defaultManager fileExistsAtPath:role.path]?role:
        At(root,[step[@"Name"] stringByAppendingPathExtension:@"pkg"]);
}

int main(int argc,const char *argv[]) { @autoreleasepool {
    if(argc==2&&!strcmp(argv[1],"--initial-screen-policy")) {
        for(NSString *guest in @[@"leopard",@"snowleopard",@"lion",@"mountainlion",@"mavericks",@"yosemite",@"elcapitan",@"sierra",@"highsierra",@"mojave",@"catalina",@"bigsur",@"monterey",@"ventura",@"sonoma",@"sequoia",@"tahoe"]) {
            VIMInstallTarget *target=[VIMInstallTarget new];target.metadata=[NSMutableDictionary new];
            target.root=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString] isDirectory:YES];
            NSString *templates=VIMIsAssistantGuest(guest)?@"Library/User Template":@"System/Library/User Template";
            [target directory:[templates stringByAppendingPathComponent:@"English.lproj/Library/Preferences"] mode:0700];
            [target directory:@"private/var/db/dslocal/nodes/Default/users" mode:0700];
            [target plist:@"private/var/db/dslocal/nodes/Default/groups/admin.plist"
                value:@{@"name":@[@"admin"],@"users":@[@"root"],@"groupmembers":@[]} mode:0600];
            [target plist:@"System/Library/CoreServices/SystemVersion.plist"
                value:@{@"ProductVersion":@"10.13.6",@"ProductBuildVersion":@"fixture"} mode:0644];
            StageAccount(target,target.root,@{@"Guest":guest,@"Enabled":@YES,@"AutomaticLogin":@NO,
                @"MachineIdentifier":NSUUID.UUID.UUIDString,@"VolumeUUID":NSUUID.UUID.UUIDString},
                @"initialtest",[@"fixture-only" dataUsingEncoding:NSUTF8StringEncoding]);
            NSDictionary *record=Plist(At(target.root,[SetupState(guest) stringByAppendingPathComponent:@"Account.plist"]));
            NSCAssert([record[@"InitialUserPreferencesPrepared"] isEqual:@NO],@"%@ stages explicit first-login intent",guest);
            NSDictionary *prefs=Plist(At(target.root,@"Users/initialtest/Library/Preferences/com.apple.screensaver.plist"));
            NSCAssert([prefs[@"idleTime"] isEqual:@0],@"%@ retains offline defaults too",guest);
            NSError *error=nil;NSCAssert([NSFileManager.defaultManager removeItemAtURL:target.root error:&error],@"%@",error);
        }
        puts("PASS new automatic accounts stage initial preferences across all DSLocal guest families");return 0;
    }
    if(argc==2&&!strcmp(argv[1],"--authenticated-home")) {
        for(id kind in @[[NSNull null],@"SALTED-SHA512",@"SALTED-SHA512-PBKDF2"]) {
            VIMInstallTarget *target=[VIMInstallTarget new];
            target.root=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:
                [@"VIM-HomeSharing-" stringByAppendingString:NSUUID.UUID.UUIDString]] isDirectory:YES];
            target.metadata=[NSMutableDictionary new];
            NSString *node=@"private/var/db/dslocal/nodes/Default";
            [target directory:[node stringByAppendingPathComponent:@"users"] mode:0700];
            [target plist:[node stringByAppendingPathComponent:@"groups/admin.plist"]
                value:@{@"name":@[@"admin"],@"users":@[@"root"],@"groupmembers":@[]} mode:0600];
            NSDictionary *existing=@{@"name":@[@"Existing Share"],@"directory_path":@[@"/Shared"]};
            NSString *kept=[node stringByAppendingPathComponent:@"sharepoints/existing.plist"];
            [target plist:kept value:existing mode:0600];
            NSString *algorithm=kind==[NSNull null]?nil:kind;
            uint32_t uid=StageDSLocalAccount(target,@"sharetest",NSUUID.UUID.UUIDString,YES,
                [@"123456" dataUsingEncoding:NSUTF8StringEncoding],algorithm);
            NSDictionary *user=Plist(At(target.root,[node stringByAppendingPathComponent:@"users/sharetest.plist"]));
            NSString *expectedHome=[@"/Users" stringByAppendingPathComponent:@"sharetest"];
            NSCAssert(uid==501&&[user[@"home"] isEqual:@[expectedHome]],@"Native home and UID are preserved");
            NSCAssert([user[@"authentication_authority"][0] containsString:@"SMB-NT"],@"Keep SMB account authentication");
            if(algorithm) {
                NSDictionary *hash=[NSPropertyListSerialization propertyListWithData:user[@"ShadowHashData"][0]
                    options:0 format:NULL error:NULL];
                NSCAssert([hash[@"NT"] length]==16&&hash[algorithm],@"Keep both native login and SMB password material");
                NSData *keyset=user[@"KerberosKeys"][0];
                NSCAssert(keyset.length==42&&[[keyset subdataWithRange:NSMakeRange(26,16)] isEqual:hash[@"NT"]],
                    @"Local Heimdal receives the same realm-independent NT key before first boot");
            } else {
                NSCAssert(!user[@"KerberosKeys"],@"Pre-Heimdal guests keep their original native sharing recipe");
            }
            for(NSString *directory in @[@"sharepoints",@"config/SharePoints"])
                NSCAssert(![NSFileManager.defaultManager fileExistsAtPath:At(target.root,
                    [node stringByAppendingPathComponent:[directory stringByAppendingPathComponent:@"sharetest.plist"]]).path],@"Do not publish the home as an explicit share");
            NSCAssert([Plist(At(target.root,kept)) isEqual:existing],@"Existing user shares are untouched");
            NSError *error=nil;NSCAssert([NSFileManager.defaultManager removeItemAtURL:target.root error:&error],@"Fixture cleanup: %@",error);
        }
        puts("PASS actual account staging keeps SMB credentials and existing shares without home SharePoints");return 0;
    }
    if(argc==4&&!strcmp(argv[1],"--catalog-bom")) {
        VIMInstallTarget *target=[VIMInstallTarget new];target.root=[NSURL fileURLWithPath:@(argv[2]) isDirectory:YES];
        target.metadata=[ReadBOM([NSURL fileURLWithPath:@(argv[3])]) mutableCopy];
        @try {NSDictionary *permissions=[target catalogPermissions];
            printf("PASS: %lu normalized installed paths, %lu catalog records\n",
                (unsigned long)target.metadata.count,(unsigned long)permissions.count);return 0;}
        @catch(NSException *failure){fprintf(stderr,"%s\n",failure.reason.UTF8String);return 1;}
    }
    if(argc==3&&!strcmp(argv[1],"--appledouble")) {
        VIMInstallTarget *target=[VIMInstallTarget new];target.root=[NSURL fileURLWithPath:@(argv[2]) isDirectory:YES];
        target.metadata=[NSMutableDictionary new];
        [target file:@"locversion.plist" data:NSData.data mode:0644];
        Require(!chmod(At(target.root,@"locversion.plist").fileSystemRepresentation,0644),@"fixture mode");
        [target.metadata removeObjectForKey:@"locversion.plist"];
        target.metadata[@"._locversion.plist"]=@[@0,@80,@(S_IFREG|0664)];
        [target file:@"known" data:[@"original data" dataUsingEncoding:NSUTF8StringEncoding] mode:0600];
        target.metadata[@"known"]=@[@123,@45,@(S_IFREG|0600)];
        target.metadata[@"._known"]=@[@0,@80,@(S_IFREG|0664)];
        [target file:@"._ordinary" data:[@"not consumed" dataUsingEncoding:NSUTF8StringEncoding] mode:0644];
        [target file:@"private/data" data:NSData.data mode:0644];
        Require(!chmod(At(target.root,@"private/data").fileSystemRepresentation,0644),@"nested fixture mode");
        [target.metadata removeObjectForKey:@"private/data"];
        target.metadata[@"private/._data"]=@[@0,@80,@(S_IFREG|0664)];
        target.metadata[@"private"]=@[@0,@0,@(S_IFDIR)];
        Require(!chmod(At(target.root,@"private").fileSystemRepresentation,0),@"private fixture mode");
        NSDictionary *permissions=[target catalogPermissions];
        NSCAssert((permissions.count==5&&
            [target.metadata[@"locversion.plist"] isEqual:@[@0,@80,@(S_IFREG|0644)]]&&
            [target.metadata[@"private/data"] isEqual:@[@0,@80,@(S_IFREG|0644)]]&&
            [target.metadata[@"known"] isEqual:@[@123,@45,@(S_IFREG|0600)]]&&
            !target.metadata[@"._locversion.plist"]&&!target.metadata[@"._known"]&&target.metadata[@"._ordinary"]),
            @"Merged sidecars preserve actual files and their logical identities");
        for(NSString *missing in @[@"missing",@"._missing"]) {
            target.metadata[missing]=@[@0,@0,@(S_IFREG|0644)];BOOL rejected=NO;
            @try{[target catalogPermissions];}@catch(NSException *failure){(void)failure;rejected=YES;}
            NSCAssert(rejected,@"A missing ordinary file or unmerged sidecar still fails");
            [target.metadata removeObjectForKey:missing];
        }
        Require(!chmod(At(target.root,@"private").fileSystemRepresentation,0700),@"fixture cleanup mode");
        puts("PASS: actual shipping catalog handles AppleDouble-created data forks and preserves missing-file failures");return 0;
    }
    if(argc==3&&!strcmp(argv[1],"--cleanup-recovery")) {
        @try {RemoveRecoveryWork([NSURL fileURLWithPath:@(argv[2])]);return 0;}
        @catch(NSException *failure){fprintf(stderr,"%s\n",failure.reason.UTF8String);return 1;}
    }
    if(argc==4&&!strcmp(argv[1],"--write-update-receipt")) {
        VIMInstallTarget *target=[VIMInstallTarget new];target.root=[NSURL fileURLWithPath:@(argv[2]) isDirectory:YES];
        target.metadata=[NSMutableDictionary new];
        @try {
            NSMutableDictionary *package=[Plist([NSURL fileURLWithPath:@(argv[3])]) mutableCopy];
            if(package[@"BOM"])package[@"BOM"]=[NSURL fileURLWithPath:package[@"BOM"]];
            WriteFlatUpdateReceipt(target,package);return 0;
        } @catch(NSException *failure) {fprintf(stderr,"%s\n",failure.reason.UTF8String);return 1;}
    }
    if(argc==4&&(!strcmp(argv[1],"--validate-highsierra-recovery")||!strcmp(argv[1],"--validate-mojave-recovery")||
                !strcmp(argv[1],"--validate-catalina-recovery"))) {
        BOOL mojave=!strcmp(argv[1],"--validate-mojave-recovery");
        BOOL catalina=!strcmp(argv[1],"--validate-catalina-recovery");
        @try {ValidateUpdatedRecovery([NSURL fileURLWithPath:@(argv[2])],[NSURL fileURLWithPath:@(argv[3])],
            catalina?@"10.15.8":mojave?@"10.14.6":@"10.13.6",catalina?@"19H2036":mojave?@"18G9323":@"17G14042");return 0;}
        @catch(NSException *failure) {fprintf(stderr,"%s\n",failure.reason.UTF8String);return 1;}
    }
    if(argc==5&&!strcmp(argv[1],"--convert-period-apfs")) {
        BOOL attached=NO;
        @try {ConvertToPeriodAPFS([NSURL fileURLWithPath:@(argv[2])],[NSURL fileURLWithPath:@(argv[3])],
            [NSURL fileURLWithPath:@(argv[4])],&attached);return 0;}
        @catch(NSException *failure) {fprintf(stderr,"%s\n",failure.reason.UTF8String);return 1;}
    }
    if(argc==5&&!strcmp(argv[1],"--prepare-period-tool")) {
        @try {
            PreparePeriodTool([NSURL fileURLWithPath:@(argv[2])],[NSURL fileURLWithPath:@(argv[3])],
                [NSURL fileURLWithPath:@(argv[4])]);return 0;
        } @catch(NSException *failure) {fprintf(stderr,"%s\n",failure.reason.UTF8String);return 1;}
    }
    if(argc==3&&!strcmp(argv[1],"--cleanup-performance")) {
        VIMInstallTarget *target=[VIMInstallTarget new];target.root=[NSURL fileURLWithPath:@(argv[2]) isDirectory:YES];
        if([NSFileManager.defaultManager contentsOfDirectoryAtPath:target.root.path error:nil].count)return 64;
        target.metadata=[NSMutableDictionary new];
        @try {
            NSArray *mode=@[@0,@0,@(S_IFREG|0644)];
            for(unsigned i=0;i<200000;i++)target.metadata[[NSString stringWithFormat:@"unchanged/%u",i]]=mode;
            for(unsigned i=0;i<2000;i++)[target file:[NSString stringWithFormat:@"obsolete/%u",i] data:NSData.data mode:0644];
            CFAbsoluteTime start=CFAbsoluteTimeGetCurrent();
            for(unsigned i=0;i<2000;i++)RemoveLeopardUpdatePath(target,[NSString stringWithFormat:@"obsolete/%u",i],YES);
            double elapsed=CFAbsoluteTimeGetCurrent()-start;
            RemoveLeopardUpdatePath(target,@"obsolete",NO);
            NSData *data=[NSPropertyListSerialization dataWithPropertyList:@{@"Seconds":@(elapsed),
                @"MetadataRemaining":@(target.metadata.count),@"FilesRemaining":@([NSFileManager.defaultManager
                    contentsOfDirectoryAtPath:target.root.path error:nil].count)}
                format:NSPropertyListXMLFormat_v1_0 options:0 error:nil];
            [NSFileHandle.fileHandleWithStandardOutput writeData:data];return 0;
        } @catch(NSException *failure) {fprintf(stderr,"%s\n",failure.reason.UTF8String);return 1;}
    }
    if(argc==5&&!strcmp(argv[1],"--prepare-recovery")) {
        BOOL attached=NO;
        @try {
            StageRecovery([NSURL fileURLWithPath:@(argv[2])],[NSURL fileURLWithPath:@(argv[3])],
                [NSURL fileURLWithPath:@(argv[4])],nil,StartupCache,&attached);return 0;
        } @catch(NSException *failure) {fprintf(stderr,"%s\n",failure.reason.UTF8String);return 1;}
    }
    if(argc==4&&!strcmp(argv[1],"--mountainlion-security-actions")) {
        VIMInstallTarget *target=[VIMInstallTarget new];target.root=[NSURL fileURLWithPath:@(argv[2]) isDirectory:YES];
        target.metadata=[NSMutableDictionary new];
        @try {
            MountainLionSecurityActions(target,@{@"Folder":[NSURL fileURLWithPath:@(argv[3]) isDirectory:YES]});return 0;
        } @catch(NSException *failure) {fprintf(stderr,"%s\n",failure.reason.UTF8String);return 1;}
    }
    if(argc==4&&!strcmp(argv[1],"--mavericks-security-actions")) {
        VIMInstallTarget *target=[VIMInstallTarget new];target.root=[NSURL fileURLWithPath:@(argv[2]) isDirectory:YES];
        target.metadata=[NSMutableDictionary new];
        @try {
            MavericksSecurityActions(target,@{@"Folder":[NSURL fileURLWithPath:@(argv[3]) isDirectory:YES]});return 0;
        } @catch(NSException *failure) {fprintf(stderr,"%s\n",failure.reason.UTF8String);return 1;}
    }
    if(argc==4&&(!strcmp(argv[1],"--yosemite-security-before")||!strcmp(argv[1],"--yosemite-security-after"))) {
        VIMInstallTarget *target=[VIMInstallTarget new];target.root=[NSURL fileURLWithPath:@(argv[2]) isDirectory:YES];
        target.metadata=[NSMutableDictionary new];
        @try {
            NSDictionary *package=@{@"Folder":[NSURL fileURLWithPath:@(argv[3]) isDirectory:YES]};
            if(!strcmp(argv[1],"--yosemite-security-before"))OSSecurityCleanup(target,package,YES,@"yosemite");
            else YosemiteSecurityActions(target,package);
            return 0;
        } @catch(NSException *failure) {fprintf(stderr,"%s\n",failure.reason.UTF8String);return 1;}
    }
    if(argc==4&&(!strcmp(argv[1],"--elcapitan-security-before")||!strcmp(argv[1],"--elcapitan-security-after")||
                !strcmp(argv[1],"--sierra-security-before")||!strcmp(argv[1],"--sierra-security-after")||
                !strcmp(argv[1],"--highsierra-security-before")||!strcmp(argv[1],"--highsierra-security-after")||
                !strcmp(argv[1],"--mojave-security-before")||!strcmp(argv[1],"--mojave-security-after")||
                !strcmp(argv[1],"--catalina-security-before")||!strcmp(argv[1],"--catalina-security-after"))) {
        VIMInstallTarget *target=[VIMInstallTarget new];target.root=[NSURL fileURLWithPath:@(argv[2]) isDirectory:YES];
        target.metadata=[NSMutableDictionary new];
        @try {
            [target record:@"" uid:getuid() gid:getgid() receipt:NO];
            NSDictionary *package=@{@"Folder":[NSURL fileURLWithPath:@(argv[3]) isDirectory:YES]};
            NSString *guest=[@(argv[1]) hasPrefix:@"--sierra"]?@"sierra":@"elcapitan";
            BOOL before=[@(argv[1]) hasSuffix:@"-before"];
            if([@(argv[1]) hasPrefix:@"--highsierra"])HighSierraSecurityActions(target,package,before);
            else if([@(argv[1]) hasPrefix:@"--mojave"])MojaveSecurityActions(target,package,before);
            else if([@(argv[1]) hasPrefix:@"--catalina"])CatalinaSecurityActions(target,package,before);
            else if(before)OSSecurityCleanup(target,package,YES,guest);
            else ProtectedHFSSecurityActions(target,package,guest);
            NSMutableArray *flags=[NSMutableArray new];
            for(NSArray *entry in [target catalogPermissions].allValues)if(entry.count>3)[flags addObject:entry[3]];
            NSData *report=[NSPropertyListSerialization dataWithPropertyList:@{@"SystemFlags":target.systemFlags?:@{},@"CatalogFlags":flags,
                @"MetadataPaths":[target.metadata.allKeys sortedArrayUsingSelector:@selector(compare:)],
                @"UserDirectoryMode":@([target.metadata[@"private/var/db/dslocal/nodes/Default/users"][2] unsignedShortValue]&07777),
                @"ApplicationsMetadata":target.metadata[@"Applications"]?:@[]}
                format:NSPropertyListXMLFormat_v1_0 options:0 error:nil];
            [NSFileHandle.fileHandleWithStandardOutput writeData:report];return 0;
        } @catch(NSException *failure) {fprintf(stderr,"%s\n",failure.reason.UTF8String);return 1;}
    }
    if(argc==4&&!strcmp(argv[1],"--lion-security-actions")) {
        VIMInstallTarget *target=[VIMInstallTarget new];target.root=[NSURL fileURLWithPath:@(argv[2]) isDirectory:YES];
        target.metadata=[NSMutableDictionary new];
        @try {
            LionSecurityActions(target,@{@"Folder":[NSURL fileURLWithPath:@(argv[3]) isDirectory:YES]});return 0;
        } @catch(NSException *failure) {fprintf(stderr,"%s\n",failure.reason.UTF8String);return 1;}
    }
    NSDictionary *flatGuests=@{@"--snow-updates":@"snowleopard",@"--lion-updates":@"lion",
        @"--mountainlion-updates":@"mountainlion",@"--mavericks-updates":@"mavericks",@"--yosemite-updates":@"yosemite",
        @"--elcapitan-updates":@"elcapitan",@"--sierra-updates":@"sierra",@"--highsierra-updates":@"highsierra",@"--mojave-updates":@"mojave",@"--catalina-updates":@"catalina"};
    NSString *flatGuest=argc==5?flatGuests[@(argv[1])]:nil;
    if(flatGuest) {
        VIMInstallTarget *target=[VIMInstallTarget new];target.root=[NSURL fileURLWithPath:@(argv[2]) isDirectory:YES];
        target.metadata=[NSMutableDictionary new];NSURL *attempt=[NSURL fileURLWithPath:@(argv[3]) isDirectory:YES];
        NSURL *source=[NSURL fileURLWithPath:@(argv[4]) isDirectory:YES];NSMutableDictionary *inputs=[NSMutableDictionary new];
        for(NSDictionary *step in VIMFlatHostUpdateRecipe(flatGuest))
            inputs[step[@"Role"]]=ProbeUpdateInput(source,step).path;
        @try {
            NSArray *applied=[flatGuest isEqual:@"snowleopard"]?ApplySnowLeopardUpdates(target,attempt,inputs):
                ([@[@"highsierra",@"mojave",@"catalina"] containsObject:flatGuest])?ApplyAPFSOSUpdates(target,attempt,inputs,flatGuest):
                ApplyOSSecurityUpdates(target,attempt,inputs,flatGuest);
            NSData *result=[NSPropertyListSerialization dataWithPropertyList:@{@"Applied":applied,
                @"Notice":Plist(At(attempt,@"HostUpdateNotice.plist"))?:@{}}
                format:NSPropertyListXMLFormat_v1_0 options:0 error:nil];
            [NSFileHandle.fileHandleWithStandardOutput writeData:result];return 0;
        } @catch(NSException *failure) {
            fprintf(stderr,"%s\n",failure.reason.UTF8String);
            return [NSFileManager.defaultManager fileExistsAtPath:At(attempt,@"Cancel").path]?130:1;
        }
    }
    if(argc==5&&!strcmp(argv[1],"--prepare-flat-updates")) {
        NSString *guest=@(argv[2]);NSArray *recipe=VIMFlatHostUpdateRecipe(guest);
        if(!recipe)return 64;
        NSURL *inputsRoot=[NSURL fileURLWithPath:@(argv[3]) isDirectory:YES];
        NSURL *work=[NSURL fileURLWithPath:@(argv[4]) isDirectory:YES];struct stat info;
        if(!lstat(work.fileSystemRepresentation,&info)||errno!=ENOENT)return 64;
        Directory(work);
        @try {
            NSMutableDictionary *inputs=[NSMutableDictionary new];
            for(NSDictionary *step in recipe)inputs[step[@"Role"]]=ProbeUpdateInput(inputsRoot,step).path;
            NSArray *prepared=PrepareFlatUpdateInputs(inputs,recipe,guest,work,work);
            NSMutableDictionary *counts=[NSMutableDictionary new];
            for(NSDictionary *package in prepared)counts[package[@"Identifier"]]=@([package[@"Metadata"] count]);
            NSData *result=[NSPropertyListSerialization dataWithPropertyList:counts format:NSPropertyListXMLFormat_v1_0 options:0 error:nil];
            [NSFileHandle.fileHandleWithStandardOutput writeData:result];return 0;
        } @catch(NSException *failure) {fprintf(stderr,"%s\n",failure.reason.UTF8String);return 1;}
        @finally {Remove(work);}
    }
    if(argc==5&&!strcmp(argv[1],"--prepare-leopard-updates")) {
        NSURL *work=[NSURL fileURLWithPath:@(argv[4]) isDirectory:YES];struct stat info;
        if(!lstat(work.fileSystemRepresentation,&info)||errno!=ENOENT)return 64;
        Directory(work);
        @try {
            NSArray *prepared=PrepareLeopardUpdateInputs(@{@"Combo":@(argv[2]),@"Security":@(argv[3])},work,work);
            NSMutableDictionary *counts=[NSMutableDictionary new];
            for(NSDictionary *package in prepared)counts[package[@"Identifier"]]=@([package[@"Metadata"] count]);
            NSData *result=[NSPropertyListSerialization dataWithPropertyList:counts format:NSPropertyListXMLFormat_v1_0 options:0 error:nil];
            [NSFileHandle.fileHandleWithStandardOutput writeData:result];return 0;
        } @catch(NSException *failure) {fprintf(stderr,"%s\n",failure.reason.UTF8String);return 1;}
        @finally {Remove(work);}
    }
    if(argc==6&&!strcmp(argv[1],"--leopard-updates")) {
        VIMInstallTarget *target=[VIMInstallTarget new];target.root=[NSURL fileURLWithPath:@(argv[2]) isDirectory:YES];
        target.metadata=[NSMutableDictionary new];NSURL *attempt=[NSURL fileURLWithPath:@(argv[3]) isDirectory:YES];
        @try {
            NSArray *applied=ApplyLeopardUpdates(target,attempt,@{@"Combo":@(argv[4]),@"Security":@(argv[5])});
            NSDictionary *notice=Plist(At(attempt,@"HostUpdateNotice.plist"));
            NSData *result=[NSPropertyListSerialization dataWithPropertyList:@{@"Applied":applied,@"Notice":notice?:@{}}
                format:NSPropertyListXMLFormat_v1_0 options:0 error:nil];
            [NSFileHandle.fileHandleWithStandardOutput writeData:result];return 0;
        } @catch(NSException *failure) {
            fprintf(stderr,"%s\n",failure.reason.UTF8String);
            return [NSFileManager.defaultManager fileExistsAtPath:At(attempt,@"Cancel").path]?130:1;
        }
    }
    if(argc==6&&!strcmp(argv[1],"--prepare-tiger-updates")) {
        NSURL *work=[NSURL fileURLWithPath:@(argv[5]) isDirectory:YES];struct stat info;
        if(!lstat(work.fileSystemRepresentation,&info)||errno!=ENOENT)return 64;
        Directory(work);
        @try {
            NSData *key=Plist([NSURL fileURLWithPath:@(argv[4])])[@"ApplePkgKeyData"];
            NSDictionary *prepared=PrepareTigerUpdateInputs(@{@"Combo":@(argv[2]),@"Security":@(argv[3])},key,work,work);
            NSMutableDictionary *counts=[NSMutableDictionary dictionary];
            for(NSString *role in prepared)counts[role]=@([prepared[role][@"BOM"] count]);
            NSData *result=[NSPropertyListSerialization dataWithPropertyList:counts format:NSPropertyListXMLFormat_v1_0 options:0 error:nil];
            [NSFileHandle.fileHandleWithStandardOutput writeData:result];return 0;
        } @catch(NSException *failure) {fprintf(stderr,"%s\n",failure.reason.UTF8String);return 1;}
        @finally {Remove(work);}
    }
    if(argc==6&&!strcmp(argv[1],"--tiger-updates")) {
        VIMInstallTarget *target=[VIMInstallTarget new];target.root=[NSURL fileURLWithPath:@(argv[2]) isDirectory:YES];
        target.metadata=[NSMutableDictionary dictionary];NSURL *attempt=[NSURL fileURLWithPath:@(argv[3]) isDirectory:YES];
        @try {
            NSArray *applied=ApplyTigerUpdates(target,attempt,@{@"Combo":@(argv[4]),@"Security":@(argv[5])});
            NSDictionary *notice=Plist(At(attempt,@"HostUpdateNotice.plist"));
            NSData *result=[NSPropertyListSerialization dataWithPropertyList:@{@"Applied":applied,@"Notice":notice?:@{}}
                format:NSPropertyListXMLFormat_v1_0 options:0 error:nil];
            [NSFileHandle.fileHandleWithStandardOutput writeData:result];return 0;
        } @catch(NSException *failure) {
            fprintf(stderr,"%s\n",failure.reason.UTF8String);
            return [NSFileManager.defaultManager fileExistsAtPath:At(attempt,@"Cancel").path]?130:1;
        }
    }
    if(argc!=2)return 64;
    // Only inventoried OS-update roles may enter the worker. Unrelated
    // products offered by the same catalog are not an update dependency.
    for(NSString *guest in @[@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",@"mavericks",
                             @"yosemite",@"elcapitan",@"sierra",@"highsierra",@"mojave",@"catalina"]) {
        NSMutableDictionary *osMedia=[NSMutableDictionary new];
        if([@[@"tiger",@"leopard"] containsObject:guest])
            [osMedia addEntriesFromDictionary:@{@"update-combo":@"/Combo",@"update-security":@"/Security"}];
        else for(NSDictionary *step in VIMFlatHostUpdateRecipe(guest))
            osMedia[[@"update-" stringByAppendingString:[step[@"Role"] lowercaseString]]]=step[@"Name"];
        NSDictionary *job=@{@"Guest":guest,@"HostInstall":@YES};
        NSDictionary *expected=VIMHostInstallUpdateRequest(job,osMedia);
        if(!expected.count)return 67;
        for(NSString *application in @[@"safari",@"itunes",@"java",@"quicktime"])
            osMedia[[@"update-" stringByAppendingString:application]]=@"/Unrelated.pkg";
        if(![VIMHostInstallUpdateRequest(job,osMedia) isEqual:expected])return 68;
    }
    for(NSString *guest in @[@"lion",@"mountainlion",@"mavericks",@"yosemite",@"elcapitan",@"sierra",
                             @"highsierra",@"mojave",@"catalina"]) {
        NSString *family=VIMHostInstallESDAppFamily(guest);
        if(!VIMHostInstallUsesESDApp(guest,@"/Original/Install.app")||
            VIMHostInstallUsesESDApp(guest,@"/Original/Install.dmg")||
            !VIMHostInstallESDVersionMatches(guest,@{@"ProductVersion":[family stringByAppendingString:@".99"],@"ProductBuildVersion":@"11G123"})||
            VIMHostInstallESDVersionMatches(guest,@{@"ProductVersion":@"11.0",@"ProductBuildVersion":@"20G1443"})||
            VIMHostInstallESDVersionMatches(guest,@{@"ProductVersion":[family stringByAppendingString:@".oops"],@"ProductBuildVersion":@"11G63"})||
            VIMHostInstallESDVersionMatches(guest,@{@"ProductVersion":family,@"ProductBuildVersion":@"../outside"}))return 65;
    }
    for(NSString *guest in @[@"tiger",@"leopard",@"snowleopard",@"bigsur",@"tahoe"])
        if(VIMHostInstallUsesESDApp(guest,@"/Original/Install.app"))return 66;
    for(NSString *guest in @[@"tiger",@"leopard",@"snowleopard"]) {
        NSString *family=VIMHostInstallDVDFamily(guest);
        for(NSString *version in @[family,[family stringByAppendingString:@".0"],
                                   [family stringByAppendingString:@".8"]])
            if(![VIMOpenGLGuestForSystemVersion(version) isEqual:guest])return 72;
        for(NSString *extension in @[@"dmg",@"iso",@"cdr",@"IMG"])
            if(!VIMHostInstallUsesDVD(guest,[@"/Original/Install" stringByAppendingPathExtension:extension]))return 69;
        if(VIMHostInstallUsesDVD(guest,@"/Original/Install.app")||VIMHostInstallUsesDVD(guest,nil)||
           !VIMHostInstallDVDVersionMatches(guest,@{@"ProductVersion":family,@"ProductBuildVersion":@"9A581"})||
           !VIMHostInstallDVDVersionMatches(guest,@{@"ProductVersion":[family stringByAppendingString:@".3"],@"ProductBuildVersion":@"10D575"})||
           VIMHostInstallDVDVersionMatches(guest,@{@"ProductVersion":@"10.9.5",@"ProductBuildVersion":@"13F34"})||
           VIMHostInstallDVDVersionMatches(guest,@{@"ProductVersion":[family stringByAppendingString:@".oops"],@"ProductBuildVersion":@"9A581"})||
           VIMHostInstallDVDVersionMatches(guest,@{@"ProductVersion":family,@"ProductBuildVersion":@"../outside"}))return 70;
    }
    for(NSString *guest in @[@"lion",@"mavericks",@"mojave",@"bigsur",@"tahoe"])
        if(VIMHostInstallUsesDVD(guest,@"/Original/Install.dmg"))return 71;
    NSDictionary *lionJob=@{@"Guest":@"lion",@"HostInstall":@YES};
    NSDictionary *lionMedia=@{@"update-security":@"/Security.pkg"};
    if(![VIMHostInstallUpdateRequest(lionJob,lionMedia)[@"LionUpdates"] isEqual:@{@"Security":@"/Security.pkg"}]||
       VIMHostInstallUpdateRequest(lionJob,@{}).count||
       VIMHostInstallUpdateRequest(@{@"Guest":@"lion",@"UserSelectedInstaller":@YES},lionMedia).count)return 8;
    if(![VIMHostInstallUpdateRequest(@{@"Guest":@"mountainlion"},lionMedia)[@"MountainLionUpdates"]
        isEqual:@{@"Security":@"/Security.pkg"}]||VIMHostInstallUpdateRequest(@{@"Guest":@"mountainlion"},@{}).count||
       VIMHostInstallUpdateRequest(@{@"Guest":@"mountainlion",@"UserSelectedInstaller":@YES},lionMedia).count)return 9;
    if(![VIMHostInstallUpdateRequest(@{@"Guest":@"mavericks"},lionMedia)[@"MavericksUpdates"]
        isEqual:@{@"Security":@"/Security.pkg"}]||VIMHostInstallUpdateRequest(@{@"Guest":@"mavericks"},@{}).count||
       VIMHostInstallUpdateRequest(@{@"Guest":@"mavericks",@"UserSelectedInstaller":@YES},lionMedia).count)return 10;
    if(![VIMHostInstallUpdateRequest(@{@"Guest":@"yosemite"},lionMedia)[@"YosemiteUpdates"]
        isEqual:@{@"Security":@"/Security.pkg"}]||VIMHostInstallUpdateRequest(@{@"Guest":@"yosemite"},@{}).count||
       VIMHostInstallUpdateRequest(@{@"Guest":@"yosemite",@"UserSelectedInstaller":@YES},lionMedia).count)return 11;
    NSDictionary *elCapitanMedia=@{@"update-security":@"/Security.pkg",@"update-fullbundle":@"/Bundle.pkg"};
    if(![VIMHostInstallUpdateRequest(@{@"Guest":@"elcapitan"},elCapitanMedia)[@"ElCapitanUpdates"]
        isEqual:@{@"Security":@"/Security.pkg",@"FullBundle":@"/Bundle.pkg"}]||
       VIMHostInstallUpdateRequest(@{@"Guest":@"elcapitan"},lionMedia).count||
       VIMHostInstallUpdateRequest(@{@"Guest":@"elcapitan",@"UserSelectedInstaller":@YES},elCapitanMedia).count)return 12;
    if(![VIMHostInstallUpdateRequest(@{@"Guest":@"sierra"},elCapitanMedia)[@"SierraUpdates"]
        isEqual:@{@"Security":@"/Security.pkg",@"FullBundle":@"/Bundle.pkg"}]||
       VIMHostInstallUpdateRequest(@{@"Guest":@"sierra"},lionMedia).count||
       VIMHostInstallUpdateRequest(@{@"Guest":@"sierra",@"UserSelectedInstaller":@YES},elCapitanMedia).count)return 13;
    NSMutableDictionary *highMedia=[NSMutableDictionary new];
    for(NSDictionary *step in VIMFlatHostUpdateRecipe(@"highsierra"))
        highMedia[[@"update-" stringByAppendingString:[step[@"Role"] lowercaseString]]]=
            [@"/" stringByAppendingString:step[@"Role"]];
    if([VIMHostInstallUpdateRequest(@{@"Guest":@"highsierra"},highMedia)[@"HighSierraUpdates"] count]!=7||
       VIMHostInstallUpdateRequest(@{@"Guest":@"highsierra",@"UserSelectedInstaller":@YES},highMedia).count)return 14;
    [highMedia removeObjectForKey:@"update-hopsecurity"];
    if(VIMHostInstallUpdateRequest(@{@"Guest":@"highsierra"},highMedia).count)return 15;
    NSMutableDictionary *mojaveMedia=[NSMutableDictionary new];
    for(NSDictionary *step in VIMFlatHostUpdateRecipe(@"mojave"))
        mojaveMedia[[@"update-" stringByAppendingString:[step[@"Role"] lowercaseString]]]=
            [@"/" stringByAppendingString:step[@"Role"]];
    if([VIMHostInstallUpdateRequest(@{@"Guest":@"mojave"},mojaveMedia)[@"MojaveUpdates"] count]!=5||
       VIMHostInstallUpdateRequest(@{@"Guest":@"mojave",@"UserSelectedInstaller":@YES},mojaveMedia).count)return 16;
    [mojaveMedia removeObjectForKey:@"update-recovery"];
    if(VIMHostInstallUpdateRequest(@{@"Guest":@"mojave"},mojaveMedia).count)return 17;
    NSMutableDictionary *catalinaMedia=[NSMutableDictionary new];
    for(NSDictionary *step in VIMFlatHostUpdateRecipe(@"catalina"))
        catalinaMedia[[@"update-" stringByAppendingString:[step[@"Role"] lowercaseString]]]=
            [@"/" stringByAppendingString:step[@"Role"]];
    if([VIMHostInstallUpdateRequest(@{@"Guest":@"catalina"},catalinaMedia)[@"CatalinaUpdates"] count]!=5||
       VIMHostInstallUpdateRequest(@{@"Guest":@"catalina",@"UserSelectedInstaller":@YES},catalinaMedia).count)return 18;
    [catalinaMedia removeObjectForKey:@"update-security"];
    if(VIMHostInstallUpdateRequest(@{@"Guest":@"catalina"},catalinaMedia).count)return 19;
    NSMutableDictionary *snowMedia=[NSMutableDictionary new];
    for(NSDictionary *step in VIMFlatHostUpdateRecipe(@"snowleopard"))
        snowMedia[[@"update-" stringByAppendingString:[step[@"Role"] lowercaseString]]]=[@"/" stringByAppendingString:step[@"Name"]];
    NSDictionary *snowJob=@{@"Guest":@"snowleopard",@"HostInstall":@YES};
    if([VIMHostInstallUpdateRequest(snowJob,snowMedia)[@"SnowLeopardUpdates"] count]!=17)return 6;
    [snowMedia removeObjectForKey:@"update-combopart6"];
    if(VIMHostInstallUpdateRequest(snowJob,snowMedia).count)return 7;
    NSDictionary *updates=@{@"HostUpdates":@{}},*native=@{@"Guest":@"tiger",@"HostInstall":@YES};
    NSDictionary *user=@{@"Guest":@"tiger",@"HostInstall":@YES,@"UserSelectedInstaller":@YES};
    NSDictionary *media=@{@"update-combo":@"/Combo",@"update-security":@"/Security"};
    NSDictionary *notice=@{@"Status":@"original-system-kept"};
    NSMutableDictionary *choice=[native mutableCopy];choice[@"DownloadSecurityUpdates"]=@NO;
    if(VIMHostInstallWantsUpdates(choice,NO,updates)||VIMHostInstallUpdateRequest(choice,media).count)return 70;
    choice=[user mutableCopy];choice[@"DownloadSecurityUpdates"]=@YES;
    if(!VIMHostInstallWantsUpdates(choice,NO,updates)||
       ![VIMHostInstallUpdateRequest(choice,media)[@"TigerUpdates"] isEqual:@{@"Combo":@"/Combo",@"Security":@"/Security"}])return 71;
    choice[@"CreateAccount"]=@NO;
    if(!VIMHostInstallWantsUpdates(choice,NO,updates))return 72;
    if(!VIMHostInstallWantsUpdates(native,NO,updates)||VIMHostInstallWantsUpdates(native,YES,updates)||
       VIMHostInstallWantsUpdates(user,NO,updates)||VIMHostInstallWantsUpdates(@{},NO,updates)||
       VIMHostInstallWantsUpdates(native,NO,@{})||
       ![VIMHostInstallUpdateRequest(native,media)[@"TigerUpdates"] isEqual:@{@"Combo":@"/Combo",@"Security":@"/Security"}]||
       ![VIMHostInstallUpdateRequest(@{@"Guest":@"leopard"},media)[@"LeopardUpdates"] isEqual:@{@"Combo":@"/Combo",@"Security":@"/Security"}]||
       VIMHostInstallUpdateRequest(user,media).count||
       VIMHostInstallUpdateRequest(native,@{@"update-combo":@"/Combo"}).count||
       ![VIMHostInstallUpdateRequest(@{@"HostUpdateNotice":notice},media)[@"HostUpdateNotice"] isEqual:notice])return 4;
    for(NSDictionary *failure in @[@{@"Domain":NSURLErrorDomain,@"Code":@(NSURLErrorNotConnectedToInternet),@"Optional":@YES},
        @{@"Domain":NSURLErrorDomain,@"Code":@(NSURLErrorTimedOut),@"Optional":@YES},
        @{@"Domain":NSURLErrorDomain,@"Code":@(NSURLErrorCancelled),@"Optional":@NO},
        @{@"Domain":NSURLErrorDomain,@"Code":@(NSURLErrorFileDoesNotExist),@"Optional":@NO},
        @{@"Domain":NSURLErrorDomain,@"Code":@(NSURLErrorFileIsDirectory),@"Optional":@NO},
        @{@"Domain":NSURLErrorDomain,@"Code":@(NSURLErrorNoPermissionsToReadFile),@"Optional":@NO},
        @{@"Domain":NSURLErrorDomain,@"Code":@(NSURLErrorCannotCreateFile),@"Optional":@NO},
        @{@"Domain":NSURLErrorDomain,@"Code":@(NSURLErrorCannotOpenFile),@"Optional":@NO},
        @{@"Domain":NSURLErrorDomain,@"Code":@(NSURLErrorCannotCloseFile),@"Optional":@NO},
        @{@"Domain":NSURLErrorDomain,@"Code":@(NSURLErrorCannotWriteToFile),@"Optional":@NO},
        @{@"Domain":NSURLErrorDomain,@"Code":@(NSURLErrorCannotRemoveFile),@"Optional":@NO},
        @{@"Domain":NSURLErrorDomain,@"Code":@(NSURLErrorCannotMoveFile),@"Optional":@NO},
        @{@"Domain":@"VirtualIntelMac.Download",@"Code":@2,@"Optional":@YES},
        @{@"Domain":@"VirtualIntelMac.Download",@"Code":@1,@"Optional":@NO},
        @{@"Domain":NSPOSIXErrorDomain,@"Code":@(ENOSPC),@"Optional":@NO}])
        if(VIMOptionalUpdateDownloadUnavailable([NSError errorWithDomain:failure[@"Domain"] code:[failure[@"Code"] integerValue] userInfo:nil])!=
           [failure[@"Optional"] boolValue])return 5;
    if(!VIMHostInstallCreatesAccount(@{})||
       !VIMHostInstallCreatesAccount(@{@"CreateAccount":@YES})||
       VIMHostInstallCreatesAccount(@{@"CreateAccount":@NO}))return 1;
    NSString *app=@"/Original/Install macOS.app";
    if(![VIMAssistantPackageSource(@{@"assistant":@"/Downloads/Payload.pkg"}) isEqual:@"/Downloads/Payload.pkg"]||
       ![VIMAssistantPackageSource(@{@"installer":app}) isEqual:
            [app stringByAppendingPathComponent:@"Contents/SharedSupport/SharedSupport.dmg"]]||
       VIMAssistantPackageSource(@{@"installer":@"relative.app"})||
       VIMAssistantPackageSource(@{@"installer":@"/Original/NotAnInstaller.iso"})||
       VIMAssistantPackageSource(@{@"assistant":@12})||VIMAssistantPackageSource(@{}))return 3;
    VIMInstallTarget *target=[VIMInstallTarget new];
    target.root=[NSURL fileURLWithPath:@(argv[1]) isDirectory:YES];
    target.metadata=[NSMutableDictionary dictionary];
    for(NSString *guest in VIMSupportedGuests()) {
        // Neither a username nor credential is needed, and no account,
        // login policy, home or Setup Assistant completion may be invented.
        StageAccount(target,target.root,@{@"Guest":guest,@"Enabled":@NO},nil,nil);
        if(target.metadata.count)return 2;
    }
    return 0;
}}
