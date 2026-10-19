// PVG exposes graphics, not Intel's hardware JPEG decoder. AppleVPA's board-ID
// table otherwise advertises the emulated physical Mac's missing decoder.
static void PrepareVPA(NSString *input, NSString *output) {
    NSError *error=nil;
    NSData *data=[NSData dataWithContentsOfFile:input options:0 error:&error];
    Check(data!=nil,error.localizedDescription?:@"Missing native AppleVPA capability table");
    NSMutableDictionary *plist=[NSPropertyListSerialization propertyListWithData:data
        options:NSPropertyListMutableContainersAndLeaves format:NULL error:&error];
    Check([plist isKindOfClass:NSMutableDictionary.class],@"Invalid AppleVPA property list");
    NSMutableDictionary *support=plist[@"System Support"];
    Check([support isKindOfClass:NSMutableDictionary.class]&&support.count,
          @"Unknown AppleVPA capability table layout");
    NSUInteger entries=0;
    for(id key in support) {
        NSMutableDictionary *entry=support[key];
        Check([entry isKindOfClass:NSMutableDictionary.class],@"Invalid AppleVPA platform entry");
        if(entry[@"jpeg"]!=nil) {
            Check(CFGetTypeID((__bridge CFTypeRef)entry[@"jpeg"])==CFBooleanGetTypeID(),
                  @"Unknown AppleVPA JPEG capability type");
            entry[@"jpeg"]=@NO;entries++;
        }
    }
    Check(entries>0,@"AppleVPA JPEG capability entries missing");
    // Preserve every other capability and current guest version's metadata.
    data=[NSPropertyListSerialization dataWithPropertyList:plist
        format:NSPropertyListXMLFormat_v1_0 options:0 error:&error];
    Check(data&&[data writeToFile:output options:NSDataWritingAtomic error:&error],
          error.localizedDescription?:@"Cannot stage AppleVPA capabilities");
}
