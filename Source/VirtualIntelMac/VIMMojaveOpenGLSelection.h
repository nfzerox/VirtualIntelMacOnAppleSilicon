#import <Foundation/Foundation.h>

typedef NS_ENUM(NSInteger,VIMMojaveOpenGLDecision) {
    VIMMojaveOpenGLWait,
    VIMMojaveOpenGLUnchanged,
    VIMMojaveOpenGLPrepare
};

// Cold, host-side selection only. Reuse the saved graphics transaction; never
// probe GL, read whole binaries or poll the GPU to decide whether to prepare.
static inline VIMMojaveOpenGLDecision VIMMojaveOpenGLSelection(BOOL desired,
    NSDictionary *report,NSDictionary *installation,NSDictionary *preparation,NSDictionary *assembly) {
    if(![report[@"Guest"] isEqual:@"mojave"]||![report[@"LiveRoot"] isEqual:@YES]||
       ![report[@"KernelMatchesSystem"] isEqual:@YES]||![report[@"InstalledSystemCandidate"] isEqual:@YES]||
       ![report[@"DesktopRunning"] isEqual:@YES])return VIMMojaveOpenGLWait;
    BOOL installed=NO,selectable=NO;
    NSDictionary *identity=nil;
    if(installation) {
        identity=installation[@"GuestResult"]?:installation[@"Preparation"][@"GuestReport"];
        if(identity&&(![identity[@"ProductBuildVersion"] isEqual:report[@"ProductBuildVersion"]]||
                      ![identity[@"VolumeUUID"] isEqual:report[@"VolumeUUID"]]))return VIMMojaveOpenGLWait;
        // A pending/unknown transaction must finish reconciliation first.
        // Explicit rollback is a safe point to prepare the selected renderer.
        if([installation[@"Phase"] isEqual:@"rolled-back"])return VIMMojaveOpenGLPrepare;
        if(![installation[@"Phase"] isEqual:@"graphics-verified"])return VIMMojaveOpenGLWait;
        id mode=installation[@"Preparation"][@"PassthroughOpenGL"];
        // Early optional installs kept the mode in the preparation's native
        // receipt. Tie it to this exact plan rather than another attempt.
        if(!mode&&[preparation[@"InstallationPlan"][@"TransactionID"] isEqual:installation[@"TransactionID"]])
            mode=preparation[@"Preparation"][@"PassthroughOpenGL"];
        installed=[mode isEqual:@YES];
        selectable=[installation[@"Preparation"][@"OpenGLBootSelection"] isEqual:@YES];
        if(!selectable&&[preparation[@"InstallationPlan"][@"TransactionID"] isEqual:installation[@"TransactionID"]])
            selectable=[preparation[@"Preparation"][@"OpenGLBootSelection"] isEqual:@YES];
    } else if(assembly) {
        identity=assembly;
        installed=[assembly[@"PassthroughOpenGLEnabled"] isEqual:@YES];
        selectable=[assembly[@"OpenGLBootSelection"] isEqual:@YES];
    }
    if(identity&&(![identity[@"ProductBuildVersion"] isEqual:report[@"ProductBuildVersion"]]||
                  ![identity[@"VolumeUUID"] isEqual:report[@"VolumeUUID"]]))return VIMMojaveOpenGLWait;
    // The capability is independent of the selected renderer. Imported Apple
    // installations can remain unchanged until the option is first requested;
    // older fixed-Passthrough installs must migrate even when switching off.
    return selectable||(!desired&&!installed)?VIMMojaveOpenGLUnchanged:VIMMojaveOpenGLPrepare;
}
