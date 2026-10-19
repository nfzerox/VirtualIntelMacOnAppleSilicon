#import <Foundation/Foundation.h>
#include <sys/stat.h>
#include <unistd.h>
static void Check(BOOL ok,NSString *message){if(!ok)@throw [NSException exceptionWithName:@"Fixture" reason:message userInfo:nil];}
#include "../tools/guest-install-trust.h"
int main(void){@autoreleasepool {@try {
    char name[]="/private/var/tmp/VirtualIntelMac.XXXXXX";
    Check(mkdtemp(name)!=NULL,@"Cannot create fixture");
    NSString *root=@(name),*file=[root stringByAppendingPathComponent:@"journal.plist"];
    Check([@"fixture" writeToFile:file atomically:YES encoding:NSUTF8StringEncoding error:NULL],@"Cannot write fixture");
    chmod(file.fileSystemRepresentation,0600);
    Check([TrustedStagingPath(file,NO,geteuid()) isEqual:root],@"Private input rejected");
    TrustedStagingPath([root stringByAppendingPathComponent:@"new.plist"],YES,geteuid());
    chmod(file.fileSystemRepresentation,0666);BOOL rejected=NO;
    @try{TrustedStagingPath(file,NO,geteuid());}@catch(NSException *e){rejected=YES;}Check(rejected,@"Writable input accepted");
    chmod(file.fileSystemRepresentation,0600);
    NSString *alias=[root stringByAppendingPathComponent:@"alias"];
    Check(symlink(file.fileSystemRepresentation,alias.fileSystemRepresentation)==0,@"Cannot create symlink");
    rejected=NO;@try{TrustedStagingPath(alias,NO,geteuid());}@catch(NSException *e){rejected=YES;}Check(rejected,@"Symlink accepted");
    chmod(root.fileSystemRepresentation,0755);
    rejected=NO;@try{TrustedStagingPath(file,NO,geteuid());}@catch(NSException *e){rejected=YES;}Check(rejected,@"Nonprivate directory accepted");
    chmod(root.fileSystemRepresentation,0700);
    printf("PASS private staging, missing destination, writable input, symlink and directory privacy; retained %s\n",name);
    return 0;
} @catch(NSException *e){fprintf(stderr,"%s\n",e.reason.UTF8String);return 1;}}}
