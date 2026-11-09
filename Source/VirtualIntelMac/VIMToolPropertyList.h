#pragma once
#import <Foundation/Foundation.h>

// Read the XML property list a system tool (diskutil, hdiutil) wrote to a log
// that also captured its stderr. Newer hosts append deprecation warnings, and
// code injected into every process (Ammonia tweaks, for example) can print
// while the tool loads or exits. Parse the log as-is first, then only the
// single <?xml … </plist> document it contains. Nothing else is inferred.
static inline id _Nullable VIMToolPropertyList(NSURL *_Nonnull log) {
    NSData *data=[NSData dataWithContentsOfURL:log];
    if(!data.length)return nil;
    id plist=[NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:NULL error:NULL];
    if(plist)return plist;
    NSRange start=[data rangeOfData:[@"<?xml" dataUsingEncoding:NSUTF8StringEncoding] options:0 range:NSMakeRange(0,data.length)];
    NSData *close=[@"</plist>" dataUsingEncoding:NSUTF8StringEncoding];
    NSRange end=[data rangeOfData:close options:NSDataSearchBackwards range:NSMakeRange(0,data.length)];
    if(start.location==NSNotFound||end.location==NSNotFound||NSMaxRange(end)<=start.location)return nil;
    NSData *document=[data subdataWithRange:NSMakeRange(start.location,NSMaxRange(end)-start.location)];
    return [NSPropertyListSerialization propertyListWithData:document options:NSPropertyListImmutable format:NULL error:NULL];
}

static inline NSDictionary *_Nullable VIMToolDictionary(NSURL *_Nonnull log) {
    id plist=VIMToolPropertyList(log);
    return [plist isKindOfClass:NSDictionary.class]?plist:nil;
}
