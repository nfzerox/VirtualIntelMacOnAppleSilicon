// Own objects/run loop only. No installer identity, authorization, hooks,
// real panes, buttons, system files or account creation are exercised.
#import "../tools/opengl-installer-automation.m"
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
@interface VIMContinuationOutletControl : NSObject { NSButton *continueButton; }
- (void)continue:(id)sender;
- (void)autopilotNextButton;
@end
static BOOL wrongGetterCalled;
@implementation VIMContinuationOutletControl
- (id)init{if((self=[super init])){continueButton=[NSButton new];[continueButton setTarget:self];[continueButton setAction:@selector(continue:)];}return self;}
- (void)dealloc{[continueButton release];[super dealloc];}
- (void)continue:(id)sender{(void)sender;}
- (void)autopilotNextButton{wrongGetterCalled=YES;}
@end
#endif
int main(void){@autoreleasepool{
    if(geteuid()==0)return 77;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    NSObject *pane=[[[NSObject alloc] init] autorelease];
    SetPaneFlag(pane,&Active,[NSNumber numberWithBool:YES]);
    BOOL active=[PaneFlag(pane,&Active) boolValue];
    AdvanceIfReady(pane);
    BOOL deferred=[PaneFlag(pane,&Queued) boolValue];
    AdvanceIfReady(pane); // A pending transition must stay coalesced.
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:2];
    while([PaneFlag(pane,&Queued) boolValue]&&[deadline timeIntervalSinceNow]>0)
        [[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode beforeDate:deadline];
    BOOL delivered=![PaneFlag(pane,&Queued) boolValue];
    BOOL untouched=![PaneFlag(pane,&Performed) boolValue]&&!authorization;
    SetPaneFlag(pane,&Active,nil);
    BOOL cleared=!PaneFlag(pane,&Active);
    VIMContinuationOutletControl *controller=[[[VIMContinuationOutletControl alloc] init] autorelease];
    BOOL outlet=ContinueButton(controller)!=nil&&!wrongGetterCalled;
    printf("native-state=%d deferred=%d delivered=%d unauthorized-untouched=%d cleared=%d typed-outlet=%d\n",active,deferred,delivered,untouched,cleared,outlet);
    return active&&deferred&&delivered&&untouched&&cleared&&outlet?0:1;
#else
    return 77;
#endif
}}
