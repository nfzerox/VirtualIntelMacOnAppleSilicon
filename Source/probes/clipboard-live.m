// Run against the visible signed app's existing diagnostic channel. This never
// prints or persists either real clipboard, and restores them without replacing
// a genuine user copy made while the qualification was in progress.
#import <AppKit/AppKit.h>
#include <unistd.h>
static NSString *python,*script,*directory,*guestTool,*account;
static NSInteger ownedHostChange=-1;
static NSDictionary *Request(NSArray *arguments) {
    NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:python];
    task.arguments=[@[script,directory] arrayByAddingObjectsFromArray:arguments];
    NSPipe *pipe=NSPipe.pipe;task.standardOutput=pipe;task.standardError=NSFileHandle.fileHandleWithNullDevice;
    if(![task launchAndReturnError:nil])@throw @"Could not run the app diagnostic adapter";
    NSTimeInterval until=NSProcessInfo.processInfo.systemUptime+35;
    while(task.running&&NSProcessInfo.processInfo.systemUptime<until)usleep(10000);
    if(task.running){[task terminate];@throw @"Diagnostic request exceeded its bound; do not repeat a pending write";}
    NSData *data=[pipe.fileHandleForReading readDataToEndOfFile];
    id reply=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    if(![reply isKindOfClass:NSDictionary.class]||reply[@"Error"])@throw @"App diagnostic operation failed";
    return reply;
}
static NSDictionary *Guest(NSString *operation,NSString *text) {
    // Preserve the exact synthetic UTF-8 bytes independently of Cocoa's
    // command-argument/filesystem encoding; this is not the clipboard wire.
    if([operation isEqual:@"write"]||[operation isEqual:@"check"]) {
        operation=[operation stringByAppendingString:@"64"];
        text=[[text dataUsingEncoding:NSUTF8StringEncoding] base64EncodedStringWithOptions:0];
    }
    NSDictionary *result=Request(@[@"/bin/launchctl",@"asuser",account,@"/usr/bin/sudo",@"-n",@"-u",[@"#" stringByAppendingString:account],guestTool,operation,text]);
    if(![result[@"ExitCode"] isEqual:@0])@throw @"Guest pasteboard adapter failed";return result;
}
static NSArray *CopyItems(void) {
    NSMutableArray *items=[NSMutableArray new];NSUInteger total=0;
    for(NSPasteboardItem *source in NSPasteboard.generalPasteboard.pasteboardItems) {
        NSPasteboardItem *item=[NSPasteboardItem new];
        for(NSString *type in source.types){NSData *data=[source dataForType:type];total+=data.length;if(total>16*1024*1024)@throw @"Clipboard is too large for a non-destructive test";if(data)[item setData:data forType:type];}
        [items addObject:item];
    }return items;
}
static void Put(NSString *text){
    NSPasteboard *board=NSPasteboard.generalPasteboard;[board clearContents];
    BOOL written=[board setString:text forType:NSPasteboardTypeString];ownedHostChange=board.changeCount;
    if(!written)@throw @"Host pasteboard rejected the synthetic copy";
}
static void AwaitConnection(void) {
    NSTimeInterval until=NSProcessInfo.processInfo.systemUptime+30;
    do {if([Request(@[@"--clipboard"])[@"Connected"] boolValue])return;usleep(200000);}while(NSProcessInfo.processInfo.systemUptime<until);
    @throw @"Clipboard did not authenticate";
}
static void HostToGuest(NSString *text) {
    NSTimeInterval started=NSProcessInfo.processInfo.systemUptime;Put(text);
    do {if([Guest(@"check",text)[@"Stdout"] hasPrefix:@"true"]){printf("Host → guest passed (%.2f s upper bound including guest probe)\n",NSProcessInfo.processInfo.systemUptime-started);return;}usleep(100000);}while(NSProcessInfo.processInfo.systemUptime-started<8);
    NSDictionary *state=Request(@[@"--clipboard"]);
    printf("Copy timeout: connected=%d allowed=%d app-active=%d window-key=%d sleeping=%d source-retained=%d\n",
        [state[@"Connected"] boolValue],[state[@"ExchangeAllowed"] boolValue],
        [state[@"ApplicationActive"] boolValue],[state[@"WindowKey"] boolValue],
        [state[@"DisplaySleeping"] boolValue],[[NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString] isEqual:text]);
    if([state[@"Transport"] isKindOfClass:NSDictionary.class])printf("Transport counters: %s\n",[state[@"Transport"] description].UTF8String);
    @throw @"Host to guest copy did not arrive";
}
static void GuestToHost(NSString *text) {
    if(![Request(@[@"--clipboard"])[@"ExchangeAllowed"] boolValue])
        @throw @"Guest window lost foreground eligibility before the copy";
    NSTimeInterval started=NSProcessInfo.processInfo.systemUptime;Guest(@"write",text);
    do {if([[NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString] isEqual:text]){printf("Guest → host passed (%.2f s including guest command)\n",NSProcessInfo.processInfo.systemUptime-started);return;}usleep(20000);}while(NSProcessInfo.processInfo.systemUptime-started<8);
    NSDictionary *state=Request(@[@"--clipboard"]);
    printf("Copy timeout: connected=%d allowed=%d app-active=%d window-key=%d sleeping=%d source-retained=%d\n",
        [state[@"Connected"] boolValue],[state[@"ExchangeAllowed"] boolValue],
        [state[@"ApplicationActive"] boolValue],[state[@"WindowKey"] boolValue],
        [state[@"DisplaySleeping"] boolValue],[Guest(@"check",text)[@"Stdout"] hasPrefix:@"true"]);
    if([state[@"Transport"] isKindOfClass:NSDictionary.class])printf("Transport counters: %s\n",[state[@"Transport"] description].UTF8String);
    @throw @"Guest to host copy did not arrive";
}
static void SelectGuest(NSArray<NSString *> *guest) {
    directory=guest[0];guestTool=guest[1];account=guest[2];
}
static void FocusGuest(void) {
    Request(@[@"--menu-state",@"guest"]);AwaitConnection();
    if(![Request(@[@"--clipboard"])[@"ExchangeAllowed"] boolValue])
        @throw @"The intended VM window is not eligible for clipboard exchange";
}
static void RequireHost(NSString *text) {
    if(![[NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString] isEqual:text])
        @throw @"Background guest changed the host clipboard (or a user copied during the test)";
}
static int QualifyTwoGuests(NSArray<NSArray<NSString *> *> *guests) {
    if([guests[0][0] isEqual:guests[1][0]])return 64;
    NSString *prefix=[@"VIMClipboardTest-" stringByAppendingString:NSUUID.UUID.UUIDString];
    NSString *(^value)(NSString *)=^NSString *(NSString *suffix){return [prefix stringByAppendingString:suffix];};
    NSArray *backup=nil;BOOL saved[2]={NO,NO},enabled[2]={NO,NO},success=NO;
    @try {
        for(NSUInteger i=0;i<2;i++) {
            SelectGuest(guests[i]);enabled[i]=[Request(@[@"--clipboard"])[@"Enabled"] boolValue];
            if(!enabled[i])@throw @"Deliberately enable sharing in both VMs before qualification";
            FocusGuest();Guest(@"save",prefix);saved[i]=YES;
        }
        backup=CopyItems();
        for(NSUInteger front=0;front<2;front++) {
            NSUInteger back=1-front;SelectGuest(guests[front]);FocusGuest();
            SelectGuest(guests[back]);
            if([Request(@[@"--clipboard"])[@"ExchangeAllowed"] boolValue])
                @throw @"Background VM is incorrectly eligible for clipboard exchange";
            SelectGuest(guests[front]);
            NSString *host=value(front?@"-host-B":@"-host-A");HostToGuest(host);
            SelectGuest(guests[back]);
            if([Guest(@"check",host)[@"Stdout"] hasPrefix:@"true"])
                @throw @"Host text reached the background VM";
            Guest(@"write",value(front?@"-background-A":@"-background-B"));usleep(600000);
            RequireHost(host);SelectGuest(guests[front]);
            if(![Guest(@"check",host)[@"Stdout"] hasPrefix:@"true"])
                @throw @"Background text reached the foreground guest";
            NSString *text=value(front?@"-guest-B":@"-guest-A");GuestToHost(text);
            SelectGuest(guests[back]);
            if([Guest(@"check",text)[@"Stdout"] hasPrefix:@"true"])
                @throw @"Foreground guest text was reflected into the background VM";
            printf("Foreground %c: bidirectional exchange and background isolation passed\n",front?'B':'A');
        }
        // B stays frontmost: stopping/reprovisioning A must not affect B's
        // authenticated private channel or import A's old clipboard.
        SelectGuest(guests[0]);Request(@[@"--clipboard",@"off"]);
        SelectGuest(guests[1]);GuestToHost(value(@"-B-with-A-off"));
        SelectGuest(guests[0]);Request(@[@"--clipboard",@"on"]);AwaitConnection();
        if([Request(@[@"--clipboard"])[@"ExchangeAllowed"] boolValue])
            @throw @"Reconnected background VM became eligible";
        usleep(400000);RequireHost(value(@"-B-with-A-off"));
        SelectGuest(guests[1]);NSDictionary *state=Request(@[@"--clipboard"]);
        if(![state[@"Connected"] boolValue])@throw @"Reprovisioning A disconnected B";
        if(![state[@"ExchangeAllowed"] boolValue]) {
            puts("Host focus changed during reprovisioning; explicitly refocusing B before copy");FocusGuest();
        }
        GuestToHost(value(@"-B-after-A-reconnect"));
        success=YES;puts("Independent off/on and reconnect passed; no clipboard contents logged");
    }@catch(id error) {
        fprintf(stderr,"FAIL: %s\n",[error isKindOfClass:NSString.class]?[error UTF8String]:"Qualification exception");
    }@finally {
        BOOL stopped=YES,cleanupOK=YES;
        for(NSUInteger i=0;i<2;i++)if(saved[i])@try {
            SelectGuest(guests[i]);
            if([Request(@[@"--clipboard",@"off"])[@"Enabled"] boolValue])stopped=NO;
        }@catch(id ignored){stopped=NO;}
        if(stopped) {
            for(NSUInteger i=0;i<2;i++)if(saved[i])@try {
                SelectGuest(guests[i]);Guest(@"restore",prefix);
            }@catch(id ignored){cleanupOK=NO;}
            if(backup&&(NSPasteboard.generalPasteboard.changeCount==ownedHostChange||[[NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString] hasPrefix:prefix])) {
                [NSPasteboard.generalPasteboard clearContents];
                if(backup.count&&![NSPasteboard.generalPasteboard writeObjects:backup])cleanupOK=NO;
            }
        }else {cleanupOK=NO;fprintf(stderr,"Cleanup needs attention; retained guest backups, no real contents logged\n");}
        for(NSUInteger i=0;i<2;i++)if(saved[i]&&enabled[i])@try {
            SelectGuest(guests[i]);Request(@[@"--clipboard",@"on"]);
        }@catch(id ignored){cleanupOK=NO;}
        if(cleanupOK)puts("Cleanup completed; newer user copies were not replaced");
        else {success=NO;fprintf(stderr,"Clipboard cleanup requires attention\n");}
    }
    return success?0:1;
}
int main(int argc,const char **argv){@autoreleasepool {
    if(argc==9) {
        python=@(argv[1]);script=@(argv[2]);setvbuf(stdout,NULL,_IOLBF,0);
        return QualifyTwoGuests(@[@[@(argv[3]),@(argv[4]),@(argv[5])],@[@(argv[6]),@(argv[7]),@(argv[8])]]);
    }
    if(argc!=6){fprintf(stderr,"clipboard-live PYTHON APP_COMMAND_SCRIPT DIAGNOSTIC_DIRECTORY GUEST_PROBE_PATH CONSOLE_UID [SECOND_DIRECTORY SECOND_GUEST_PROBE SECOND_UID]\n");return 64;}
    python=@(argv[1]);script=@(argv[2]);directory=@(argv[3]);guestTool=@(argv[4]);account=@(argv[5]);
    setvbuf(stdout,NULL,_IOLBF,0);
    NSString *prefix=[@"VIMClipboardTest-" stringByAppendingString:NSUUID.UUID.UUIDString];
    NSArray *backup=nil;BOOL guestSaved=NO,originalEnabled=YES,success=NO,changed=NO;
    @try {
        originalEnabled=[Request(@[@"--clipboard"])[@"Enabled"] boolValue];
        if(!originalEnabled)@throw @"Enable sharing deliberately before qualification";
        Request(@[@"--menu-state",@"guest"]);AwaitConnection();
        if(![Request(@[@"--clipboard"])[@"ExchangeAllowed"] boolValue])@throw @"Bring the visible guest window frontmost before qualification";
        backup=CopyItems();
        Guest(@"save",prefix);guestSaved=YES;
        changed=YES;HostToGuest([prefix stringByAppendingString:@"-host"]);GuestToHost([prefix stringByAppendingString:@"-guest"]);
        HostToGuest([prefix stringByAppendingString:@"-café-日本語\nsecond line"]);
        GuestToHost([prefix stringByAppendingString:@"-naïve-中文\nnext line"]);
        Request(@[@"--clipboard",@"off"]);usleep(300000);
        Put([prefix stringByAppendingString:@"-disabled-host"]);usleep(500000);
        if([Guest(@"check",[prefix stringByAppendingString:@"-disabled-host"])[@"Stdout"] hasPrefix:@"true"])@throw @"Sharing off still exported host text";
        Guest(@"write",[prefix stringByAppendingString:@"-disabled-guest"]);usleep(500000);
        if(![[NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString] isEqual:[prefix stringByAppendingString:@"-disabled-host"]])@throw @"Sharing off still imported guest text";
        puts("Off stops both directions");
        Request(@[@"--clipboard",@"on"]);AwaitConnection();usleep(300000);
        if(![[NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString] isEqual:[prefix stringByAppendingString:@"-disabled-host"]]||
           ![Guest(@"check",[prefix stringByAppendingString:@"-disabled-guest"])[@"Stdout"] hasPrefix:@"true"])@throw @"Reconnect exported old clipboard contents";
        HostToGuest([prefix stringByAppendingString:@"-reconnect-host"]);GuestToHost([prefix stringByAppendingString:@"-reconnect-guest"]);
        success=YES;puts("Reconnect and no-old-content checks passed");
    }@catch(id ignored){fprintf(stderr,"FAIL: %s\n",[ignored isKindOfClass:NSString.class]?[ignored UTF8String]:"Qualification exception");}
    @finally {
        @try {
            if(changed)Request(@[@"--clipboard",@"off"]);
            if(guestSaved)Guest(@"restore",prefix);
            if(backup&&(NSPasteboard.generalPasteboard.changeCount==ownedHostChange||[[NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString] hasPrefix:prefix])) {
                [NSPasteboard.generalPasteboard clearContents];
                if(backup.count&&![NSPasteboard.generalPasteboard writeObjects:backup])@throw @"Host clipboard restoration failed";
            }
            if(changed&&originalEnabled)Request(@[@"--clipboard",@"on"]);
        }@catch(id ignored){fprintf(stderr,"Cleanup requires attention; no real clipboard data was logged\n");success=NO;}
    }
    return success?0:1;
}}
