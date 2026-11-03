#pragma once
#include <SystemConfiguration/SystemConfiguration.h>

// These two ISA UARTs belong to our control and clipboard transports, not to
// networking. Native SCMonitor prompts only when default configuration adds a
// service for a previously unrepresented interface (configd395 SCMonitor and
// SCNetworkSet). Reserve just these ports as disabled services before the new
// account can start a user notification agent. Never change global discovery,
// Ethernet, USB modems, other serial ports, names or protocol configuration.
static inline Boolean VIMOpenGLControlSerialName(CFStringRef type,CFStringRef name){
    // Snow Leopard exposes ISA UARTs as Modem, despite networksetup describing
    // the same serial1/serial2 hardware as Built-in Serial Port. Match both
    // native classifications, but never a generic modem or USB serial device.
    return type&&name&&(CFEqual(type,kSCNetworkInterfaceTypeSerial)||CFEqual(type,kSCNetworkInterfaceTypeModem))&&
        (CFEqual(name,CFSTR("serial1"))||CFEqual(name,CFSTR("serial2")));
}
static inline CFStringRef VIMOpenGLControlInterfacePort(SCNetworkInterfaceRef interface){
    for(unsigned depth=0;interface&&depth<8;depth++,interface=SCNetworkInterfaceGetInterface(interface)){
        CFStringRef type=SCNetworkInterfaceGetInterfaceType(interface),name=SCNetworkInterfaceGetBSDName(interface);
        if(VIMOpenGLControlSerialName(type,name))return name;
    }
    return NULL;
}
static inline int VIMOpenGLReserveControlNetworkPorts(void){
    CFArrayRef interfaces=SCNetworkInterfaceCopyAll(),services=NULL;
    SCPreferencesRef preferences=NULL;SCNetworkSetRef set=NULL;
    Boolean locked=FALSE,changed=FALSE,handled[2]={FALSE,FALSE};int result=75;
    if(!interfaces)return 75;
    Boolean found=FALSE;
    for(CFIndex i=0;i<CFArrayGetCount(interfaces);i++)
        if(VIMOpenGLControlInterfacePort((SCNetworkInterfaceRef)CFArrayGetValueAtIndex(interfaces,i))){found=TRUE;break;}
    // Modern guests may use only virtio channels. No UART means no change.
    if(!found){CFRelease(interfaces);return 0;}
    preferences=SCPreferencesCreate(NULL,CFSTR("Virtual Intel Mac control ports"),NULL);
    if(!preferences||!SCPreferencesLock(preferences,FALSE))goto done;
    locked=TRUE;set=SCNetworkSetCopyCurrent(preferences);
    if(!set)goto done; // Let the existing cold setup retry after configd starts.
    services=SCNetworkServiceCopyAll(preferences);result=1;
    for(CFIndex i=0;i<CFArrayGetCount(interfaces);i++){
        SCNetworkInterfaceRef interface=(SCNetworkInterfaceRef)CFArrayGetValueAtIndex(interfaces,i);
        CFStringRef port=VIMOpenGLControlInterfacePort(interface);if(!port)continue;
        unsigned index=CFEqual(port,CFSTR("serial1"))?0:1;if(handled[index])continue;handled[index]=TRUE;
        Boolean represented=FALSE;
        for(CFIndex j=0;services&&j<CFArrayGetCount(services);j++){
            SCNetworkServiceRef service=(SCNetworkServiceRef)CFArrayGetValueAtIndex(services,j);
            CFStringRef existing=VIMOpenGLControlInterfacePort(SCNetworkServiceGetInterface(service));
            if(!existing||!CFEqual(existing,port))continue;
            represented=TRUE;
            if(SCNetworkServiceGetEnabled(service)){
                if(!SCNetworkServiceSetEnabled(service,FALSE))goto done;
                changed=TRUE;
            }
        }
        if(represented)continue;
        SCNetworkInterfaceRef ppp=SCNetworkInterfaceCreateWithInterface(interface,kSCNetworkInterfaceTypePPP);
        if(!ppp)goto done;
        SCNetworkServiceRef service=SCNetworkServiceCreate(preferences,ppp);CFRelease(ppp);
        if(!service)goto done;
        Boolean reserved=SCNetworkServiceSetEnabled(service,FALSE)&&SCNetworkSetAddService(set,service);
        CFRelease(service);if(!reserved)goto done;changed=TRUE;
    }
    // One cold preference transaction, not polling or a desktop hot path.
    if(changed&&(!SCPreferencesCommitChanges(preferences)||!SCPreferencesApplyChanges(preferences)))goto done;
    result=0;
done:
    if(services)CFRelease(services);if(set)CFRelease(set);
    if(locked)SCPreferencesUnlock(preferences);if(preferences)CFRelease(preferences);
    CFRelease(interfaces);return result;
}
