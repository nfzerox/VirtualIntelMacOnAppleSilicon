#import "VIMRegionalSettings.h"
#import "VIMLocalization.h"
#import "VIMGuestAgent.h"
#import <CoreFoundation/CoreFoundation.h>
// One policy implementation is shared with period guest tools. Xcode's app
// target discovers this translation unit; command-line guest builds link it
// directly from tools/ instead.
#include "../tools/guest-regional-policy.m"

static BOOL VIMRegionalStringList(id value, NSUInteger maximum) {
    if(![value isKindOfClass:NSArray.class]||[value count]>maximum)return NO;
    for(id item in value)if(![item isKindOfClass:NSString.class]||[item length]>256)return NO;
    return YES;
}
static NSArray *VIMRegionalCleanMappings(id value) {
    if(![value isKindOfClass:NSArray.class]||[value count]>16)return nil;
    NSMutableArray *clean=[NSMutableArray new];
    for(id row in value) {
        if(![row isKindOfClass:NSDictionary.class])return nil;
        NSMutableDictionary *record=[NSMutableDictionary new];
        for(NSString *key in @[@"RequestedLanguage",@"AcceptedLanguage",@"NativeLanguage",@"PreferenceLanguage",@"PreferenceEncoding"]) {
            id item=row[key];if(![item isKindOfClass:NSString.class]||![item length]||[item length]>256)return nil;
            record[key]=item;
        }
        [clean addObject:record];
    }
    return clean;
}
static BOOL VIMRegionalGuestPlan(id value, NSDictionary *requested) {
    if(![value isKindOfClass:NSDictionary.class]||![value[@"PlanVersion"] isEqual:@2]||![value[@"Requested"] isEqual:requested])return NO;
    for(NSString *key in @[@"Warnings",@"Skipped"])if(!VIMRegionalStringList(value[key],64))return NO;
    if(value[@"UserLanguages"]&&!VIMRegionalStringList(value[@"UserLanguages"],16))return NO;
    if(value[@"LanguageMappings"]&&!VIMRegionalCleanMappings(value[@"LanguageMappings"]))return NO;
    for(NSString *key in @[@"DesktopLanguage",@"DesktopNativeLanguage",@"LoginLanguage",@"LoginNativeLanguage",@"LocaleIdentifier",@"TimeZoneIdentifier"]) {
        id item=value[key];if(item&&(![item isKindOfClass:NSString.class]||![item length]||[item length]>256))return NO;
    }
    return YES;
}
static BOOL VIMRegionalInvalidResult(NSError **error) {
    if(error&&!*error)*error=[NSError errorWithDomain:@"VirtualIntelMac.RegionalSettings" code:1 userInfo:@{NSLocalizedDescriptionKey:VIMLocalized(@"regional.invalid-result", @"The guest’s regional setup result could not be confirmed.")}];
    return NO;
}

