#import "../VirtualIntelMac/VIMMojaveOpenGLSelection.h"
#include <assert.h>
int main(void) {@autoreleasepool {
    NSDictionary *report=@{@"Guest":@"mojave",@"LiveRoot":@YES,@"KernelMatchesSystem":@YES,
        @"InstalledSystemCandidate":@YES,@"DesktopRunning":@YES,@"ProductBuildVersion":@"18Z9999",@"VolumeUUID":@"fixture-volume"};
    assert(VIMMojaveOpenGLSelection(NO,report,nil,nil,nil)==VIMMojaveOpenGLUnchanged);
    assert(VIMMojaveOpenGLSelection(YES,report,nil,nil,nil)==VIMMojaveOpenGLPrepare);
    NSMutableDictionary *other=[report mutableCopy];other[@"DesktopRunning"]=@NO;
    assert(VIMMojaveOpenGLSelection(YES,other,nil,nil,nil)==VIMMojaveOpenGLWait);
    other[@"DesktopRunning"]=@YES;other[@"Guest"]=@"catalina";
    assert(VIMMojaveOpenGLSelection(YES,other,nil,nil,nil)==VIMMojaveOpenGLWait);
    NSMutableDictionary *installation=[@{@"Phase":@"graphics-verified",@"GuestResult":report,
        @"TransactionID":@"fixture-transaction",@"Preparation":@{@"PassthroughOpenGL":@YES}} mutableCopy];
    assert(VIMMojaveOpenGLSelection(YES,report,installation,nil,nil)==VIMMojaveOpenGLPrepare);
    assert(VIMMojaveOpenGLSelection(NO,report,installation,nil,nil)==VIMMojaveOpenGLPrepare);
    for(NSString *phase in @[@"dispatching",@"awaiting-reboot",@"outcome-unknown"]){
        installation[@"Phase"]=phase;
        assert(VIMMojaveOpenGLSelection(NO,report,installation,nil,nil)==VIMMojaveOpenGLWait);
    }
    installation[@"Phase"]=@"rolled-back";
    assert(VIMMojaveOpenGLSelection(NO,report,installation,nil,nil)==VIMMojaveOpenGLPrepare);
    NSMutableDictionary *updated=[report mutableCopy];updated[@"ProductBuildVersion"]=@"18Z10000";
    assert(VIMMojaveOpenGLSelection(YES,updated,installation,nil,nil)==VIMMojaveOpenGLWait);
    installation[@"Phase"]=@"graphics-verified";installation[@"Preparation"]=@{};
    NSDictionary *preparation=@{@"InstallationPlan":@{@"TransactionID":@"fixture-transaction"},
        @"Preparation":@{@"PassthroughOpenGL":@YES}};
    assert(VIMMojaveOpenGLSelection(YES,report,installation,preparation,nil)==VIMMojaveOpenGLPrepare);
    assert(VIMMojaveOpenGLSelection(NO,report,installation,preparation,nil)==VIMMojaveOpenGLPrepare);
    NSDictionary *unrelated=@{@"InstallationPlan":@{@"TransactionID":@"other-attempt"},
        @"Preparation":@{@"PassthroughOpenGL":@YES}};
    assert(VIMMojaveOpenGLSelection(NO,report,installation,unrelated,nil)==VIMMojaveOpenGLUnchanged);
    NSMutableDictionary *assembly=[report mutableCopy];assembly[@"PassthroughOpenGLEnabled"]=@YES;
    assert(VIMMojaveOpenGLSelection(YES,report,nil,nil,assembly)==VIMMojaveOpenGLPrepare);
    assert(VIMMojaveOpenGLSelection(NO,report,nil,nil,assembly)==VIMMojaveOpenGLPrepare);
    assert(VIMMojaveOpenGLSelection(NO,updated,nil,nil,assembly)==VIMMojaveOpenGLWait);
    installation[@"Preparation"]=@{@"OpenGLBootSelection":@YES};
    assembly[@"OpenGLBootSelection"]=@YES;
    for(NSNumber *desired in @[@NO,@YES]) {
        assert(VIMMojaveOpenGLSelection(desired.boolValue,report,installation,nil,nil)==VIMMojaveOpenGLUnchanged);
        assert(VIMMojaveOpenGLSelection(desired.boolValue,report,nil,nil,assembly)==VIMMojaveOpenGLUnchanged);
    }
    installation[@"Preparation"]=@{};
    NSDictionary *selectable=@{@"InstallationPlan":@{@"TransactionID":@"fixture-transaction"},
        @"Preparation":@{@"OpenGLBootSelection":@YES}};
    assert(VIMMojaveOpenGLSelection(NO,report,installation,selectable,nil)==VIMMojaveOpenGLUnchanged);
    assert(VIMMojaveOpenGLSelection(YES,report,installation,selectable,nil)==VIMMojaveOpenGLUnchanged);
    puts("PASS Mojave selection: one-time migration, both zero-setup mode changes, pending work, rollback and OS updates");return 0;
}}
