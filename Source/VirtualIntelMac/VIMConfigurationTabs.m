#import "VIMConfigurationTabs.h"

@interface VIMConfigurationTabs ()
@property NSTabView *tabs;
@property NSSegmentedControl *selection;
@end
@implementation VIMConfigurationTabs
- (instancetype)initWithTabs:(NSTabView *)tabs {
    if((self=[super initWithFrame:NSZeroRect])) {
        self.tabs=tabs;tabs.tabViewType=NSNoTabsNoBorder;tabs.drawsBackground=NO;
        self.clipsToBounds=NO;tabs.clipsToBounds=NO;
        self.selection=[NSSegmentedControl segmentedControlWithLabels:[tabs.tabViewItems valueForKey:@"label"]
            trackingMode:NSSegmentSwitchTrackingSelectOne target:self action:@selector(selectTab:)];
        NSFont *font=self.selection.font?:[NSFont systemFontOfSize:NSFont.systemFontSize];
        for(NSInteger segment=0;segment<self.selection.segmentCount;segment++)
            [self.selection setWidth:ceil([[self.selection labelForSegment:segment] sizeWithAttributes:@{NSFontAttributeName:font}].width)+28 forSegment:segment];
        self.selection.selectedSegment=[tabs.tabViewItems indexOfObject:tabs.selectedTabViewItem];
        NSBox *frame=[NSBox new];frame.identifier=@"ConfigurationTabFrame";
        frame.boxType=NSBoxCustom;frame.cornerRadius=4;
        frame.borderWidth=.5;frame.borderColor=NSColor.separatorColor;frame.fillColor=NSColor.clearColor;
        for(NSView *view in @[frame,tabs,self.selection]){view.translatesAutoresizingMaskIntoConstraints=NO;[self addSubview:view];}
        [NSLayoutConstraint activateConstraints:@[
            [self.selection.centerXAnchor constraintEqualToAnchor:self.centerXAnchor],
            [self.selection.topAnchor constraintEqualToAnchor:self.topAnchor],
            [frame.topAnchor constraintEqualToAnchor:self.selection.centerYAnchor],
            [frame.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
            [frame.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
            [frame.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],
            [tabs.topAnchor constraintEqualToAnchor:self.selection.bottomAnchor constant:8],
            [tabs.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:4],
            [tabs.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-4],
            [tabs.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-4]]];
    }return self;
}
- (void)selectTab:(NSSegmentedControl *)sender {
    [self.tabs selectTabViewItemAtIndex:sender.selectedSegment];
    sender.selectedSegment=[self.tabs.tabViewItems indexOfObject:self.tabs.selectedTabViewItem];
}
@end
