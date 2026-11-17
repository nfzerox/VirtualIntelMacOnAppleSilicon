#import "VIMLocalization.h"

// The guest progress executable is copied without a resource bundle. Send
// only presentation labels once; never translate its control/protocol values.
static inline NSDictionary<NSString *,NSString *> *VIMGuestSetupLabels(void) {
    return @{
        @"Virtual Intel Mac Setup":VIMLocalized(@"guestsetup.virtual-intel-mac-setup", @"Virtual Intel Mac Setup"),
        @"Finishing Setup":VIMLocalized(@"guestsetup.finishing-setup", @"Finishing Setup"),
        @"Preparing your virtual Mac…":VIMLocalized(@"guestsetup.preparing-your-virtual-mac", @"Preparing your virtual Mac…"),
        @"Setup may take up to 30 minutes. Keep this Mac running.":VIMLocalized(@"guestsetup.setup-may-take-up-to-30-minutes-keep-this", @"Setup may take up to 30 minutes. Keep this Mac running."),
        @"Restart Now":VIMLocalized(@"session.restart-now", @"Restart Now"),
        @"Postpone":VIMLocalized(@"guestsetup.postpone", @"Postpone"),
        @"Restart to Finish Setup":VIMLocalized(@"guestsetup.restart-to-finish-setup", @"Restart to Finish Setup"),
        @"Setup Needs Attention":VIMLocalized(@"guestsetup.setup-needs-attention", @"Setup Needs Attention"),
        @"Setup Is Ready":VIMLocalized(@"guestsetup.setup-is-ready", @"Setup Is Ready"),
        @"Close":VIMLocalized(@"guestsetup.close", @"Close"),
        @"Your virtual Mac will restart in %ld:%02ld. Save any open work.":VIMLocalized(@"guestsetup.your-virtual-mac-will-restart-in-ld-02ld-save", @"Your virtual Mac will restart in %ld:%02ld. Save any open work."),
        @"You can close this window.":VIMLocalized(@"guestsetup.you-can-close-this-window", @"You can close this window."),
        @"Waiting for setup status…":VIMLocalized(@"guestsetup.waiting-for-setup-status", @"Waiting for setup status…"),
        @"Couldn’t Send Your Choice":VIMLocalized(@"guestsetup.couldn-t-send-your-choice", @"Couldn’t Send Your Choice"),
        @"Try again, or use Restart in the Virtual Intel Mac app.":VIMLocalized(@"guestsetup.try-again-or-use-restart-in-the-virtual-intel", @"Try again, or use Restart in the Virtual Intel Mac app."),
        @"Setup Is Still in Progress":VIMLocalized(@"guestsetup.setup-is-still-in-progress", @"Setup Is Still in Progress"),
        @"Keep this window and your virtual Mac open until setup finishes. Interrupting installation could leave macOS unable to start.":VIMLocalized(@"guestsetup.keep-this-window-and-your-virtual-mac-open-until", @"Keep this window and your virtual Mac open until setup finishes. Interrupting installation could leave macOS unable to start."),
        @"Continue Setup":VIMLocalized(@"library.continue-setup", @"Continue Setup"),
        @"Installing graphics and guest support…":VIMLocalized(@"guestsetup.installing-graphics-and-guest-support", @"Installing graphics and guest support…"),
        @"Updating macOS boot caches. This may take several minutes…":VIMLocalized(@"guestsetup.updating-macos-boot-caches-this-may-take-several-minutes", @"Updating macOS boot caches. This may take several minutes…"),
        @"Finishing installation…":VIMLocalized(@"guestsetup.finishing-installation", @"Finishing installation…"),
        @"Restoring the original system files. Keep this Mac running…":VIMLocalized(@"guestsetup.restoring-the-original-system-files-keep-this-mac-running", @"Restoring the original system files. Keep this Mac running…"),
        @"Setup couldn’t finish. The original system files were restored.":VIMLocalized(@"session.setup-couldn-t-finish-the-original-system-files-were-restored", @"Setup couldn’t finish. The original system files were restored."),
    };
}
