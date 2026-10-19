// Run in the guest: compare advertised VPA JPEG capability with session creation.
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
int main(void) {
    alarm(10);
    void *lib=dlopen("/System/Library/PrivateFrameworks/AppleVPA.framework/AppleVPA",RTLD_NOW);
    if(!lib){fprintf(stderr,"%s\n",dlerror());return 2;}
    Boolean (*available)(void)=dlsym(lib,"VPA_HWJPEG_GetAccelerationAvailability");
    int (*create)(CFAllocatorRef,CFDictionaryRef,CFTypeRef *)=dlsym(lib,"VPA_HWJPEG_DecodeSessionCreate");
    if(!available||!create)return 3;
    CFTypeRef session=NULL;
    printf("advertised=%d\n",available());fflush(stdout);
    int result=create(kCFAllocatorDefault,NULL,&session);
    printf("sessionStatus=%d session=%s\n",result,session?"present":"NULL");
    if(session)CFRelease(session);
    return 0;
}
