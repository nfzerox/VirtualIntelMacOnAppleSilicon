// Real Aqua pasteboard qualification adapter. Only synthetic test values leave
// the guest. Preserve prior contents on a private named pasteboard, not a file.
#import <AppKit/AppKit.h>
#include <sys/stat.h>
#include <unistd.h>
static NSArray *CopyItems(NSPasteboard *board) {
    NSMutableArray *copies=[NSMutableArray new];NSUInteger total=0;
    for(NSPasteboardItem *item in board.pasteboardItems) {
        NSPasteboardItem *copy=[NSPasteboardItem new];
        for(NSString *type in item.types) {
            NSData *data=[item dataForType:type];total+=data.length;
            if(total>16*1024*1024)return nil;
            if(data)[copy setData:data forType:type];
        }[copies addObject:copy];
    }return copies;
}
int main(int argc,const char **argv){@autoreleasepool {
    struct stat console;
    if(argc!=3||getuid()==0||stat("/dev/console",&console)||console.st_uid!=getuid())return 77;
    NSString *operation=@(argv[1]),*text=@(argv[2]);
    if([operation isEqual:@"write64"]||[operation isEqual:@"check64"]) {
        NSData *bytes=[[NSData alloc] initWithBase64EncodedString:text options:0];
        text=bytes?[[NSString alloc] initWithData:bytes encoding:NSUTF8StringEncoding]:nil;
        operation=[operation isEqual:@"write64"]?@"write":@"check";
    }
    if(![text hasPrefix:@"VIMClipboardTest-"]||text.length>160)return 65;
    NSPasteboard *general=NSPasteboard.generalPasteboard;
    if([operation isEqual:@"save"]) {
        NSArray *items=CopyItems(general);if(!items)return 75;
        NSPasteboard *backup=[NSPasteboard pasteboardWithName:text];[backup clearContents];
        return items.count&&![backup writeObjects:items]?74:0;
    }
    if([operation isEqual:@"restore"]) {
        NSPasteboard *backup=[NSPasteboard pasteboardWithName:text];
        // Do not overwrite a real user copy made during the test.
        if([[general stringForType:NSPasteboardTypeString] hasPrefix:text]) {
            NSArray *items=CopyItems(backup);if(!items)return 75;
            [general clearContents];if(items.count&&![general writeObjects:items])return 74;
        }
        [backup clearContents];[backup releaseGlobally];return 0;
    }
    if([operation isEqual:@"write"]){[general clearContents];return [general setString:text forType:NSPasteboardTypeString]?0:74;}
    if([operation isEqual:@"check"]){puts([[general stringForType:NSPasteboardTypeString] isEqual:text]?"true":"false");return 0;}
    return 64;
}}
