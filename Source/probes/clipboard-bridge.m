#import "VIMClipboardBridge.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
@interface VIMClipboardBridge (Fixture)
- (void)tick;
@end
static void Pump(void){NSDate *until=[NSDate dateWithTimeIntervalSinceNow:0.15];while(until.timeIntervalSinceNow>0)[NSRunLoop.mainRunLoop runMode:NSDefaultRunLoopMode beforeDate:until];}
static void Send(int fd,NSDictionary *object){NSMutableData *data=[[NSJSONSerialization dataWithJSONObject:object options:0 error:NULL] mutableCopy];[data appendBytes:"\n" length:1];NSCAssert(send(fd,data.bytes,data.length,0)==data.length,@"send");Pump();}
int main(void){@autoreleasepool {
    VIMClipboardBridge *bridge=[VIMClipboardBridge new];NSError *error=nil;
    NSString *directory=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    [NSFileManager.defaultManager createDirectoryAtPath:directory withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:nil];
    NSString *path=[directory stringByAppendingPathComponent:@"clipboard"];
    int server=socket(AF_UNIX,SOCK_STREAM,0);struct sockaddr_un address={.sun_family=AF_UNIX};strlcpy(address.sun_path,path.fileSystemRepresentation,sizeof(address.sun_path));
    NSCAssert(server>=0&&!bind(server,(struct sockaddr *)&address,sizeof(address))&&!listen(server,1),@"QEMU endpoint fixture");
    NSCAssert([bridge startWithSocketPath:path error:&error],@"start: %@",error);NSCAssert(bridge.token.length==64,@"private endpoint");
    __block NSUInteger lost=0;bridge.connectionLost=^{lost++;};
    int fd=accept(server,NULL,NULL);Send(fd,@{@"type":@"hello",@"protocol":@2,@"token":@"wrong"});
    char buffer[256];NSCAssert(recv(fd,buffer,sizeof(buffer),MSG_DONTWAIT)==0,@"wrong token disconnect");close(fd);
    NSCAssert([bridge startWithSocketPath:path error:&error],@"reconnect: %@",error);
    fd=accept(server,NULL,NULL);Send(fd,@{@"type":@"hello",@"protocol":@2,@"token":bridge.token});
    ssize_t size=recv(fd,buffer,sizeof(buffer),MSG_DONTWAIT);NSCAssert(size>0,@"ready response");
    NSDictionary *ready=[NSJSONSerialization JSONObjectWithData:[NSData dataWithBytes:buffer length:size] options:0 error:NULL];
    NSCAssert([ready[@"type"] isEqual:@"ready"],@"authenticated handshake");
    // Advance metadata only, not a 30-second sleep. The production timer
    // emits a ping even with default-deny foreground policy. A valid pong
    // retains authentication and must not trigger recovery.
    NSUInteger before=lost;
    [bridge setValue:@(NSProcessInfo.processInfo.systemUptime-6) forKey:@"lastHeartbeat"];[bridge tick];Pump();
    size=recv(fd,buffer,sizeof(buffer),MSG_DONTWAIT);NSCAssert(size>0,@"idle liveness ping");
    NSDictionary *ping=[NSJSONSerialization JSONObjectWithData:[NSData dataWithBytes:buffer length:size] options:0 error:NULL];
    NSCAssert([ping[@"type"] isEqual:@"ping"]&&[ping[@"sequence"] isEqual:@1],@"bounded content-free heartbeat");
    Send(fd,@{@"type":@"pong",@"sequence":ping[@"sequence"]});
    NSCAssert(bridge.connected&&lost==before,@"pong retains connection");
    // Default policy denies pasteboard access, even for an authenticated guest.
    Send(fd,@{@"type":@"text",@"sequence":@1,@"text":@"not applied"});
    Send(fd,@{@"type":@"text",@"sequence":@1,@"text":@"duplicate"});
    NSCAssert(recv(fd,buffer,sizeof(buffer),MSG_DONTWAIT)==0,@"duplicate sequence disconnect");close(fd);
    NSCAssert(lost==before+1,@"one unexpected-disconnect notification");
    NSCAssert([bridge startWithSocketPath:path error:&error],@"heartbeat reconnect");
    fd=accept(server,NULL,NULL);Send(fd,@{@"type":@"hello",@"protocol":@2,@"token":bridge.token});recv(fd,buffer,sizeof(buffer),MSG_DONTWAIT);
    [bridge setValue:@1 forKey:@"awaitingHeartbeat"];[bridge setValue:@(NSProcessInfo.processInfo.systemUptime-1) forKey:@"heartbeatDeadline"];[bridge tick];Pump();
    NSCAssert(!bridge.connected&&lost==before+2,@"dead helper detected despite open QEMU socket");close(fd);
    NSCAssert([bridge startWithSocketPath:path error:&error],@"explicit stop reconnect");
    fd=accept(server,NULL,NULL);Send(fd,@{@"type":@"hello",@"protocol":@2,@"token":bridge.token});recv(fd,buffer,sizeof(buffer),MSG_DONTWAIT);
    NSUInteger stopped=lost;[bridge stop];Pump();NSCAssert(!bridge.connected&&!bridge.token&&lost==stopped,@"explicit stop never reprovisions");
    size=recv(fd,buffer,sizeof(buffer),MSG_DONTWAIT);NSCAssert(size>0,@"explicit stop reaches guest before socket closes");
    NSDictionary *stop=[NSJSONSerialization JSONObjectWithData:[NSData dataWithBytes:buffer length:size] options:0 error:NULL];
    NSCAssert([stop[@"type"] isEqual:@"stop"],@"content-free quit control frame");
    NSCAssert(recv(fd,buffer,sizeof(buffer),MSG_DONTWAIT)==0,@"explicit stop closes transport");close(fd);
    bridge.displayPowerOnly=YES;
    __block NSUInteger powerChanges=0;__block BOOL asleep=NO;
    bridge.displayPowerChanged=^(BOOL value){powerChanges++;asleep=value;};
    NSCAssert([bridge startWithSocketPath:path error:&error],@"separate power channel");
    fd=accept(server,NULL,NULL);Send(fd,@{@"type":@"hello",@"protocol":@2,@"token":bridge.token});recv(fd,buffer,sizeof(buffer),MSG_DONTWAIT);
    Send(fd,@{@"type":@"display-power",@"sleeping":@YES});NSCAssert(asleep&&powerChanges==1,@"power sleep forwarded");
    Send(fd,@{@"type":@"display-power",@"sleeping":@NO});NSCAssert(!asleep&&powerChanges==2,@"power wake forwarded");
    Send(fd,@{@"type":@"display-power",@"sleeping":@1});NSCAssert(!bridge.connected,@"numeric impostor is not a Boolean");close(fd);
    [bridge stop];
    close(server);unlink(path.fileSystemRepresentation);rmdir(directory.fileSystemRepresentation);
    puts("PASS: private serial authentication/reconnect, default deny, replay rejection, idle heartbeat/pong, dead-helper detection, explicit quit/teardown; no clipboard access enabled");
}return 0;}
