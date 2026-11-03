#pragma once
#import <CoreFoundation/CoreFoundation.h>
#include <unistd.h>

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
#include <launch.h>
#include <string.h>
// The Snow restore environment has liblaunch but no ServiceManagement
// framework (nor a dyld cache containing it). These setup helpers run as root
// in the system bootstrap namespace. Query its actual job dictionary without
// requiring an extra framework or inferring readiness from a plist on disk.
static inline bool VIMOpenGLSystemJobLoaded(CFStringRef label) {
    char text[256];
    if(geteuid()!=0||!CFStringGetCString(label,text,sizeof(text),kCFStringEncodingUTF8))return false;
    launch_data_t request=launch_data_alloc(LAUNCH_DATA_DICTIONARY);
    launch_data_t name=launch_data_new_string(text);
    if(!request||!name){if(request)launch_data_free(request);if(name)launch_data_free(name);return false;}
    if(!launch_data_dict_insert(request,name,LAUNCH_KEY_GETJOB)){
        launch_data_free(name);launch_data_free(request);return false;
    }
    launch_data_t reply=launch_msg(request);launch_data_free(request);
    bool loaded=false;
    if(reply&&launch_data_get_type(reply)==LAUNCH_DATA_DICTIONARY){
        launch_data_t value=launch_data_dict_lookup(reply,LAUNCH_JOBKEY_LABEL);
        const char *actual=value&&launch_data_get_type(value)==LAUNCH_DATA_STRING?launch_data_get_string(value):NULL;
        loaded=actual&&!strcmp(actual,text);
    }
    if(reply)launch_data_free(reply);return loaded;
}
#else
#import <ServiceManagement/ServiceManagement.h>
static inline bool VIMOpenGLSystemJobLoaded(CFStringRef label) {
    CFDictionaryRef job=SMJobCopyDictionary(kSMDomainSystemLaunchd,label);
    if(job)CFRelease(job);return job!=NULL;
}
#endif
