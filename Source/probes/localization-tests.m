#import <Cocoa/Cocoa.h>
#import "VIMLocalization.h"
#import "VIMInstallerProgressLocalization.h"
#import "VIMGuestSetupLabels.h"
#import "VIMCreationProgressCopy.h"
#include "../tools/guest-progress-labels.h"

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
        Check([VIMLocalizedLabel(@"macOS Release") isEqual:chinese ? @"macOS版本" : @"macOS Release"], @"Release sorting was not localized");
        Check([VIMLocalized(@"settings.best-for-retina-display", @"Best for Retina display") isEqual:chinese?@"最适合Retina显示屏":@"Best for Retina display"], @"Retina must retain its product name");
        Check([VIMLocalized(@"ui.retina-resolution", @"Retina Resolution") isEqual:chinese?@"分辨率":@"Retina Resolution"], @"Wrong outer resolution-menu label");
        Check([VIMLocalizedLabel(@"Hardware") isEqual:chinese ? @"硬件" : @"Hardware"], @"Settings category was not localized");
        Check([VIMLocalizedLabel(name) isEqual:name], @"Unknown identifiers or user names must remain unchanged");
        Check([VIMLocalized(@"settings.cpu.backend.hybrid", @"Hybrid Hypervisor") isEqual:chinese?@"融合式硬件加速虚拟化":@"Hybrid Hypervisor"], @"Wrong Hybrid Hypervisor terminology");
        Check([VIMLocalized(@"settings.cpu.backend.tcg", @"TCG Emulation") isEqual:chinese?@"软件TCG模拟器":@"TCG Emulation"], @"Wrong TCG terminology");
        Check([VIMLocalized(@"settings.opengl.passthrough", @"Passthrough OpenGL") isEqual:chinese?@"透传OpenGL":@"Passthrough OpenGL"], @"Wrong Passthrough OpenGL terminology");
        Check([VIMLocalized(@"app.name", @"Virtual Intel Mac") isEqual:chinese?@"虚拟Intel Mac":@"Virtual Intel Mac"], @"Wrong localized application name");
        for(NSString *key in @[@"CFBundleName",@"CFBundleDisplayName"])
            Check([[NSBundle.mainBundle objectForInfoDictionaryKey:key] isEqual:chinese?@"虚拟Intel Mac":@"Virtual Intel Mac"], @"Finder/application bundle name was not localized");
        Check([VIMLocalized(@"updatechecker.view-release", @"View Releases") isEqual:chinese?@"查看发布版本":@"View Releases"], @"Release browsing is unavailable when already up to date");
        NSString *credit=VIMLocalized(@"vimmachinesettings.hybrid-hypervisor-is-invented-by-sam-henri-gold-the", @"Hybrid Hypervisor is developed by Sam Henri Gold.");
        Check([credit containsString:@"Sam Henri Gold"], @"The developer's name must retain its correct spelling");
        Check([VIMMinutesRemaining(1, NO) isEqual:chinese ? @"还需1分钟" : @"1 minute remaining"], @"Singular minute is incorrect");
        Check([VIMMinutesRemaining(12, YES) isEqual:chinese ? @"大约还需12分钟" : @"About 12 minutes remaining"], @"Approximate duration is incorrect");
        Check([VIMHoursRemaining(2, 1) isEqual:chinese ? @"还需2小时1分钟" : @"2 hours 1 minute remaining"], @"Mixed hour/minute duration is incorrect");
        Check([VIMHoursRemaining(1, 0) isEqual:chinese ? @"还需1小时" : @"1 hour remaining"], @"Exact hour is incorrect");
        NSString *retry = [NSString localizedStringWithFormat:VIMLocalized(@"download.recovery-countdown", @"Reconnecting in %ld s…"), (long)7];
        Check([retry isEqual:chinese ? @"7秒后重新连接…" : @"Reconnecting in 7 s…"], @"Download recovery countdown has the wrong language or format");
        NSMenu *root = [NSMenu new];
        NSMenuItem *file = [[NSMenuItem alloc] initWithTitle:chinese ? @"文件" : @"File" action:nil keyEquivalent:@""];
        file.identifier = @"vim.file"; [root addItem:file];
        Check(VIMMenuItem(root, @"vim.file") == file, @"Menu lookup depends on the translated title");
        Check(VIMMenuItem(root, @"vim.missing") == nil, @"Missing menu returned an unrelated item");
        NSString *permission = [NSBundle.mainBundle localizedStringForKey:@"NSMicrophoneUsageDescription" value:@"" table:@"InfoPlist"];
        Check(chinese ? [permission containsString:@"麦克风"] : [permission containsString:@"microphone"], @"Permission description has the wrong language");
        for(NSString *guest in VIMSupportedGuests()) {
            NSString *release=VIMReleaseNameForGuest(guest);
            NSString *title=[NSString stringWithFormat:@"Installing %@ system files…",release];
            NSString *localized=VIMLocalizedInstallerProgress(title,guest);
            Check([localized isEqual:chinese?[NSString stringWithFormat:@"正在安装%@系统文件…",release]:title],
                @"Native worker progress lost its translation or OS release name");
        }
        Check([VIMLocalizedInstallerProgress(@"Publishing and checking the startup snapshot…",@"bigsur")
            isEqual:chinese?@"正在发布并检查启动快照…":@"Publishing and checking the startup snapshot…"],
            @"Snapshot progress was not localized");
        Check([VIMLocalizedInstallerProgress(@"New worker phase 123",@"future") isEqual:@"New worker phase 123"],
            @"Unknown worker copy must remain usable");
        NSString *credits=[NSString stringWithFormat:VIMLocalized(@"appdelegate.gpu-acceleration-and-app-ui-by-nfzerox-hybrid-hypervisor", @"GPU Acceleration and App UI by nfzerox\nHybrid Hypervisor by samhenrigold\n%@"),VIMLocalized(@"app.project-on-github", @"Project on GitHub")];
        Check([credits rangeOfString:VIMLocalized(@"app.project-on-github", @"Project on GitHub")].location!=NSNotFound,
            @"The current About text lost its localized project link");
        NSString *developers=[NSString stringWithFormat:VIMLocalized(@"appdelegate.developers", @"Developers: %@, %@, %@, %@, %@, and %@"),
            @"Damon Lu",@"Ely Wright",@"Nihaal Sharma",@"UrkiMimi",@"Zach Bennoui",@"いえなが"];
        Check([developers isEqual:chinese?@"开发者：Damon Lu、Ely Wright、Nihaal Sharma、UrkiMimi、Zach Bennoui和いえなが":
            @"Developers: Damon Lu, Ely Wright, Nihaal Sharma, UrkiMimi, Zach Bennoui, and いえなが"],@"Developer names/order or localized punctuation changed");
        NSString *automaticCopy=VIMLocalized(@"creation.your-virtual-mac-will-install-and-set-itself-up-automatically", @"Your virtual Mac will install and set itself up automatically.");
        for(NSNumber *download in @[@NO,@YES])for(NSNumber *host in @[@NO,@YES])for(NSNumber *automatic in @[@NO,@YES]) {
            NSString *copy=VIMCreationProgressIntroduction(download.boolValue,host.boolValue,automatic.boolValue,
                @"DOWNLOAD",@"HOST",@"MANUAL",automaticCopy);
            if(download.boolValue)Check([copy isEqual:@"DOWNLOAD"],@"Download-only copy must not ask the user to run the installer");
            else if(host.boolValue)Check([copy isEqual:@"HOST"],@"Host installation must not show guest-installer instructions");
            else Check(([copy rangeOfString:@"MANUAL"].location!=NSNotFound)==!automatic.boolValue,
                @"Only manual guest installation may ask the user to interact with the installer");
        }
        NSDictionary *labels=VIMGuestSetupLabels();
        Check([VIMProgressDisplayLabel(labels,@"Finishing Setup") isEqual:chinese?@"正在完成设置":@"Finishing Setup"],
            @"Standalone guest UI labels did not follow the app language");
        Check([VIMProgressDisplayLabel(labels,@"Setup couldn’t finish. The original system files were restored.")
            isEqual:chinese?@"设置未能完成。原系统文件已恢复。":@"Setup couldn’t finish. The original system files were restored."],
            @"Native installer state was not translated at the guest display boundary");
        Check([VIMProgressDisplayLabel(@{@"Close":@5},@"Close") isEqual:@"Close"]&&
              [VIMProgressDisplayLabel(nil,@"Unknown future title") isEqual:@"Unknown future title"],
            @"Absent/invalid optional guest labels must retain their English fallback");
        printf("Bundle localization passed: %s\n", argv[argc - 1]);
    }
    return 0;
}
