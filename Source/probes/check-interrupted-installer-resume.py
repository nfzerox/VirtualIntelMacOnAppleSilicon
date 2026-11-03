#!/usr/bin/env python3
"""Finite host fixture for the actual native interrupted-installer guard.

Only fixture stat owners are modeled as guest root: no host privilege or VM
is required. The production function is extracted verbatim and compiled.
"""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "tools/opengl-install-bootstrap.m").read_text()
start = source.index("static BOOL CanResumeInstaller(")
end = source.index("\nstatic int Recovery(", start)
guard = source[start:end]
prefix = r'''
#import <Foundation/Foundation.h>
#include <sys/stat.h>
#include <unistd.h>
static BOOL rootOwner=YES;
static NSString *processText=@"/sbin/launchd\n/usr/libexec/LCA\n";
static NSString *CurrentGuest(void){return @"snowleopard";}
static NSData *Run(NSString *path,NSArray *arguments){
    (void)path;(void)arguments;return [processText dataUsingEncoding:NSUTF8StringEncoding];
}
static int GuestStat(const char *path,struct stat *st){
    int result=lstat(path,st);if(!result)st->st_uid=rootOwner?0:501;return result;
}
#define lstat GuestStat
'''
suffix = r'''
#undef lstat
static void Put(id object,NSString *path,mode_t mode){
    assert([object writeToFile:path atomically:YES]);assert(!chmod(path.fileSystemRepresentation,mode));
}
int main(void){@autoreleasepool{
    char temporary[]="/tmp/VIMResumeFixture.XXXXXX";assert(mkdtemp(temporary));
    NSString *root=[NSString stringWithUTF8String:temporary];
    NSFileManager *fm=[NSFileManager defaultManager];
    NSString *state=[root stringByAppendingString:@"/State"],*users=[root stringByAppendingString:@"/private/var/db/dslocal/nodes/Default/users"];
    assert([fm createDirectoryAtPath:state withIntermediateDirectories:YES attributes:nil error:NULL]);
    assert(!chmod(state.fileSystemRepresentation,0700));
    assert([fm createDirectoryAtPath:users withIntermediateDirectories:YES attributes:nil error:NULL]);
    NSString *machine=@"ADAF1B31-297A-446B-BE6F-2231EF3FAAF1",*volume=@"47D388A2-AE00-3FF2-A34F-BCEDF8F9D519";
    NSDictionary *policy=@{@"Enabled":@YES,@"Guest":@"snowleopard",@"MachineIdentifier":machine,@"VolumeUUID":volume};
    NSString *owner=[state stringByAppendingPathComponent:@"Ownership.plist"];
    Put(policy,owner,0600);Put(@{@"uid":@[@"0"]},[users stringByAppendingPathComponent:@"root.plist"],0600);
    assert(CanResumeInstaller(root,state,policy,machine,volume));
    assert(!CanResumeInstaller(root,state,policy,@"foreign-machine",volume));
    assert(!CanResumeInstaller(root,state,policy,machine,@"foreign-volume"));
    rootOwner=NO;assert(!CanResumeInstaller(root,state,policy,machine,volume));rootOwner=YES;
    assert(!chmod(owner.fileSystemRepresentation,0644));assert(!CanResumeInstaller(root,state,policy,machine,volume));assert(!chmod(owner.fileSystemRepresentation,0600));
    NSString *account=[users stringByAppendingPathComponent:@"person.plist"];
    Put(@{@"uid":@[@"501"]},account,0600);assert(!CanResumeInstaller(root,state,policy,machine,volume));assert([fm removeItemAtPath:account error:NULL]);
    for(NSString *file in @[@"Account.plist",@"Graphics.plist"]){
        NSString *path=[state stringByAppendingPathComponent:file];Put(@{},path,0600);
        assert(!CanResumeInstaller(root,state,policy,machine,volume));assert([fm removeItemAtPath:path error:NULL]);
    }
    NSString *done=[root stringByAppendingString:@"/private/var/db/.AppleSetupDone"];
    Put(@{},done,0600);assert(!CanResumeInstaller(root,state,policy,machine,volume));assert([fm removeItemAtPath:done error:NULL]);
    processText=@"/System/Installation/CDIS/Mac OS X Installer.app/Contents/MacOS/Mac OS X Installer\n";
    assert(!CanResumeInstaller(root,state,policy,machine,volume));
    processText=@"/usr/sbin/installer\n";assert(!CanResumeInstaller(root,state,policy,machine,volume));
    processText=@"";assert(!CanResumeInstaller(root,state,policy,machine,volume));
    processText=@"/usr/libexec/LCA\n";assert(CanResumeInstaller(root,state,policy,machine,volume));
    assert([fm removeItemAtPath:root error:NULL]);puts("Interrupted installer ownership/account/worker fixtures passed.");
}return 0;}
'''
with tempfile.TemporaryDirectory(prefix="VIMResumeGuard-") as directory:
    directory = pathlib.Path(directory)
    fixture = directory / "fixture.m"
    fixture.write_text(prefix + guard + suffix)
    executable = directory / "fixture"
    subprocess.run(["xcrun", "clang", "-Wall", "-Wextra", "-Werror",
                    "-fno-objc-arc", str(fixture), "-framework", "Foundation",
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
