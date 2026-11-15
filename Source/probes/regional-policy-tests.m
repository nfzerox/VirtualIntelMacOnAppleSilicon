#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#import "guest-regional-policy.h"

@interface NSLocale (VIMRegionalOldRegistryFixture)
+ (NSArray *)vimRegionalFixtureLanguages;
+ (NSArray *)vimRegionalFixtureCountries;
@end
@implementation NSLocale (VIMRegionalOldRegistryFixture)
+ (NSArray *)vimRegionalFixtureLanguages { return [NSArray arrayWithObject:@"en"]; }
+ (NSArray *)vimRegionalFixtureCountries { return [NSArray arrayWithObject:@"US"]; }
@end
@interface NSBundle (VIMRegionalBackstopFixture)
+ (NSArray *)vimRegionalLocalizations:(NSArray *)available preferences:(NSArray *)preferences;
@end
@implementation NSBundle (VIMRegionalBackstopFixture)
+ (NSArray *)vimRegionalLocalizations:(NSArray *)available preferences:(NSArray *)preferences {
    NSString *first=[preferences count]?[preferences objectAtIndex:0]:nil;
    if([first isEqual:@"zh-Hant-HK"]||[first isEqual:@"nb-NO"])return [NSArray arrayWithObject:@"English"];
    return [self vimRegionalLocalizations:available preferences:preferences];
}
@end

