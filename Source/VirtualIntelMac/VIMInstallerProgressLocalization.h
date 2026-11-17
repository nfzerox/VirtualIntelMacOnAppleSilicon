#import <Foundation/Foundation.h>
#import "VIMGuestFamilies.h"

// The worker keeps its English diagnostic/protocol output. Localize only the
// title at the app's presentation boundary, never a stage identifier, path or
// command argument. English titles are this small table's readable keys.
static inline NSString *VIMLocalizedInstallerProgress(NSString *title,NSString *guest) {
    NSBundle *bundle=NSBundle.mainBundle;
    NSString *translated=[bundle localizedStringForKey:title value:title table:@"InstallerProgress"];
    if(![translated isEqual:title])return translated;
    NSString *release=VIMReleaseNameForGuest(guest);
    if(!release)return title;
    for(NSString *format in @[@"Opening the %@ installer…",@"Installing %@ system files…",@"Installing macOS %@…",
        @"Checking the optional %@ update…",@"Checking the optional %@ OS updates…",
        @"Unpacking the original %@ disk images…",@"Unpacking the original %@ installer…"]) {
        if([title isEqual:[NSString stringWithFormat:format,release]]) {
            NSString *localized=[bundle localizedStringForKey:format value:format table:@"InstallerProgress"];
            return [NSString stringWithFormat:localized,release];
        }
    }
    // New worker copy without a translation is still useful; localization is
    // never an admission check or a reason to interrupt an installation.
    return title;
}
