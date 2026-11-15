#import "guest-regional-policy.h"
#import <CoreFoundation/CoreFoundation.h>

static void VIMRegionalError(NSError **error, int code, NSString *reason) {
    if (error) *error = [NSError errorWithDomain:@"VIMRegionalPolicyError" code:code
        userInfo:[NSDictionary dictionaryWithObject:reason forKey:NSLocalizedDescriptionKey]];
}
static BOOL VIMRegionalASCII(NSString *value, unsigned minimum, unsigned maximum, int kind) {
    unsigned length = (unsigned)[value length], index;
    if (length < minimum || length > maximum) return NO;
    for (index = 0; index < length; index++) {
        unichar c = [value characterAtIndex:index];
        BOOL alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        BOOL digit = c >= '0' && c <= '9';
        if (!(kind == 1 ? alpha : kind == 2 ? digit : alpha || digit)) return NO;
    }
    return YES;
}
static NSString *VIMRegionalAlias(NSString *value, NSString *pairs) {
    NSArray *parts = [pairs componentsSeparatedByString:@" "];
    unsigned index;
    for (index = 0; index + 1 < [parts count]; index += 2)
        if ([value isEqual:[parts objectAtIndex:index]]) return [parts objectAtIndex:index + 1];
    return value;
}
static NSString *VIMRegionalRegion(id value) {
    if (![value isKindOfClass:[NSString class]]) return nil;
    NSString *region = [value uppercaseString];
    region = VIMRegionalAlias(region, @"UK GB BU MM DD DE FX FR TP TL YD YE ZR CD");
    if (VIMRegionalASCII(region, 3, 3, 2)) return region;
    if (VIMRegionalASCII(region, 2, 2, 1)) return region;
    return nil;
}
static NSString *VIMRegionalNativeAlias(NSString *input) {
    NSString *key = [input lowercaseString];
    NSArray *names = [NSArray arrayWithObjects:@"english", @"french", @"german", @"italian", @"dutch", @"spanish", @"portuguese", @"japanese", @"korean", @"chinese", @"simplified chinese", @"traditional chinese", @"swedish", @"danish", @"norwegian", @"finnish", @"russian", @"polish", @"czech", @"hungarian", @"turkish", @"arabic", @"hebrew", @"greek", @"thai", @"ukrainian", @"catalan", @"croatian", @"romanian", @"slovak", @"indonesian", @"malay", @"vietnamese", nil];
    NSArray *codes = [NSArray arrayWithObjects:@"en", @"fr", @"de", @"it", @"nl", @"es", @"pt", @"ja", @"ko", @"zh", @"zh-Hans", @"zh-Hant", @"sv", @"da", @"nb", @"fi", @"ru", @"pl", @"cs", @"hu", @"tr", @"ar", @"he", @"el", @"th", @"uk", @"ca", @"hr", @"ro", @"sk", @"id", @"ms", @"vi", nil];
    unsigned index;
    for (index = 0; index < [names count]; index++)
        if ([key isEqual:[names objectAtIndex:index]]) return [codes objectAtIndex:index];
    return input;
}
/* The CF primitive is present in Tiger; the equivalent NSLocale class
 * selector is absent from the period Foundation interfaces. Copy into an
 * autoreleased Foundation value before releasing the owned CF result. */
