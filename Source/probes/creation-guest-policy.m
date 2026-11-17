#import "../VirtualIntelMac/VIMGuestFamilies.h"
#include <assert.h>
int main(void) { @autoreleasepool {
    NSArray *shipping=VIMCreationGuestChoices(NO),*experimental=VIMCreationGuestChoices(YES);
    assert(shipping.count==13&&[shipping.firstObject isEqual:@"tiger"]&&[shipping.lastObject isEqual:@"bigsur"]);
    for(NSString *guest in shipping)assert(!VIMIsPostBigSurGuest(guest));
    assert([experimental isEqual:VIMSupportedGuests()]&&experimental.count==18);
    assert([experimental containsObject:@"monterey"]&&[experimental.lastObject isEqual:@"tahoe"]);
    assert((shipping.count+1)/2==7); // Two columns include one recommendation row.
    assert((experimental.count+2)/3==6);
    puts("PASS default Tiger–Big Sur creation and gated Monterey–Tahoe experiment");
    return 0;
} }
