// Host-only finite qualification of the production Tiger media handoff.
// Copies only the measured startup/package scripts into a private fixture;
// never edits the mounted original DVD or any installed guest/system.
#import "../VirtualIntelMac/VIMTigerInstallerSupport.h"
int main(int argc,const char **argv){@autoreleasepool{
    if(argc!=3)return 64;
    NSFileManager *fm=NSFileManager.defaultManager;NSError *error=nil;
    NSURL *support=[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[2]] isDirectory:YES];
    if(!strcmp(argv[1],"--payload-only")){
        NSDictionary *payload=VIMTigerControlPayload(support,&error);
        if(!payload||!VIMTigerInstallerGraphics(support,&error)||!VIMTigerInstallerAudio(support,&error)){
            fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;
        }
        printf("PASS production Tiger installer payload: %lu native control/SMB files, graphics and audio\n",(unsigned long)payload.count);return 0;
    }
    NSURL *DVD=[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]] isDirectory:YES];
    NSString *postflight=@"System/Installation/Packages/OSInstall.mpkg/Contents/Resources/postflight";
    NSData *original=VIMTigerControlFile([DVD URLByAppendingPathComponent:postflight]);
    NSData *rc=VIMTigerControlFile([DVD URLByAppendingPathComponent:@"etc/rc.cdrom"]);
    if(!original||!rc||!VIMTigerControlPayload(support,&error)){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
    NSURL *fixture=[[NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES]
        URLByAppendingPathComponent:[@"vim-tiger-installer-" stringByAppendingString:NSUUID.UUID.UUIDString] isDirectory:YES];
    if(![fm createDirectoryAtURL:[fixture URLByAppendingPathComponent:postflight].URLByDeletingLastPathComponent
        withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:&error]||
       ![fm createDirectoryAtURL:[fixture URLByAppendingPathComponent:@"etc"] withIntermediateDirectories:NO attributes:nil error:&error])return 2;
    NSURL *script=[fixture URLByAppendingPathComponent:postflight];
    if(![original writeToURL:script options:0 error:&error]||
       ![fm setAttributes:@{NSFilePosixPermissions:@0555} ofItemAtPath:script.path error:&error]||
       ![rc writeToURL:[fixture URLByAppendingPathComponent:@"etc/rc.cdrom"] options:0 error:&error])return 3;
    NSDictionary *receipt=nil;
    if(!VIMTigerStageInstallerControls(fixture,support,&receipt,&error)||
       !VIMTigerVerifyInstallerControls(fixture,support,receipt,&error)) {
        fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 4;
    }
    if([[fm attributesOfItemAtPath:script.path error:nil][NSFilePosixPermissions] unsignedIntValue]!=0555||
       receipt[@"TigerControlFiles"]==nil||[receipt[@"TigerControlFiles"] count]!=[VIMTigerControlNames() count]||
       ![receipt[@"TigerControlBootstrapVersion"] isEqual:@8]||[receipt[@"TigerGraphicsFiles"] count]!=6||
       [receipt[@"TigerAudioFiles"] count]!=3||
       [[NSString stringWithContentsOfURL:script encoding:NSUTF8StringEncoding error:nil] rangeOfString:@"--finish-install \"$3\""].location==NSNotFound||
       ![[NSData dataWithContentsOfURL:[fixture URLByAppendingPathComponent:@"etc/rc.cdrom"]] isEqual:rc])return 5;
    // A second preparation must not overwrite an unknown/prior transaction.
    if(VIMTigerStageInstallerControls(fixture,support,NULL,NULL))return 6;
    NSMutableDictionary *wrong=[receipt mutableCopy];wrong[@"TigerControlFiles"]=@{};
    if(VIMTigerVerifyInstallerControls(fixture,support,wrong,NULL))return 7;
    NSURL *graphics=[fixture URLByAppendingPathComponent:@"System/Installation/Packages/Extras/VirtualIntelMac/Graphics/GLEngine"];
    NSData *validGraphics=[NSData dataWithContentsOfURL:graphics];NSMutableData *badGraphics=[validGraphics mutableCopy];
    ((unsigned char *)badGraphics.mutableBytes)[badGraphics.length-1]^=1;
    if(![badGraphics writeToURL:graphics options:0 error:&error]||VIMTigerVerifyInstallerControls(fixture,support,receipt,NULL)||
       ![validGraphics writeToURL:graphics options:0 error:&error]||!VIMTigerVerifyInstallerControls(fixture,support,receipt,NULL))return 12;
    NSURL *audio=[fixture URLByAppendingPathComponent:@"System/Installation/Packages/Extras/VirtualIntelMac/Audio/VoodooHDA.kext/Contents/MacOS/VoodooHDA"];
    NSData *validAudio=[NSData dataWithContentsOfURL:audio];NSMutableData *badAudio=[validAudio mutableCopy];
    ((unsigned char *)badAudio.mutableBytes)[badAudio.length-1]^=1;
    if(![badAudio writeToURL:audio options:0 error:&error]||VIMTigerVerifyInstallerControls(fixture,support,receipt,NULL)||
       ![validAudio writeToURL:audio options:0 error:&error]||!VIMTigerVerifyInstallerControls(fixture,support,receipt,NULL))return 13;
    NSURL *tool=[fixture URLByAppendingPathComponent:@"System/Installation/Packages/Extras/VirtualIntelMac/opengl-desktop-status"];
    NSMutableData *changed=[[NSData dataWithContentsOfURL:tool] mutableCopy];
    ((unsigned char *)changed.mutableBytes)[changed.length-1]^=1;
    if(![changed writeToURL:tool options:0 error:&error]||VIMTigerVerifyInstallerControls(fixture,support,receipt,NULL))return 8;
    if(VIMTigerNativePostflight([@"#!/bin/sh\nexit 0\n" dataUsingEncoding:NSUTF8StringEncoding],NULL))return 9;
    if(![VIMTigerControlFile([DVD URLByAppendingPathComponent:postflight]) isEqual:original]||
       ![VIMTigerControlFile([DVD URLByAppendingPathComponent:@"etc/rc.cdrom"]) isEqual:rc])return 10;
    if(![fm removeItemAtURL:fixture error:&error])return 11;
    puts("PASS native Tiger startup/postflight target argument, native controls and SMB client/server payload, six local graphics and three audio files, unchanged originals/mode, and incomplete/prior-payload rejection");
    return 0;
}}
