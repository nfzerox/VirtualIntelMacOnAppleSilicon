/* Finite interface-selection test only. No host preferences are opened or
 * changed; the three native interface accessors are modeled for layered PPP. */
#include <SystemConfiguration/SystemConfiguration.h>
#include <assert.h>
#include <stdio.h>
struct Interface {CFStringRef type,name;struct Interface *child;};
static CFStringRef Type(SCNetworkInterfaceRef value){return ((const struct Interface *)value)->type;}
static CFStringRef Name(SCNetworkInterfaceRef value){return ((const struct Interface *)value)->name;}
static SCNetworkInterfaceRef Child(SCNetworkInterfaceRef value){return (SCNetworkInterfaceRef)((const struct Interface *)value)->child;}
#define SCNetworkInterfaceGetInterfaceType Type
#define SCNetworkInterfaceGetBSDName Name
#define SCNetworkInterfaceGetInterface Child
#include "../tools/opengl-control-network.h"
#undef SCNetworkInterfaceGetInterfaceType
#undef SCNetworkInterfaceGetBSDName
#undef SCNetworkInterfaceGetInterface
int main(void){
    assert(VIMOpenGLControlSerialName(kSCNetworkInterfaceTypeSerial,CFSTR("serial1")));
    assert(VIMOpenGLControlSerialName(kSCNetworkInterfaceTypeSerial,CFSTR("serial2")));
    const CFStringRef untouched[]={CFSTR("en0"),CFSTR("serial0"),CFSTR("serial3"),
        CFSTR("usbserial1"),CFSTR("usbmodem1"),CFSTR("serial12"),CFSTR("serial1\n"),CFSTR("Serial1"),NULL};
    for(unsigned i=0;i<sizeof(untouched)/sizeof(*untouched);i++)
        assert(!VIMOpenGLControlSerialName(kSCNetworkInterfaceTypeSerial,untouched[i]));
    assert(!VIMOpenGLControlSerialName(kSCNetworkInterfaceTypeEthernet,CFSTR("serial1")));
    assert(VIMOpenGLControlSerialName(kSCNetworkInterfaceTypeModem,CFSTR("serial1")));
    assert(VIMOpenGLControlSerialName(kSCNetworkInterfaceTypeModem,CFSTR("serial2")));
    assert(!VIMOpenGLControlSerialName(kSCNetworkInterfaceTypeModem,CFSTR("usbmodem1")));
    struct Interface serial={kSCNetworkInterfaceTypeSerial,CFSTR("serial2"),NULL};
    struct Interface ppp={kSCNetworkInterfaceTypePPP,NULL,&serial};
    assert(CFEqual(VIMOpenGLControlInterfacePort((SCNetworkInterfaceRef)&ppp),CFSTR("serial2")));
    serial.type=kSCNetworkInterfaceTypeModem;
    assert(CFEqual(VIMOpenGLControlInterfacePort((SCNetworkInterfaceRef)&ppp),CFSTR("serial2")));
    serial.name=CFSTR("usbserial2");assert(!VIMOpenGLControlInterfacePort((SCNetworkInterfaceRef)&ppp));
    puts("PASS exact reserved UARTs and layered PPP; Ethernet, modems and other serial ports untouched");
    return 0;
}
