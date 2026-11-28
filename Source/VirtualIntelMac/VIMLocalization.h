#ifndef VIMLocalization_h
#define VIMLocalization_h

#import <Cocoa/Cocoa.h>

// Catalog entries are managed explicitly so keys remain stable when copy changes.
// Always pass literal keys and English fallbacks; test-localization checks both.
#define VIMLocalized(key, fallback) NSLocalizedStringWithDefaultValue(key, nil, NSBundle.mainBundle, fallback, nil)

// These are existing configuration/sort tokens, never translated stored values.
static inline NSString *VIMLocalizedLabel(NSString *identifier) {
    static NSDictionary<NSString *, NSString *> *labels;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ labels = @{
        @"General": VIMLocalized(@"category.general", @"General"),
        @"Hardware": VIMLocalized(@"category.hardware", @"Hardware"),
        @"Sharing": VIMLocalized(@"category.sharing", @"Sharing"),
        @"CPU & Memory": VIMLocalized(@"category.cpu-memory", @"CPU & Memory"),
        @"Graphics": VIMLocalized(@"category.graphics", @"Graphics"),
        @"Mouse & Keyboard": VIMLocalized(@"category.mouse-keyboard", @"Mouse & Keyboard"),
        @"Network": VIMLocalized(@"category.network", @"Network"),
        @"Sound": VIMLocalized(@"category.sound", @"Sound"),
        @"Sound Output": VIMLocalized(@"category.sound-output", @"Sound Output"),
        @"Microphone": VIMLocalized(@"category.microphone", @"Microphone"),
        @"Camera": VIMLocalized(@"category.camera", @"Camera"),
        @"Keyboard": VIMLocalized(@"category.keyboard", @"Keyboard"),
        @"Hard Disk": VIMLocalized(@"category.hard-disk", @"Hard Disk"),
        @"CD/DVD": VIMLocalized(@"category.optical", @"CD/DVD"),
        @"USB": VIMLocalized(@"category.usb", @"USB"),
        @"USB Devices": VIMLocalized(@"category.usb-devices", @"USB Devices"),
        @"File Sharing": VIMLocalized(@"category.file-sharing", @"File Sharing"),
        @"Remote Login": VIMLocalized(@"category.remote-login", @"Remote Login"),
        @"Clipboard": VIMLocalized(@"category.clipboard", @"Clipboard"),
        @"Name": VIMLocalized(@"sort.name", @"Name"),
        @"Type": VIMLocalized(@"sort.type", @"Type"),
        @"macOS Release": VIMLocalized(@"sort.release", @"macOS Release"),
        @"Date Last Opened": VIMLocalized(@"sort.last-opened", @"Date Last Opened"),
        @"Date Created": VIMLocalized(@"sort.created", @"Date Created"),
        @"Size": VIMLocalized(@"sort.size", @"Size"),
        @"Tags": VIMLocalized(@"sort.tags", @"Tags"),
        @"None": VIMLocalized(@"sort.none", @"None")
    }; });
    return labels[identifier] ?: identifier;
}

// Search identifiers, including nested menus, rather than visible menu titles.
static inline NSMenuItem *VIMMenuItem(NSMenu *menu, NSString *identifier) {
    for (NSMenuItem *item in menu.itemArray) {
        if ([item.identifier isEqual:identifier]) return item;
        NSMenuItem *child = VIMMenuItem(item.submenu, identifier);
        if (child) return child;
    }
    return nil;
}

static inline NSString *VIMMinutesRemaining(NSInteger minutes, BOOL approximate) {
    if (approximate) return [NSString localizedStringWithFormat:minutes == 1
        ? VIMLocalized(@"time.about-one-minute", @"About %ld minute remaining")
        : VIMLocalized(@"time.about-minutes", @"About %ld minutes remaining"), (long)minutes];
    return [NSString localizedStringWithFormat:minutes == 1
        ? VIMLocalized(@"time.one-minute", @"%ld minute remaining")
        : VIMLocalized(@"time.minutes", @"%ld minutes remaining"), (long)minutes];
}

static inline NSString *VIMHoursRemaining(NSInteger hours, NSInteger minutes) {
    if (!minutes) return [NSString localizedStringWithFormat:hours == 1
        ? VIMLocalized(@"time.one-hour", @"%ld hour remaining")
        : VIMLocalized(@"time.hours", @"%ld hours remaining"), (long)hours];
    NSString *format;
    if (hours == 1) format = minutes == 1
        ? VIMLocalized(@"time.one-hour-one-minute", @"%ld hour %ld minute remaining")
        : VIMLocalized(@"time.one-hour-minutes", @"%ld hour %ld minutes remaining");
    else format = minutes == 1
        ? VIMLocalized(@"time.hours-one-minute", @"%ld hours %ld minute remaining")
        : VIMLocalized(@"time.hours-minutes", @"%ld hours %ld minutes remaining");
    return [NSString localizedStringWithFormat:format, (long)hours, (long)minutes];
}

#endif