static NSString *VIMRegionalCanonicalLanguage(NSString *value) {
#if __has_feature(objc_arc)
    CFStringRef canonical = CFLocaleCreateCanonicalLanguageIdentifierFromString(kCFAllocatorDefault, (__bridge CFStringRef)value);
    NSString *result = canonical ? [NSString stringWithString:(__bridge NSString *)canonical] : nil;
#else
    CFStringRef canonical = CFLocaleCreateCanonicalLanguageIdentifierFromString(kCFAllocatorDefault, (CFStringRef)value);
    NSString *result = canonical ? [NSString stringWithString:(NSString *)canonical] : nil;
#endif
    if (canonical) CFRelease(canonical);
    return result;
}
static BOOL VIMRegionalAmbiguousScript(NSString *identity) {
    return [[NSArray arrayWithObjects:@"zh", @"yue", @"sr", @"az", @"uz", @"pa", @"mn", nil] containsObject:identity];
}
static NSString *VIMRegionalKnownRegionalScript(NSString *identity, NSString *region) {
    NSString *pairs = nil;
    if ([identity isEqual:@"zh"]) pairs = @"CN Hans SG Hans TW Hant HK Hant MO Hant";
    if ([identity isEqual:@"yue"]) pairs = @"CN Hans HK Hant MO Hant";
    if ([identity isEqual:@"az"]) pairs = @"AZ Latn IR Arab";
    if ([identity isEqual:@"uz"]) pairs = @"UZ Latn AF Arab";
    if ([identity isEqual:@"pa"]) pairs = @"IN Guru PK Arab";
    if ([identity isEqual:@"mn"]) pairs = @"MN Cyrl CN Mong";
    if ([identity isEqual:@"sr"]) pairs = @"RS Cyrl BA Cyrl";
    if (!pairs || !region) return nil;
    NSString *script = VIMRegionalAlias(region, pairs);
    return [script isEqual:region] ? nil : script;
}
/* Validate bounded syntax independently of this process's ISO registry. A host
 * may know a newer language or country than the guest's Foundation does. CF
 * supplies known canonical aliases; only native capability inventories later
 * establish support, never successful construction of an arbitrary NSLocale.
 */
