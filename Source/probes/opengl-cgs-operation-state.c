/* Read-only Mavericks Dock ripple gate. Its matching decrypted Dock calls
 * CGSServerOperationState with EDI=15 and consumes AL (byte boolean). This
 * reports that operation's state; do not label it QE/CI without following
 * the matching CoreGraphics server implementation and physical comparison.
 */
#include <ApplicationServices/ApplicationServices.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
int main(void)
{
    uint8_t (*operation)(uint32_t)=dlsym(RTLD_DEFAULT,"CGSServerOperationState");
    uint8_t (*accelerated)(CGDirectDisplayID)=dlsym(RTLD_DEFAULT,"CGDisplayUsesOpenGLAcceleration");
    if(!operation || !accelerated)return 2;
    CGDirectDisplayID display=CGMainDisplayID();
    printf("display=%u OpenGLAcceleration=%u DockOperation15=%u\n",display,accelerated(display),operation(15));
    return 0;
}
