// Real Aqua pasteboard qualification adapter. Only synthetic test values leave
// the guest. Preserve prior contents on a private named pasteboard, not a file.
#import <AppKit/AppKit.h>
#include <sys/stat.h>
#include <unistd.h>
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
#define NSPasteboardTypeString NSStringPboardType
#endif
static void Clear(NSPasteboard *board) {
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    [board declareTypes:@[] owner:nil];
#else
    [board clearContents];
#endif
}
static NSData *DecodeBase64(NSString *text) {
    // Snow has no declared NSData base64 initializer. Keep this fixture's
    // UTF-8 argument transport independent of newer Foundation methods.
    NSData *input=[text dataUsingEncoding:NSASCIIStringEncoding allowLossyConversion:NO];
    if(!input||!input.length||input.length%4||input.length>2048)return nil;
    NSMutableData *output=[NSMutableData data];const unsigned char *p=input.bytes;
    const char *alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for(NSUInteger i=0;i<input.length;i+=4){
        unsigned value=0,padding=0;
        for(unsigned j=0;j<4;++j){
            if(p[i+j]=='='){if(j<2||i+4!=input.length)return nil;padding++;value<<=6;}
            else {const char *v=p[i+j]?strchr(alphabet,p[i+j]):NULL;if(!v||padding)return nil;value=(value<<6)|(unsigned)(v-alphabet);}
        }
        if((padding==2&&(value&0xffff))||(padding==1&&(value&0xff)))return nil;
        unsigned char bytes[]={(unsigned char)(value>>16),(unsigned char)(value>>8),(unsigned char)value};
        [output appendBytes:bytes length:3-padding];
    }return output;
}
static NSArray *CopyItems(NSPasteboard *board) {
    NSMutableArray *copies=[NSMutableArray new];NSUInteger total=0;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    // Leopard has one native pasteboard flavor set, not NSPasteboardItem.
    // Preserve every flavor without inventing later multi-item semantics.
    NSMutableDictionary *copy=[NSMutableDictionary new];
    for(NSString *type in board.types) {
        NSData *data=[board dataForType:type];total+=data.length;
        if(total>16*1024*1024)return nil;
        if(data)[copy setObject:data forKey:type];
    }
    [copies addObject:copy];
#else
    for(NSPasteboardItem *item in board.pasteboardItems) {
        NSPasteboardItem *copy=[NSPasteboardItem new];
        for(NSString *type in item.types) {
            NSData *data=[item dataForType:type];total+=data.length;
            if(total>16*1024*1024)return nil;
            if(data)[copy setData:data forType:type];
        }[copies addObject:copy];
    }return copies;
#endif
    return copies;
}
static BOOL WriteItems(NSPasteboard *board,NSArray *items) {
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    NSDictionary *copy=items.count?[items objectAtIndex:0]:@{};
    [board declareTypes:copy.allKeys owner:nil];
    for(NSString *type in copy)if(![board setData:[copy objectForKey:type] forType:type])return NO;
    return YES;
#else
    return !items.count||[board writeObjects:items];
#endif
}
static int Qualify(int argc,const char **argv){
    struct stat console;
    if(argc!=3||getuid()==0||stat("/dev/console",&console)||console.st_uid!=getuid())return 77;
    NSString *operation=@(argv[1]),*text=@(argv[2]);
    if([operation isEqual:@"write64"]||[operation isEqual:@"check64"]) {
        NSData *bytes=DecodeBase64(text);
        text=bytes?[[NSString alloc] initWithData:bytes encoding:NSUTF8StringEncoding]:nil;
        operation=[operation isEqual:@"write64"]?@"write":@"check";
    }
    if(![text hasPrefix:@"VIMClipboardTest-"]||text.length>160)return 65;
    if([operation isEqual:@"local"]){
        NSPasteboard *board=[NSPasteboard pasteboardWithUniqueName];
        [board declareTypes:@[NSPasteboardTypeString] owner:nil];
        BOOL written=[board setString:text forType:NSPasteboardTypeString];
        BOOL equal=[[board stringForType:NSPasteboardTypeString] isEqual:text];
        printf("native-bits=%lu pasteboard=%d written=%d matched=%d\n",(unsigned long)(8*sizeof(void *)),board!=nil,written,equal);
        Clear(board);[board releaseGlobally];return written&&equal?0:74;
    }
    NSPasteboard *general=NSPasteboard.generalPasteboard;
    if([operation isEqual:@"status"]){printf("pasteboard=%d flavors=%lu change=%ld\n",general!=nil,(unsigned long)general.types.count,(long)general.changeCount);return general?0:69;}
    if(!general)return 69;
    if([operation isEqual:@"save"]) {
        NSArray *items=CopyItems(general);if(!items)return 75;
        NSPasteboard *backup=[NSPasteboard pasteboardWithName:text];Clear(backup);
        return WriteItems(backup,items)?0:74;
    }
    if([operation isEqual:@"restore"]) {
        NSPasteboard *backup=[NSPasteboard pasteboardWithName:text];
        // Do not overwrite a real user copy made during the test.
        if([[general stringForType:NSPasteboardTypeString] hasPrefix:text]) {
            NSArray *items=CopyItems(backup);if(!items)return 75;
            Clear(general);if(!WriteItems(general,items))return 74;
        }
        Clear(backup);[backup releaseGlobally];return 0;
    }
    if([operation isEqual:@"write"]){Clear(general);[general addTypes:@[NSPasteboardTypeString] owner:nil];return [general setString:text forType:NSPasteboardTypeString]?0:74;}
    if([operation isEqual:@"check"]){puts([[general stringForType:NSPasteboardTypeString] isEqual:text]?"true":"false");return 0;}
    return 64;
}
int main(int argc,const char **argv){
    NSAutoreleasePool *pool=[NSAutoreleasePool new];int result=Qualify(argc,argv);[pool drain];return result;
}
