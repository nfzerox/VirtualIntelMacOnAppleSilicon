// Diagnose the period NetFSServer ABI. Read-only unless explicitly --enable.
#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>
#import "../tools/opengl-sharing.h"
int main(int argc,const char **argv){@autoreleasepool{
    if(argc==2&&!strcmp(argv[1],"--configure")){
        if(geteuid()!=0)return 77;
        int result=VIMOpenGLConfigureFileSharing();printf("configure-result=%d disk-policy=%d\n",result,VIMOpenGLFileSharingConfigured());return result;
    }
    CFURLRef url=CFURLCreateWithFileSystemPath(NULL,CFSTR("/System/Library/PrivateFrameworks/NetFSServer.framework"),kCFURLPOSIXPathStyle,true);
    CFBundleRef bundle=url?CFBundleCreate(NULL,url):NULL;if(url)CFRelease(url);
    if(!bundle){puts("bundle-created=0");return 1;}
    int (*enabled)(const char *,int *)=(void *)CFBundleGetFunctionPointerForName(bundle,CFSTR("nsc_server_isenabled"));
    int (*enable)(const char *)=(void *)CFBundleGetFunctionPointerForName(bundle,CFSTR("nsc_server_enable"));
    void *disable=CFBundleGetFunctionPointerForName(bundle,CFSTR("nsc_server_disable"));
    printf("bundle-loaded=%d enabled-symbol=%d enable-symbol=%d disable-symbol=%d\n",
        CFBundleIsExecutableLoaded(bundle),enabled!=NULL,enable!=NULL,disable!=NULL);
    if(enabled)for(const char **name=(const char *[]){"smb","SMB","afp",NULL};*name;name++){
        int on=0,result=enabled(*name,&on);printf("protocol=%s query-result=%d enabled=%d\n",*name,result,on);
    }
    if(argc==2&&!strcmp(argv[1],"--enable")&&enable){
        if(geteuid()!=0)return 77;
        int result=enable("smb"),on=0,query=enabled?enabled("smb",&on):-1;
        printf("enable-result=%d query-result=%d enabled=%d\n",result,query,on);
    }
    id services=[[NSDictionary dictionaryWithContentsOfFile:@"/Library/Preferences/SystemConfiguration/com.apple.smb.server.plist"] objectForKey:@"EnabledServices"];
    printf("disk-policy=%d\n",[services isKindOfClass:[NSArray class]]&&[services containsObject:@"disk"]);
    CFRelease(bundle);return 0;
}}
