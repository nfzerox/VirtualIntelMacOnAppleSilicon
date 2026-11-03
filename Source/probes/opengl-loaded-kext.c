/* Read-only period-driver identity. Compare the loaded UUID to the packaged
 * Mach-O before attributing a same-version EFI driver experiment to new code.
 * Later IOKit has a dictionary-returning, two-array query. Snow's actual
 * kextstat instead uses the one-argument array-returning constructor.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <stdio.h>
int main(void){
    void *library=dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",RTLD_LAZY|RTLD_LOCAL);
    CFDictionaryRef (*loaded)(CFArrayRef,CFArrayRef)=library?dlsym(library,"OSKextCopyLoadedKextInfo"):NULL;
    // NULL requests the loaded set; filter identifiers locally on Snow.
    CFArrayRef (*snowLoaded)(CFArrayRef)=library?dlsym(library,"OSKextCreateLoadedKextInfo"):NULL;
    if(!loaded&&!snowLoaded)return 69;
    const void *ids[]={CFSTR("net.osx86.driver.VMsvga2"),CFSTR("net.osx86.driver.VMsvga2Accel")};
    CFArrayRef filter=CFArrayCreate(NULL,ids,2,&kCFTypeArrayCallBacks);
    CFTypeRef info=loaded?(CFTypeRef)loaded(filter,NULL):(CFTypeRef)snowLoaded(NULL);CFRelease(filter);if(!info)return 1;
    CFTypeID kind=CFGetTypeID(info);
    if(kind!=CFDictionaryGetTypeID()&&kind!=CFArrayGetTypeID()){CFRelease(info);return 65;}
    for(unsigned i=0;i<2;i++){
        CFDictionaryRef record=NULL;
        if(kind==CFDictionaryGetTypeID())record=CFDictionaryGetValue(info,ids[i]);
        else for(CFIndex j=0;j<CFArrayGetCount((CFArrayRef)info);j++){
            CFDictionaryRef candidate=CFArrayGetValueAtIndex((CFArrayRef)info,j);
            if(CFGetTypeID(candidate)!=CFDictionaryGetTypeID())continue;
            CFStringRef identifier=CFDictionaryGetValue(candidate,kCFBundleIdentifierKey);
            if(identifier&&CFEqual(identifier,ids[i])){record=candidate;break;}
        }
        CFDataRef uuid=record?CFDictionaryGetValue(record,CFSTR("OSBundleUUID")):NULL;
        char name[128];CFStringGetCString(ids[i],name,sizeof(name),kCFStringEncodingUTF8);
        printf("%s ",name);
        if(uuid&&CFGetTypeID(uuid)==CFDataGetTypeID()&&CFDataGetLength(uuid)==16){
            const UInt8 *p=CFDataGetBytePtr(uuid);for(unsigned j=0;j<16;j++)printf("%02x",p[j]);
        }else printf("UUID unavailable");
        puts("");
    }
    CFRelease(info);return 0;
}
