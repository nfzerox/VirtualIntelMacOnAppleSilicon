// Finite helper-only experiment. Load after donor classes; no host patch.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>
#include "../vendor/qemu/hw/display/apple-gfx-packed422.h"
#include <stdio.h>

__attribute__((constructor)) static void install(void) {
    VIMInstallPacked422Backing();
    fprintf(stderr,"PVG v216 backing experiment installed\n");
}
