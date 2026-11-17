#import <Foundation/Foundation.h>

// Apple's assembled 11+ installer links/copies the signed outer package as
// SharedSupport.dmg. Do not feed its unsigned inner DMG to the host recipe.
// This only resolves the input; the worker still authenticates the package
// and checks its real asset version before extracting any OS payload.
NS_INLINE NSString *VIMAssistantPackageSource(NSDictionary *media) {
    id package=media[@"assistant"];
    if(package)return [package isKindOfClass:NSString.class]&&[package isAbsolutePath]?package:nil;
    id installer=media[@"installer"];
    if(![installer isKindOfClass:NSString.class]||![installer isAbsolutePath]||
       ![[installer pathExtension].lowercaseString isEqual:@"app"])return nil;
    return [installer stringByAppendingPathComponent:@"Contents/SharedSupport/SharedSupport.dmg"];
}
