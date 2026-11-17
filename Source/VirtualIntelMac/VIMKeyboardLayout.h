#pragma once
#import <Foundation/Foundation.h>
#import <Carbon/Carbon.h>

// Physical shape, not the host's selected language/input method. Guest layouts
// and IMEs remain native; changing a language must not turn a JIS board into ANSI.
NS_INLINE NSString *VIMKeyboardLayoutName(PhysicalKeyboardLayoutType layout) {
    if(layout==kKeyboardISO)return @"iso";
    if(layout==kKeyboardJIS)return @"jis";
    return @"ansi";
}

// HIToolbox documents these calls as main-thread-only. Session launch already
// creates its AppKit window/view on that thread. Keep the virtual type stable
// for the boot, including while another physical keyboard is plugged in.
NS_INLINE NSString *VIMHostKeyboardLayout(void) {
    return VIMKeyboardLayoutName(KBGetLayoutType(LMGetKbdType()));
}