static NSString *VIMRegionalLanguage(id value, BOOL native) {
    if (![value isKindOfClass:[NSString class]] || ![value length] || [value length] > 64) return nil;
    NSString *input = native ? VIMRegionalNativeAlias(value) : value;
    input = [[input componentsSeparatedByString:@"_"] componentsJoinedByString:@"-"];
    input = VIMRegionalAlias([input lowercaseString], @"i-klingon tlh i-lux lb no-bok nb no-nyn nn en-gb-oed en-GB-oxendict zh-min-nan nan");
    NSArray *parts = [input componentsSeparatedByString:@"-"];
    NSString *rawPrimary = [[parts objectAtIndex:0] lowercaseString];
    if (!VIMRegionalASCII(rawPrimary, 2, 8, 1)) return nil;
    NSString *primary = VIMRegionalAlias(rawPrimary, @"iw he in id ji yi jw jv mo ro tl fil sh sr");
    NSString *foundationPrimary = VIMRegionalCanonicalLanguage(primary);
    if (VIMRegionalASCII(foundationPrimary, 2, 8, 1)) primary = foundationPrimary;
    NSMutableArray *output = [NSMutableArray arrayWithObject:primary];
    unsigned index = 1, extlangs = 0;
    while (index < [parts count] && extlangs < 3 && [rawPrimary length] <= 3 && VIMRegionalASCII([parts objectAtIndex:index], 3, 3, 1)) {
        [output addObject:[[parts objectAtIndex:index] lowercaseString]]; index++; extlangs++;
    }
    if (extlangs) {
        NSString *identity = [output componentsJoinedByString:@"-"];
        /* Preserve an extlang's identity even when period CF does not know its
         * preferred spelling. These explicit aliases cover the canonical
         * Chinese forms whose generic zh prefix would otherwise be unsafe. */
        NSString *alias = VIMRegionalAlias(identity, @"zh-yue yue zh-cmn zh zh-hak hak zh-nan nan zh-wuu wuu zh-gan gan zh-hsn hsn");
        NSString *canonicalIdentity = VIMRegionalCanonicalLanguage(alias);
        if (VIMRegionalASCII(canonicalIdentity, 2, 8, 1)) {
            primary = canonicalIdentity;
            [output removeAllObjects]; [output addObject:primary];
        }
    }
    NSString *script = nil, *region = nil;
    if (index < [parts count] && VIMRegionalASCII([parts objectAtIndex:index], 4, 4, 1)) {
        script = [[parts objectAtIndex:index] capitalizedString]; index++;
    }
    if (index < [parts count]) {
        NSString *part = [parts objectAtIndex:index];
        if (VIMRegionalASCII(part, 2, 2, 1) || VIMRegionalASCII(part, 3, 3, 2)) {
            region = VIMRegionalRegion(part);
            if (!region) return nil;
            index++;
        }
    }
    if (!script && [rawPrimary isEqual:@"sh"]) script = @"Latn";
    if (!script && [output count] == 1) script = VIMRegionalKnownRegionalScript(primary, region);
    if (script) [output addObject:script];
    if (region) [output addObject:region];
    NSMutableArray *variants = [NSMutableArray array], *singletons = [NSMutableArray array];
    while (index < [parts count]) {
        NSString *part = [[parts objectAtIndex:index] lowercaseString];
        BOOL variant = VIMRegionalASCII(part, 5, 8, 0) || (VIMRegionalASCII(part, 4, 4, 0) && [part characterAtIndex:0] >= '0' && [part characterAtIndex:0] <= '9');
        if (!variant) break;
        if ([variants containsObject:part]) return nil;
        [variants addObject:part]; [output addObject:part]; index++;
    }
    while (index < [parts count]) {
        NSString *singleton = [[parts objectAtIndex:index] lowercaseString];
        if (!VIMRegionalASCII(singleton, 1, 1, 0) || [singletons containsObject:singleton]) return nil;
        [singletons addObject:singleton]; [output addObject:singleton]; index++;
        unsigned first = index, minimum = [singleton isEqual:@"x"] ? 1 : 2;
        while (index < [parts count] && VIMRegionalASCII([parts objectAtIndex:index], minimum, 8, 0)) {
            [output addObject:[[parts objectAtIndex:index] lowercaseString]]; index++;
        }
        if (index == first || ([singleton isEqual:@"x"] && index != [parts count])) return nil;
    }
    return [output componentsJoinedByString:@"-"];
}
static BOOL VIMRegionalZoneSyntax(id value) {
    if (![value isKindOfClass:[NSString class]] || ![value length] || [value length] > 128) return NO;
    if ([value hasPrefix:@"-"]) return NO;
    NSArray *parts = [value componentsSeparatedByString:@"/"];
    unsigned index, offset;
    for (index = 0; index < [parts count]; index++) {
        NSString *part = [parts objectAtIndex:index];
        if (![part length] || [part isEqual:@"."] || [part isEqual:@".."] || [part hasPrefix:@"."]) return NO;
        for (offset = 0; offset < [part length]; offset++) {
            unichar c = [part characterAtIndex:offset];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '+')) return NO;
        }
    }
    return YES;
}
static BOOL VIMRegionalVersion(id value) {
    return [value isKindOfClass:[NSNumber class]] && [value doubleValue] == 1.0;
}
NSDictionary *VIMRegionalNormalizeRequest(id request, NSError **error) {
    if (error) *error = nil;
    if (!request) return nil;
    if (![request isKindOfClass:[NSDictionary class]]) { VIMRegionalError(error, 1, @"Regional request must be a dictionary."); return nil; }
    NSString *source = [request objectForKey:@"Source"], *policy = [request objectForKey:@"UnsupportedPolicy"];
    id languages = [request objectForKey:@"PreferredLanguages"];
    if (!VIMRegionalVersion([request objectForKey:@"FormatVersion"]) ||
        !([source isEqual:@"host"] || [source isEqual:@"custom"]) ||
        !([policy isEqual:@"fallback"] || [policy isEqual:@"strict"]) ||
        ![languages isKindOfClass:[NSArray class]] || ![languages count] || [languages count] > 16) {
        VIMRegionalError(error, 1, @"Invalid regional request version, source, policy, or language list."); return nil;
    }
    NSMutableArray *normalizedLanguages = [NSMutableArray array];
    unsigned index;
    for (index = 0; index < [languages count]; index++) {
        NSString *language = VIMRegionalLanguage([languages objectAtIndex:index], NO);
        if (!language) { VIMRegionalError(error, 1, @"Invalid preferred language identifier."); return nil; }
        if (![normalizedLanguages containsObject:language]) [normalizedLanguages addObject:language];
    }
    NSMutableDictionary *normalized = [NSMutableDictionary dictionaryWithObjectsAndKeys:
        [NSNumber numberWithInt:1], @"FormatVersion", [NSString stringWithString:source], @"Source",
        [NSArray arrayWithArray:normalizedLanguages], @"PreferredLanguages", [NSString stringWithString:policy], @"UnsupportedPolicy", nil];
    id regionValue = [request objectForKey:@"RegionCode"], zone = [request objectForKey:@"TimeZoneIdentifier"];
    if (regionValue) {
        NSString *region = VIMRegionalRegion(regionValue);
        if (!region) { VIMRegionalError(error, 1, @"Invalid region code."); return nil; }
        [normalized setObject:region forKey:@"RegionCode"];
    }
    if (zone) {
        if (!VIMRegionalZoneSyntax(zone)) { VIMRegionalError(error, 1, @"Invalid time zone identifier."); return nil; }
        [normalized setObject:[NSString stringWithString:zone] forKey:@"TimeZoneIdentifier"];
    }
    return [NSDictionary dictionaryWithDictionary:normalized];
}
static NSString *VIMRegionalIdentity(NSString *language) {
    NSArray *parts = [language componentsSeparatedByString:@"-"];
    NSMutableArray *identity = [NSMutableArray arrayWithObject:[parts objectAtIndex:0]];
    unsigned index;
    for (index = 1; index < [parts count]; index++) {
        NSString *part = [parts objectAtIndex:index];
        if (!VIMRegionalASCII(part, 3, 3, 1)) break;
        [identity addObject:part];
    }
    return [identity componentsJoinedByString:@"-"];
}
static NSString *VIMRegionalScript(NSString *language) {
    NSArray *parts = [language componentsSeparatedByString:@"-"];
    unsigned index;
    for (index = 1; index < [parts count]; index++) {
        NSString *part = [parts objectAtIndex:index];
        if ([part length] == 1) break;
        if (VIMRegionalASCII(part, 4, 4, 1)) return part;
    }
    NSString *primary = VIMRegionalIdentity(language);
    if ([primary rangeOfString:@"-"].location != NSNotFound) return nil;
    NSString *region = nil;
    for (index = 1; index < [parts count]; index++) {
        NSString *part = [parts objectAtIndex:index];
        if ([part length] == 1) break;
        if (VIMRegionalASCII(part, 2, 2, 1) || VIMRegionalASCII(part, 3, 3, 2)) { region = part; break; }
    }
    /* A regional ID for a language with multiple common writing systems must
     * never inherit its language-wide default when the regional mapping is
     * unknown. Exact native identities still match in SameLanguage below. */
    if (region && VIMRegionalAmbiguousScript(primary))
        return VIMRegionalKnownRegionalScript(primary, region);
    /* Positive default-script evidence only. The sw/lt/is/sl/ckb entries are
     * confirmed by Unicode CLDR 47 likely_subtags: sw_Latn_TZ, lt_Latn_LT,
     * is_Latn_IS, sl_Latn_SI, ckb_Arab_IQ. Unknown languages do not inherit a script.
     * https://www.unicode.org/cldr/charts/47/supplemental/likely_subtags.html */
    NSString *defaults = @"zh Hans yue Hant sr Cyrl az Latn uz Latn pa Guru mn Cyrl en Latn fr Latn de Latn it Latn es Latn pt Latn nl Latn sv Latn da Latn nb Latn nn Latn fi Latn pl Latn cs Latn sk Latn sl Latn hr Latn ro Latn hu Latn tr Latn vi Latn id Latn ms Latn fil Latn ca Latn cy Latn eo Latn sw Latn lt Latn is Latn ckb Arab ru Cyrl uk Cyrl bg Cyrl mk Cyrl be Cyrl ar Arab fa Arab ur Arab he Hebr yi Hebr el Grek th Thai ja Jpan ko Kore hi Deva bn Beng ta Taml te Telu ka Geor hy Armn";
    NSString *script = VIMRegionalAlias(primary, defaults);
    return [script isEqual:primary] ? nil : script;
}
static BOOL VIMRegionalSameLanguage(NSString *left, NSString *right) {
    if ([left isEqual:right]) return YES;
    if (![VIMRegionalIdentity(left) isEqual:VIMRegionalIdentity(right)]) return NO;
    NSString *leftScript = VIMRegionalScript(left), *rightScript = VIMRegionalScript(right);
    /* Every nonexact match requires positive script compatibility. Two
     * missing scripts do not establish that different regions, or a region
     * and a generic resource, use the same writing system. */
    return leftScript && rightScript && [leftScript isEqual:rightScript];
}
NSString *VIMRegionalCanonicalLanguageIdentifier(id value) { return VIMRegionalLanguage(value, YES); }
BOOL VIMRegionalCompatibleLanguageIdentifiers(NSString *left, NSString *right) {
    NSString *a=VIMRegionalLanguage(left,YES), *b=VIMRegionalLanguage(right,YES);
    return a && b && VIMRegionalSameLanguage(a,b);
}
static NSArray *VIMRegionalLanguageRecords(NSArray *values) {
    NSMutableArray *records = [NSMutableArray array]; unsigned index;
    for (index = 0; index < [values count]; index++) {
        NSString *native = [values objectAtIndex:index];
        NSString *canonical = VIMRegionalLanguage(native, YES);
        if (canonical) [records addObject:[NSDictionary dictionaryWithObjectsAndKeys:canonical, @"Canonical", [NSString stringWithString:native], @"Native", nil]];
    }
    return records;
}
static NSDictionary *VIMRegionalMatch(NSString *requested, NSArray *records) {
    NSMutableArray *safeNames = [NSMutableArray array]; unsigned index;
    for (index = 0; index < [records count]; index++) {
        NSDictionary *record = [records objectAtIndex:index];
        NSString *canonical = [record objectForKey:@"Canonical"], *native = [record objectForKey:@"Native"];
        if (!VIMRegionalSameLanguage(requested, canonical)) continue;
        [safeNames addObject:native];
    }
    if (![safeNames count]) return nil;
    // Bundle knows native parent-language order. Its backstop is safe only
    // after filtering language identity and script: otherwise an unsupported
    // preference could silently match English or an incompatible writing system.
    NSArray *choices=[NSBundle preferredLocalizationsFromArray:safeNames forPreferences:[NSArray arrayWithObject:requested]];
    NSString *chosen=[choices count]?[choices objectAtIndex:0]:nil;
    for(index=0;index<[records count];index++) {
        NSDictionary *record=[records objectAtIndex:index];
        if([[record objectForKey:@"Native"] isEqual:chosen]&&[safeNames containsObject:chosen])return record;
    }
    // Some period Bundle implementations return an English backstop even
    // outside the supplied array. The inventory already proves these safe
    // language/script alternatives exist; prefer an exact or generic native
    // resource and later encode/verify its exact native spelling.
    NSDictionary *best=nil;int score=-1;
    for(index=0;index<[records count];index++) {
        NSDictionary *record=[records objectAtIndex:index];
        NSString *native=[record objectForKey:@"Native"],*canonical=[record objectForKey:@"Canonical"];
        if(![safeNames containsObject:native])continue;
        int candidate=[canonical isEqual:requested]?3:([canonical rangeOfString:@"-"].location==NSNotFound?2:1);
        if(candidate>score){best=record;score=candidate;}
    }
    return best;
}
static NSDictionary *VIMRegionalFallback(NSArray *records, NSDictionary *capabilities, BOOL login) {
    NSDictionary *record = VIMRegionalMatch(@"en", records);
    if (record) return record;
    NSArray *keys = login ? [NSArray arrayWithObjects:@"DefaultLoginLanguage", @"DevelopmentLanguage", nil] : [NSArray arrayWithObjects:@"DevelopmentLanguage", @"DefaultDesktopLanguage", nil];
    unsigned index;
    for (index = 0; index < [keys count]; index++) {
        NSString *language = VIMRegionalLanguage([capabilities objectForKey:[keys objectAtIndex:index]], YES);
        if (language) { record = VIMRegionalMatch(language, records); if (record) return record; }
    }
    return nil;
}
static void VIMRegionalAdd(NSMutableArray *array, NSString *value) {
    if (![array containsObject:value]) [array addObject:value];
}
static NSString *VIMRegionalZone(NSString *requested, NSArray *available) {
    unsigned index, groupIndex;
    for (index = 0; index < [available count]; index++) {
        NSString *candidate = [available objectAtIndex:index];
        if (VIMRegionalZoneSyntax(candidate) && [candidate caseInsensitiveCompare:requested] == NSOrderedSame) return candidate;
    }
    /* IANA backward-link groups, never an offset comparison or a guessed UTC.
     * The returned member must still occur in this guest's own inventory. */
    NSArray *groups = [NSArray arrayWithObjects:
        @"UTC,Etc/UTC,UCT,Etc/UCT,Universal,Etc/Universal,Zulu,Etc/Zulu",
        @"GMT,Etc/GMT,GMT0,Etc/GMT0,Greenwich,Etc/Greenwich,GMT+0,GMT-0,Etc/GMT+0,Etc/GMT-0",
        @"America/Los_Angeles,US/Pacific", @"America/New_York,US/Eastern",
        @"America/Chicago,US/Central", @"America/Denver,US/Mountain",
        @"America/Phoenix,US/Arizona", @"America/Anchorage,US/Alaska",
        @"Pacific/Honolulu,US/Hawaii", @"Asia/Kolkata,Asia/Calcutta",
        @"Asia/Ho_Chi_Minh,Asia/Saigon", @"Europe/Kyiv,Europe/Kiev", nil];
    for (groupIndex = 0; groupIndex < [groups count]; groupIndex++) {
        NSArray *group = [[groups objectAtIndex:groupIndex] componentsSeparatedByString:@","];
        BOOL found = NO;
        for (index = 0; index < [group count]; index++)
            if ([[group objectAtIndex:index] caseInsensitiveCompare:requested] == NSOrderedSame) { found = YES; break; }
        if (!found) continue;
        unsigned member;
        for (member = 0; member < [group count]; member++)
            for (index = 0; index < [available count]; index++)
                if ([[available objectAtIndex:index] caseInsensitiveCompare:[group objectAtIndex:member]] == NSOrderedSame) return [available objectAtIndex:index];
    }
    return nil;
}
static NSString *VIMRegionalLocale(NSString *language, NSString *region, NSArray *locales) {
    BOOL supportedRegion = NO; NSString *nativeMatch = nil; unsigned index;
    NSString *primary = [[VIMRegionalIdentity(language) componentsSeparatedByString:@"-"] componentsJoinedByString:@"_"];
    /* Use only an explicit script in the locale we write, not a guessed script. */
    NSArray *languageParts = [language componentsSeparatedByString:@"-"];
    NSString *script = nil;
    for (index = 1; index < [languageParts count]; index++) {
        NSString *part = [languageParts objectAtIndex:index];
        if ([part length] == 1) break;
        if (VIMRegionalASCII(part, 4, 4, 1)) { script = part; break; }
    }
    NSString *wanted = script ? [NSString stringWithFormat:@"%@_%@_%@", primary, script, region] : [NSString stringWithFormat:@"%@_%@", primary, region];
    NSString *wantedLanguage = VIMRegionalLanguage(wanted, NO);
    for (index = 0; index < [locales count]; index++) {
        NSString *candidate = [locales objectAtIndex:index];
        NSDictionary *components = [NSLocale componentsFromLocaleIdentifier:candidate];
        NSString *candidateRegion = VIMRegionalRegion([components objectForKey:NSLocaleCountryCode]);
        if (![candidateRegion isEqual:region]) continue;
        NSString *candidateLanguage = VIMRegionalLanguage(candidate, YES);
        if (!candidateLanguage) continue;
        supportedRegion = YES;
        if (![wantedLanguage isEqual:candidateLanguage]) continue;
        if ([candidate isEqual:wanted]) return candidate;
        if (!nativeMatch || [candidate compare:nativeMatch options:NSLiteralSearch] == NSOrderedAscending) nativeMatch = candidate;
    }
    if (nativeMatch) return nativeMatch;
    return supportedRegion ? wanted : nil;
}
static NSString *VIMRegionalIndependentLocale(NSArray *preferred, NSString *region, NSDictionary *capabilities) {
    NSArray *locales = [capabilities objectForKey:@"LocaleIdentifiers"];
    NSString *bestNative = nil, *bestLanguage = nil;
    int bestScore = -1;
    unsigned index, languageIndex;
    for (index = 0; index < [locales count]; index++) {
        NSString *candidate = [locales objectAtIndex:index];
        NSDictionary *components = [NSLocale componentsFromLocaleIdentifier:candidate];
        if (![VIMRegionalRegion([components objectForKey:NSLocaleCountryCode]) isEqual:region]) continue;
        NSString *language = VIMRegionalLanguage(candidate, YES);
        if (!language) continue;
        int score = 0;
        if ([candidate isEqual:[capabilities objectForKey:@"DefaultLocaleIdentifier"]] ||
            [candidate isEqual:[capabilities objectForKey:@"CurrentLocaleIdentifier"]]) score = 100;
        for (languageIndex = 0; languageIndex < [preferred count]; languageIndex++) {
            if (VIMRegionalSameLanguage([preferred objectAtIndex:languageIndex], language)) {
                score = 1000 - (int)languageIndex; break;
            }
        }
        if (!bestNative || score > bestScore || (score == bestScore && [candidate compare:bestNative options:NSLiteralSearch] == NSOrderedAscending)) {
            bestNative = candidate; bestLanguage = language; bestScore = score;
        }
    }
    /* A native locale can establish the region independently of any selected
     * desktop resource. It never establishes an invented DesktopLanguage. */
    return bestLanguage ? VIMRegionalLocale(bestLanguage, region, locales) : nil;
}
NSDictionary *VIMRegionalResolve(NSDictionary *request, NSDictionary *capabilities, NSError **error) {
    if (error) *error = nil;
    if (!request) return nil;
    NSDictionary *normalized = VIMRegionalNormalizeRequest(request, error);
    if (!normalized) return nil;
    if (![capabilities isKindOfClass:[NSDictionary class]] || !VIMRegionalVersion([capabilities objectForKey:@"FormatVersion"])) {
        VIMRegionalError(error, 2, @"Invalid regional capability format."); return nil;
    }
    NSArray *keys = [NSArray arrayWithObjects:@"DesktopLanguages", @"LoginLanguages", @"LocaleIdentifiers", @"TimeZoneIdentifiers", nil];
    unsigned index, item;
    for (index = 0; index < [keys count]; index++) {
        id values = [capabilities objectForKey:[keys objectAtIndex:index]];
        if (![values isKindOfClass:[NSArray class]] || [values count] > 10000) { VIMRegionalError(error, 2, @"Invalid regional capability list."); return nil; }
        for (item = 0; item < [values count]; item++)
            if (![[values objectAtIndex:item] isKindOfClass:[NSString class]] || [[values objectAtIndex:item] length] > 256) { VIMRegionalError(error, 2, @"Invalid regional capability identifier."); return nil; }
    }
    BOOL strict = [[normalized objectForKey:@"UnsupportedPolicy"] isEqual:@"strict"];
    NSArray *desktopRecords = VIMRegionalLanguageRecords([capabilities objectForKey:@"DesktopLanguages"]);
    NSArray *loginRecords = VIMRegionalLanguageRecords([capabilities objectForKey:@"LoginLanguages"]);
    NSArray *preferred = [normalized objectForKey:@"PreferredLanguages"];
    NSMutableArray *userLanguages = [NSMutableArray array], *userResources = [NSMutableArray array], *warnings = [NSMutableArray array], *skipped = [NSMutableArray array];
    NSDictionary *desktop = nil, *login = nil;
    for (index = 0; index < [preferred count]; index++) {
        NSString *language = [preferred objectAtIndex:index];
        NSDictionary *match = VIMRegionalMatch(language, desktopRecords);
        if (match) { if (!desktop) desktop = match; [userLanguages addObject:language]; [userResources addObject:[match objectForKey:@"Native"]]; }
        else {
            if (strict) { VIMRegionalError(error, 3, @"A requested desktop language is unavailable."); return nil; }
            VIMRegionalAdd(warnings, @"PreferredLanguageUnavailable");
            [skipped addObject:[@"PreferredLanguages:" stringByAppendingString:language]];
        }
        if (!login) login = VIMRegionalMatch(language, loginRecords);
    }
    if (!desktop) {
        desktop = VIMRegionalFallback(desktopRecords, capabilities, NO);
        if (desktop) { [userLanguages addObject:[desktop objectForKey:@"Canonical"]]; [userResources addObject:[desktop objectForKey:@"Native"]]; }
        VIMRegionalAdd(warnings, @"DesktopLanguageFallback");
    }
    if (!login) login = VIMRegionalFallback(loginRecords, capabilities, YES);
    NSDictionary *firstLogin = VIMRegionalMatch([preferred objectAtIndex:0], loginRecords);
    if (!firstLogin) {
        if (strict) { VIMRegionalError(error, 3, @"The requested login language is unavailable."); return nil; }
        VIMRegionalAdd(warnings, @"LoginLanguageFallback");
    }
    NSMutableDictionary *result = [NSMutableDictionary dictionaryWithObjectsAndKeys:
        normalized, @"Requested", [NSArray arrayWithArray:userLanguages], @"UserLanguages", [NSArray arrayWithArray:userResources], @"UserNativeLanguages", nil];
    if (desktop) {
        [result setObject:[desktop objectForKey:@"Canonical"] forKey:@"DesktopLanguage"];
        [result setObject:[desktop objectForKey:@"Native"] forKey:@"DesktopNativeLanguage"];
    } else { VIMRegionalAdd(skipped, @"DesktopLanguage"); VIMRegionalAdd(warnings, @"DesktopLanguageUnavailable"); }
    if (login) {
        [result setObject:[login objectForKey:@"Canonical"] forKey:@"LoginLanguage"];
        [result setObject:[login objectForKey:@"Native"] forKey:@"LoginNativeLanguage"];
    } else { VIMRegionalAdd(skipped, @"LoginLanguage"); VIMRegionalAdd(warnings, @"LoginLanguageUnavailable"); }
    NSString *region = [normalized objectForKey:@"RegionCode"];
    if (region) {
        NSString *locale = desktop ? VIMRegionalLocale([desktop objectForKey:@"Canonical"], region, [capabilities objectForKey:@"LocaleIdentifiers"]) : VIMRegionalIndependentLocale(preferred, region, capabilities);
        if (locale) [result setObject:locale forKey:@"LocaleIdentifier"];
        else {
            if (strict) { VIMRegionalError(error, 3, @"The requested region cannot be verified in guest locale data."); return nil; }
            VIMRegionalAdd(skipped, @"RegionCode"); VIMRegionalAdd(warnings, @"RegionUnavailable");
        }
    }
    NSString *zone = [normalized objectForKey:@"TimeZoneIdentifier"];
    if (zone) {
        NSString *resolvedZone = VIMRegionalZone(zone, [capabilities objectForKey:@"TimeZoneIdentifiers"]);
        if (resolvedZone) [result setObject:resolvedZone forKey:@"TimeZoneIdentifier"];
        else {
            if (strict) { VIMRegionalError(error, 3, @"The requested time zone is unavailable."); return nil; }
            VIMRegionalAdd(skipped, @"TimeZoneIdentifier"); VIMRegionalAdd(warnings, @"TimeZoneUnavailable");
        }
    }
    [result setObject:[NSArray arrayWithArray:warnings] forKey:@"Warnings"];
    [result setObject:[NSArray arrayWithArray:skipped] forKey:@"Skipped"];
    return [NSDictionary dictionaryWithDictionary:result];
}
