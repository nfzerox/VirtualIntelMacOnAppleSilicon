/* Private-domain test only: never writes the user's real screen-saver policy. */
#include "../tools/guest-screensaver-preferences.h"
#include <stdio.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/wait.h>

int main(int argc,char **argv) {
    if(argc!=2)return 64;
    struct passwd *account=getpwnam(argv[1]);
    if(!account||account->pw_uid!=geteuid())return 77;
    CFStringRef user=kCFPreferencesCurrentUser;
    CFUUIDRef uuid=CFUUIDCreate(NULL);
    CFStringRef identifier=uuid?CFUUIDCreateString(NULL,uuid):NULL;
    CFStringRef domain=identifier?CFStringCreateWithFormat(NULL,NULL,CFSTR("com.mac.virtualintelmac.screen-test.%@"),identifier):NULL;
    if(!domain)return 1;
    int old=600,retained=7;
    CFNumberRef previous=CFNumberCreate(NULL,kCFNumberIntType,&old);
    CFNumberRef marker=CFNumberCreate(NULL,kCFNumberIntType,&retained);
    CFPreferencesSetValue(CFSTR("idleTime"),previous,domain,user,kCFPreferencesCurrentHost);
    CFPreferencesSetValue(CFSTR("fixtureUnrelated"),marker,domain,user,kCFPreferencesCurrentHost);
    CFPreferencesSynchronize(domain,user,kCFPreferencesCurrentHost);
    Boolean ok=VIMPrepareScreenSaverPreferences(domain,user);
    CFPropertyListRef actual=CFPreferencesCopyValue(CFSTR("fixtureUnrelated"),domain,user,kCFPreferencesCurrentHost);
    ok=ok&&actual&&CFEqual(actual,marker);
    char name[256];
    if(!CFStringGetCString(domain,name,sizeof(name),kCFStringEncodingUTF8))ok=false;
    else {
        pid_t child=fork();
        if(child==0){execl("/usr/bin/defaults","defaults","-currentHost","read",name,"idleTime",(char *)NULL);_exit(127);}
        int status=0;
        if(child<0||waitpid(child,&status,0)!=child||!WIFEXITED(status)||WEXITSTATUS(status))ok=false;
    }
    CFStringRef keys[]={CFSTR("idleTime"),CFSTR("askForPassword"),CFSTR("askForPasswordDelay"),CFSTR("fixtureUnrelated")};
    CFStringRef hosts[]={kCFPreferencesCurrentHost,kCFPreferencesAnyHost};
    unsigned h,k;
    for(h=0;h<2;h++) {
        for(k=0;k<4;k++)CFPreferencesSetValue(keys[k],NULL,domain,user,hosts[h]);
        CFPreferencesSynchronize(domain,user,hosts[h]);
    }
    if(actual)CFRelease(actual);
    CFRelease(marker);CFRelease(previous);CFRelease(domain);CFRelease(identifier);CFRelease(uuid);
    puts(ok?"PASS: user-visible CurrentHost and AnyHost defaults; unrelated preferences preserved":"FAIL: initial screen-saver preferences");
    return ok?0:1;
}
