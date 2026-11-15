#import "VIMRegionalSettingsView.h"
#import "VIMRegionalSettings.h"
#import "VIMLocalization.h"

@interface VIMRegionalSettingsView () <NSTokenFieldDelegate>
@property NSButton *experimental;
@property NSButton *followHost;
@property NSButton *allowFallback;
@property NSTokenField *language;
@property NSComboBox *region;
@property NSComboBox *zone;
@property NSArray<NSDictionary *> *languages;
@property NSArray<NSDictionary *> *regions;
@property NSArray<NSDictionary *> *zones;
@property NSDictionary *seed;
@end

@implementation VIMRegionalSettingsView
- (NSComboBox *)combo:(NSArray<NSDictionary *> *)choices label:(NSString *)label {
    NSComboBox *combo=[NSComboBox new];[combo addItemsWithObjectValues:[choices valueForKey:@"Title"]];
    combo.completes=YES;combo.numberOfVisibleItems=12;combo.accessibilityLabel=label;
    [combo.widthAnchor constraintEqualToConstant:260].active=YES;return combo;
}
- (instancetype)initWithRequest:(NSDictionary *)request {
    if((self=[super initWithFrame:NSZeroRect])){
        _enabled=YES;self.languages=VIMRegionalSettings.languageChoices;self.regions=VIMRegionalSettings.regionChoices;self.zones=VIMRegionalSettings.timeZoneChoices;
        self.experimental=[NSButton checkboxWithTitle:VIMLocalized(@"regional.experimental", @"Set language, region and time zone (Experimental)") target:self action:@selector(updateEnabled:)];
        NSTextField *experimentalDetail=[NSTextField wrappingLabelWithString:VIMLocalized(@"regional.experimental-explanation", @"This experimental feature may prevent initial setup from completing on some macOS versions. Leave it disabled to use the guest’s defaults.")];
        experimentalDetail.textColor=NSColor.secondaryLabelColor;experimentalDetail.font=[NSFont systemFontOfSize:11];
        self.followHost=[NSButton checkboxWithTitle:VIMLocalized(@"regional.follow-host", @"Use this Mac’s language, region and time zone") target:self action:@selector(updateEnabled:)];
        self.allowFallback=[NSButton checkboxWithTitle:VIMLocalized(@"regional.allow-fallback", @"Use supported alternatives when needed") target:nil action:nil];
        NSString *language=VIMLocalized(@"regional.language", @"Preferred languages:"),*region=VIMLocalized(@"regional.region", @"Region:"),*zone=VIMLocalized(@"regional.time-zone", @"Time zone:");
        self.language=[NSTokenField new];self.language.delegate=self;self.language.accessibilityLabel=language;
        self.language.tokenizingCharacterSet=[NSCharacterSet characterSetWithCharactersInString:@",，"];
        [self.language.widthAnchor constraintEqualToConstant:260].active=YES;
        self.region=[self combo:self.regions label:region];self.zone=[self combo:self.zones label:zone];
        NSGridView *grid=[NSGridView gridViewWithViews:@[@[[NSTextField labelWithString:language],self.language],@[[NSTextField labelWithString:region],self.region],@[[NSTextField labelWithString:zone],self.zone]]];
        grid.rowSpacing=12;grid.columnSpacing=12;[grid columnAtIndex:0].xPlacement=NSGridCellPlacementTrailing;
        NSTextField *detail=[NSTextField wrappingLabelWithString:VIMLocalized(@"regional.explanation", @"Applied once before the first desktop login. Add languages in preference order, using names or language IDs separated by commas. Support depends on the guest’s macOS version. Later changes inside the virtual Mac are preserved.")];
        detail.textColor=NSColor.secondaryLabelColor;detail.font=[NSFont systemFontOfSize:11];
        NSStackView *stack=[NSStackView stackViewWithViews:@[self.experimental,experimentalDetail,self.followHost,grid,self.allowFallback,detail]];
        stack.orientation=NSUserInterfaceLayoutOrientationVertical;stack.alignment=NSLayoutAttributeLeading;stack.spacing=12;
        stack.translatesAutoresizingMaskIntoConstraints=NO;[self addSubview:stack];
        [NSLayoutConstraint activateConstraints:@[[stack.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],[stack.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],[stack.topAnchor constraintEqualToAnchor:self.topAnchor],[stack.bottomAnchor constraintEqualToAnchor:self.bottomAnchor],[detail.widthAnchor constraintEqualToAnchor:stack.widthAnchor],[experimentalDetail.widthAnchor constraintEqualToAnchor:stack.widthAnchor]]];
        [self restoreRequest:request];
    }return self;
}
- (void)select:(NSComboBox *)combo choices:(NSArray *)choices value:(NSString *)value {
    NSUInteger index=[choices indexOfObjectPassingTest:^BOOL(NSDictionary *entry,NSUInteger idx,BOOL *stop){return [entry[@"Value"] isEqual:value];}];
    if(index!=NSNotFound){[combo selectItemAtIndex:index];combo.stringValue=choices[index][@"Title"];}
    else {if(combo.indexOfSelectedItem>=0)[combo deselectItemAtIndex:combo.indexOfSelectedItem];combo.stringValue=value?:@"";}
}
- (NSString *)value:(NSComboBox *)combo choices:(NSArray *)choices {
    // Editable combo text can outlive its selected index. Resolve only an exact
    // displayed choice; typed identifiers are validated by the shared policy.
    for(NSDictionary *entry in choices)if([entry[@"Title"] isEqual:combo.stringValue])return entry[@"Value"];
    return [combo.stringValue stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
}
- (void)restoreRequest:(NSDictionary *)request {
    self.seed=request?:@{};self.experimental.state=request?NSControlStateValueOn:NSControlStateValueOff;
    NSDictionary *values=request?:[VIMRegionalSettings hostSnapshotWithError:NULL]?:@{};
    self.followHost.state=[values[@"Source"] isEqual:@"custom"]?NSControlStateValueOff:NSControlStateValueOn;
    self.allowFallback.state=[values[@"UnsupportedPolicy"] isEqual:@"strict"]?NSControlStateValueOff:NSControlStateValueOn;
    self.language.objectValue=values[@"PreferredLanguages"]?:@[];
    [self select:self.region choices:self.regions value:values[@"RegionCode"]];
    [self select:self.zone choices:self.zones value:values[@"TimeZoneIdentifier"]];[self updateEnabled:nil];
}
- (void)setEnabled:(BOOL)enabled {_enabled=enabled;[self updateEnabled:nil];}
- (void)setFrozen:(BOOL)frozen {_frozen=frozen;[self updateEnabled:nil];}
- (void)updateEnabled:(id)sender {
    BOOL editable=self.enabled&&!self.frozen;
    self.experimental.enabled=editable;
    BOOL active=editable&&self.experimental.state==NSControlStateValueOn;
    self.followHost.enabled=active;
    BOOL custom=active&&self.followHost.state==NSControlStateValueOff;
    self.language.enabled=self.region.enabled=self.zone.enabled=custom;self.allowFallback.enabled=custom;
}
- (NSDictionary *)requestWithError:(NSError **)error {
    if(error)*error=nil;
    if(!self.enabled||self.experimental.state!=NSControlStateValueOn)return nil;
    if(self.frozen)return self.seed.count?self.seed:nil;
    if(self.followHost.state==NSControlStateValueOn)return [VIMRegionalSettings hostSnapshotWithError:error];
    // Finish a typed token before reading the ordered preference list.
    [self.window makeFirstResponder:nil];
    NSArray *languages=self.language.objectValue?:@[];
    NSMutableDictionary *request=[@{@"FormatVersion":@1,@"Source":@"custom",@"PreferredLanguages":languages,
        @"UnsupportedPolicy":self.allowFallback.state==NSControlStateValueOn?@"fallback":@"strict"} mutableCopy];
    NSString *region=[self value:self.region choices:self.regions],*zone=[self value:self.zone choices:self.zones];
    if(region.length)request[@"RegionCode"]=region;if(zone.length)request[@"TimeZoneIdentifier"]=zone;
    return [VIMRegionalSettings normalizeRequest:request error:error];
}
- (NSString *)tokenField:(NSTokenField *)field displayStringForRepresentedObject:(id)object {
    for(NSDictionary *entry in self.languages)if([entry[@"Value"] isEqual:object])return entry[@"Title"];
    return [object isKindOfClass:NSString.class]?object:@"";
}
- (id)tokenField:(NSTokenField *)field representedObjectForEditingString:(NSString *)string {
    NSString *value=[string stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    for(NSDictionary *entry in self.languages)if([entry[@"Title"] isEqual:value])return entry[@"Value"];
    return value;
}
- (NSString *)tokenField:(NSTokenField *)field editingStringForRepresentedObject:(id)object {return object;}
- (NSArray *)tokenField:(NSTokenField *)field completionsForSubstring:(NSString *)substring indexOfToken:(NSInteger)index indexOfSelectedItem:(NSInteger *)selected {
    NSMutableArray *matches=[NSMutableArray new];
    for(NSDictionary *entry in self.languages)if([entry[@"Title"] rangeOfString:substring options:NSCaseInsensitiveSearch].location!=NSNotFound||[entry[@"Value"] hasPrefix:substring]) [matches addObject:entry[@"Title"]];
    return matches;
}
- (NSDictionary *)draftState {
    [self.window makeFirstResponder:nil];
    return @{@"Experimental":@(self.experimental.state),@"Host":@(self.followHost.state),@"Fallback":@(self.allowFallback.state),@"Languages":self.language.objectValue?:@[],@"Region":self.region.stringValue,@"Zone":self.zone.stringValue};
}
- (void)restoreDraftState:(NSDictionary *)state {
    if(self.frozen)return;
    self.experimental.state=[state[@"Experimental"] integerValue];
    self.followHost.state=[state[@"Host"] integerValue];self.allowFallback.state=[state[@"Fallback"] integerValue];
    self.language.objectValue=state[@"Languages"]?:@[];self.region.stringValue=state[@"Region"]?:@"";self.zone.stringValue=state[@"Zone"]?:@"";[self updateEnabled:nil];
}
@end
