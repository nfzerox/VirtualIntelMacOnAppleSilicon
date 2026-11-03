// Native read-only SharingPref/SFL control. Never enables a service or changes
// authorization. A missing API is reported, not treated as disabled sharing.
#include <CoreServices/CoreServices.h>
#include <dlfcn.h>
#include <stdio.h>
int main(void){
    const CFStringRef *name=dlsym(RTLD_DEFAULT,"kLSSharedFileListSharePoints");
    const CFStringRef *property=dlsym(RTLD_DEFAULT,"kLSSharedFileListSMBSharingEnabled");
    printf("sharing-symbols=%d\n",name&&property&&*name&&*property);
    if(!name||!property||!*name||!*property)return 1;
    LSSharedFileListRef list=LSSharedFileListCreate(NULL,*name,NULL);
    printf("native-sharing-list=%d\n",list!=NULL);if(!list)return 1;
    CFTypeRef value=LSSharedFileListCopyProperty(list,*property);CFRelease(list);
    int boolean=value&&CFGetTypeID(value)==CFBooleanGetTypeID();
    printf("native-sharing-property=%d enabled=%d\n",boolean,boolean&&CFBooleanGetValue(value));
    if(value)CFRelease(value);return boolean?0:1;
}
