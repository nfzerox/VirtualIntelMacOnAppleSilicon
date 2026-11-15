#import "guest-regional-settings.h"
#import "guest-regional-policy.h"
// This unit uses Tiger’s original plist API, not the newer-selector shim.
#ifdef NSPropertyListSerialization
#undef NSPropertyListSerialization
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#if __has_feature(objc_arc)
#define VIM_AUTORELEASE(x) (x)
#define VIM_RELEASE(x) do {} while (0)
#else
#define VIM_AUTORELEASE(x) [(x) autorelease]
#define VIM_RELEASE(x) [(x) release]
#endif

static BOOL VIMRegionalFail(NSError **error, NSInteger code, NSString *reason) {
    if (error) *error=[NSError errorWithDomain:@"VIMGuestRegionalError" code:code
        userInfo:[NSDictionary dictionaryWithObject:reason forKey:NSLocalizedDescriptionKey]];
    return NO;
}
static NSDictionary *VIMRegionalEnvironment(void) {
    NSMutableDictionary *environment=[NSMutableDictionary dictionaryWithDictionary:[[NSProcessInfo processInfo] environment]];
    [environment setObject:@"C" forKey:@"LC_ALL"]; [environment setObject:@"C" forKey:@"LANG"];
    return environment;
}
static id VIMRegionalPropertyList(NSData *data) {
    if (![data length]) return nil;
    return [NSPropertyListSerialization propertyListFromData:data mutabilityOption:NSPropertyListMutableContainersAndLeaves
        format:NULL errorDescription:NULL];
}
static NSData *VIMRegionalPlistData(id value) {
    return [NSPropertyListSerialization dataFromPropertyList:value format:NSPropertyListXMLFormat_v1_0 errorDescription:NULL];
}
@interface VIMNativeRegionalBackend : NSObject <VIMGuestRegionalBackend>
@end
@implementation VIMNativeRegionalBackend
- (NSDictionary *)runExecutable:(NSString *)executable arguments:(NSArray *)arguments environment:(NSDictionary *)environment error:(NSError **)error {
    NSTask *task=VIM_AUTORELEASE([[NSTask alloc] init]); NSPipe *pipe=[NSPipe pipe];
    [task setLaunchPath:executable]; [task setArguments:arguments]; [task setEnvironment:environment];
    [task setStandardInput:[NSFileHandle fileHandleWithNullDevice]];
    [task setStandardOutput:pipe]; [task setStandardError:pipe];
    @try {
        [task launch]; NSData *data=[[pipe fileHandleForReading] readDataToEndOfFile]; [task waitUntilExit];
        return [NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithInt:[task terminationStatus]],@"Status",data,@"Output",nil];
    } @catch (NSException *exception) {
        VIMRegionalFail(error,10,@"A native regional command could not be launched."); return nil;
    }
}
- (NSDictionary *)regionalCapabilities { return VIMGuestRegionalCapabilities(); }
@end
static NSDictionary *VIMRegionalRun(id<VIMGuestRegionalBackend> backend, NSString *path, NSArray *arguments, NSError **error) {
    NSDictionary *result=[backend runExecutable:path arguments:arguments environment:VIMRegionalEnvironment() error:error];
    if (![result isKindOfClass:[NSDictionary class]] || ![[result objectForKey:@"Status"] isKindOfClass:[NSNumber class]] ||
        ![[result objectForKey:@"Output"] isKindOfClass:[NSData class]]) {
        VIMRegionalFail(error,10,@"A native regional command returned an invalid result."); return nil;
    }
    double status=[[result objectForKey:@"Status"] doubleValue];
    if(status<0||status>255||status!=(double)[[result objectForKey:@"Status"] intValue]) {
        VIMRegionalFail(error,10,@"A native regional command returned an invalid exit status.");return nil;
    }
    return result;
}
static NSString *VIMRegionalOutput(NSDictionary *result) {
    id data=[result objectForKey:@"Output"];
    return [data isKindOfClass:[NSData class]]?VIM_AUTORELEASE([[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]):nil;
}
static NSString *VIMRegionalCanonical(NSString *language) {
    return VIMRegionalCanonicalLanguageIdentifier(language);
}
static BOOL VIMRegionalSelectionMatches(NSArray *available, NSArray *preferences, NSString *native) {
    if (![available count] || ![preferences count] || !native) return NO;
    NSArray *chosen=[NSBundle preferredLocalizationsFromArray:available forPreferences:preferences];
    return [chosen count] && [VIMRegionalCanonical([chosen objectAtIndex:0]) isEqual:VIMRegionalCanonical(native)];
}
static NSArray *VIMRegionalDesktopInventories(NSDictionary *capabilities, NSError **error) {
    id evidence=[capabilities objectForKey:@"ResourceEvidence"];
    if(evidence&&![evidence isKindOfClass:[NSDictionary class]]){VIMRegionalFail(error,15,@"Invalid native resource evidence.");return nil;}
    NSMutableArray *inventories=[NSMutableArray array];
    NSArray *names=[NSArray arrayWithObjects:@"Finder",@"AppKit",nil];unsigned index,item;
    for(index=0;index<[names count];index++) {
        id record=[evidence objectForKey:[names objectAtIndex:index]];
        if(record&&![record isKindOfClass:[NSDictionary class]]){VIMRegionalFail(error,15,@"Invalid native resource inventory.");return nil;}
        id values=[record objectForKey:@"Languages"]?:[capabilities objectForKey:@"DesktopLanguages"];
        if(![values isKindOfClass:[NSArray class]]||[values count]>10000){VIMRegionalFail(error,15,@"Invalid native resource languages.");return nil;}
        for(item=0;item<[values count];item++)if(![[values objectAtIndex:item] isKindOfClass:[NSString class]]||[[values objectAtIndex:item] length]>256){VIMRegionalFail(error,15,@"Invalid native resource language.");return nil;}
        [inventories addObject:values];
    }
    return inventories;
}
static NSArray *VIMRegionalResourceLanguages(NSString *bundlePath, NSMutableDictionary *evidence) {
    NSBundle *bundle=[NSBundle bundleWithPath:bundlePath]; NSString *resources=[bundle resourcePath];
    NSMutableArray *languages=[NSMutableArray array];
    NSArray *files=resources?[[NSFileManager defaultManager] directoryContentsAtPath:resources]:nil;
    unsigned index;
    for (index=0; index<[files count]; index++) {
        NSString *file=[files objectAtIndex:index]; BOOL directory=NO;
        if (![[file pathExtension] isEqual:@"lproj"] ||
            ![[NSFileManager defaultManager] fileExistsAtPath:[resources stringByAppendingPathComponent:file] isDirectory:&directory] || !directory) continue;
        NSString *native=[file stringByDeletingPathExtension];
        /* Base resources are language-independent, never a language inventory. */
        if ([native caseInsensitiveCompare:@"Base"]==NSOrderedSame)continue;
        if (VIMRegionalCanonical(native)) [languages addObject:native];
    }
    NSArray *sorted=[languages sortedArrayUsingSelector:@selector(compare:)];
    [evidence setObject:bundlePath forKey:@"BundlePath"];
    if (resources) [evidence setObject:resources forKey:@"ResourcesPath"];
    [evidence setObject:sorted forKey:@"Languages"];
    return sorted;
}
static BOOL VIMRegionalContainsLanguage(NSArray *languages, NSString *value) {
    NSString *canonical=VIMRegionalCanonical(value); if (!canonical) return NO; unsigned index;
    for (index=0; index<[languages count]; index++)
        if ([canonical isEqual:VIMRegionalCanonical([languages objectAtIndex:index])]) return YES;
    return NO;
}
static NSString *VIMRegionalAccountVersion(id value) {
    if(![value isKindOfClass:[NSString class]]||[value length]>32)return nil;
    NSArray *parts=[value componentsSeparatedByString:@"."]; if([parts count]<2||[parts count]>3)return nil;
    unsigned index,offset;
    for(index=0;index<[parts count];index++){
        NSString *part=[parts objectAtIndex:index];if(![part length]||[part length]>3)return nil;
        for(offset=0;offset<[part length];offset++){unichar c=[part characterAtIndex:offset];if(c<'0'||c>'9')return nil;}
    }
    if([[parts objectAtIndex:0] intValue]>127)return nil;
    for(index=1;index<[parts count];index++)if([[parts objectAtIndex:index] intValue]>255)return nil;
    return [value compare:@"10.9" options:NSNumericSearch]!=NSOrderedAscending?value:nil;
}
NSDictionary *VIMGuestRegionalCapabilities(void) {
    NSMutableDictionary *finderEvidence=[NSMutableDictionary dictionary], *appKitEvidence=[NSMutableDictionary dictionary], *loginEvidence=[NSMutableDictionary dictionary];
    NSString *finderPath=@"/System/Library/CoreServices/Finder.app", *appKitPath=@"/System/Library/Frameworks/AppKit.framework", *loginPath=@"/System/Library/CoreServices/loginwindow.app";
    NSArray *finder=VIMRegionalResourceLanguages(finderPath,finderEvidence), *appKit=VIMRegionalResourceLanguages(appKitPath,appKitEvidence), *login=VIMRegionalResourceLanguages(loginPath,loginEvidence);
    NSMutableArray *desktop=[NSMutableArray array]; unsigned index;
    for (index=0; index<[finder count]; index++) {
        NSString *native=[finder objectAtIndex:index];
        if (VIMRegionalContainsLanguage(appKit,native)) [desktop addObject:native];
    }
    NSFileManager *manager=[NSFileManager defaultManager];
    BOOL recovery=![manager fileExistsAtPath:finderPath] || [manager fileExistsAtPath:@"/etc/rc.cdrom"] ||
        [manager fileExistsAtPath:@"/System/Installation/CDIS/Recovery Springboard.app"];
    NSDictionary *systemVersion=[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"];
    VIMNativeRegionalBackend *backend=VIM_AUTORELEASE([[VIMNativeRegionalBackend alloc] init]);
    NSDictionary *zoneHelp=VIMRegionalRun(backend,@"/usr/sbin/systemsetup",[NSArray arrayWithObject:@"-help"],NULL);
    NSString *zoneText=VIMRegionalOutput(zoneHelp);
    BOOL zoneSetter=[[zoneHelp objectForKey:@"Status"] intValue]==0 && zoneText && [zoneText rangeOfString:@"-settimezone" options:NSCaseInsensitiveSearch].location!=NSNotFound &&
        [zoneText rangeOfString:@"-gettimezone" options:NSCaseInsensitiveSearch].location!=NSNotFound;
    NSMutableArray *zones=[NSMutableArray array];
    if(zoneSetter) {
        NSDictionary *listed=VIMRegionalRun(backend,@"/usr/sbin/systemsetup",[NSArray arrayWithObject:@"-listtimezones"],NULL);
        NSString *text=VIMRegionalOutput(listed);
        if([[listed objectForKey:@"Status"] intValue]==0 && text) {
            NSArray *lines=[text componentsSeparatedByString:@"\n"];
            for(index=0;index<[lines count];index++) {
                NSString *zone=[[lines objectAtIndex:index] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
                if([zone length]&&[zone length]<=128&&[NSTimeZone timeZoneWithName:zone]) {
                    NSDictionary *r=[NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithInt:1],@"FormatVersion",@"custom",@"Source",@"fallback",@"UnsupportedPolicy",[NSArray arrayWithObject:@"en"],@"PreferredLanguages",zone,@"TimeZoneIdentifier",nil];
                    if(VIMRegionalNormalizeRequest(r,NULL)&&![zones containsObject:zone])[zones addObject:zone];
                }
            }
        }
        zoneSetter=[zones count]>0;
    }
    BOOL languageSetter=[manager isExecutableFileAtPath:@"/usr/bin/defaults"];
    BOOL userSetter=languageSetter && [manager isExecutableFileAtPath:@"/usr/bin/sudo"];
    BOOL nativeFormat=NO;
    for(index=0;index<[desktop count];index++) {
        NSString *native=[desktop objectAtIndex:index];NSArray *canonical=[NSArray arrayWithObject:VIMRegionalCanonical(native)];
        if(!VIMRegionalSelectionMatches(finder,canonical,native)||!VIMRegionalSelectionMatches(appKit,canonical,native)){nativeFormat=YES;break;}
    }
    NSMutableDictionary *directions=[NSMutableDictionary dictionary];
    typedef CFIndex (*DirectionFunction)(CFStringRef);
    DirectionFunction direction=(DirectionFunction)dlsym(RTLD_DEFAULT,"CFLocaleGetLanguageCharacterDirection");
    NSMutableArray *directionLanguages=[NSMutableArray arrayWithArray:login];
    for(index=0;index<[desktop count];index++)if(![directionLanguages containsObject:[desktop objectAtIndex:index]])[directionLanguages addObject:[desktop objectAtIndex:index]];
    if(direction)for(index=0;index<[directionLanguages count];index++) {
        NSString *native=[directionLanguages objectAtIndex:index], *canonical=VIMRegionalCanonical(native);
#if __has_feature(objc_arc)
        CFIndex value=direction((__bridge CFStringRef)canonical);
#else
        CFIndex value=direction((CFStringRef)canonical);
#endif
        if(value==1||value==2)[directions setObject:[NSNumber numberWithInt:value==2?1:0] forKey:native];
    }
    NSMutableDictionary *result=[NSMutableDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithInt:1],@"FormatVersion",
        desktop,@"DesktopLanguages",login,@"LoginLanguages",[NSLocale availableLocaleIdentifiers],@"LocaleIdentifiers",
        [NSArray arrayWithArray:zones],@"TimeZoneIdentifiers",recovery?@"Recovery":@"Installed",@"Environment",
        nativeFormat?@"native":@"canonical",@"LanguagePreferenceFormat",directions,@"LoginTextDirections",directions,@"DesktopTextDirections",
        [NSNumber numberWithBool:userSetter],@"UserDefaultsSetterAvailable",[NSNumber numberWithBool:languageSetter],@"LoginLanguageSetterAvailable",
        [NSNumber numberWithBool:zoneSetter],@"TimeZoneSetterAvailable",
        [NSDictionary dictionaryWithObjectsAndKeys:finderEvidence,@"Finder",appKitEvidence,@"AppKit",loginEvidence,@"LoginWindow",nil],@"ResourceEvidence",nil];
    NSString *version=[systemVersion objectForKey:@"ProductVersion"], *build=[systemVersion objectForKey:@"ProductBuildVersion"];
    if (version) [result setObject:version forKey:@"Version"]; if (build) [result setObject:build forKey:@"Build"];
    // A new native account originates on this OS. Without loginwindow’s
    // system-version record, UserAccountUpdater passes previousVersion=0.0
    // to IntlPreferences and discards secondary languages as pre-10.9 data.
    // Record only the system version; leave build stamps and unrelated native
    // updater work alone. This remains an owned pre-first-login transaction.
    NSString *migration=VIMRegionalAccountVersion(version);
    if(migration&&[manager isExecutableFileAtPath:@"/System/Library/CoreServices/UserAccountUpdater"])
        [result setObject:migration forKey:@"AccountSystemVersion"];
    NSArray *zoneVersions=[NSArray arrayWithObjects:@"/var/db/timezone/zoneinfo/+VERSION",@"/usr/share/zoneinfo/+VERSION",nil];
    for(index=0;index<[zoneVersions count];index++) {
        NSData *data=[NSData dataWithContentsOfFile:[zoneVersions objectAtIndex:index]];
        NSString *text=[data length]&&[data length]<=64?VIM_AUTORELEASE([[NSString alloc] initWithData:data encoding:NSASCIIStringEncoding]):nil;
        text=[text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if([text length]){[result setObject:text forKey:@"TimeZoneDataVersion"];break;}
    }
    NSString *development=[[[NSBundle bundleWithPath:finderPath] infoDictionary] objectForKey:@"CFBundleDevelopmentRegion"];
    if (VIMRegionalContainsLanguage(desktop,development)) [result setObject:development forKey:@"DevelopmentLanguage"];
    NSDictionary *global=[NSDictionary dictionaryWithContentsOfFile:@"/Library/Preferences/.GlobalPreferences.plist"];
    id defaultLanguages=[global objectForKey:@"AppleLanguages"];
    NSString *defaultLanguage=[defaultLanguages isKindOfClass:[NSArray class]] && [defaultLanguages count]?[defaultLanguages objectAtIndex:0]:nil;
    if (VIMRegionalContainsLanguage(desktop,defaultLanguage)) [result setObject:defaultLanguage forKey:@"DefaultDesktopLanguage"];
    if (VIMRegionalContainsLanguage(login,defaultLanguage)) [result setObject:defaultLanguage forKey:@"DefaultLoginLanguage"];
    return [NSDictionary dictionaryWithDictionary:result];
}

/* All absent defaults are represented by a plist-safe record, never NSNull. */
static NSDictionary *VIMRegionalValue(id value) {
    return value?[NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithBool:YES],@"Present",value,@"Value",nil]:
        [NSDictionary dictionaryWithObject:[NSNumber numberWithBool:NO] forKey:@"Present"];
}
static BOOL VIMRegionalOwnedPath(NSString *path, BOOL directory) {
    struct stat st;
    return !lstat([path fileSystemRepresentation],&st) && st.st_uid==geteuid() && !(st.st_mode&0077) &&
        (directory?S_ISDIR(st.st_mode):(S_ISREG(st.st_mode)&&st.st_nlink==1));
}
static NSDictionary *VIMRegionalReadJournal(NSString *path) {
    if (!VIMRegionalOwnedPath(path,NO)) return nil;
    int fd=open([path fileSystemRepresentation],O_RDONLY|O_NOFOLLOW); if (fd<0) return nil;
    struct stat st; NSMutableData *bytes=[NSMutableData data];
    if (fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_nlink!=1 || st.st_uid!=geteuid() || (st.st_mode&0077) || st.st_size>4*1024*1024) { close(fd); return nil; }
    char buffer[8192]; ssize_t size;
    while ((size=read(fd,buffer,sizeof(buffer)))>0) [bytes appendBytes:buffer length:(unsigned)size];
    close(fd); if (size<0) return nil;
    id result=VIMRegionalPropertyList(bytes); return [result isKindOfClass:[NSDictionary class]]?result:nil;
}
static BOOL VIMRegionalSave(NSDictionary *journal, NSString *path, NSError **error) {
    NSString *parent=[path stringByDeletingLastPathComponent];
    if (!VIMRegionalOwnedPath(parent,YES) || !VIMRegionalOwnedPath(path,NO)) return VIMRegionalFail(error,11,@"The regional journal path is not privately owned.");
    NSData *bytes=VIMRegionalPlistData(journal); if (!bytes) return VIMRegionalFail(error,11,@"The regional journal cannot be serialized.");
    NSString *pattern=[parent stringByAppendingPathComponent:@".RegionalSettings.XXXXXX"];
    char *temporary=strdup([pattern fileSystemRepresentation]); int fd=temporary?mkstemp(temporary):-1;
    if (fd<0) { free(temporary); return VIMRegionalFail(error,11,@"The regional journal temporary file cannot be created."); }
    BOOL ok=fchmod(fd,0600)==0; const char *buffer=[bytes bytes]; NSUInteger left=[bytes length];
    while (ok && left) { ssize_t count=write(fd,buffer,left); if (count<0&&errno==EINTR) continue; if(count<=0){ok=NO;break;} buffer+=count; left-=count; }
    if (ok) ok=fsync(fd)==0; if (close(fd)) ok=NO;
    if (ok) ok=rename(temporary,[path fileSystemRepresentation])==0;
    if (!ok) unlink(temporary); free(temporary);
    if (ok) { int parentFD=open([parent fileSystemRepresentation],O_RDONLY|O_NOFOLLOW); if(parentFD<0)ok=NO; else { if(fsync(parentFD))ok=NO; close(parentFD); } }
    return ok?YES:VIMRegionalFail(error,11,@"The regional journal could not be durably saved.");
}
static NSArray *VIMRegionalDefaultsArguments(NSArray *arguments, NSString *user) {
    NSMutableArray *result=[NSMutableArray array];
    if (user) [result addObjectsFromArray:[NSArray arrayWithObjects:@"-H",@"-u",user,@"--",@"/usr/bin/defaults",nil]];
    [result addObjectsFromArray:arguments]; return result;
}
static NSDictionary *VIMRegionalReadDefaults(id<VIMGuestRegionalBackend> backend, NSString *user, NSString *userDomain, NSError **error) {
    NSString *domain=user?(userDomain?:@"-g"):@"/Library/Preferences/.GlobalPreferences";
    NSString *path=user?@"/usr/bin/sudo":@"/usr/bin/defaults";
    NSArray *arguments=[NSArray arrayWithObjects:@"export",domain,@"-",nil];
    NSDictionary *result=VIMRegionalRun(backend,path,VIMRegionalDefaultsArguments(arguments,user),error);
    if (!result) return nil;
    id plist=[[result objectForKey:@"Status"] intValue]==0?VIMRegionalPropertyList([result objectForKey:@"Output"]):nil;
    if ([plist isKindOfClass:[NSDictionary class]]) return plist;
    /* Tiger/Leopard defaults may not implement export. Their read output is an
     * OpenStep property list; parse it as typed data, never split array text. */
    result=VIMRegionalRun(backend,path,VIMRegionalDefaultsArguments([NSArray arrayWithObjects:@"read",domain,nil],user),error);
    if (!result) return nil;
    if ([[result objectForKey:@"Status"] intValue]==0) {
        plist=VIMRegionalPropertyList([result objectForKey:@"Output"]);
        if ([plist isKindOfClass:[NSDictionary class]]) return plist;
    } else {
        NSString *output=VIMRegionalOutput(result);
        if (output && [output rangeOfString:@"does not exist"].location!=NSNotFound &&
            ([output rangeOfString:@"NSGlobalDomain"].location!=NSNotFound || [output rangeOfString:domain].location!=NSNotFound)) return [NSDictionary dictionary];
    }
    VIMRegionalFail(error,12,@"Native global preferences could not be read as a typed property list."); return nil;
}
static NSDictionary *VIMRegionalReadSetting(id<VIMGuestRegionalBackend> backend, NSString *step, NSString *user, NSError **error) {
    if ([step isEqual:@"TimeZone"]) {
        NSDictionary *result=VIMRegionalRun(backend,@"/usr/sbin/systemsetup",[NSArray arrayWithObject:@"-gettimezone"],error);
        NSString *output=VIMRegionalOutput(result);
        if (!result || [[result objectForKey:@"Status"] intValue]!=0 || ![output hasPrefix:@"Time Zone: "]) { VIMRegionalFail(error,12,@"The system time zone could not be read back."); return nil; }
        NSString *zone=[[output substringFromIndex:11] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (![zone length] || [zone rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"\r\n"]].location!=NSNotFound) { VIMRegionalFail(error,12,@"The native time zone output was malformed."); return nil; }
        return VIMRegionalValue(zone);
    }
    BOOL direction=[step isEqual:@"LoginTextDirection"]||[step isEqual:@"UserTextDirection"];
    BOOL login=[step isEqual:@"LoginLanguage"]||[step isEqual:@"LoginTextDirection"];
    BOOL version=[step isEqual:@"UserSystemVersion"]||[step isEqual:@"UserSystemVersionNumber"];
    NSDictionary *domain=VIMRegionalReadDefaults(backend,login?nil:user,version?@"loginwindow":nil,error); if (!domain) return nil;
    NSString *key=direction?@"AppleTextDirection":([step isEqual:@"UserSystemVersion"]?@"SystemVersionStampAsString":([step isEqual:@"UserSystemVersionNumber"]?@"SystemVersionStampAsNumber":([step isEqual:@"UserLocale"]?@"AppleLocale":@"AppleLanguages"))); id value=[domain objectForKey:key];
    if((direction||[step isEqual:@"UserSystemVersionNumber"])&&[value isKindOfClass:[NSString class]]) {
        // OpenStep defaults output loses numeric type information. Confirm
        // the native stored type before interpreting its decimal text; a
        // genuine string containing "0" must remain an incompatible value.
        NSString *numberDomain=login?@"/Library/Preferences/.GlobalPreferences":(version?@"loginwindow":@"-g");
        NSDictionary *typed=VIMRegionalRun(backend,login?@"/usr/bin/defaults":@"/usr/bin/sudo",VIMRegionalDefaultsArguments([NSArray arrayWithObjects:@"read-type",numberDomain,key,nil],login?nil:user),error);
        NSString *type=[VIMRegionalOutput(typed) stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        BOOL number=[type isEqual:@"Type is integer"]||(direction&&[type isEqual:@"Type is boolean"]);
        unsigned long long parsed=0;unsigned index;BOOL decimal=[value length]>0&&[value length]<=10;
        for(index=0;decimal&&index<[value length];index++) {
            unichar c=[value characterAtIndex:index];if(c<'0'||c>'9'){decimal=NO;break;}
            parsed=parsed*10+(c-'0');if(parsed>0xffffffffULL)decimal=NO;
        }
        if(!typed||[[typed objectForKey:@"Status"] intValue]!=0||!number||!decimal){VIMRegionalFail(error,12,@"A native numeric regional preference could not be typed.");return nil;}
        value=[NSNumber numberWithUnsignedInt:(unsigned)parsed];
    }
    if (value) {
        if ([key isEqual:@"AppleLanguages"]) {
            if (![value isKindOfClass:[NSArray class]]) { VIMRegionalFail(error,12,@"Native preferred languages are not an array."); return nil; }
            unsigned index; NSMutableArray *canonical=[NSMutableArray array];
            for(index=0;index<[value count];index++) { NSString *language=VIMRegionalCanonical([value objectAtIndex:index]); if(!language){VIMRegionalFail(error,12,@"Native preferred language value is malformed.");return nil;} [canonical addObject:language]; }
            if(![step isEqual:@"UserLanguageEncoding"])value=canonical;
        } else if(direction) {
            if(![value isKindOfClass:[NSNumber class]]||([value doubleValue]!=0&&[value doubleValue]!=1)){VIMRegionalFail(error,12,@"Native text direction is not a valid number.");return nil;}
        } else if ([step isEqual:@"UserSystemVersionNumber"]) {
            if(![value isKindOfClass:[NSNumber class]]||[value doubleValue]!=[value unsignedIntValue]) { VIMRegionalFail(error,12,@"The native account system version is not numeric."); return nil; }
        } else if (![value isKindOfClass:[NSString class]]) { VIMRegionalFail(error,12,@"A native regional preference is not a string."); return nil; }
    }
    return VIMRegionalValue(value);
}
static NSArray *VIMRegionalNativeUserArguments(NSArray *languages, NSDictionary *capabilities) {
    NSMutableArray *arguments=[NSMutableArray array]; unsigned index;
    NSArray *inventories=VIMRegionalDesktopInventories(capabilities,NULL);if(!inventories)return nil;
    BOOL nativeFormat=[[capabilities objectForKey:@"LanguagePreferenceFormat"] isEqual:@"native"];
    for(index=0;index<[languages count];index++) {
        NSString *language=[languages objectAtIndex:index];
        NSDictionary *request=[NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithInt:1],@"FormatVersion",@"custom",@"Source",@"fallback",@"UnsupportedPolicy",[NSArray arrayWithObject:language],@"PreferredLanguages",nil];
        NSString *native=[VIMRegionalResolve(request,capabilities,NULL) objectForKey:@"DesktopNativeLanguage"];
        if(!native||!VIMRegionalCompatibleLanguageIdentifiers(language,native))return nil;
        NSArray *preference=[NSArray arrayWithObject:language];
        BOOL roundtrip=VIMRegionalSelectionMatches([capabilities objectForKey:@"DesktopLanguages"],preference,native)&&
            VIMRegionalSelectionMatches([inventories objectAtIndex:0],preference,native)&&VIMRegionalSelectionMatches([inventories objectAtIndex:1],preference,native);
        NSString *argument=nativeFormat||!roundtrip?native:language;
        preference=[NSArray arrayWithObject:argument];
        if(!VIMRegionalSelectionMatches([inventories objectAtIndex:0],preference,native)||!VIMRegionalSelectionMatches([inventories objectAtIndex:1],preference,native))return nil;
        [arguments addObject:argument];
    }
    return [NSArray arrayWithArray:arguments];
}
static BOOL VIMRegionalSameLanguageValues(id raw, NSArray *canonical) {
    if(![raw isKindOfClass:[NSArray class]]||[raw count]!=[canonical count])return NO;
    unsigned index;for(index=0;index<[canonical count];index++)
        if(![[canonical objectAtIndex:index] isEqual:VIMRegionalCanonical([raw objectAtIndex:index])])return NO;
    return YES;
}
static BOOL VIMRegionalWriteSetting(id<VIMGuestRegionalBackend> backend, NSDictionary *step, NSDictionary *resolved, NSString *user, NSError **error) {
    NSString *name=[step objectForKey:@"Name"], *path=nil; NSArray *arguments=nil; id value=[[step objectForKey:@"After"] objectForKey:@"Value"];
    if ([name isEqual:@"TimeZone"]) { path=@"/usr/sbin/systemsetup"; arguments=[NSArray arrayWithObjects:@"-settimezone",value,nil]; }
    else if ([name isEqual:@"LoginLanguage"]||[name isEqual:@"LoginTextDirection"]) {
        // Tiger/Snow languagesetup replaces the entire global domain. Use
        // the native per-key writer on the installed system instead; CDIS's
        // installer prompt log is not an installed loginwindow preference.
        path=@"/usr/bin/defaults";
        arguments=[name isEqual:@"LoginLanguage"]?[NSArray arrayWithObjects:@"write",@"/Library/Preferences/.GlobalPreferences",@"AppleLanguages",@"-array",[resolved objectForKey:@"LoginNativeLanguage"],nil]:[NSArray arrayWithObjects:@"write",@"/Library/Preferences/.GlobalPreferences",@"AppleTextDirection",@"-int",[value stringValue],nil];
    }
    else {
        if([name isEqual:@"UserLanguages"]&&[resolved objectForKey:@"UserLanguageArguments"])value=[resolved objectForKey:@"UserLanguageArguments"];
        BOOL direction=[name isEqual:@"UserTextDirection"];
        BOOL version=[name isEqual:@"UserSystemVersion"]||[name isEqual:@"UserSystemVersionNumber"];
        NSString *key=direction?@"AppleTextDirection":[name isEqual:@"UserSystemVersion"]?@"SystemVersionStampAsString":([name isEqual:@"UserSystemVersionNumber"]?@"SystemVersionStampAsNumber":([name isEqual:@"UserLocale"]?@"AppleLocale":@"AppleLanguages"));
        NSString *type=([name isEqual:@"UserSystemVersionNumber"]||direction)?@"-int":(([name isEqual:@"UserLocale"]||version)?@"-string":@"-array");
        NSMutableArray *write=[NSMutableArray arrayWithObjects:@"write",version?@"loginwindow":@"-g",key,type,nil];
        if([name isEqual:@"UserSystemVersionNumber"]||direction)value=[value stringValue];
        if ([value isKindOfClass:[NSArray class]]) [write addObjectsFromArray:value]; else [write addObject:value];
        path=@"/usr/bin/sudo"; arguments=VIMRegionalDefaultsArguments(write,user);
    }
    NSDictionary *result=VIMRegionalRun(backend,path,arguments,error);
    return result&&[[result objectForKey:@"Status"] intValue]==0?YES:VIMRegionalFail(error,13,@"A regional setter did not finish successfully; its saved intent remains pending.");
}
static BOOL VIMRegionalSafeUser(NSString *user) {
    if (![user isKindOfClass:[NSString class]] || ![user length] || [user length]>64) return NO;
    unsigned index; for(index=0;index<[user length];index++){unichar c=[user characterAtIndex:index];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return NO;}
    return ![user hasPrefix:@"-"] && ![user isEqual:@"."] && ![user isEqual:@".."];
}
static BOOL VIMRegionalBooleanNumber(id value) {
    return [value isKindOfClass:[NSNumber class]]&&([value doubleValue]==0||[value doubleValue]==1);
}
NSDictionary *VIMGuestRegionalPlan(NSDictionary *request, NSDictionary *capabilities, NSError **error) {
    if(![capabilities isKindOfClass:[NSDictionary class]]){VIMRegionalFail(error,15,@"Invalid native regional capabilities.");return nil;}
    NSArray *flags=[NSArray arrayWithObjects:@"UserDefaultsSetterAvailable",@"LoginLanguageSetterAvailable",@"TimeZoneSetterAvailable",nil];unsigned flagIndex;
    for(flagIndex=0;flagIndex<[flags count];flagIndex++) {
        id flag=[capabilities objectForKey:[flags objectAtIndex:flagIndex]];
        if(flag&&!VIMRegionalBooleanNumber(flag)){VIMRegionalFail(error,15,@"Invalid native regional setter availability.");return nil;}
    }
    id format=[capabilities objectForKey:@"LanguagePreferenceFormat"];
    if(format&&!([format isEqual:@"native"]||[format isEqual:@"canonical"])){VIMRegionalFail(error,15,@"Invalid native language preference format.");return nil;}
    NSDictionary *resolved=VIMRegionalResolve(request,capabilities,error); if(!resolved)return nil;
    NSMutableDictionary *effective=[NSMutableDictionary dictionaryWithDictionary:resolved];
    NSMutableArray *warnings=[NSMutableArray arrayWithArray:[resolved objectForKey:@"Warnings"]], *skipped=[NSMutableArray arrayWithArray:[resolved objectForKey:@"Skipped"]];
    NSArray *settingKeys=[NSArray arrayWithObjects:@"UserLanguages",@"LocaleIdentifier",@"LoginLanguage",@"TimeZoneIdentifier",nil];
    NSArray *setterKeys=[NSArray arrayWithObjects:@"UserDefaultsSetterAvailable",@"UserDefaultsSetterAvailable",@"LoginLanguageSetterAvailable",@"TimeZoneSetterAvailable",nil];
    NSArray *names=[NSArray arrayWithObjects:@"UserLanguages",@"UserLocale",@"LoginLanguage",@"TimeZone",nil];
    unsigned index;
    for(index=0;index<[names count];index++) {
        NSString *key=[settingKeys objectAtIndex:index], *name=[names objectAtIndex:index]; id value=[resolved objectForKey:key];
        if (!value || ([value isKindOfClass:[NSArray class]]&&![value count]) || [[capabilities objectForKey:[setterKeys objectAtIndex:index]] boolValue])continue;
        if ([[[resolved objectForKey:@"Requested"] objectForKey:@"UnsupportedPolicy"] isEqual:@"strict"]) {
            VIMRegionalFail(error,15,@"A required native regional setter is unavailable."); return nil;
        }
        [warnings addObject:[name stringByAppendingString:@"SetterUnavailable"]]; [skipped addObject:name]; [effective removeObjectForKey:key];
        if([name isEqual:@"LoginLanguage"])[effective removeObjectForKey:@"LoginNativeLanguage"];
        if([name isEqual:@"UserLanguages"]){[effective removeObjectForKey:@"DesktopLanguage"];[effective removeObjectForKey:@"DesktopNativeLanguage"];}
    }
    NSString *migration=VIMRegionalAccountVersion([capabilities objectForKey:@"AccountSystemVersion"]);
    if(migration&&[[effective objectForKey:@"UserLanguages"] count]){
        NSArray *parts=[migration componentsSeparatedByString:@"."];
        unsigned number=((unsigned)[[parts objectAtIndex:0] intValue]<<24)|((unsigned)[[parts objectAtIndex:1] intValue]<<16)|([parts count]>2?(unsigned)[[parts objectAtIndex:2] intValue]<<8:0);
        [effective setObject:migration forKey:@"UserSystemVersion"];
        [effective setObject:[NSNumber numberWithUnsignedInt:number] forKey:@"UserSystemVersionNumber"];
    }
    NSArray *userLanguages=[effective objectForKey:@"UserLanguages"];
    if([userLanguages count]) {
        NSArray *arguments=VIMRegionalNativeUserArguments(userLanguages,capabilities);
        NSArray *native=[resolved objectForKey:@"UserNativeLanguages"];
        if([arguments count]!=[userLanguages count]||[native count]!=[userLanguages count]) {
            VIMRegionalFail(error,15,@"Native language preference encoding could not be verified.");return nil;
        }
        NSMutableArray *semantic=[NSMutableArray array], *encoded=[NSMutableArray array], *mappings=[NSMutableArray array];
        for(index=0;index<[userLanguages count];index++) {
            NSString *requested=[userLanguages objectAtIndex:index], *argument=[arguments objectAtIndex:index];
            NSString *accepted=VIMRegionalCanonical([native objectAtIndex:index]), *preference=VIMRegionalCanonical(argument);
            if(![encoded containsObject:argument]){[encoded addObject:argument];[semantic addObject:preference];}
            [mappings addObject:[NSDictionary dictionaryWithObjectsAndKeys:requested,@"RequestedLanguage",accepted,@"AcceptedLanguage",[native objectAtIndex:index],@"NativeLanguage",preference,@"PreferenceLanguage",argument,@"PreferenceEncoding",nil]];
            NSString *requestedRegion=[[NSLocale componentsFromLocaleIdentifier:requested] objectForKey:NSLocaleCountryCode];
            NSString *acceptedRegion=[[NSLocale componentsFromLocaleIdentifier:accepted] objectForKey:NSLocaleCountryCode];
            BOOL variant=requestedRegion&&((acceptedRegion&&![acceptedRegion isEqual:requestedRegion])||![preference isEqual:requested]);
            if(variant&&![warnings containsObject:@"LanguageVariantFallback"])[warnings addObject:@"LanguageVariantFallback"];
        }
        [effective setObject:[NSArray arrayWithArray:semantic] forKey:@"UserLanguages"];
        if(![encoded isEqual:semantic])[effective setObject:[NSArray arrayWithArray:encoded] forKey:@"UserLanguageArguments"];
        [effective setObject:[NSArray arrayWithArray:mappings] forKey:@"LanguageMappings"];
        NSArray *inventories=VIMRegionalDesktopInventories(capabilities,error);if(!inventories)return nil;
        NSString *desktop=[effective objectForKey:@"DesktopNativeLanguage"];
        if(!VIMRegionalSelectionMatches([inventories objectAtIndex:0],encoded,desktop)||!VIMRegionalSelectionMatches([inventories objectAtIndex:1],encoded,desktop)) {
            VIMRegionalFail(error,15,@"The ordered native preferences do not select the planned desktop resources.");return nil;
        }
    }
    NSString *login=[effective objectForKey:@"LoginNativeLanguage"];
    if(login&&!VIMRegionalSelectionMatches([capabilities objectForKey:@"LoginLanguages"],[NSArray arrayWithObject:login],login)) {
        VIMRegionalFail(error,15,@"The native login language could not be selected.");return nil;
    }
    NSArray *directionInventories=[NSArray arrayWithObjects:@"LoginTextDirections",@"DesktopTextDirections",nil];
    NSArray *directionNames=[NSArray arrayWithObjects:@"LoginTextDirection",@"UserTextDirection",nil];
    NSArray *nativeKeys=[NSArray arrayWithObjects:@"LoginNativeLanguage",@"DesktopNativeLanguage",nil];
    for(index=0;index<[directionNames count];index++) {
        id directions=[capabilities objectForKey:[directionInventories objectAtIndex:index]];
        if(directions&&![directions isKindOfClass:[NSDictionary class]]){VIMRegionalFail(error,15,@"Invalid native language direction inventory.");return nil;}
        id direction=[directions objectForKey:[effective objectForKey:[nativeKeys objectAtIndex:index]]?:@""];
        if(direction) {
            if(![direction isKindOfClass:[NSNumber class]]||([direction doubleValue]!=0&&[direction doubleValue]!=1)){VIMRegionalFail(error,15,@"Invalid native language direction.");return nil;}
            [effective setObject:direction forKey:[directionNames objectAtIndex:index]];
        }
    }
    [effective removeObjectForKey:@"UserNativeLanguages"];
    [effective setObject:[NSNumber numberWithInt:2] forKey:@"PlanVersion"];
    [effective setObject:[NSArray arrayWithArray:warnings] forKey:@"Warnings"];
    [effective setObject:[NSArray arrayWithArray:skipped] forKey:@"Skipped"];
    return [NSDictionary dictionaryWithDictionary:effective];
}
static NSString *VIMRegionalVersionEvidence(id value) {
    if(![value isKindOfClass:[NSString class]] || ![value length] || [value length]>128)return nil;
    unsigned index; for(index=0;index<[value length];index++){unichar c=[value characterAtIndex:index];if(c<32||c>126)return nil;}
    return [NSString stringWithString:value];
}
static NSMutableDictionary *VIMRegionalNewStep(NSString *name, id value) {
    return [NSMutableDictionary dictionaryWithObjectsAndKeys:name,@"Name",VIMRegionalValue(value),@"After",@"planned",@"Phase",nil];
}
static NSMutableArray *VIMRegionalStepsForPlan(NSDictionary *resolved) {
    NSArray *keys=[NSArray arrayWithObjects:@"UserSystemVersion",@"UserSystemVersionNumber",@"UserLanguages",@"UserLanguageArguments",@"UserTextDirection",@"LocaleIdentifier",@"LoginLanguage",@"LoginTextDirection",@"TimeZoneIdentifier",nil];
    NSArray *names=[NSArray arrayWithObjects:@"UserSystemVersion",@"UserSystemVersionNumber",@"UserLanguages",@"UserLanguageEncoding",@"UserTextDirection",@"UserLocale",@"LoginLanguage",@"LoginTextDirection",@"TimeZone",nil];
    NSMutableArray *steps=[NSMutableArray array];unsigned index;
    for(index=0;index<[keys count];index++) {
        id value=[resolved objectForKey:[keys objectAtIndex:index]];NSString *name=[names objectAtIndex:index];
        if(!value||([value isKindOfClass:[NSArray class]]&&![value count]))continue;
        if([name isEqual:@"LoginLanguage"])value=[NSArray arrayWithObject:value];
        [steps addObject:VIMRegionalNewStep(name,value)];
    }
    return steps;
}
static BOOL VIMRegionalValidValueRecord(id record, NSString *name, BOOL required) {
    if (![record isKindOfClass:[NSDictionary class]] || !VIMRegionalBooleanNumber([record objectForKey:@"Present"]))return NO;
    BOOL present=[[record objectForKey:@"Present"] boolValue]; id value=[record objectForKey:@"Value"];
    if (!present)return !required && !value && [record count]==1;
    if ([record count]!=2 || !value)return NO;
    if ([name isEqual:@"UserLanguages"] || [name isEqual:@"UserLanguageEncoding"] || [name isEqual:@"LoginLanguage"]) {
        // The request limit applies to After only. Native Before lists can be
        // longer and must remain readable when resuming our own saved intent.
        if (![value isKindOfClass:[NSArray class]] || (required && (![value count]||[value count]>16)))return NO;
        unsigned index; for(index=0;index<[value count];index++)if(!VIMRegionalCanonical([value objectAtIndex:index]))return NO;
        return YES;
    }
    if([name isEqual:@"LoginTextDirection"]||[name isEqual:@"UserTextDirection"])return [value isKindOfClass:[NSNumber class]]&&([value doubleValue]==0||[value doubleValue]==1);
    if([name isEqual:@"UserSystemVersionNumber"])return [value isKindOfClass:[NSNumber class]]&&[value doubleValue]==[value unsignedIntValue];
    return [value isKindOfClass:[NSString class]] && (!required || ([value length]>0&&[value length]<=256));
}
static BOOL VIMRegionalValidStringList(id value, unsigned maximum) {
    if(![value isKindOfClass:[NSArray class]]||[value count]>maximum)return NO;
    unsigned index;for(index=0;index<[value count];index++) {
        id item=[value objectAtIndex:index];if(![item isKindOfClass:[NSString class]]||![item length]||[item length]>256)return NO;
    }
    return YES;
}
static BOOL VIMRegionalValidSteps(NSArray *steps, NSDictionary *resolved) {
    id version=[resolved objectForKey:@"PlanVersion"];
    if(version&&(![version isKindOfClass:[NSNumber class]]||[version doubleValue]!=2))return NO;
    unsigned index,item,field;
    NSArray *languageKeys=[NSArray arrayWithObjects:@"DesktopLanguage",@"DesktopNativeLanguage",@"LoginLanguage",@"LoginNativeLanguage",@"LoginLanguageArgument",nil];
    for(index=0;index<[languageKeys count];index++) {
        id value=[resolved objectForKey:[languageKeys objectAtIndex:index]];if(value&&!VIMRegionalCanonical(value))return NO;
    }
    NSArray *listKeys=[NSArray arrayWithObjects:@"Warnings",@"Skipped",nil];
    for(index=0;index<[listKeys count];index++) {
        id value=[resolved objectForKey:[listKeys objectAtIndex:index]];if(value&&!VIMRegionalValidStringList(value,64))return NO;
    }
    if(version) {
        id languages=[resolved objectForKey:@"UserLanguages"];
        if(languages&&!VIMRegionalValidStringList(languages,16))return NO;
        BOOL user=[languages count]>0,login=[resolved objectForKey:@"LoginLanguage"]!=nil;
        if(user!=([resolved objectForKey:@"DesktopLanguage"]!=nil)||user!=([resolved objectForKey:@"DesktopNativeLanguage"]!=nil)||
           login!=([resolved objectForKey:@"LoginNativeLanguage"]!=nil))return NO;
        if(user&&(![[resolved objectForKey:@"DesktopLanguage"] isEqual:VIMRegionalCanonical([resolved objectForKey:@"DesktopNativeLanguage"])]||
           !VIMRegionalCompatibleLanguageIdentifiers([languages objectAtIndex:0],[resolved objectForKey:@"DesktopNativeLanguage"])))return NO;
        BOOL migration=[resolved objectForKey:@"UserSystemVersion"]!=nil;
        if(migration!=([resolved objectForKey:@"UserSystemVersionNumber"]!=nil)||
           (!user&&(migration||[resolved objectForKey:@"UserLanguageArguments"]||[resolved objectForKey:@"UserTextDirection"]))||
           (!login&&([resolved objectForKey:@"LoginLanguageArgument"]||[resolved objectForKey:@"LoginTextDirection"])))return NO;
        NSDictionary *requested=[resolved objectForKey:@"Requested"];NSArray *skipped=[resolved objectForKey:@"Skipped"];
        if(!VIMRegionalValidStringList(skipped,64)||!VIMRegionalValidStringList([resolved objectForKey:@"Warnings"],64))return NO;
        BOOL strict=[[requested objectForKey:@"UnsupportedPolicy"] isEqual:@"strict"];
        if(!user&&(strict||(![skipped containsObject:@"UserLanguages"]&&![skipped containsObject:@"DesktopLanguage"])))return NO;
        if(!login&&(strict||![skipped containsObject:@"LoginLanguage"]))return NO;
        if([requested objectForKey:@"RegionCode"]&&![resolved objectForKey:@"LocaleIdentifier"]&&
           (strict||(![skipped containsObject:@"RegionCode"]&&![skipped containsObject:@"UserLocale"])))return NO;
        if([requested objectForKey:@"TimeZoneIdentifier"]&&![resolved objectForKey:@"TimeZoneIdentifier"]&&
           (strict||(![skipped containsObject:@"TimeZoneIdentifier"]&&![skipped containsObject:@"TimeZone"])))return NO;
    }
    id mappings=[resolved objectForKey:@"LanguageMappings"];
    if(mappings) {
        if(![mappings isKindOfClass:[NSArray class]]||[mappings count]>16)return NO;
        NSArray *mappingKeys=[NSArray arrayWithObjects:@"RequestedLanguage",@"AcceptedLanguage",@"NativeLanguage",@"PreferenceLanguage",@"PreferenceEncoding",nil];
        for(item=0;item<[mappings count];item++) {
            id row=[mappings objectAtIndex:item];
            if(![row isKindOfClass:[NSDictionary class]])return NO;
            for(field=0;field<[mappingKeys count];field++)if(!VIMRegionalCanonical([row objectForKey:[mappingKeys objectAtIndex:field]]))return NO;
        }
    }
    NSArray *names=[NSArray arrayWithObjects:@"UserSystemVersion",@"UserSystemVersionNumber",@"UserLanguages",@"UserLanguageEncoding",@"UserTextDirection",@"UserLocale",@"LoginLanguage",@"LoginTextDirection",@"TimeZone",nil];
    NSArray *keys=[NSArray arrayWithObjects:@"UserSystemVersion",@"UserSystemVersionNumber",@"UserLanguages",@"UserLanguageArguments",@"UserTextDirection",@"LocaleIdentifier",@"LoginLanguage",@"LoginTextDirection",@"TimeZoneIdentifier",nil];
    NSMutableDictionary *targets=[NSMutableDictionary dictionary];NSMutableArray *order=[NSMutableArray array];
    for(index=0;index<[names count];index++) {
        NSString *name=[names objectAtIndex:index]; id target=[resolved objectForKey:[keys objectAtIndex:index]];
        if(!target || ([target isKindOfClass:[NSArray class]]&&![target count]))continue;
        if([name isEqual:@"LoginLanguage"]) {
            NSString *native=[resolved objectForKey:@"LoginNativeLanguage"];
            if(![target isKindOfClass:[NSString class]] || ![target isEqual:VIMRegionalCanonical(native)] ||
               ([resolved objectForKey:@"LoginLanguageArgument"]&&![[resolved objectForKey:@"LoginLanguageArgument"] isEqual:native]))return NO;
            target=[NSArray arrayWithObject:target];
        }
        NSDictionary *record=VIMRegionalValue(target);
        if(!VIMRegionalValidValueRecord(record,name,YES))return NO;
        [targets setObject:record forKey:name];
        [order addObject:name];
    }
    if([resolved objectForKey:@"UserLanguageArguments"]&&!VIMRegionalSameLanguageValues([resolved objectForKey:@"UserLanguageArguments"],[resolved objectForKey:@"UserLanguages"]))return NO;
    if([targets count]!=[steps count])return NO;
    for(index=0;index<[steps count];index++) {
        id step=[steps objectAtIndex:index]; if(![step isKindOfClass:[NSDictionary class]])return NO;
        NSString *name=[step objectForKey:@"Name"], *phase=[step objectForKey:@"Phase"];
        if(![name isKindOfClass:[NSString class]] || ![phase isKindOfClass:[NSString class]])return NO;
        if(![name isEqual:[order objectAtIndex:index]])return NO;
        id expected=[targets objectForKey:name];
        if(!expected || ![expected isEqual:[step objectForKey:@"After"]])return NO;
        if([phase isEqual:@"planned"]) { if([step objectForKey:@"Before"])return NO; }
        else if(([phase isEqual:@"intent"]||[phase isEqual:@"verified"]) && VIMRegionalValidValueRecord([step objectForKey:@"Before"],name,NO)) {}
        else return NO;
        [targets removeObjectForKey:name];
    }
    return [targets count]==0;
}
static BOOL VIMRegionalUpgradePlan(NSMutableDictionary *regional, NSDictionary *request, NSDictionary *capabilities,
    id<VIMGuestRegionalBackend> backend, NSString *user, NSError **error) {
    NSArray *oldSteps=[regional objectForKey:@"Steps"];
    NSMutableDictionary *observations=[NSMutableDictionary dictionary], *oldByName=[NSMutableDictionary dictionary];
    unsigned index;
    for(index=0;index<[oldSteps count];index++) {
        NSDictionary *step=[oldSteps objectAtIndex:index];NSString *name=[step objectForKey:@"Name"],*phase=[step objectForKey:@"Phase"];
        NSDictionary *observed=VIMRegionalReadSetting(backend,name,user,error);if(!observed)return NO;
        BOOL after=[observed isEqual:[step objectForKey:@"After"]];
        BOOL before=[observed isEqual:[step objectForKey:@"Before"]];
        if(([phase isEqual:@"verified"]&&!after)||([phase isEqual:@"intent"]&&!before&&!after))
            return VIMRegionalFail(error,16,@"A saved regional preference changed before plan upgrade; refusing to overwrite it.");
        [observations setObject:observed forKey:name];[oldByName setObject:step forKey:name];
    }
    NSDictionary *resolved=VIMGuestRegionalPlan(request,capabilities,error);if(!resolved)return NO;
    NSMutableArray *steps=VIMRegionalStepsForPlan(resolved);
    for(index=0;index<[steps count];index++) {
        NSMutableDictionary *step=[steps objectAtIndex:index];NSString *name=[step objectForKey:@"Name"];
        NSDictionary *old=[oldByName objectForKey:name],*observed=[observations objectForKey:name];
        if(!observed)observed=VIMRegionalReadSetting(backend,name,user,error);if(!observed)return NO;
        BOOL sameTarget=[[old objectForKey:@"After"] isEqual:[step objectForKey:@"After"]];
        if(!sameTarget&&[name isEqual:@"UserLanguageEncoding"]) {
            NSArray *raw=[[step objectForKey:@"After"] objectForKey:@"Value"];
            NSMutableArray *canonical=[NSMutableArray array];unsigned item;
            for(item=0;item<[raw count];item++)[canonical addObject:VIMRegionalCanonical([raw objectAtIndex:item])];
            sameTarget=VIMRegionalSameLanguageValues([[old objectForKey:@"After"] objectForKey:@"Value"],canonical);
        }
        // A planned key still has its original initialization authority, but
        // no recorded Before. Do not use that absence to authorize a changed
        // target over an unrelated present value during a version upgrade.
        if([[old objectForKey:@"Phase"] isEqual:@"planned"]&&!sameTarget&&[[observed objectForKey:@"Present"] boolValue]&&
           ![observed isEqual:[old objectForKey:@"After"]]&&![observed isEqual:[step objectForKey:@"After"]])
            return VIMRegionalFail(error,16,@"An unstarted regional target changed during upgrade without a matching recorded value; refusing to overwrite it.");
        if([[old objectForKey:@"Phase"] isEqual:@"verified"]&&[[old objectForKey:@"After"] isEqual:[step objectForKey:@"After"]])
            [steps replaceObjectAtIndex:index withObject:old];
        else { [step setObject:observed forKey:@"Before"];[step setObject:@"intent" forKey:@"Phase"]; }
    }
    if(!VIMRegionalValidSteps(steps,resolved))return VIMRegionalFail(error,14,@"The upgraded regional plan is inconsistent.");
    [regional setObject:resolved forKey:@"Resolved"];[regional setObject:steps forKey:@"Steps"];
    [regional setObject:[resolved objectForKey:@"Warnings"] forKey:@"Warnings"];
    [regional setObject:[resolved objectForKey:@"Skipped"] forKey:@"Skipped"];
    return YES;
}
static BOOL VIMRegionalVerifyStep(id<VIMGuestRegionalBackend> backend, NSDictionary *step, NSString *user, NSError **error) {
    NSDictionary *observed=VIMRegionalReadSetting(backend,[step objectForKey:@"Name"],user,error);
    if(!observed)return NO;
    return [observed isEqual:[step objectForKey:@"After"]]?YES:
        VIMRegionalFail(error,16,@"A verified regional preference changed before desktop handoff; refusing to overwrite it.");
}
static BOOL VIMRegionalVerifyResources(id<VIMGuestRegionalBackend> backend, NSDictionary *resolved,
    NSDictionary *capabilities, NSString *user, NSError **error) {
    NSArray *languages=[resolved objectForKey:@"UserLanguages"];
    if([languages count]) {
        NSDictionary *domain=VIMRegionalReadDefaults(backend,user,nil,error);if(!domain)return NO;
        id actual=[domain objectForKey:@"AppleLanguages"];
        if(!VIMRegionalSameLanguageValues(actual,languages))return VIMRegionalFail(error,16,@"User language preferences changed before final resource verification.");
        id direction=[resolved objectForKey:@"UserTextDirection"];
        if(direction) {
            NSDictionary *typed=VIMRegionalReadSetting(backend,@"UserTextDirection",user,error);if(!typed)return NO;
            if(![[typed objectForKey:@"Value"] isEqual:direction])return VIMRegionalFail(error,16,@"Native desktop text direction does not match the saved language plan.");
        }
        NSArray *inventories=VIMRegionalDesktopInventories(capabilities,error);if(!inventories)return NO;
        NSArray *finder=[inventories objectAtIndex:0],*appKit=[inventories objectAtIndex:1];
        NSString *native=[resolved objectForKey:@"DesktopNativeLanguage"];
        if(!VIMRegionalSelectionMatches(finder,actual,native)||!VIMRegionalSelectionMatches(appKit,actual,native))
            return VIMRegionalFail(error,16,@"Native desktop resources do not match the saved language plan.");
    }
    if([resolved objectForKey:@"LoginLanguage"]) {
        NSDictionary *domain=VIMRegionalReadDefaults(backend,nil,nil,error);if(!domain)return NO;
        id actual=[domain objectForKey:@"AppleLanguages"];
        if(!VIMRegionalSameLanguageValues(actual,[NSArray arrayWithObject:[resolved objectForKey:@"LoginLanguage"]])||
           !VIMRegionalSelectionMatches([capabilities objectForKey:@"LoginLanguages"],actual,[resolved objectForKey:@"LoginNativeLanguage"]))
            return VIMRegionalFail(error,16,@"Native login resources do not match the saved language plan.");
        id direction=[resolved objectForKey:@"LoginTextDirection"];
        if(direction) {
            NSDictionary *typed=VIMRegionalReadSetting(backend,@"LoginTextDirection",user,error);if(!typed)return NO;
            if(![[typed objectForKey:@"Value"] isEqual:direction])return VIMRegionalFail(error,16,@"Native login text direction does not match the saved language plan.");
        }
    }
    return YES;
}
BOOL VIMGuestApplyRegionalSettingsWithBackend(NSDictionary *request, NSString *user, uid_t uid,
    NSMutableDictionary *accountJournal, NSString *journalPath, id<VIMGuestRegionalBackend> backend, NSError **error) {
    if (error) *error=nil;
    if (!request) return YES;
    NSDictionary *normalized=VIMRegionalNormalizeRequest(request,error); if(!normalized)return NO;
    if (![accountJournal isKindOfClass:[NSMutableDictionary class]] || !VIMRegionalSafeUser(user) || uid<501 || uid>=60000 ||
        ![[accountJournal objectForKey:@"FormatVersion"] isEqual:[NSNumber numberWithInt:1]] ||
        ![[accountJournal objectForKey:@"Username"] isEqual:user] || ![[accountJournal objectForKey:@"UID"] isKindOfClass:[NSNumber class]] ||
        [[accountJournal objectForKey:@"UID"] doubleValue]!=(double)uid || ![[accountJournal objectForKey:@"GeneratedUID"] isKindOfClass:[NSString class]] ||
        ![[accountJournal objectForKey:@"MachineIdentifier"] isKindOfClass:[NSString class]] || ![[accountJournal objectForKey:@"VolumeUUID"] isKindOfClass:[NSString class]] ||
        ![journalPath isKindOfClass:[NSString class]] || ![journalPath isAbsolutePath] || !VIMRegionalOwnedPath([journalPath stringByDeletingLastPathComponent],YES) ||
        ![VIMRegionalReadJournal(journalPath) isEqual:accountJournal]) return VIMRegionalFail(error,14,@"The owned new-account transaction does not match its private journal.");
    id existing=[accountJournal objectForKey:@"RegionalSettings"];
    if (existing && (![existing isKindOfClass:[NSDictionary class]] || ![[existing objectForKey:@"Requested"] isEqual:normalized]))
        return VIMRegionalFail(error,14,@"The regional request differs from the immutable saved transaction.");
    NSString *accountPhase=[accountJournal objectForKey:@"Phase"];
    if ([accountPhase isEqual:@"complete"] || [accountPhase isEqual:@"autologin-armed"]) return YES;
    if ([[existing objectForKey:@"Phase"] isEqual:@"verified"]) return YES;
    if (![accountPhase isEqual:@"planned"] && ![accountPhase isEqual:@"account-created"]) return VIMRegionalFail(error,14,@"Regional settings require a new account before the desktop handoff.");
    NSDictionary *capabilities=[backend regionalCapabilities];
    NSMutableDictionary *regional=existing?VIM_AUTORELEASE([existing mutableCopy]):nil;
    if (!regional) {
        NSDictionary *resolved=VIMGuestRegionalPlan(normalized,capabilities,error); if(!resolved)return NO;
        NSArray *warnings=[resolved objectForKey:@"Warnings"], *skipped=[resolved objectForKey:@"Skipped"];
        NSMutableArray *steps=VIMRegionalStepsForPlan(resolved);
        regional=[NSMutableDictionary dictionaryWithObjectsAndKeys:normalized,@"Requested",resolved,@"Resolved",warnings,@"Warnings",skipped,@"Skipped",steps,@"Steps",@"applying",@"Phase",nil];
        NSString *version=VIMRegionalVersionEvidence([capabilities objectForKey:@"Version"]), *build=VIMRegionalVersionEvidence([capabilities objectForKey:@"Build"]);
        if(version)[regional setObject:version forKey:@"ProductVersion"];
        if(build)[regional setObject:build forKey:@"ProductBuildVersion"];
        NSString *zoneVersion=VIMRegionalVersionEvidence([capabilities objectForKey:@"TimeZoneDataVersion"]);
        if(zoneVersion)[regional setObject:zoneVersion forKey:@"TimeZoneDataVersion"];
        [accountJournal setObject:regional forKey:@"RegionalSettings"];
        if(!VIMRegionalSave(accountJournal,journalPath,error))return NO;
    } else [accountJournal setObject:regional forKey:@"RegionalSettings"];
    id rawSteps=[regional objectForKey:@"Steps"]; NSDictionary *resolved=[regional objectForKey:@"Resolved"];
    if (![rawSteps isKindOfClass:[NSArray class]] || ![resolved isKindOfClass:[NSDictionary class]] ||
        !VIMRegionalValidStringList([regional objectForKey:@"Warnings"],64) || !VIMRegionalValidStringList([regional objectForKey:@"Skipped"],64) ||
        ![[resolved objectForKey:@"Requested"] isEqual:normalized] || ![[regional objectForKey:@"Phase"] isEqual:@"applying"] ||
        !VIMRegionalValidSteps(rawSteps,resolved))return VIMRegionalFail(error,14,@"The regional recovery journal is malformed.");
    if(![resolved objectForKey:@"PlanVersion"]) {
        if(!VIMRegionalUpgradePlan(regional,normalized,capabilities,backend,user,error)||!VIMRegionalSave(accountJournal,journalPath,error))return NO;
        rawSteps=[regional objectForKey:@"Steps"];resolved=[regional objectForKey:@"Resolved"];
    }
    // Older in-progress journals verified canonical language identity, but
    // period Foundation also requires native spelling to select its resources.
    // Add a separate raw-encoding intent; retain every prior verified step and
    // the exact requested languages. Completed/handoff accounts return above.
    NSArray *arguments=VIMRegionalNativeUserArguments([resolved objectForKey:@"UserLanguages"],capabilities);
    if(![resolved objectForKey:@"UserLanguageArguments"]&&[arguments count]&&![arguments isEqual:[resolved objectForKey:@"UserLanguages"]]){
        NSMutableDictionary *updated=[NSMutableDictionary dictionaryWithDictionary:resolved];[updated setObject:arguments forKey:@"UserLanguageArguments"];
        NSMutableArray *updatedSteps=[NSMutableArray arrayWithArray:rawSteps];
        unsigned position=0;while(position<[updatedSteps count]&&![[[updatedSteps objectAtIndex:position] objectForKey:@"Name"] isEqual:@"UserLanguages"])position++;
        if(position==[updatedSteps count])return VIMRegionalFail(error,14,@"The saved user language transaction has no semantic target.");
        [updatedSteps insertObject:VIMRegionalNewStep(@"UserLanguageEncoding",arguments) atIndex:position+1];
        [regional setObject:updated forKey:@"Resolved"];[regional setObject:updatedSteps forKey:@"Steps"];
        if(!VIMRegionalValidSteps(updatedSteps,updated)||!VIMRegionalSave(accountJournal,journalPath,error))return NO;
        resolved=updated;rawSteps=updatedSteps;
    }
    NSMutableArray *steps=[NSMutableArray arrayWithArray:rawSteps]; [regional setObject:steps forKey:@"Steps"];
    unsigned index;
    for(index=0;index<[steps count];index++) {
        id rawStep=[steps objectAtIndex:index];
        if(![rawStep isKindOfClass:[NSDictionary class]])return VIMRegionalFail(error,14,@"The regional recovery step is malformed.");
        NSMutableDictionary *step=VIM_AUTORELEASE([rawStep mutableCopy]); [steps replaceObjectAtIndex:index withObject:step];
        NSString *name=[step objectForKey:@"Name"], *phase=[step objectForKey:@"Phase"];
        if (![[NSArray arrayWithObjects:@"UserSystemVersion",@"UserSystemVersionNumber",@"UserLanguages",@"UserLanguageEncoding",@"UserTextDirection",@"UserLocale",@"LoginLanguage",@"LoginTextDirection",@"TimeZone",nil] containsObject:name] ||
            ![[step objectForKey:@"After"] isKindOfClass:[NSDictionary class]])return VIMRegionalFail(error,14,@"The regional recovery target is malformed.");
        if ([phase isEqual:@"verified"]) {
            if(!VIMRegionalVerifyStep(backend,step,user,error))return NO;
            continue;
        }
        NSDictionary *observed=VIMRegionalReadSetting(backend,name,user,error); if(!observed)return NO;
        if([name isEqual:@"UserLanguageEncoding"]&&!VIMRegionalSameLanguageValues([observed objectForKey:@"Value"],[resolved objectForKey:@"UserLanguages"]))
            return VIMRegionalFail(error,16,@"The user language choice changed before native-format correction; refusing to overwrite it.");
        if ([phase isEqual:@"planned"]) {
            [step setObject:observed forKey:@"Before"]; [step setObject:@"intent" forKey:@"Phase"];
            if(!VIMRegionalSave(accountJournal,journalPath,error))return NO;
        } else if (![phase isEqual:@"intent"] || ![[step objectForKey:@"Before"] isKindOfClass:[NSDictionary class]])return VIMRegionalFail(error,14,@"The regional recovery intent is malformed.");
        if (![observed isEqual:[step objectForKey:@"After"]]) {
            if (![observed isEqual:[step objectForKey:@"Before"]])return VIMRegionalFail(error,16,@"A regional preference changed after an uncertain apply; refusing to overwrite it.");
            if(!VIMRegionalWriteSetting(backend,step,resolved,user,error))return NO;
            observed=VIMRegionalReadSetting(backend,name,user,error); if(!observed)return NO;
            if(![observed isEqual:[step objectForKey:@"After"]])return VIMRegionalFail(error,16,@"A regional setter completed without the requested value in native readback.");
        }
        [step setObject:@"verified" forKey:@"Phase"];
        if(!VIMRegionalSave(accountJournal,journalPath,error))return NO;
    }
    for(index=0;index<[steps count];index++)if(!VIMRegionalVerifyStep(backend,[steps objectAtIndex:index],user,error))return NO;
    if(!VIMRegionalVerifyResources(backend,resolved,capabilities,user,error))return NO;
    [regional setObject:@"verified" forKey:@"Phase"];
    return VIMRegionalSave(accountJournal,journalPath,error);
}
BOOL VIMGuestApplyRegionalSettings(NSDictionary *request, NSString *user, uid_t uid,
    NSMutableDictionary *accountJournal, NSString *journalPath, NSError **error) {
    if(error)*error=nil;
    if(!request)return YES;
    if(geteuid()!=0)return VIMRegionalFail(error,17,@"Applying new-account regional settings requires root.");
    VIMNativeRegionalBackend *backend=VIM_AUTORELEASE([[VIMNativeRegionalBackend alloc] init]);
    return VIMGuestApplyRegionalSettingsWithBackend(request,user,uid,accountJournal,journalPath,backend,error);
}
