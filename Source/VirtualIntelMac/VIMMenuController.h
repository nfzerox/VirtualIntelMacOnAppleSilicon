#import <Cocoa/Cocoa.h>
@class VIMLibraryViewController;
@interface VIMMenuController : NSObject <NSMenuDelegate,NSMenuItemValidation>
- (instancetype)initWithLibrary:(VIMLibraryViewController *)library;
@end
