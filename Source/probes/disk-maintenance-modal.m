#import <Cocoa/Cocoa.h>
#import "VIMDiskMaintenance.h"
#include <unistd.h>

int main(int argc,const char *argv[]) {@autoreleasepool {
    NSCAssert(argc==2,@"Pass qemu-img");
    [NSApplication sharedApplication];
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *package=[fm.temporaryDirectory URLByAppendingPathComponent:[@"vim-modal-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    for(NSString *name in @[@"Disks",@"State"])[fm createDirectoryAtURL:[package URLByAppendingPathComponent:name] withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *identifier=NSUUID.UUID.UUIDString;
    [@{@"Identifier":identifier,@"Disk":@"Disks/System.qcow2"} writeToURL:[package URLByAppendingPathComponent:@"Machine.plist"] atomically:YES];
    NSTask *create=[NSTask new];NSURL *tool=[NSURL fileURLWithPath:@(argv[1])];create.executableURL=tool;
    create.arguments=@[@"create",@"-f",@"qcow2",@"-o",@"preallocation=full",[package URLByAppendingPathComponent:@"Disks/System.qcow2"].path,@"16M"];
    create.standardOutput=NSFileHandle.fileHandleWithNullDevice;
    NSCAssert([create launchAndReturnError:nil],@"Create fixture");[create waitUntilExit];NSCAssert(create.terminationStatus==0,@"Create fixture disk");
    NSWindow *parent=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,320,160) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    NSWindow *modal=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,320,120) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    NSAlert *confirm=[NSAlert new];confirm.messageText=@"Disk maintenance fixture";[confirm addButtonWithTitle:@"Continue"];
    [parent makeKeyAndOrderFront:nil];
    // Enter the modal loop from an actual sheet completion on the main queue.
    // Dispatching service callbacks to that same queue would deadlock here.
    dispatch_async(dispatch_get_main_queue(),^{
        [confirm beginSheetModalForWindow:parent completionHandler:^(NSModalResponse response){
            __block BOOL completed=NO,progressed=NO;
            NSError *error=nil;
            NSCAssert([VIMDiskMaintenance startForPackage:package identifier:identifier qemuImage:tool compressed:NO
                progress:^(NSString *phase,double fraction){NSCAssert(NSThread.isMainThread,@"Main progress");progressed=YES;}
                completion:^(NSDictionary *result,NSError *failure){
                    NSCAssert(result&&!failure&&![VIMDiskMaintenance hasActiveOperation],@"Completed safely inside modal: %@",failure);
                    completed=YES;[NSApp stopModal];
                } error:&error],@"Start: %@",error);
            [NSApp runModalForWindow:modal];
            NSCAssert(completed&&progressed,@"Modal callbacks completed");
            [fm removeItemAtURL:package error:nil];
            puts("PASS: native sheet-to-modal progress and completion without main-queue deadlock");fflush(stdout);exit(0);
        }];
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,100*NSEC_PER_MSEC),dispatch_get_main_queue(),^{[parent endSheet:confirm.window returnCode:NSAlertFirstButtonReturn];});
    });
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{sleep(20);fprintf(stderr,"FAIL: modal maintenance timed out\n");[fm removeItemAtURL:package error:nil];exit(3);});
    [NSApp run];
}return 0;}
