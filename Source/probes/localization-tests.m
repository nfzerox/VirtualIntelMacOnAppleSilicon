#import <Cocoa/Cocoa.h>
#import "VIMLocalization.h"

static void Check(BOOL condition, NSString *message) {
    if (!condition) { fprintf(stderr, "%s\n", message.UTF8String); exit(1); }
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        BOOL chinese = [@(argv[argc - 1]) isEqual:@"zh-Hans"];
        Check([VIMLocalized(@"common.cancel", @"Cancel") isEqual:chinese ? @"取消" : @"Cancel"], @"Wrong application language or English fallback");
        Check([VIMLocalized(@"missing.translation", @"English fallback") isEqual:@"English fallback"], @"Missing keys must retain their fallback");
        NSString *name = @"My Mac / 测试";
        NSString *title = [NSString stringWithFormat:VIMLocalized(@"library.remove.named", @"Remove “%@”"), name];
        Check([title isEqual:chinese ? @"移除“My Mac / 测试”" : @"Remove “My Mac / 测试”"], @"Formatting changed the user's machine name");
        Check([VIMLocalizedLabel(@"Date Created") isEqual:chinese ? @"创建日期" : @"Date Created"], @"Sort label was not localized");
        Check([VIMLocalizedLabel(@"Hardware") isEqual:chinese ? @"硬件" : @"Hardware"], @"Settings category was not localized");
        Check([VIMLocalizedLabel(name) isEqual:name], @"Unknown identifiers or user names must remain unchanged");
        Check([VIMMinutesRemaining(1, NO) isEqual:chinese ? @"还需 1 分钟" : @"1 minute remaining"], @"Singular minute is incorrect");
        Check([VIMMinutesRemaining(12, YES) isEqual:chinese ? @"大约还需 12 分钟" : @"About 12 minutes remaining"], @"Approximate duration is incorrect");
        Check([VIMHoursRemaining(2, 1) isEqual:chinese ? @"还需 2 小时 1 分钟" : @"2 hours 1 minute remaining"], @"Mixed hour/minute duration is incorrect");
        Check([VIMHoursRemaining(1, 0) isEqual:chinese ? @"还需 1 小时" : @"1 hour remaining"], @"Exact hour is incorrect");
        NSMenu *root = [NSMenu new];
        NSMenuItem *file = [[NSMenuItem alloc] initWithTitle:chinese ? @"文件" : @"File" action:nil keyEquivalent:@""];
        file.identifier = @"vim.file"; [root addItem:file];
        Check(VIMMenuItem(root, @"vim.file") == file, @"Menu lookup depends on the translated title");
        Check(VIMMenuItem(root, @"vim.missing") == nil, @"Missing menu returned an unrelated item");
        NSString *permission = [NSBundle.mainBundle localizedStringForKey:@"NSMicrophoneUsageDescription" value:@"" table:@"InfoPlist"];
        Check(chinese ? [permission containsString:@"麦克风"] : [permission containsString:@"microphone"], @"Permission description has the wrong language");
        printf("Bundle localization passed: %s\n", argv[argc - 1]);
    }
    return 0;
}
