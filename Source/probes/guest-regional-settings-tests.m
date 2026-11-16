#import <Foundation/Foundation.h>
#import "guest-regional-settings.h"
#include <sys/stat.h>
#include <unistd.h>
static unsigned checks, failures;
static void Check(BOOL ok, const char *message) { checks++; if (!ok) { failures++; fprintf(stderr,"FAIL: %s\n", message); } }
static NSData *Plist(id value) { return [NSPropertyListSerialization dataFromPropertyList:value format:NSPropertyListXMLFormat_v1_0 errorDescription:NULL]; }
@interface FixtureBackend : NSObject <VIMGuestRegionalBackend> {
@public NSMutableDictionary *userDomain, *systemDomain, *versionDomain; NSMutableArray *commands; NSString *zone; NSString *journalPath; NSString *periodVersion; BOOL failLanguageWrite; BOOL changeLanguagesAfterLogin, directionSupported, changeLocaleAfterLogin, changeVersionAfterLogin; BOOL failWrite, wrongReadback, exportUnavailable, changedAfterWrite, settersUnavailable, malformedDefaults, openStepDefaults, legacyResources, migrationAware, wrongMigrationReadback, failAfterEffect, breakJournalAfterWrite; unsigned writes, failCommand; id invalidStatus;
}
@end
@implementation FixtureBackend
- (id)init { if ((self=[super init])) { userDomain=[[NSMutableDictionary alloc] init]; systemDomain=[[NSMutableDictionary alloc] init]; versionDomain=[[NSMutableDictionary alloc] init]; commands=[[NSMutableArray alloc] init]; zone=@"Europe/London"; } return self; }
- (NSDictionary *)regionalCapabilities { NSMutableDictionary *capabilities=[NSMutableDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithInt:1],@"FormatVersion",(legacyResources?[NSArray arrayWithObjects:@"English",@"French",@"zh_TW",@"zh_CN",nil]:[NSArray arrayWithObjects:@"en",@"fr",@"zh-Hant-TW",nil]),@"DesktopLanguages",(legacyResources?[NSArray arrayWithObjects:@"English",@"French",@"zh_TW",@"zh_CN",nil]:[NSArray arrayWithObjects:@"en",@"fr",@"zh-Hant-TW",nil]),@"LoginLanguages",[NSArray arrayWithObjects:@"en_US",@"fr_FR",@"zh_Hans_CN",nil],@"LocaleIdentifiers",[NSArray arrayWithObjects:@"Europe/London",@"Asia/Shanghai",nil],@"TimeZoneIdentifiers",[NSNumber numberWithBool:!settersUnavailable],@"UserDefaultsSetterAvailable",[NSNumber numberWithBool:!settersUnavailable],@"LoginLanguageSetterAvailable",[NSNumber numberWithBool:!settersUnavailable],@"TimeZoneSetterAvailable",nil]; if(periodVersion)[capabilities setObject:periodVersion forKey:@"Version"]; if(legacyResources)[capabilities setObject:@"native" forKey:@"LanguagePreferenceFormat"]; if(migrationAware)[capabilities setObject:@"10.14.6" forKey:@"AccountSystemVersion"]; if(directionSupported){NSDictionary *directions=[NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithInt:0],@"fr",[NSNumber numberWithInt:0],@"French",nil];[capabilities setObject:directions forKey:@"LoginTextDirections"];[capabilities setObject:directions forKey:@"DesktopTextDirections"];} return capabilities; }
- (NSDictionary *)runExecutable:(NSString *)path arguments:(NSArray *)arguments environment:(NSDictionary *)environment error:(NSError **)error {
    (void)error;
    [commands addObject:[NSArray arrayWithObjects:path,arguments,environment,nil]];
    if(failCommand==[commands count]&&!failAfterEffect)return [NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithInt:1],@"Status",[NSData data],@"Output",nil];
    int status=0; NSData *output=[NSData data];
    BOOL sudo=[path isEqual:@"/usr/bin/sudo"];
    if (![[environment objectForKey:@"LC_ALL"] isEqual:@"C"] || (sudo && ([arguments count]<5 || ![[arguments subarrayWithRange:NSMakeRange(0,5)] isEqual:[NSArray arrayWithObjects:@"-H",@"-u",@"alice",@"--",@"/usr/bin/defaults",nil]])))
        return [NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithInt:99],@"Status",[NSData data],@"Output",nil];
    NSArray *args=sudo?[arguments subarrayWithRange:NSMakeRange(5,[arguments count]-5)]:arguments;
    NSMutableDictionary *domain=sudo?([[args objectAtIndex:1] isEqual:@"loginwindow"]?versionDomain:userDomain):systemDomain;
    if ([path isEqual:@"/usr/sbin/systemsetup"]) {
        if ([[args objectAtIndex:0] isEqual:@"-gettimezone"]) output=[[NSString stringWithFormat:@"Time Zone: %@\n",zone] dataUsingEncoding:NSUTF8StringEncoding];
        else { writes++; if (!wrongReadback) zone=[args objectAtIndex:1]; }
    } else if ([path isEqual:@"/usr/sbin/languagesetup"]) {
        Check(NO,"regional setup must never invoke the destructive languagesetup command");
        status=1;
    } else if([[args objectAtIndex:0] isEqual:@"read-type"]) {
        id value=[domain objectForKey:[args objectAtIndex:2]];
        output=[([value isKindOfClass:[NSNumber class]]?@"Type is integer\n":@"Type is string\n") dataUsingEncoding:NSUTF8StringEncoding];
    } else if ([[args objectAtIndex:0] isEqual:@"export"] || [[args objectAtIndex:0] isEqual:@"read"]) {
        if (exportUnavailable && [[args objectAtIndex:0] isEqual:@"export"]) status=1;
        else if (malformedDefaults) output=[@"unexpected output" dataUsingEncoding:NSUTF8StringEncoding];
        else if (openStepDefaults) output=[[domain description] dataUsingEncoding:NSUTF8StringEncoding];
        else output=Plist(domain);
    } else if ([[args objectAtIndex:0] isEqual:@"write"]) {
        writes++;
        NSDictionary *saved=[NSDictionary dictionaryWithContentsOfFile:journalPath];
        NSArray *steps=[[saved objectForKey:@"RegionalSettings"] objectForKey:@"Steps"];
        BOOL intent=NO; unsigned i;
        for(i=0;i<[steps count];i++) if ([[[steps objectAtIndex:i] objectForKey:@"Phase"] isEqual:@"intent"]) intent=YES;
        Check(intent,"write has durable intent before OS side effect");
        if(!sudo&&[[args objectAtIndex:2] isEqual:@"AppleLanguages"]&&failLanguageWrite)status=1;
        if (failWrite) status=1;
        if (!wrongReadback && !(!sudo&&failLanguageWrite&&[[args objectAtIndex:2] isEqual:@"AppleLanguages"])) {
            NSString *key=[args objectAtIndex:2]; id value=[[args objectAtIndex:3] isEqual:@"-array"]?[args subarrayWithRange:NSMakeRange(4,[args count]-4)]:[args objectAtIndex:4];
            if([[args objectAtIndex:3] isEqual:@"-int"])value=[NSNumber numberWithInt:[value intValue]];
            if(!(wrongMigrationReadback&&[key isEqual:@"SystemVersionStampAsString"]))[domain setObject:changedAfterWrite?@"externally-changed":value forKey:key];
            if(!sudo&&[key isEqual:@"AppleLanguages"]&&changeLanguagesAfterLogin)[userDomain setObject:[NSArray arrayWithObject:@"en"] forKey:@"AppleLanguages"];
            if(!sudo&&[key isEqual:@"AppleLanguages"]&&changeLocaleAfterLogin)[userDomain setObject:@"de_DE" forKey:@"AppleLocale"];
            if(!sudo&&[key isEqual:@"AppleLanguages"]&&changeVersionAfterLogin)[versionDomain setObject:@"10.8.5" forKey:@"SystemVersionStampAsString"];
        }
        if(breakJournalAfterWrite)chmod([journalPath fileSystemRepresentation],0644);
    } else status=2;
    if(failCommand==[commands count])status=1;
    return [NSDictionary dictionaryWithObjectsAndKeys:invalidStatus?:[NSNumber numberWithInt:status],@"Status",output,@"Output",nil];
}
@end
static NSDictionary *Request(void) { return [NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithInt:1],@"FormatVersion",@"custom",@"Source",[NSArray arrayWithObject:@"fr"],@"PreferredLanguages",@"FR",@"RegionCode",@"Asia/Shanghai",@"TimeZoneIdentifier",@"strict",@"UnsupportedPolicy",nil]; }
static NSMutableDictionary *Journal(void) { return [NSMutableDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithInt:1],@"FormatVersion",@"alice",@"Username",[NSNumber numberWithInt:501],@"UID",@"guid",@"GeneratedUID",@"machine",@"MachineIdentifier",@"volume",@"VolumeUUID",@"planned",@"Phase",nil]; }
static NSString *SaveFixture(NSMutableDictionary *journal) { char dir[]="/tmp/vim-regional-test.XXXXXX"; char *made=mkdtemp(dir); NSString *path=[[NSString stringWithUTF8String:made] stringByAppendingPathComponent:@"Account.plist"]; [Plist(journal) writeToFile:path atomically:YES]; chmod([path fileSystemRepresentation],0600); return path; }
static BOOL Apply(NSMutableDictionary *journal, NSString *path, FixtureBackend *backend, NSError **error) { backend->journalPath=path; return VIMGuestApplyRegionalSettingsWithBackend(Request(),@"alice",501,journal,path,backend,error); }
static void Remove(NSString *path) { [[NSFileManager defaultManager] removeFileAtPath:[path stringByDeletingLastPathComponent] handler:nil]; }
static void NativePlanTests(void) {
    FixtureBackend *backend=[[FixtureBackend alloc] init];
    NSMutableDictionary *caps=[NSMutableDictionary dictionaryWithDictionary:[backend regionalCapabilities]];
    NSArray *native=[NSArray arrayWithObjects:@"English",@"French",@"Japanese",@"no",@"zh_CN",@"zh_TW",nil];
    [caps setObject:native forKey:@"DesktopLanguages"];[caps setObject:native forKey:@"LoginLanguages"];
    [caps setObject:@"native" forKey:@"LanguagePreferenceFormat"];
    [caps setObject:@"10.9.5" forKey:@"Version"];
    NSMutableDictionary *request=[NSMutableDictionary dictionaryWithDictionary:Request()];
    [request setObject:[NSArray arrayWithObjects:@"zh-Hant-HK",@"ja",@"nb-NO",nil] forKey:@"PreferredLanguages"];
    NSDictionary *plan=VIMGuestRegionalPlan(request,caps,NULL);
    Check([[plan objectForKey:@"UserLanguageArguments"] isEqual:[NSArray arrayWithObjects:@"zh_TW",@"Japanese",@"no",nil]],"native-format capability encodes every accepted preference without a 10.6 version cutoff");
    Check([[plan objectForKey:@"UserLanguages"] isEqual:[NSArray arrayWithObjects:@"zh-Hant-TW",@"ja",@"nb",nil]],"effective semantic targets describe the actual native preferences after regional fallback");
    Check([[[plan objectForKey:@"Requested"] objectForKey:@"PreferredLanguages"] isEqual:[request objectForKey:@"PreferredLanguages"]],"regional fallback never rewrites immutable requested preferences");
    Check([[plan objectForKey:@"Warnings"] containsObject:@"LanguageVariantFallback"]&&[[plan objectForKey:@"LanguageMappings"] count]==3,"regional fallback records each accepted choice and a visible warning");
    [caps setObject:[NSDictionary dictionaryWithObject:[NSNumber numberWithInt:1] forKey:@"zh_TW"] forKey:@"LoginTextDirections"];
    [caps setObject:[NSDictionary dictionaryWithObject:[NSNumber numberWithInt:1] forKey:@"zh_TW"] forKey:@"DesktopTextDirections"];
    plan=VIMGuestRegionalPlan(request,caps,NULL);
    Check([[plan objectForKey:@"UserTextDirection"] isEqual:[NSNumber numberWithInt:1]],"desktop writing direction has its own target independent of login defaults");
    Check([[plan objectForKey:@"LoginTextDirection"] isEqual:[NSNumber numberWithInt:1]],"native writing direction is part of the verified login plan");
    [caps setObject:@"canonical" forKey:@"LanguagePreferenceFormat"];
    [request setObject:[NSArray arrayWithObject:@"en-US"] forKey:@"PreferredLanguages"];
    plan=VIMGuestRegionalPlan(request,caps,NULL);
    Check(![[plan objectForKey:@"Warnings"] containsObject:@"LanguageVariantFallback"]&&[[plan objectForKey:@"UserLanguages"] isEqual:[NSArray arrayWithObject:@"en-US"]],"a supported regional preference using a generic resource is not reported as a failed language setting");
    NSArray *common=[NSArray arrayWithObjects:@"English",@"French",nil];
    [caps setObject:common forKey:@"DesktopLanguages"];[caps setObject:common forKey:@"LoginLanguages"];
    [request setObject:[NSArray arrayWithObject:@"fr-CA"] forKey:@"PreferredLanguages"];
    NSDictionary *finder=[NSDictionary dictionaryWithObject:[NSArray arrayWithObjects:@"English",@"French",@"fr_CA",nil] forKey:@"Languages"];
    NSDictionary *appKit=[NSDictionary dictionaryWithObject:common forKey:@"Languages"];
    [caps setObject:[NSDictionary dictionaryWithObjectsAndKeys:finder,@"Finder",appKit,@"AppKit",nil] forKey:@"ResourceEvidence"];
    plan=VIMGuestRegionalPlan(request,caps,NULL);
    Check([[plan objectForKey:@"UserLanguageArguments"] isEqual:[NSArray arrayWithObject:@"French"]],"preflight verifies the full Finder and AppKit inventories before preserving a regional preference");
    finder=[NSDictionary dictionaryWithObject:[NSArray arrayWithObject:@"English"] forKey:@"Languages"];
    [caps setObject:[NSDictionary dictionaryWithObjectsAndKeys:finder,@"Finder",appKit,@"AppKit",nil] forKey:@"ResourceEvidence"];
    Check(!VIMGuestRegionalPlan(request,caps,NULL),"inconsistent desktop evidence is rejected during read-only planning before account writes");
}
static void RecoveryUpgradeTests(void) {
    NSMutableDictionary *template=[NSMutableDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithInt:1],@"FormatVersion",@"custom",@"Source",[NSArray arrayWithObject:@"zh-Hant-HK"],@"PreferredLanguages",@"strict",@"UnsupportedPolicy",nil];
    for(unsigned variant=0;variant<5;variant++) {
        BOOL planned=variant>=2, changed=(variant==1||variant==3), unchangedTarget=variant==4;
        NSString *language=unchangedTarget?@"fr":@"zh-Hant-HK",*accepted=unchangedTarget?@"fr":@"zh-Hant-TW",*native=unchangedTarget?@"French":@"zh_TW";
        NSMutableDictionary *request=[NSMutableDictionary dictionaryWithDictionary:template];[request setObject:[NSArray arrayWithObject:language] forKey:@"PreferredLanguages"];
        NSMutableDictionary *j=Journal();FixtureBackend *b=[[FixtureBackend alloc] init];b->legacyResources=YES;
        [b->userDomain setObject:[NSArray arrayWithObject:(changed||unchangedTarget)?@"de":language] forKey:@"AppleLanguages"];
        NSDictionary *resolution=[NSDictionary dictionaryWithObjectsAndKeys:request,@"Requested",[NSArray arrayWithObject:language],@"UserLanguages",accepted,@"DesktopLanguage",native,@"DesktopNativeLanguage",accepted,@"LoginLanguage",native,@"LoginNativeLanguage",nil];
        NSDictionary *absent=[NSDictionary dictionaryWithObject:[NSNumber numberWithBool:NO] forKey:@"Present"];
        NSDictionary *userAfter=[NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithBool:YES],@"Present",[NSArray arrayWithObject:language],@"Value",nil];
        NSDictionary *loginAfter=[NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithBool:YES],@"Present",[NSArray arrayWithObject:accepted],@"Value",nil];
        NSMutableDictionary *userStep=[NSMutableDictionary dictionaryWithObjectsAndKeys:@"UserLanguages",@"Name",planned?@"planned":@"verified",@"Phase",userAfter,@"After",nil];
        if(!planned)[userStep setObject:absent forKey:@"Before"];
        NSDictionary *loginStep=[NSDictionary dictionaryWithObjectsAndKeys:@"LoginLanguage",@"Name",@"intent",@"Phase",absent,@"Before",loginAfter,@"After",nil];
        [j setObject:[NSDictionary dictionaryWithObjectsAndKeys:request,@"Requested",resolution,@"Resolved",@"applying",@"Phase",[NSArray arrayWithObjects:userStep,loginStep,nil],@"Steps",[NSArray array],@"Warnings",[NSArray array],@"Skipped",nil] forKey:@"RegionalSettings"];
        NSString *path=SaveFixture(j);b->journalPath=path;NSError *error=nil;
        BOOL applied=VIMGuestApplyRegionalSettingsWithBackend(request,@"alice",501,j,path,b,&error);
        if(unchangedTarget)Check(applied&&[[b->userDomain objectForKey:@"AppleLanguages"] isEqual:[NSArray arrayWithObject:@"French"]],"unchanged planned targets retain their original initial-setting authority during upgrade");
        else if(changed)Check(!applied&&b->writes==0&&[[b->userDomain objectForKey:@"AppleLanguages"] isEqual:[NSArray arrayWithObject:@"de"]],"legacy-plan upgrade refuses a user preference outside its recorded before/after values");
        else {
            Check(applied&&[[b->userDomain objectForKey:@"AppleLanguages"] isEqual:[NSArray arrayWithObject:@"zh_TW"]],"in-progress legacy regional plan upgrades to verified native regional fallback");
            Check([[[j objectForKey:@"RegionalSettings"] objectForKey:@"Requested"] isEqual:request],"legacy plan upgrade retains the exact original request");
        }
        Remove(path);
    }
}
static void FinalDriftTests(void) {
    for(unsigned variant=0;variant<4;variant++) {
        NSMutableDictionary *j=Journal();NSString *path=SaveFixture(j);FixtureBackend *b=[[FixtureBackend alloc] init];NSError *error=nil;
        b->migrationAware=YES;
        if(variant<2) {
            b->changeLocaleAfterLogin=variant==0;b->changeVersionAfterLogin=variant==1;
            Check(!Apply(j,path,b,&error),"final verification rejects a previously verified locale or account version changed by a later step");
        } else {
            Check(Apply(j,path,b,&error),"drift recovery fixture applies initially");
            NSMutableDictionary *regional=[[j objectForKey:@"RegionalSettings"] mutableCopy];
            [regional setObject:@"applying" forKey:@"Phase"];[j setObject:regional forKey:@"RegionalSettings"];
            if(variant==2)[b->userDomain setObject:@"de_DE" forKey:@"AppleLocale"];else b->zone=@"Europe/London";
            [Plist(j) writeToFile:path atomically:YES];chmod([path fileSystemRepresentation],0600);
            unsigned before=b->writes;
            Check(!Apply(j,path,b,&error)&&b->writes==before,"resuming an unfinished transaction rejects drift in verified locale or timezone without overwriting it");
        }
        Check(![[[j objectForKey:@"RegionalSettings"] objectForKey:@"Phase"] isEqual:@"verified"],"drift cannot publish a verified regional receipt");
        Remove(path);
    }
}
static void MalformedBoundaryTests(void) {
    FixtureBackend *b=[[FixtureBackend alloc] init];
    NSArray *badFlags=[NSArray arrayWithObjects:[NSArray array],@"yes",[NSNumber numberWithInt:2],[NSNumber numberWithInt:-1],[NSNumber numberWithDouble:0.5],nil];
    for(NSString *key in [NSArray arrayWithObjects:@"UserDefaultsSetterAvailable",@"LoginLanguageSetterAvailable",@"TimeZoneSetterAvailable",nil]) {
        for(id flag in badFlags) {
            NSMutableDictionary *caps=[NSMutableDictionary dictionaryWithDictionary:[b regionalCapabilities]];[caps setObject:flag forKey:key];
            BOOL rejected=NO;NSError *error=nil;
            @try { rejected=!VIMGuestRegionalPlan(Request(),caps,&error)&&error!=nil; } @catch(NSException *exception) { (void)exception; }
            Check(rejected,"malformed setter availability returns an error without throwing or silently enabling a setter");
        }
    }
    for(unsigned variant=0;variant<10;variant++) {
        NSMutableDictionary *j=Journal();NSString *path=SaveFixture(j);b=[[FixtureBackend alloc] init];b->wrongReadback=YES;NSError *error=nil;
        Check(!Apply(j,path,b,&error),"malformed journal fixture retains a pending transaction");b->wrongReadback=NO;
        NSMutableDictionary *regional=[j objectForKey:@"RegionalSettings"],*resolved=[[regional objectForKey:@"Resolved"] mutableCopy];
        [regional setObject:resolved forKey:@"Resolved"];
        NSMutableArray *steps=[regional objectForKey:@"Steps"];
        if(variant==0) {
            NSMutableDictionary *before=[NSMutableDictionary dictionaryWithDictionary:[[steps objectAtIndex:0] objectForKey:@"Before"]];
            [before setObject:[NSNumber numberWithInt:2] forKey:@"Present"];[[steps objectAtIndex:0] setObject:before forKey:@"Before"];
        }
        if(variant==1)[j setObject:[NSNumber numberWithDouble:501.5] forKey:@"UID"];
        if(variant==2)[regional setObject:[[steps reverseObjectEnumerator] allObjects] forKey:@"Steps"];
        if(variant==3)[resolved setObject:[NSNumber numberWithInt:1] forKey:@"DesktopNativeLanguage"];
        if(variant==4)[regional setObject:@"not a list" forKey:@"Warnings"];
        if(variant==5)[resolved setObject:@"not mappings" forKey:@"LanguageMappings"];
        if(variant==6)[resolved setObject:[NSNumber numberWithInt:1] forKey:@"UserLanguageArguments"];
        if(variant==7) {
            [resolved removeAllObjects];[resolved addEntriesFromDictionary:[NSDictionary dictionaryWithObjectsAndKeys:Request(),@"Requested",[NSNumber numberWithInt:2],@"PlanVersion",[NSArray array],@"Warnings",[NSArray array],@"Skipped",nil]];
            [regional setObject:[NSArray array] forKey:@"Steps"];
        }
        if(variant==8)[resolved removeObjectForKey:@"DesktopNativeLanguage"];
        if(variant==9) {
            [resolved removeObjectForKey:@"UserLanguages"];[resolved removeObjectForKey:@"DesktopLanguage"];[resolved removeObjectForKey:@"DesktopNativeLanguage"];
            NSMutableArray *remaining=[NSMutableArray array];for(NSDictionary *step in steps)if(![[step objectForKey:@"Name"] isEqual:@"UserLanguages"])[remaining addObject:step];
            [regional setObject:remaining forKey:@"Steps"];
        }
        [Plist(j) writeToFile:path atomically:YES];chmod([path fileSystemRepresentation],0600);unsigned before=b->writes;BOOL rejected=NO;
        @try { rejected=!Apply(j,path,b,&error)&&error!=nil&&b->writes==before; } @catch(NSException *exception) { (void)exception; }
        Check(rejected,"malformed recovery metadata is rejected before native side effects without throwing");Remove(path);
    }
}
static void CommandRecoveryTests(void) {
    NSMutableDictionary *j=Journal();NSString *path=SaveFixture(j);FixtureBackend *b=[[FixtureBackend alloc] init];NSError *error=nil;
    b->legacyResources=YES;b->migrationAware=YES;b->directionSupported=YES;
    Check(Apply(j,path,b,&error),"failure matrix baseline reaches native verified state");
    unsigned count=(unsigned)[b->commands count],normalWrites=b->writes;Remove(path);
    for(unsigned after=0;after<2;after++)for(unsigned command=1;command<=count;command++) {
        j=Journal();path=SaveFixture(j);b=[[FixtureBackend alloc] init];
        b->legacyResources=YES;b->migrationAware=YES;b->directionSupported=YES;b->failCommand=command;b->failAfterEffect=after;
        Apply(j,path,b,&error);
        // Recreate caller state from the on-disk journal, as a restarted helper does.
        j=[NSMutableDictionary dictionaryWithContentsOfFile:path];b->failCommand=0;
        Check(Apply(j,path,b,&error),"every native command failure can resume from its durable on-disk intent");
        Check(b->writes==normalWrites,"recovery never repeats a setter whose native After value already matches");
        Check([[b->userDomain objectForKey:@"AppleLanguages"] isEqual:[NSArray arrayWithObject:@"French"]]&&[[b->userDomain objectForKey:@"AppleLocale"] isEqual:@"fr_FR"]&&[b->zone isEqual:@"Asia/Shanghai"]&&[[[j objectForKey:@"RegionalSettings"] objectForKey:@"Phase"] isEqual:@"verified"],"fault recovery preserves language encoding, region, timezone and verified state");
        Remove(path);
    }
    j=Journal();path=SaveFixture(j);b=[[FixtureBackend alloc] init];b->breakJournalAfterWrite=YES;
    Check(!Apply(j,path,b,&error)&&b->writes==1,"journal save failure after a native write cannot report success");
    chmod([path fileSystemRepresentation],0600);b->breakJournalAfterWrite=NO;j=[NSMutableDictionary dictionaryWithContentsOfFile:path];
    Check(Apply(j,path,b,&error)&&b->writes==4,"durable intent survives failed post-write journal persistence without replaying its setter");Remove(path);
    for(id status in [NSArray arrayWithObjects:[NSNumber numberWithDouble:0.5],[NSNumber numberWithInt:256],@"0",nil]) {
        j=Journal();path=SaveFixture(j);b=[[FixtureBackend alloc] init];b->invalidStatus=status;
        Check(!Apply(j,path,b,&error)&&b->writes==0,"invalid native exit status cannot authorize writes");Remove(path);
    }
    fprintf(stderr,"Exercised %u command boundaries before and after native execution.\n",count);
}
static void LongNativePreferenceTests(void) {
    NSMutableDictionary *j=Journal();NSString *path=SaveFixture(j);FixtureBackend *b=[[FixtureBackend alloc] init];NSError *error=nil;
    NSArray *native=[NSArray arrayWithObjects:@"en",@"fr",@"de",@"it",@"nl",@"es",@"pt",@"ja",@"ko",@"zh-Hans",@"zh-Hant",@"sv",@"da",@"nb",@"fi",@"ru",@"pl",@"cs",@"hu",@"tr",nil];
    [b->userDomain setObject:native forKey:@"AppleLanguages"];b->failLanguageWrite=YES;
    [b->userDomain setObject:[@"x" stringByPaddingToLength:300 withString:@"x" startingAtIndex:0] forKey:@"AppleLocale"];
    Check(!Apply(j,path,b,&error),"long native preference fixture interrupts after user language initialization");
    j=[NSMutableDictionary dictionaryWithContentsOfFile:path];b->failLanguageWrite=NO;
    Check(Apply(j,path,b,&error),"a captured native Before list longer than the request limit remains recoverable");Remove(path);
}
int main(void) {
#if __has_feature(objc_arc)
    @autoreleasepool {
#else
    NSAutoreleasePool *pool=[[NSAutoreleasePool alloc] init];
#endif
    NSError *error=nil;
    NativePlanTests();
    RecoveryUpgradeTests();
    FinalDriftTests();
    MalformedBoundaryTests();
    CommandRecoveryTests();
    LongNativePreferenceTests();
    NSMutableDictionary *j=Journal(); NSString *path=SaveFixture(j); FixtureBackend *b=[[FixtureBackend alloc] init];
    [b->systemDomain setObject:@"retain installed regional defaults" forKey:@"UnrelatedPreference"];
    Check(Apply(j,path,b,&error),"fresh owned account applies regional request");
    Check([[b->systemDomain objectForKey:@"UnrelatedPreference"] isEqual:@"retain installed regional defaults"],"initial login language preserves every non-target global preference on destructive legacy systems");
    Check([[b->userDomain objectForKey:@"AppleLanguages"] isEqual:[NSArray arrayWithObject:@"fr"]],"target user gets typed preferred array");
    Check([[b->userDomain objectForKey:@"AppleLocale"] isEqual:@"fr_FR"],"target user gets native locale");
    Check([[b->systemDomain objectForKey:@"AppleLanguages"] isEqual:[NSArray arrayWithObject:@"fr"]],"login language readback comes from system globals");
    Check([b->zone isEqual:@"Asia/Shanghai"],"timezone applied and read back");
    Check([[[j objectForKey:@"RegionalSettings"] objectForKey:@"Phase"] isEqual:@"verified"],"verified regional result persisted");
    unsigned old=b->writes; [b->userDomain setObject:@"later user choice" forKey:@"AppleLocale"];
    Check(Apply(j,path,b,&error)&&b->writes==old,"verified transaction never reapplies user choices");
    [j setObject:@"login-credential-prepared" forKey:@"Phase"];
    [Plist(j) writeToFile:path atomically:YES]; chmod([path fileSystemRepresentation],0600);
    unsigned commandCount=(unsigned)[b->commands count];
    Check(Apply(j,path,b,&error)&&b->writes==old&&[b->commands count]==commandCount,"verified regional settings permit interrupted first-login recovery without native reads or writes"); Remove(path);
    j=Journal(); [j setObject:@"login-credential-prepared" forKey:@"Phase"]; path=SaveFixture(j); b=[[FixtureBackend alloc] init];
    Check(!Apply(j,path,b,&error)&&[b->commands count]==0&&![j objectForKey:@"RegionalSettings"],"unverified regional settings cannot begin after login credential preparation"); Remove(path);
    j=Journal();path=SaveFixture(j);b=[[FixtureBackend alloc] init];b->directionSupported=YES;
    [b->userDomain setObject:[NSNumber numberWithInt:1] forKey:@"AppleTextDirection"];
    [b->systemDomain setObject:[NSNumber numberWithInt:1] forKey:@"AppleTextDirection"];
    Check(Apply(j,path,b,&error),"native direction settings apply within the owned transaction");
    Check([[b->userDomain objectForKey:@"AppleTextDirection"] isEqual:[NSNumber numberWithInt:0]]&&[[b->systemDomain objectForKey:@"AppleTextDirection"] isEqual:[NSNumber numberWithInt:0]],"user and login text directions are both read back before handoff");Remove(path);
    j=Journal();path=SaveFixture(j);b=[[FixtureBackend alloc] init];b->directionSupported=YES;b->exportUnavailable=YES;b->openStepDefaults=YES;
    [b->userDomain setObject:[NSNumber numberWithInt:1] forKey:@"AppleTextDirection"];
    Check(Apply(j,path,b,&error),"period numeric defaults require native read-type confirmation before parsing OpenStep text");Remove(path);
    j=Journal();path=SaveFixture(j);b=[[FixtureBackend alloc] init];b->directionSupported=YES;b->exportUnavailable=YES;b->openStepDefaults=YES;
    [b->userDomain setObject:@"0" forKey:@"AppleTextDirection"];
    Check(!Apply(j,path,b,&error)&&[[b->userDomain objectForKey:@"AppleTextDirection"] isEqual:@"0"],"a numeric-looking native string cannot be reinterpreted as an integer preference");Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init]; b->migrationAware=YES; [b->versionDomain setObject:@"original build record" forKey:@"BuildVersionStampAsString"];
    Check(Apply(j,path,b,&error),"migration-aware fresh account applies regional settings");
    Check([[b->versionDomain objectForKey:@"SystemVersionStampAsString"] isEqual:@"10.14.6"]&&[[b->versionDomain objectForKey:@"SystemVersionStampAsNumber"] unsignedIntValue]==0x0a0e0600,"new-account system origin has verified native string and numeric versions before first login");
    Check([[b->versionDomain objectForKey:@"BuildVersionStampAsString"] isEqual:@"original build record"]&&![b->userDomain objectForKey:@"AppleLanguagesDidMigrate"],"initial account version preserves native build records and does not manufacture a migration-complete flag");
    NSArray *migrationSteps=[[j objectForKey:@"RegionalSettings"] objectForKey:@"Steps"];
    Check([[[migrationSteps objectAtIndex:0] objectForKey:@"Name"] isEqual:@"UserSystemVersion"],"new-account system version is durably verified before publishing user languages");
    Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init]; b->migrationAware=YES; b->wrongMigrationReadback=YES;
    Check(!Apply(j,path,b,&error)&&![b->userDomain objectForKey:@"AppleLanguages"],"unverified migration marker cannot publish a language list vulnerable to first-login truncation");
    b->wrongMigrationReadback=NO;
    Check(Apply(j,path,b,&error),"pending migration marker resumes before user languages are published");
    Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init]; b->wrongReadback=YES;
    Check(!Apply(j,path,b,&error),"setter success without matching readback fails");
    Check(![[[j objectForKey:@"RegionalSettings"] objectForKey:@"Phase"] isEqual:@"verified"],"failed verification cannot mark transaction complete");
    old=b->writes; b->wrongReadback=NO;
    Check(Apply(j,path,b,&error)&&b->writes>old,"unchanged Before allows retry after interrupted setter"); Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init]; b->changedAfterWrite=YES;
    Check(!Apply(j,path,b,&error),"unexpected preference after write fails"); old=b->writes;
    Check(!Apply(j,path,b,&error)&&b->writes==old,"uncertain externally changed preference is never overwritten"); Remove(path);
    j=Journal();path=SaveFixture(j);b=[[FixtureBackend alloc] init];b->changeLanguagesAfterLogin=YES;
    Check(!Apply(j,path,b,&error)&&![[[j objectForKey:@"RegionalSettings"] objectForKey:@"Phase"] isEqual:@"verified"],"final native resource verification catches a language change after an earlier verified step");
    old=b->writes;b->changeLanguagesAfterLogin=NO;
    Check(!Apply(j,path,b,&error)&&b->writes==old,"failed final language verification never overwrites a later user choice on retry");Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init]; b->failWrite=YES;
    Check(!Apply(j,path,b,&error),"nonzero command leaves intent pending"); old=b->writes; b->failWrite=NO;
    Check(Apply(j,path,b,&error)&&b->writes==old+3,"matching After recovers without duplicate first setter"); Remove(path);
    j=Journal(); [j setObject:@"autologin-armed" forKey:@"Phase"]; path=SaveFixture(j); b=[[FixtureBackend alloc] init];
    Check(Apply(j,path,b,&error)&&b->writes==0&&![j objectForKey:@"RegionalSettings"],"old armed journal preserves guest defaults"); Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init]; [j setObject:@"different" forKey:@"MachineIdentifier"];
    Check(!Apply(j,path,b,&error)&&b->writes==0,"disk journal must match caller exactly"); Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init]; chmod([path fileSystemRepresentation],0644);
    Check(!Apply(j,path,b,&error)&&b->writes==0,"unsafe journal permissions rejected"); Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init]; b->exportUnavailable=YES;
    Check(Apply(j,path,b,&error),"old defaults read plist fallback verifies typed values"); Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init]; b->exportUnavailable=YES; b->openStepDefaults=YES;
    Check(Apply(j,path,b,&error),"period OpenStep defaults output is parsed as typed preferences"); Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init];
    [b->userDomain setObject:[NSArray array] forKey:@"AppleLanguages"]; [b->userDomain setObject:@"" forKey:@"AppleLocale"];
    Check(Apply(j,path,b,&error),"typed empty preference overrides are valid initial values"); Remove(path);
    NSMutableDictionary *badEvidence=[NSMutableDictionary dictionaryWithDictionary:[b regionalCapabilities]];
    [badEvidence setObject:[NSNumber numberWithInt:104] forKey:@"Version"];
    Check(VIMGuestRegionalPlan(Request(),badEvidence,&error)!=nil,"malformed optional product version cannot crash native regional planning");
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init]; b->legacyResources=YES;
    b->periodVersion=@"10.6.7";
    NSMutableDictionary *snow=[NSMutableDictionary dictionaryWithDictionary:Request()];
    [snow setObject:[NSArray arrayWithObject:@"zh-Hans-CN"] forKey:@"PreferredLanguages"];[snow setObject:@"CN" forKey:@"RegionCode"];b->journalPath=path;
    Check(VIMGuestApplyRegionalSettingsWithBackend(snow,@"alice",501,j,path,b,&error),"Snow Leopard initializes the Chinese login preference through the per-key writer");
    Check([[b->userDomain objectForKey:@"AppleLanguages"] isEqual:[NSArray arrayWithObject:@"zh_CN"]],"period desktop preferences use resource-compatible native language IDs"); Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init];b->legacyResources=YES;b->periodVersion=@"10.6.7";b->failLanguageWrite=YES;
    Check(!Apply(j,path,b,&error),"interrupted period login setter retains its pending intent");
    NSMutableDictionary *oldRegional=[j objectForKey:@"RegionalSettings"];
    NSMutableDictionary *oldResolution=[NSMutableDictionary dictionaryWithDictionary:[oldRegional objectForKey:@"Resolved"]];
    [oldResolution removeObjectForKey:@"LoginLanguageArgument"];[oldResolution removeObjectForKey:@"UserLanguageArguments"];[oldRegional setObject:oldResolution forKey:@"Resolved"];
    NSMutableArray *legacySteps=[NSMutableArray array];
    for(NSDictionary *step in [oldRegional objectForKey:@"Steps"])if(![[step objectForKey:@"Name"] isEqual:@"UserLanguageEncoding"])[legacySteps addObject:step];
    [oldRegional setObject:legacySteps forKey:@"Steps"];[b->userDomain setObject:[NSArray arrayWithObject:@"fr"] forKey:@"AppleLanguages"];
    [Plist(j) writeToFile:path atomically:YES];chmod([path fileSystemRepresentation],0600);old=b->writes;b->failLanguageWrite=NO;
    Check(Apply(j,path,b,&error)&&b->writes==old+3,"old pending Snow Leopard journal corrects equivalent language spelling and resumes without replaying the verified locale");
    Check([[b->userDomain objectForKey:@"AppleLanguages"] isEqual:[NSArray arrayWithObject:@"French"]],"old canonical preference journal gains a durable native encoding step");
    Check([[[j objectForKey:@"RegionalSettings"] objectForKey:@"Requested"] isEqual:Request()],"period argument correction preserves immutable user intent"); Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init];b->legacyResources=YES;b->periodVersion=@"10.6.7";b->failLanguageWrite=YES;
    Apply(j,path,b,&error);
    oldRegional=[j objectForKey:@"RegionalSettings"];oldResolution=[NSMutableDictionary dictionaryWithDictionary:[oldRegional objectForKey:@"Resolved"]];
    [oldResolution removeObjectForKey:@"UserLanguageArguments"];[oldRegional setObject:oldResolution forKey:@"Resolved"];legacySteps=[NSMutableArray array];
    for(NSDictionary *step in [oldRegional objectForKey:@"Steps"])if(![[step objectForKey:@"Name"] isEqual:@"UserLanguageEncoding"])[legacySteps addObject:step];
    [oldRegional setObject:legacySteps forKey:@"Steps"];[b->userDomain setObject:[NSArray arrayWithObject:@"de"] forKey:@"AppleLanguages"];
    [Plist(j) writeToFile:path atomically:YES];chmod([path fileSystemRepresentation],0600);old=b->writes;b->failLanguageWrite=NO;
    Check(!Apply(j,path,b,&error)&&b->writes==old&&[[b->userDomain objectForKey:@"AppleLanguages"] isEqual:[NSArray arrayWithObject:@"de"]],"native encoding upgrade refuses to replace a changed language choice"); Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init]; b->legacyResources=YES; b->periodVersion=@"10.4.11";
    Check(Apply(j,path,b,&error),"Tiger initializes its native login preference without the destructive legacy command"); Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init]; b->legacyResources=YES;
    Check(Apply(j,path,b,&error),"period login resources survive typed canonical readback"); Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init]; b->legacyResources=YES;
    NSMutableDictionary *chinese=[NSMutableDictionary dictionaryWithDictionary:Request()]; [chinese removeObjectForKey:@"RegionCode"];
    [chinese setObject:[NSArray arrayWithObject:@"zh-Hant-TW"] forKey:@"PreferredLanguages"]; b->journalPath=path;
    Check(VIMGuestApplyRegionalSettingsWithBackend(chinese,@"alice",501,j,path,b,&error),"Chinese legacy resource region survives native preference verification"); Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init]; b->malformedDefaults=YES;
    Check(!Apply(j,path,b,&error)&&b->writes==0,"malformed native defaults output cannot justify a write"); Remove(path);
    b=[[FixtureBackend alloc] init]; b->settersUnavailable=YES;
    Check(!VIMGuestRegionalPlan(Request(),[b regionalCapabilities],&error) && error && [b->commands count]==0,"pure strict preflight rejects unavailable setters before an account exists");
    NSMutableDictionary *planFallback=[NSMutableDictionary dictionaryWithDictionary:Request()]; [planFallback setObject:@"fallback" forKey:@"UnsupportedPolicy"];
    NSDictionary *plan=VIMGuestRegionalPlan(planFallback,[b regionalCapabilities],&error);
    Check(plan && [[plan objectForKey:@"Warnings"] count]==4 && ![plan objectForKey:@"UserLanguages"] && ![plan objectForKey:@"LoginLanguage"] && ![plan objectForKey:@"LocaleIdentifier"] && ![plan objectForKey:@"TimeZoneIdentifier"] && [b->commands count]==0,"pure fallback preflight exposes and omits every unavailable setter");
    Check(!VIMGuestRegionalPlan(nil,[b regionalCapabilities],&error)&&!error,"pure absent preflight preserves legacy policy");
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init]; b->settersUnavailable=YES;
    Check(!Apply(j,path,b,&error)&&b->writes==0,"strict request rejects unavailable native setters before writing"); Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init]; b->settersUnavailable=YES;
    NSMutableDictionary *fallback=[NSMutableDictionary dictionaryWithDictionary:Request()]; [fallback setObject:@"fallback" forKey:@"UnsupportedPolicy"];
    b->journalPath=path;
    Check(VIMGuestApplyRegionalSettingsWithBackend(fallback,@"alice",501,j,path,b,&error)&&b->writes==0,"fallback records unsupported setters without mutating preferences");
    Check([[[j objectForKey:@"RegionalSettings"] objectForKey:@"Warnings"] count]==4,"fallback exposes each unavailable setter warning"); Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init]; b->wrongReadback=YES; Apply(j,path,b,&error);
    NSMutableDictionary *changed=[NSMutableDictionary dictionaryWithDictionary:Request()]; [changed setObject:@"US" forKey:@"RegionCode"]; old=b->writes;
    Check(!VIMGuestApplyRegionalSettingsWithBackend(changed,@"alice",501,j,path,b,&error)&&b->writes==old,"saved requested identity cannot be changed on retry");
    NSMutableDictionary *regional=[j objectForKey:@"RegionalSettings"];
    NSMutableArray *steps=[regional objectForKey:@"Steps"]; NSMutableDictionary *first=[steps objectAtIndex:0];
    [first setObject:[NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithBool:YES],@"Present",@"bad target",@"Value",nil] forKey:@"After"];
    [Plist(j) writeToFile:path atomically:YES]; chmod([path fileSystemRepresentation],0600);
    Check(!Apply(j,path,b,&error)&&b->writes==old,"malformed recovery target is rejected before retry setter"); Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init];
    Check(VIMGuestApplyRegionalSettingsWithBackend(nil,@"alice",501,j,path,b,&error)&&[b->commands count]==0,"absent request preserves legacy behavior"); Remove(path);
    j=Journal(); path=SaveFixture(j); b=[[FixtureBackend alloc] init];
    NSString *linked=[[path stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"linked.plist"];
    symlink([path fileSystemRepresentation],[linked fileSystemRepresentation]);
    Check(!Apply(j,linked,b,&error)&&b->writes==0,"symlink journal cannot authorize preference changes"); Remove(path);
    fprintf(stderr,"%s: %u guest regional checks, %u failures\n",failures?"FAIL":"PASS",checks,failures);
#if __has_feature(objc_arc)
    }
#else
    [pool drain];
#endif
    return failures?1:0;
}
