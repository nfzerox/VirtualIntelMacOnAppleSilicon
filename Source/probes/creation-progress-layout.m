#import <AppKit/AppKit.h>
#import "../VirtualIntelMac/VIMCreationProgressLayout.h"

int main(void) { @autoreleasepool {
    [NSApplication sharedApplication];
    NSProgressIndicator *transition=[NSProgressIndicator new];transition.style=NSProgressIndicatorStyleBar;
    transition.minValue=0;transition.maxValue=1;transition.indeterminate=YES;[transition startAnimation:nil];
    VIMSetDeterminateCreationProgress(transition,0);
    NSCAssert(!transition.indeterminate&&transition.doubleValue==0,@"Download starts determinate before body bytes");
    double fraction=VIMCreationDownloadFraction(3500000000,-1,5690000000);
    NSCAssert(fabs(fraction-3500000000.0/5690000000.0)<1e-12,@"Use the known size when the GET omits its length");
    VIMSetDeterminateCreationProgress(transition,.5*fraction);
    NSCAssert(!transition.indeterminate&&transition.doubleValue>0,@"Known download bytes advance the real bar");
    NSCAssert(VIMCreationDownloadFraction(10,100,200)==.1,@"A GET response supersedes an older size hint");
    NSCAssert(VIMCreationDownloadFraction(10,-1,0)==0,@"Unknown sizes do not invent completed bytes");
    NSCAssert(VIMCreationDownloadFraction(200,-1,100)==1,@"A display hint must not overflow the bar");
    puts("PASS determinate bar transition, unknown GET length fallback and authoritative response length");
    for(NSNumber *width in @[@400,@320]) {
        NSView *page=[[NSView alloc] initWithFrame:NSMakeRect(0,0,width.doubleValue,400)];
        NSTextField *heading=[NSTextField wrappingLabelWithString:@"Creating\nmacOS Tahoe"];
        heading.font=[NSFont systemFontOfSize:27];
        NSTextField *intro=[NSTextField wrappingLabelWithString:@"We’ll download macOS from Apple and prepare your virtual disk. Your virtual Mac will start when it’s ready."];
        intro.font=[NSFont systemFontOfSize:13];
        NSTextField *status=[NSTextField wrappingLabelWithString:@"Preparing your Mac…"];
        status.font=[NSFont systemFontOfSize:13];
        NSTextField *detail=[NSTextField wrappingLabelWithString:@"Estimating remaining time…"];
        detail.font=[NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular];
        VIMConstrainCreationProgressRow(status);VIMConstrainCreationProgressRow(detail);
        NSProgressIndicator *progress=[NSProgressIndicator new];progress.style=NSProgressIndicatorStyleBar;
        NSArray<NSView *> *views=@[heading,intro,status,progress,detail];
        NSStackView *stack=[NSStackView stackViewWithViews:views];
        stack.orientation=NSUserInterfaceLayoutOrientationVertical;stack.alignment=NSLayoutAttributeLeading;stack.spacing=16;
        [stack setCustomSpacing:28 afterView:intro];[stack setCustomSpacing:4 afterView:status];[stack setCustomSpacing:4 afterView:progress];
        stack.translatesAutoresizingMaskIntoConstraints=NO;[page addSubview:stack];
        for(NSView *view in views)[view.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active=YES;
        [NSLayoutConstraint activateConstraints:@[[stack.leadingAnchor constraintEqualToAnchor:page.leadingAnchor],
            [stack.trailingAnchor constraintEqualToAnchor:page.trailingAnchor],
            [stack.centerYAnchor constraintEqualToAnchor:page.centerYAnchor]]];
        [page layoutSubtreeIfNeeded];
        NSCAssert(NSHeight(stack.frame)>100&&NSHeight(status.frame)>10&&NSHeight(detail.frame)>10,
            @"The test must resolve real nonzero AppKit geometry");
        NSMutableArray<NSValue *> *frames=[NSMutableArray array];
        for(NSView *view in views)[frames addObject:[NSValue valueWithRect:view.frame]];
        NSArray *states=@[
            @[@"Downloading macOS…",@"0.00 GB of 18.39 GB (Calculating speed…) - Estimating time remaining…"],
            @[@"Downloading macOS…",@"3.60 GB of 18.39 GB (12.34 MB/s) - 20 minutes remaining"],
            @[@"Restoring the original System volume’s file compression…",@"3 hours 59 minutes remaining"],
            @[@"Compressing system files…",@"Elapsed time: 0:01"],
            @[@"Preparing startup metadata…",@"Elapsed time: 12:59:59"],
            @[@"Paused",@"Your download is saved. Resume to start preparation again."],
            @[@"Finishing your virtual Mac…",@"Less than a minute remaining"]];
        for(NSArray<NSString *> *state in states) {
            status.stringValue=state[0];detail.stringValue=state[1];[page layoutSubtreeIfNeeded];
            for(NSUInteger i=0;i<views.count;i++)
                NSCAssert(NSEqualRects(views[i].frame,frames[i].rectValue),@"Progress update moved row %lu at %@ points",(unsigned long)i,width);
            NSCAssert(status.maximumNumberOfLines==1&&detail.maximumNumberOfLines==1,@"Progress labels must stay single-line");
        }
    }
    puts("PASS creation progress rows and surrounding content remain fixed across download and preparation states");
    return 0;
} }
