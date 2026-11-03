/* Read-only native display-layout evidence. No mode change, capture, UI
 * automation or WindowServer connection ownership. Build against a period
 * SDK; the row-byte query is a public (deprecated) Snow Leopard API. */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
int main(void)
{
    CGDirectDisplayID display=CGMainDisplayID();
    CGDisplayModeRef mode=CGDisplayCopyDisplayMode(display);
    if(!mode)return 1;
    printf("logical=%zux%zu pixels=%zux%zu row_bytes=%zu\n",
        CGDisplayModeGetWidth(mode),CGDisplayModeGetHeight(mode),
        CGDisplayPixelsWide(display),CGDisplayPixelsHigh(display),
        CGDisplayBytesPerRow(display));
    CGDisplayModeRelease(mode);return 0;
}
