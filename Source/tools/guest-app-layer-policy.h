// Remove only the obsolete AppKit override, using cfprefsd's normal API so
// running apps cannot restore a stale on-disk plist over unrelated preferences.
// No renderer selection, graphics files, shader caches or boot caches change.
static NSDictionary *RemoveMojaveAppLayerOverrides(NSArray<NSString *> *users) {
    NSArray *domains=@[@"com.apple.Aperture",@"com.apple.iPhoto",@"com.apple.iWork.Keynote",
                       @"com.apple.iWork.Pages",@"com.apple.iWork.Numbers"];
    NSMutableArray *owners=[NSMutableArray arrayWithObject:(__bridge id)kCFPreferencesAnyUser];
    [owners addObjectsFromArray:users];
    NSUInteger removed=0,failed=0;
    for(NSString *owner in owners)for(NSString *domain in domains)
      for(id host in @[(__bridge id)kCFPreferencesAnyHost,(__bridge id)kCFPreferencesCurrentHost]) {
        CFPropertyListRef value=CFPreferencesCopyValue(CFSTR("NSViewAllowsRootLayerBacking"),
            (__bridge CFStringRef)domain,(__bridge CFStringRef)owner,(__bridge CFStringRef)host);
        if(!value)continue;
        CFRelease(value);
        CFPreferencesSetValue(CFSTR("NSViewAllowsRootLayerBacking"),NULL,
            (__bridge CFStringRef)domain,(__bridge CFStringRef)owner,(__bridge CFStringRef)host);
        if(CFPreferencesSynchronize((__bridge CFStringRef)domain,(__bridge CFStringRef)owner,
                                   (__bridge CFStringRef)host))removed++;
        else failed++;
    }
    return @{@"RemovedOverrides":@(removed),@"Failures":@(failed),@"RestartRequired":@NO};
}
