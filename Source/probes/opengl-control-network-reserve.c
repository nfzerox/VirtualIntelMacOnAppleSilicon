/* Explicit, one-shot repair for already-created development guests. Uses the
 * same native preference transaction as automatic account setup; no account,
 * Ethernet, discovery policy or desktop notification settings are changed. */
#include "../tools/opengl-control-network.h"
#include <stdio.h>
#include <unistd.h>

int main(void) {
    if (geteuid() != 0) {
        fputs("Run inside the test guest as root.\n", stderr);
        return 77;
    }
    CFArrayRef interfaces = SCNetworkInterfaceCopyAll();
    for (CFIndex i = 0; interfaces && i < CFArrayGetCount(interfaces); i++) {
        SCNetworkInterfaceRef interface = (SCNetworkInterfaceRef)CFArrayGetValueAtIndex(interfaces, i);
        CFShow(SCNetworkInterfaceGetInterfaceType(interface));
        CFStringRef name = SCNetworkInterfaceGetBSDName(interface);
        if (name) CFShow(name);
    }
    if (interfaces) CFRelease(interfaces);
    int result = VIMOpenGLReserveControlNetworkPorts();
    printf("Control serial-port reservation: %s (%d)\n",
           result == 0 ? "complete" : "not committed", result);
    return result;
}
