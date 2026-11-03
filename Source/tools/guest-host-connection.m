// Open an interactive connection with our isolated modern client in the real
// console user's Terminal. No AppleEvents, password capture, global ssh config,
// user PATH change, daemon replacement or weakening of host sshd policy.
#import <Foundation/Foundation.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
int main(int argc,const char **argv){@autoreleasepool{
    if(argc!=4||strcmp(argv[1],"--ssh")||geteuid()==0)return 64;
    struct in_addr address;if(inet_pton(AF_INET,argv[2],&address)!=1)return 64;
    NSString *user=@(argv[3]);
    NSRegularExpression *pattern=[NSRegularExpression regularExpressionWithPattern:@"^[A-Za-z_][A-Za-z0-9_.-]{0,63}$" options:0 error:NULL];
    if([pattern numberOfMatchesInString:user options:0 range:NSMakeRange(0,[user length])]!=1)return 64;
    NSString *client=@"/Library/Application Support/Virtual Intel Mac/GuestTools/guest-ssh";
    if(![[NSFileManager defaultManager] isExecutableFileAtPath:client])return 66;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    NSArray *caches=NSSearchPathForDirectoriesInDomains(NSCachesDirectory,NSUserDomainMask,YES);
    if(![caches count])return 73;
    NSURL *directory=[NSURL fileURLWithPath:[caches objectAtIndex:0] isDirectory:YES];
#else
    NSURL *directory=[[[NSFileManager defaultManager] URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask] objectAtIndex:0];
#endif
    directory=[directory URLByAppendingPathComponent:@"com.virtualintelmac/Connections" isDirectory:YES];
    if(![[NSFileManager defaultManager] createDirectoryAtURL:directory withIntermediateDirectories:YES attributes:
         [NSDictionary dictionaryWithObject:[NSNumber numberWithInt:0700] forKey:NSFilePosixPermissions] error:NULL])return 73;
    NSString *command=[NSString stringWithFormat:@"'%@' -l '%@' '%s'",client,user,argv[2]];
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    // Tiger Terminal advertises .term and executable .command documents, not
    // Leopard's .terminal profile schema. Use its native command document
    // with only our absolute client and the validated username/IPv4 arguments.
    NSURL *profile=[directory URLByAppendingPathComponent:@"Host.command"];
    NSData *script=[[NSString stringWithFormat:@"#!/bin/sh\nexec %@\n",command] dataUsingEncoding:NSUTF8StringEncoding];
    if(![script writeToURL:profile atomically:YES]||chmod([[profile path] fileSystemRepresentation],0700))return 73;
#else
    NSURL *profile=[directory URLByAppendingPathComponent:@"Host.terminal"];
    NSDictionary *settings=[NSDictionary dictionaryWithObjectsAndKeys:@"Window Settings",@"type",@"This Mac",@"name",
        command,@"CommandString",[NSNumber numberWithBool:NO],@"RunCommandAsShell",[NSNumber numberWithDouble:2.08],@"ProfileCurrentVersion",nil];
    if(![settings writeToURL:profile atomically:YES])return 73;
#endif
    NSTask *open=[[[NSTask alloc] init] autorelease];[open setLaunchPath:@"/usr/bin/open"];
    [open setArguments:[NSArray arrayWithObjects:@"-b",@"com.apple.Terminal",[profile path],nil]];
    [open setStandardInput:[NSFileHandle fileHandleWithNullDevice]];
    @try{[open launch];[open waitUntilExit];return [open terminationStatus];}@catch(NSException *exception){return 1;}
}}
