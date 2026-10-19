#pragma once
// Read only the kernel-published lock flag, not user/session dictionaries.
// This works in the root guest agent's bootstrap context where CGSession's
// current-user dictionary can be nil. Unknown never grants pasteboard access.
#import <IOKit/IOKitLib.h>
#import <CoreFoundation/CoreFoundation.h>

static inline NSInteger VIMConsoleLockStateFromProperty(CFTypeRef value) {
    return value&&CFGetTypeID(value)==CFBooleanGetTypeID()?
        (CFBooleanGetValue((CFBooleanRef)value)?1:0):-1;
}
static inline NSInteger VIMConsoleLockState(void) {
    io_registry_entry_t root=IORegistryGetRootEntry(MACH_PORT_NULL);
    if(!root)return -1;
    CFTypeRef value=IORegistryEntryCreateCFProperty(root,CFSTR("IOConsoleLocked"),NULL,0);
    IOObjectRelease(root);
    NSInteger state=VIMConsoleLockStateFromProperty(value);
    if(value)CFRelease(value);
    return state;
}
