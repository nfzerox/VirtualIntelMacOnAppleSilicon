#import <Foundation/Foundation.h>
#include <string.h>

// A creation-time snapshot, not a recurring host-to-guest preference sync.
NS_INLINE NSArray<NSString *> *VIMGuestLocalePreferenceKeys(void) {
    return @[@"AppleLanguages",@"AppleLocale",@"AppleMetricUnits",@"AppleMeasurementUnits",
        @"AppleTemperatureUnit",@"AppleICUForce24HourTime",@"AppleICUForce12HourTime",
        @"AppleFirstWeekday",@"AppleMinDaysInFirstWeek",@"AppleICUDateFormatStrings",
        @"AppleICUTimeFormatStrings",@"AppleICUNumberFormatStrings",@"AppleCollationOrder"];
}
NS_INLINE NSDictionary *VIMCaptureGuestLocale(void) {
    NSDictionary *global=[NSUserDefaults.standardUserDefaults persistentDomainForName:NSGlobalDomain]?:@{};
    NSMutableDictionary *preferences=[NSMutableDictionary dictionary];
    for(NSString *key in VIMGuestLocalePreferenceKeys())if(global[key])preferences[key]=global[key];
    if(![preferences[@"AppleLanguages"] isKindOfClass:NSArray.class]||![preferences[@"AppleLanguages"] count])
        preferences[@"AppleLanguages"]=NSLocale.preferredLanguages.count?NSLocale.preferredLanguages:@[@"en"];
    if(![preferences[@"AppleLocale"] isKindOfClass:NSString.class]||![preferences[@"AppleLocale"] length])
        preferences[@"AppleLocale"]=NSLocale.currentLocale.localeIdentifier;
    NSTimeZone *zone=NSTimeZone.localTimeZone;
    NSMutableDictionary *result=[@{@"FormatVersion":@1,@"Preferences":preferences,@"TimeZone":zone.name} mutableCopy];
    NSData *data=zone.data;
    if(data.length>=4&&!memcmp(data.bytes,"TZif",4))result[@"TimeZoneData"]=data;
    return result;
}
NS_INLINE NSDictionary *VIMGuestLocalePreferences(NSDictionary *profile) {
    NSDictionary *source=[profile[@"Preferences"] isKindOfClass:NSDictionary.class]?profile[@"Preferences"]:@{};
    NSMutableDictionary *preferences=[NSMutableDictionary dictionary];
    for(NSString *key in VIMGuestLocalePreferenceKeys()) {
        id value=source[key];
        if(value&&[NSPropertyListSerialization propertyList:value isValidForFormat:NSPropertyListXMLFormat_v1_0])preferences[key]=value;
    }
    NSMutableArray *languages=[NSMutableArray array];
    if([preferences[@"AppleLanguages"] isKindOfClass:NSArray.class])
        for(id language in preferences[@"AppleLanguages"])
            if([language isKindOfClass:NSString.class]&&[language length])[languages addObject:language];
    preferences[@"AppleLanguages"]=languages.count?languages:@[@"en"];
    if(![preferences[@"AppleLocale"] isKindOfClass:NSString.class]||![preferences[@"AppleLocale"] length])preferences[@"AppleLocale"]=@"en_US";
    return preferences;
}
NS_INLINE NSString *VIMGuestLocaleTemplate(NSArray<NSString *> *available,NSDictionary *profile) {
    NSArray *languages=VIMGuestLocalePreferences(profile)[@"AppleLanguages"];
    NSArray *preferred=[NSBundle preferredLocalizationsFromArray:available forPreferences:[languages arrayByAddingObject:@"en"]];
    return preferred.firstObject;
}