static int failures = 0;
static int checks = 0;
static void Check(BOOL passed, const char *name) {
    checks++;
    if (!passed) { fprintf(stderr, "FAIL: %s\n", name); failures++; }
}
static NSArray *List(NSString *items) { return [items componentsSeparatedByString:@","]; }
static NSMutableDictionary *Request(NSString *languages, NSString *region, NSString *zone, BOOL strict) {
    NSMutableDictionary *result = [NSMutableDictionary dictionaryWithObjectsAndKeys:
        [NSNumber numberWithInt:1], @"FormatVersion", @"custom", @"Source",
        List(languages), @"PreferredLanguages", strict ? @"strict" : @"fallback", @"UnsupportedPolicy", nil];
    if (region) [result setObject:region forKey:@"RegionCode"];
    if (zone) [result setObject:zone forKey:@"TimeZoneIdentifier"];
    return result;
}
static NSMutableDictionary *Caps(NSString *desktop, NSString *login, NSString *locales, NSString *zones) {
    return [NSMutableDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithInt:1], @"FormatVersion",
        [desktop length] ? List(desktop) : [NSArray array], @"DesktopLanguages",
        [login length] ? List(login) : [NSArray array], @"LoginLanguages",
        [locales length] ? List(locales) : [NSArray array], @"LocaleIdentifiers",
        [zones length] ? List(zones) : [NSArray array], @"TimeZoneIdentifiers", nil];
}
static BOOL Equal(id value, id expected) { return value && [value isEqual:expected]; }
static BOOL Has(NSDictionary *result, NSString *key, NSString *value) {
    return [[result objectForKey:key] containsObject:value];
}
static void NativeVariantTests(void) {
    NSDictionary *caps = Caps(@"English,en_GB,en_AU,pt,pt_PT,Spanish,es_419,zh_CN,zh_HK,zh_TW", @"English,en_GB,en_AU,pt,pt_PT,Spanish,es_419,zh_CN,zh_HK,zh_TW", @"en_US", @"");
    NSArray *requests = List(@"en-IN,pt-AO,es-MX,zh-Hant,zh-Hant-CN");
    NSArray *expected = List(@"en_GB,pt_PT,es_419,zh_TW,zh_TW");
    for (unsigned i = 0; i < [requests count]; i++) {
        NSDictionary *result = VIMRegionalResolve(Request([requests objectAtIndex:i], nil, nil, NO), caps, NULL);
        Check(Equal([result objectForKey:@"DesktopNativeLanguage"], [expected objectAtIndex:i]), "native parent-language data determines the selected regional resource");
        Check(Equal([result objectForKey:@"LoginNativeLanguage"], [expected objectAtIndex:i]), "login and desktop use the same native regional matching contract");
    }
    Method native=class_getClassMethod([NSBundle class],@selector(preferredLocalizationsFromArray:forPreferences:));
    Method fixture=class_getClassMethod([NSBundle class],@selector(vimRegionalLocalizations:preferences:));
    method_exchangeImplementations(native,fixture);
    @try {
        NSDictionary *old=Caps(@"English,zh_CN,zh_TW,no",@"English,zh_CN,zh_TW,no",@"en_US",@"");
        NSDictionary *r=VIMRegionalResolve(Request(@"zh-Hant-HK",nil,nil,YES),old,NULL);
        Check(Equal([r objectForKey:@"DesktopNativeLanguage"],@"zh_TW"),"an out-of-set native English backstop cannot discard positively verified traditional Chinese support");
        r=VIMRegionalResolve(Request(@"nb-NO",nil,nil,YES),old,NULL);
        Check(Equal([r objectForKey:@"DesktopNativeLanguage"],@"no"),"an out-of-set native backstop cannot discard a verified Bokmal resource");
    } @finally { method_exchangeImplementations(native,fixture); }
}
static void NormalizationTests(void) {
    NSError *error = nil;
    Check(VIMRegionalNormalizeRequest(nil, &error) == nil && error == nil, "absent request preserves legacy defaults");
    NSDictionary *normalized = VIMRegionalNormalizeRequest(Request(@"EN_us,iw-IL,sR_lAtN_rs,en-US", @"gb", @"Europe/London", NO), &error);
    Check(Equal([normalized objectForKey:@"PreferredLanguages"], List(@"en-US,he-IL,sr-Latn-RS")), "canonical case, aliases, scripts and stable deduplication");
    Check(Equal([normalized objectForKey:@"RegionCode"], @"GB"), "canonical region case");
    Check(Equal([normalized objectForKey:@"TimeZoneIdentifier"], @"Europe/London"), "time zone preserved");
    Check(VIMRegionalNormalizeRequest(Request(@"fil,haw,tlh,eo,cy,sw,es-419,sl-rozaj-biske,en-US-u-ca-gregory", @"419", nil, NO), &error) != nil, "accept real ISO languages, variants and extensions");
    normalized = VIMRegionalNormalizeRequest(Request(@"eng,fra,ger,zho,cmn,scc,scr", nil, nil, NO), &error);
    Check(Equal([normalized objectForKey:@"PreferredLanguages"], List(@"en,fr,de,zh,sr,hr")), "canonicalize real ISO3 and historical aliases omitted from ISO2 inventory");
    normalized = VIMRegionalNormalizeRequest(Request(@"zh-yue-Hant,yue-Hant", nil, nil, NO), &error);
    Check(Equal([normalized objectForKey:@"PreferredLanguages"], List(@"yue-Hant")), "canonicalize equivalent Cantonese extlang identity without losing it");
    Check(VIMRegionalNormalizeRequest(Request(@"zz", nil, nil, NO), &error) != nil && error == nil, "syntactic unknown language remains a support decision");
    Check(VIMRegionalNormalizeRequest(Request(@"en-ABCD-extra!", nil, nil, NO), &error) == nil, "reject malformed tag");
    Check(VIMRegionalNormalizeRequest(Request(@"en--US", nil, nil, NO), &error) == nil, "reject empty subtag");
    Check(VIMRegionalNormalizeRequest(Request(@"en-US-u", nil, nil, NO), &error) == nil, "reject extension without a payload");
    Check(VIMRegionalNormalizeRequest(Request(@"en-US-u-ca-gregory-u-nu-latn", nil, nil, NO), &error) == nil, "reject repeated extension singleton");
    Check(VIMRegionalNormalizeRequest(Request(@"en", @"ZZ", nil, NO), &error) != nil && error == nil, "syntactic unknown region remains a support decision");
    Check(VIMRegionalNormalizeRequest(Request(@"en", nil, @"../etc/localtime", NO), &error) == nil, "reject unsafe timezone path");
    Check(VIMRegionalNormalizeRequest(Request(@"en", nil, @"Europe/London\n", NO), &error) == nil, "reject timezone control character");
    Check(VIMRegionalNormalizeRequest(Request(@"en", nil, @"-settimezone", NO), &error) == nil, "reject option-shaped timezone identifier");
    NSMutableDictionary *bad = Request(@"en", nil, nil, NO);
    [bad setObject:[NSNumber numberWithInt:2] forKey:@"FormatVersion"];
    Check(VIMRegionalNormalizeRequest(bad, &error) == nil, "reject future request format");
    [bad setObject:[NSNumber numberWithInt:1] forKey:@"FormatVersion"];
    [bad setObject:@"hostile" forKey:@"Source"];
    Check(VIMRegionalNormalizeRequest(bad, &error) == nil, "validate source enum");
    bad = Request(@"en", nil, nil, NO); [bad setObject:[NSArray array] forKey:@"PreferredLanguages"];
    Check(VIMRegionalNormalizeRequest(bad, &error) == nil, "reject empty language array");
    bad = Request(@"en", nil, nil, NO); [bad setObject:[NSArray arrayWithObject:[NSNumber numberWithInt:3]] forKey:@"PreferredLanguages"];
    Check(VIMRegionalNormalizeRequest(bad, &error) == nil, "reject nonstring language");
    bad = Request(@"en", nil, nil, NO); [bad setObject:[NSNumber numberWithDouble:1.5] forKey:@"FormatVersion"];
    Check(VIMRegionalNormalizeRequest(bad, &error) == nil, "reject fractional version");
    bad = Request(@"en", nil, nil, NO); [bad setObject:@"sometimes" forKey:@"UnsupportedPolicy"];
    Check(VIMRegionalNormalizeRequest(bad, &error) == nil, "validate unsupported policy enum");
    NSMutableArray *many = [NSMutableArray array]; unsigned i;
    for (i = 0; i < 17; i++) [many addObject:@"en"];
    bad = Request(@"en", nil, nil, NO); [bad setObject:many forKey:@"PreferredLanguages"];
    Check(VIMRegionalNormalizeRequest(bad, &error) == nil, "limit before deduplication");
    NSMutableArray *mutableLanguages = [NSMutableArray arrayWithObject:@"en"];
    bad = Request(@"en", nil, nil, NO); [bad setObject:mutableLanguages forKey:@"PreferredLanguages"];
    normalized = VIMRegionalNormalizeRequest(bad, &error); [mutableLanguages addObject:@"fr"];
    Check(Equal([normalized objectForKey:@"PreferredLanguages"], List(@"en")), "normalization freezes mutable request values");
}
static void ResolutionTests(void) {
    NSError *error = nil;
    NSMutableDictionary *caps = Caps(@"en,zh-Hans,zh-Hant,sr-Cyrl,sr-Latn", @"English,zh_CN,zh_TW", @"en_US,zh_CN,zh_TW,es_419", @"Etc/UTC,Asia/Shanghai,America/Los_Angeles");
    NSDictionary *result = VIMRegionalResolve(Request(@"zh-Hans-CN,en", @"US", @"Asia/Shanghai", NO), caps, &error);
    Check(result != nil && error == nil, "resolve supported request");
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"zh-Hans"), "match desktop language while preserving script");
    Check(Equal([result objectForKey:@"LoginNativeLanguage"], @"zh_CN"), "retain native login resource name");
    Check(Equal([result objectForKey:@"UserLanguages"], List(@"zh-Hans-CN,en")), "preserve ordered supported preference specificity");
    Check(Equal([result objectForKey:@"LocaleIdentifier"], @"zh_Hans_US"), "derive independent verified region despite absent exact locale");
    Check(Equal([result objectForKey:@"TimeZoneIdentifier"], @"Asia/Shanghai"), "select verified timezone");
    Check([[result objectForKey:@"Warnings"] count] == 0 && [[result objectForKey:@"Skipped"] count] == 0, "supported request has no warnings or skips");
    result = VIMRegionalResolve(Request(@"zh-Hant", @"US", nil, NO), Caps(@"zh-Hans,en", @"zh_CN,English", @"en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"en"), "never cross Chinese scripts for fallback");
    Check(Equal([result objectForKey:@"LoginLanguage"], @"en"), "login fallback independent of desktop");
    Check(Has(result, @"Warnings", @"PreferredLanguageUnavailable") && Has(result, @"Warnings", @"DesktopLanguageFallback"), "fallback emits stable warning codes");
    Check(Equal([result objectForKey:@"LocaleIdentifier"], @"en_US"), "fallback retains requested independent region");
    result = VIMRegionalResolve(Request(@"sr-Latn", nil, nil, NO), Caps(@"sr,en", @"sr,English", @"sr_RS,en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"en"), "generic Serbian cannot silently replace explicit Latin");
    result = VIMRegionalResolve(Request(@"az-IR", nil, nil, NO), Caps(@"az-Latn,en", @"az-Latn,English", @"az_IR,en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"en") && Has(result, @"Warnings", @"PreferredLanguageUnavailable"), "Iranian Azerbaijani never silently selects Latin");
    result = VIMRegionalResolve(Request(@"az-Arab", nil, nil, NO), Caps(@"az-IR,en", @"az_IR,English", @"az_IR,en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopNativeLanguage"], @"az-IR") && Equal([result objectForKey:@"LoginNativeLanguage"], @"az_IR"), "explicit Arabic matches verified Iranian Azerbaijani native language");
    result = VIMRegionalResolve(Request(@"az-RU", nil, nil, NO), Caps(@"az-Latn,en", @"az-Latn,English", @"en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"en"), "unverified multiscript regional default does not become Latin");
    result = VIMRegionalResolve(Request(@"az-RU", nil, nil, NO), Caps(@"az-RU,en", @"az_RU,English", @"en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopNativeLanguage"], @"az-RU"), "an exact unresolved script identity can match its native resource");
    result = VIMRegionalResolve(Request(@"sw-KE", nil, nil, NO), Caps(@"sw,en", @"sw,English", @"sw_KE,en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"sw") && Equal([result objectForKey:@"LoginLanguage"], @"sw"), "Swahili regional preference matches its supported base language");
    result = VIMRegionalResolve(Request(@"lt-LT", nil, nil, YES), Caps(@"lt,en", @"lt,English", @"lt_LT,en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"lt") && Equal([result objectForKey:@"LoginLanguage"], @"lt") && error == nil, "strict Lithuanian regional preference matches its supported base language");
    result = VIMRegionalResolve(Request(@"is-IS", nil, nil, YES), Caps(@"is,en", @"is,English", @"is_IS,en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"is") && Equal([result objectForKey:@"LoginLanguage"], @"is") && error == nil, "strict Icelandic regional preference matches its supported base language");
    result = VIMRegionalResolve(Request(@"sw-Latn-KE", nil, nil, NO), Caps(@"sw-Arab,en", @"sw-Arab,English", @"sw_KE,en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"en"), "ordinary language fallback still rejects an explicit script conflict");
    result = VIMRegionalResolve(Request(@"kk-CN", nil, nil, NO), Caps(@"kk-KZ,en", @"kk-KZ,English", @"en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"en") && Has(result, @"Warnings", @"PreferredLanguageUnavailable"), "unknown script evidence cannot match Chinese Kazakh to Kazakhstan Kazakh");
    result = VIMRegionalResolve(Request(@"ku-IR", nil, nil, NO), Caps(@"ku-TR,en", @"ku-TR,English", @"en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"en"), "unknown script evidence cannot match Kurdish across differing regions");
    result = VIMRegionalResolve(Request(@"kk-CN", nil, nil, NO), Caps(@"kk,en", @"kk,English", @"en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"en"), "generic unknown-script resource cannot establish Chinese Kazakh compatibility");
    result = VIMRegionalResolve(Request(@"kk-Arab", nil, nil, NO), Caps(@"kk-CN,en", @"kk-CN,English", @"en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"en"), "explicit script requires confirmed compatible native evidence");
    result = VIMRegionalResolve(Request(@"kk-CN", nil, nil, YES), Caps(@"kk-KZ,en", @"kk-KZ,English", @"en_US", @""), &error);
    Check(result == nil && [error code] == 3, "strict rejects nonexact unverified script compatibility");
    result = VIMRegionalResolve(Request(@"en-GB", nil, nil, YES), Caps(@"en", @"English", @"en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"en"), "stable English region-to-base matching remains supported");
    result = VIMRegionalResolve(Request(@"fr-CA", nil, nil, YES), Caps(@"fr,en", @"fr,English", @"en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"fr"), "stable French region-to-base matching remains supported");
    result = VIMRegionalResolve(Request(@"de-AT", nil, nil, YES), Caps(@"de,en", @"de,English", @"en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"de"), "stable German region-to-base matching remains supported");
    result = VIMRegionalResolve(Request(@"vi-VN", nil, nil, YES), Caps(@"vi,en", @"vi,English", @"en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"vi"), "stable Vietnamese region-to-base matching remains supported");
    result = VIMRegionalResolve(Request(@"sl-SI", nil, nil, YES), Caps(@"sl,en", @"sl,English", @"en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"sl"), "native Slovenian resource supports its regional preference");
    result = VIMRegionalResolve(Request(@"no-NO", nil, nil, YES), Caps(@"no,en", @"no,English", @"en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"nb") && Equal([result objectForKey:@"DesktopNativeLanguage"], @"no"), "Norwegian regional alias retains native no resource spelling");
    result = VIMRegionalResolve(Request(@"zh-yue-Hant", nil, nil, NO), Caps(@"zh-Hant,en", @"zh-Hant,English", @"en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"en"), "Cantonese extlang never matches generic Chinese");
    result = VIMRegionalResolve(Request(@"yue-Hant", nil, nil, NO), Caps(@"zh-yue-Hant,en", @"zh-yue-Hant,English", @"en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"yue-Hant") && Equal([result objectForKey:@"DesktopNativeLanguage"], @"zh-yue-Hant"), "standalone Cantonese matches equivalent native extlang spelling");
    result = VIMRegionalResolve(Request(@"zh-foo-Hant", nil, nil, NO), Caps(@"zh-Hant,en", @"zh-Hant,English", @"en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"en"), "unrecognized extlang identity cannot collapse to its primary");
    result = VIMRegionalResolve(Request(@"en-US-u-ca-gregory", nil, nil, NO), caps, &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"en"), "extensions do not prevent same script language fallback");
    result = VIMRegionalResolve(Request(@"fr,en", @"GB", @"US/Pacific", NO), caps, &error);
    Check(Equal([result objectForKey:@"TimeZoneIdentifier"], @"America/Los_Angeles"), "only verified IANA timezone alias is selected");
    Check([result objectForKey:@"LocaleIdentifier"] == nil && Has(result, @"Skipped", @"RegionCode"), "unsupported region omitted instead of guessed");
    result = VIMRegionalResolve(Request(@"es-419", @"419", nil, NO), Caps(@"es,en", @"Spanish,English", @"es_419,en_US", @""), &error);
    Check(Equal([result objectForKey:@"LocaleIdentifier"], @"es_419"), "numeric regional locale supported");
    result = VIMRegionalResolve(Request(@"en", @"US", nil, NO), Caps(@"en", @"English", @"en_US_POSIX", @""), &error);
    Check(Equal([result objectForKey:@"LocaleIdentifier"], @"en_US"), "derive region without adopting unrequested locale variant");
    result = VIMRegionalResolve(Request(@"en", nil, @"Atlantis/Nowhere", NO), caps, &error);
    Check([result objectForKey:@"TimeZoneIdentifier"] == nil && Has(result, @"Warnings", @"TimeZoneUnavailable"), "unknown timezone never becomes UTC");
    result = VIMRegionalResolve(Request(@"en", nil, @"utc", NO), caps, &error);
    Check(Equal([result objectForKey:@"TimeZoneIdentifier"], @"Etc/UTC"), "verified timezone aliases normalize casing");
    result = VIMRegionalResolve(Request(@"en", nil, nil, NO), caps, &error);
    Check([result objectForKey:@"LocaleIdentifier"] == nil && [result objectForKey:@"TimeZoneIdentifier"] == nil, "absent optional choices preserve guest defaults");
    Check(VIMRegionalResolve(Request(@"zh-Hant", nil, nil, YES), Caps(@"en,zh-Hans", @"English", @"en_US", @""), &error) == nil && error != nil, "strict unsupported language fails");
    Check(VIMRegionalResolve(Request(@"en", @"GB", nil, YES), caps, &error) == nil, "strict unavailable region fails");
    Check(VIMRegionalResolve(Request(@"en", nil, @"Mars/Olympus", YES), caps, &error) == nil, "strict unavailable timezone fails");
    Check(VIMRegionalResolve(Request(@"en,ja", nil, nil, YES), Caps(@"en", @"English", @"en_US", @""), &error) == nil, "strict validates unsupported secondary user preference");
    result = VIMRegionalResolve(Request(@"en--US", nil, nil, NO), caps, &error);
    Check(result == nil && error != nil, "resolver validates raw request before matching");
    result = VIMRegionalResolve(Request(@"zz", @"ZZ", nil, NO), Caps(@"en", @"English", @"en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"en") && Has(result, @"Warnings", @"PreferredLanguageUnavailable") && Has(result, @"Skipped", @"RegionCode"), "unknown syntax never gains support from locale construction");
    result = VIMRegionalResolve(Request(@"zz", nil, nil, YES), Caps(@"en", @"English", @"en_US", @""), &error);
    Check(result == nil && [error code] == 3, "strict unknown language fails as unsupported instead of malformed");
    result = VIMRegionalResolve(Request(@"zz", nil, nil, NO), Caps(@"", @"English", @"en_US", @""), &error);
    Check([result objectForKey:@"DesktopLanguage"] == nil && Has(result, @"Warnings", @"DesktopLanguageUnavailable") && [[result objectForKey:@"UserLanguages"] count] == 0, "unknown language cannot invent a missing desktop fallback");
    result = VIMRegionalResolve(Request(@"ja", nil, nil, NO), Caps(@"en-GB,en,en-US", @"English", @"en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"en"), "English fallback prefers generic canonical language deterministically");
    result = VIMRegionalResolve(Request(@"de", nil, nil, NO), Caps(@"French,English,German", @"French,English", @"de_DE,en_US", @""), &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"de") && Equal([result objectForKey:@"DesktopNativeLanguage"], @"German"), "native lproj desktop aliases");
    Check(Equal([result objectForKey:@"LoginNativeLanguage"], @"English"), "login chooses verified English rather than first available language");
    caps = Caps(@"fr,de", @"French,German", @"fr_FR,de_DE", @"");
    result = VIMRegionalResolve(Request(@"ja", nil, nil, NO), caps, &error);
    Check([result objectForKey:@"DesktopLanguage"] == nil && Has(result, @"Skipped", @"DesktopLanguage"), "no arbitrary first resource fallback");
    result = VIMRegionalResolve(Request(@"ja", @"US", nil, NO), Caps(@"fr", @"fr", @"fr_US", @""), &error);
    Check([result objectForKey:@"DesktopLanguage"] == nil && Equal([result objectForKey:@"LocaleIdentifier"], @"fr_US") && !Has(result, @"Skipped", @"RegionCode"), "verified region survives absent desktop and language fallback");
    result = VIMRegionalResolve(Request(@"ja", @"US", nil, NO), Caps(@"fr", @"fr", @"fr_US,ja_US", @""), &error);
    Check([result objectForKey:@"DesktopLanguage"] == nil && Equal([result objectForKey:@"LocaleIdentifier"], @"ja_US"), "independent locale prefers requested language verified by locale inventory");
    [caps setObject:@"German" forKey:@"DevelopmentLanguage"]; [caps setObject:@"French" forKey:@"DefaultLoginLanguage"];
    result = VIMRegionalResolve(Request(@"ja", nil, nil, NO), caps, &error);
    Check(Equal([result objectForKey:@"DesktopLanguage"], @"de") && Equal([result objectForKey:@"LoginLanguage"], @"fr"), "explicit verified development and login default fallback");
    [caps setObject:@"ja" forKey:@"DevelopmentLanguage"];
    result = VIMRegionalResolve(Request(@"ja", nil, nil, NO), caps, &error);
    Check([result objectForKey:@"DesktopLanguage"] == nil, "development fallback must exist in target resources");
    caps = Caps(@"en", @"English", @"en_US", @""); [caps setObject:@"en" forKey:@"DesktopLanguages"];
    Check(VIMRegionalResolve(Request(@"en", nil, nil, NO), caps, &error) == nil && error != nil, "reject malformed capability schema");
    Check(VIMRegionalResolve(nil, nil, &error) == nil && error == nil, "absent request bypasses capability requirements");
}
static void OlderRegistryTests(void) {
    NSError *error = nil;
    NSDictionary *hostRequest = VIMRegionalNormalizeRequest(Request(@"ckb-IQ", @"SS", nil, NO), &error);
    Check(hostRequest != nil && error == nil, "host request for newer language and country is structurally valid");
    Method languages = class_getClassMethod([NSLocale class], @selector(ISOLanguageCodes));
    Method fixtureLanguages = class_getClassMethod([NSLocale class], @selector(vimRegionalFixtureLanguages));
    Method countries = class_getClassMethod([NSLocale class], @selector(ISOCountryCodes));
    Method fixtureCountries = class_getClassMethod([NSLocale class], @selector(vimRegionalFixtureCountries));
    method_exchangeImplementations(languages, fixtureLanguages);
    method_exchangeImplementations(countries, fixtureCountries);
    @try {
        Check(![[NSLocale ISOLanguageCodes] containsObject:@"ckb"] && ![[NSLocale ISOCountryCodes] containsObject:@"SS"], "probe simulates an older ISO registry");
        NSDictionary *normalized = VIMRegionalNormalizeRequest(hostRequest, &error);
        Check(Equal([normalized objectForKey:@"PreferredLanguages"], List(@"ckb-IQ")) && Equal([normalized objectForKey:@"RegionCode"], @"SS") && error == nil, "older ISO inventory cannot invalidate immutable host request");
        NSDictionary *result = VIMRegionalResolve(hostRequest, Caps(@"en", @"English", @"en_US", @""), &error);
        Check(Equal([result objectForKey:@"DesktopLanguage"], @"en") && Has(result, @"Warnings", @"PreferredLanguageUnavailable") && Has(result, @"Skipped", @"RegionCode") && error == nil, "old guest falls back and skips unsupported newer region");
        result = VIMRegionalResolve(Request(@"ckb-IQ", nil, nil, NO), Caps(@"ckb,en", @"ckb,English", @"en_US", @""), &error);
        Check(Equal([result objectForKey:@"DesktopLanguage"], @"ckb") && !Has(result, @"Warnings", @"PreferredLanguageUnavailable"), "actual native resource can establish support despite old ISO inventory");
        normalized = VIMRegionalNormalizeRequest(Request(@"zh-yue-Hant", @"SS", nil, NO), &error);
        Check(Equal([normalized objectForKey:@"PreferredLanguages"], List(@"yue-Hant")) && Equal([normalized objectForKey:@"RegionCode"], @"SS"), "extlang aliases do not depend on old ISO inventory");
    } @finally {
        method_exchangeImplementations(countries, fixtureCountries);
        method_exchangeImplementations(languages, fixtureLanguages);
    }
}
int main(void) {
#if !__has_feature(objc_arc)
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
#endif
    @autoreleasepool {
    NormalizationTests(); ResolutionTests(); OlderRegistryTests(); NativeVariantTests();
    fprintf(failures ? stderr : stdout, "%s: %d regional policy checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    }
#if !__has_feature(objc_arc)
    [pool drain];
#endif
    return failures ? 1 : 0;
}
