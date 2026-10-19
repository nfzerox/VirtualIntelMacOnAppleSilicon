// Link against the app's production objects except main.o. No VM is started,
// no user defaults are changed, and no real library is scanned.
#import <Cocoa/Cocoa.h>
#import "VIMLibraryViewController.h"
@interface VIMLibraryViewController (LibraryRowProbe)
- (NSView *)tableView:(NSTableView *)table viewForTableColumn:(NSTableColumn *)column row:(NSInteger)row;
@end
int main(int argc,const char **argv){@autoreleasepool {
    [NSApplication sharedApplication];
    NSCAssert(argc==2,@"Supply the bundled Tiger wallpaper source");
    NSImage *fallback=[[NSImage alloc] initWithContentsOfFile:@(argv[1])];
    NSCAssert(fallback&&[fallback setName:@"tigerWallpaper"],@"Load the real fallback artwork for this unbundled probe");
    Class unavailable=NSClassFromString(@"VIMUnavailableMac");
    NSCAssert(unavailable,@"Link the actual library implementation");
    VIMLibraryViewController *controller=[VIMLibraryViewController new];
    NSTableView *table=[NSTableView new];table.rowHeight=126;
    [controller setValue:table forKey:@"table"];
    for(NSString *guest in @[@"",@"unsupported",@"mojave",@"catalina",@"bigsur"]) {
        id machine=[unavailable new];
        [machine setValue:@"Unavailable Mac" forKey:@"savedName"];
        [machine setValue:guest forKey:@"savedGuest"];
        [machine setValue:[NSURL fileURLWithPath:@"/unavailable/Test.vintelmac"] forKey:@"missingURL"];
        [machine setValue:@"Mac Unavailable" forKey:@"reason"];
        [controller setValue:@[machine] forKey:@"visibleMachines"];
        for(NSNumber *compact in @[@NO,@YES]) {
            [controller setValue:compact forKey:@"compact"];
            NSTableCellView *row=(id)[controller tableView:table viewForTableColumn:[NSTableColumn new] row:0];
            NSCAssert(row.imageView&&row.imageView.image,@"Missing artwork must have a placeholder, not crash");
            NSCAssert(row.imageView.image==fallback,@"Use the Tiger wallpaper when OS artwork is unavailable");
            NSCAssert(!row.imageView.contentTintColor,@"Fallback wallpaper must not have symbol tint styling");
            NSCAssert([row.textField.stringValue isEqual:@"Unavailable Mac"],@"Keep the recoverable library entry");
        }
    }
    puts("PASS: production compact/expanded recovery rows tolerate absent/unknown OS artwork");
    return 0;
}}
