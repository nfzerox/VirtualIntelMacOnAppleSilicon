/* Per-launch GL_RESOURCES comparison using real Launch Services document
 * delivery. Passing a file as argv does not grant sandboxed apps its access.
 * No global environment, app modification, injection or system preference.
 * Protected Apple apps can ignore GL_RESOURCES. Verify the loaded driver
 * before interpreting results as a comparison; this tool does not bypass it.
 */
#include <ApplicationServices/ApplicationServices.h>
#include <CoreServices/CoreServices.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv)
{
    if(argc!=4){fprintf(stderr,"Use APP DOCUMENT GL_RESOURCES.\n");return 64;}
    FSRef application;
    OSStatus status=FSPathMakeRef((const UInt8 *)argv[1],&application,NULL);
    if(status){fprintf(stderr,"Application lookup failed: %d\n",(int)status);return 1;}
    CFURLRef document=CFURLCreateFromFileSystemRepresentation(NULL,(const UInt8 *)argv[2],strlen(argv[2]),false);
    CFStringRef resources=CFStringCreateWithFileSystemRepresentation(NULL,argv[3]);
    if(!document||!resources){if(document)CFRelease(document);if(resources)CFRelease(resources);return 1;}
    CFArrayRef urls=CFArrayCreate(NULL,(const void **)&document,1,&kCFTypeArrayCallBacks);
    const void *key=CFSTR("GL_RESOURCES"),*value=resources;
    CFDictionaryRef environment=CFDictionaryCreate(NULL,&key,&value,1,
        &kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    const void *arguments[]={CFSTR("-ApplePersistenceIgnoreState"),CFSTR("YES")};
    CFArrayRef argumentsArray=CFArrayCreate(NULL,arguments,2,&kCFTypeArrayCallBacks);
    LSApplicationParameters parameters={0};
    parameters.application=&application;parameters.environment=environment;parameters.argv=argumentsArray;
    parameters.flags=kLSLaunchNewInstance|kLSLaunchDontAddToRecents;
    ProcessSerialNumber process={0};pid_t pid=0;
    status=urls&&environment&&argumentsArray?
        LSOpenURLsWithRole(urls,kLSRolesAll,NULL,&parameters,&process,1):memFullErr;
    if(!status)status=GetProcessPID(&process,&pid);
    if(argumentsArray)CFRelease(argumentsArray);if(environment)CFRelease(environment);if(urls)CFRelease(urls);
    CFRelease(resources);CFRelease(document);
    if(status||pid<1){fprintf(stderr,"Document launch failed: %d\n",(int)status);return 1;}
    printf("TEST_APP_PID=%d\n",pid);return 0;
}