@implementation VIMRegionalSettings
+ (NSDictionary *)normalizeRequest:(id)request error:(NSError **)error {
    NSError *failure=nil;NSDictionary *normalized=VIMRegionalNormalizeRequest(request,&failure);
    if(!normalized&&failure&&error)*error=[NSError errorWithDomain:failure.domain code:failure.code userInfo:@{
        NSLocalizedDescriptionKey:VIMLocalized(@"regional.invalid-request", @"Choose a valid language, region and time zone."),NSUnderlyingErrorKey:failure}];
    return normalized;
}
+ (NSDictionary *)hostSnapshotWithError:(NSError **)error {
    NSArray *languages=CFBridgingRelease(CFPreferencesCopyValue(CFSTR("AppleLanguages"),kCFPreferencesAnyApplication,kCFPreferencesCurrentUser,kCFPreferencesAnyHost));
    NSString *locale=CFBridgingRelease(CFPreferencesCopyValue(CFSTR("AppleLocale"),kCFPreferencesAnyApplication,kCFPreferencesCurrentUser,kCFPreferencesAnyHost));
    if(![languages isKindOfClass:NSArray.class]||!languages.count)languages=NSLocale.preferredLanguages;
    if(![locale isKindOfClass:NSString.class]||!locale.length)locale=NSLocale.currentLocale.localeIdentifier;
    NSString *region=[[NSLocale alloc] initWithLocaleIdentifier:locale].countryCode;
    NSMutableDictionary *request=[@{@"FormatVersion":@1,@"Source":@"host",@"PreferredLanguages":languages,
        @"UnsupportedPolicy":@"fallback"} mutableCopy];
    if(region.length)request[@"RegionCode"]=region;
    NSString *zone=NSTimeZone.systemTimeZone.name;if(zone.length)request[@"TimeZoneIdentifier"]=zone;
    return VIMRegionalNormalizeRequest(request,error);
}
+ (NSArray *)sortedChoices:(NSDictionary<NSString *,NSString *> *)labels {
    NSMutableArray *choices=[NSMutableArray new];
    for(NSString *value in labels)[choices addObject:@{@"Value":value,@"Title":labels[value]}];
    [choices sortUsingComparator:^NSComparisonResult(NSDictionary *left,NSDictionary *right){
        NSComparisonResult order=[left[@"Title"] localizedStandardCompare:right[@"Title"]];
        return order==NSOrderedSame?[left[@"Value"] compare:right[@"Value"]]:order;
    }];
    return choices;
}
+ (NSArray *)languageChoices {
    NSLocale *display=NSLocale.currentLocale;NSMutableDictionary *labels=[NSMutableDictionary new];
    NSMutableSet *languages=[NSMutableSet setWithArray:NSLocale.ISOLanguageCodes];
    for(NSString *identifier in @[@"zh-Hans",@"zh-Hant",@"zh-Hant-HK",@"pt-BR",@"pt-PT",@"en-GB",@"en-US",@"fr-CA",@"sr-Latn",@"sr-Cyrl",@"es-419"])[languages addObject:identifier];
    for(NSString *language in languages){
        NSString *canonical=[NSLocale canonicalLanguageIdentifierFromString:language];
        NSString *title=[display localizedStringForLanguageCode:canonical]?:[display localizedStringForLocaleIdentifier:canonical]?:canonical;
        // A label collision must never select a different language silently.
        if([labels.allValues containsObject:title])title=[NSString stringWithFormat:@"%@ (%@)",title,canonical];
        if(canonical.length)labels[canonical]=title;
    }
    return [self sortedChoices:labels];
}
+ (NSArray *)regionChoices {
    NSLocale *display=NSLocale.currentLocale;NSMutableDictionary *labels=[NSMutableDictionary new];
    NSMutableSet *codes=[NSMutableSet setWithArray:NSLocale.ISOCountryCodes];
    for(NSString *identifier in NSLocale.availableLocaleIdentifiers){
        NSString *code=[[NSLocale alloc] initWithLocaleIdentifier:identifier].countryCode;if(code.length)[codes addObject:code];
    }
    for(NSString *code in codes)labels[code]=[display localizedStringForCountryCode:code]?:code;
    return [self sortedChoices:labels];
}
+ (NSArray *)timeZoneChoices {
    NSMutableDictionary *labels=[NSMutableDictionary new];
    for(NSString *zone in NSTimeZone.knownTimeZoneNames)labels[zone]=zone;
    return [self sortedChoices:labels];
}
+ (void)preflightRequest:(NSDictionary *)request agent:(VIMGuestAgent *)agent executable:(NSString *)executable packageURL:(NSURL *)package completion:(void (^)(NSError *))completion {
    if(!request){completion(nil);return;}
    NSError *error=nil;NSDictionary *normalized=[self normalizeRequest:request error:&error];
    if(!normalized){completion(error);return;}
    [agent runExecutable:executable arguments:@[@"--regional-plan"] standardInput:[NSJSONSerialization dataWithJSONObject:normalized options:0 error:NULL] timeout:0 completion:^(NSDictionary *reply,NSError *failure){
        if(failure){completion(failure);return;}
        NSString *message=nil;
        if([reply[@"exitcode"] isEqual:@64]){
            if([normalized[@"UnsupportedPolicy"] isEqual:@"fallback"]){
                NSMutableArray *skipped=[NSMutableArray arrayWithObjects:@"UserLanguages",@"LoginLanguage",nil];
                if(normalized[@"RegionCode"])[skipped addObject:@"LocaleIdentifier"];
                if(normalized[@"TimeZoneIdentifier"])[skipped addObject:@"TimeZoneIdentifier"];
                NSDictionary *result=@{@"Requested":normalized,@"Resolved":@{},@"Warnings":@[@"RegionalToolsUnavailable"],@"Skipped":skipped,@"Phase":@"skipped"};
                NSError *saveError=nil;BOOL saved=[self storeResult:result requested:normalized packageURL:package error:&saveError];
                completion(saved?nil:saveError);return;
            }
            message=VIMLocalized(@"regional.tools-required", @"This guest’s setup tools do not support regional settings. Rebuild or update the guest tools, or allow supported alternatives.");
        }else if([reply[@"exitcode"] isEqual:@0]){
            id data=reply[@"stdout"];
            id envelope=[data isKindOfClass:NSData.class]&&[data length]<=4*1024*1024?[NSJSONSerialization JSONObjectWithData:data options:0 error:NULL]:nil;
            if([envelope isKindOfClass:NSDictionary.class]&&[envelope[@"FormatVersion"] isEqual:@1]&&[envelope[@"Environment"] isEqual:@"Installed"]&&VIMRegionalGuestPlan(envelope[@"Plan"],normalized)) {
                completion(nil);return;
            }
        }else if([reply[@"exitcode"] isEqual:@65]) {
            message=VIMLocalized(@"regional.unsupported", @"This macOS version does not support all requested regional settings. Choose other settings or allow supported alternatives.");
        }
        completion([NSError errorWithDomain:@"VirtualIntelMac.RegionalSettings" code:2 userInfo:@{NSLocalizedDescriptionKey:message?:VIMLocalized(@"regional.capabilities-unconfirmed", @"The installed guest’s regional capabilities could not be confirmed. Try setup again after the installed system has started.")}]);
    }];
}
+ (BOOL)storeResult:(id)result requested:(NSDictionary *)request packageURL:(NSURL *)package error:(NSError **)error {
    if(error)*error=nil;
    NSDictionary *normalized=VIMRegionalNormalizeRequest(request,error);
    if(!normalized||![result isKindOfClass:NSDictionary.class]||![result[@"Requested"] isEqual:normalized]||
       ![result[@"Resolved"] isKindOfClass:NSDictionary.class]||![@[@"applying",@"verified",@"skipped"] containsObject:result[@"Phase"]]){
        return VIMRegionalInvalidResult(error);
    }
    NSMutableDictionary *resolved=[NSMutableDictionary new];
    for(NSString *key in @[@"DesktopLanguage",@"LoginLanguage",@"LocaleIdentifier",@"TimeZoneIdentifier"]){
        id value=result[@"Resolved"][key];
        if(value) {
            if(![value isKindOfClass:NSString.class]||![value length]||[value length]>256)return VIMRegionalInvalidResult(error);
            resolved[key]=value;
        }
    }
    id rawMappings=result[@"Resolved"][@"LanguageMappings"];
    if(rawMappings) {
        NSArray *mappings=VIMRegionalCleanMappings(rawMappings);if(!mappings)return VIMRegionalInvalidResult(error);
        resolved[@"LanguageMappings"]=mappings;
    }
    id userLanguages=result[@"Resolved"][@"UserLanguages"];
    if(userLanguages) {
        if(!VIMRegionalStringList(userLanguages,16))return VIMRegionalInvalidResult(error);
        resolved[@"UserLanguages"]=userLanguages;
    }
    NSMutableDictionary *clean=[@{@"Requested":normalized,@"Resolved":resolved,@"Phase":result[@"Phase"]} mutableCopy];
    for(NSString *key in @[@"Warnings",@"Skipped"]){
        id list=result[key];if(!VIMRegionalStringList(list,64))return VIMRegionalInvalidResult(error);
        clean[key]=list;
    }
    for(NSString *key in @[@"ProductVersion",@"ProductBuildVersion",@"TimeZoneDataVersion"]){
        id value=result[key];if([value isKindOfClass:NSString.class]&&[value length]<=80)clean[key]=value;
    }
    NSURL *file=[package URLByAppendingPathComponent:@"State/RegionalSettings.plist"];
    if([[NSDictionary dictionaryWithContentsOfURL:file] isEqual:clean])return YES;
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:clean format:NSPropertyListBinaryFormat_v1_0 options:0 error:error];
    return data&&[data writeToURL:file options:NSDataWritingAtomic error:error];
}
@end
