#import <Foundation/Foundation.h>

// Development command files are scoped to one emulator lifetime. In
// particular, a write queued during setup must not run after its reboot.
static inline BOOL VIMDiagnosticRequestMatchesSession(NSDictionary *request,NSString *session) {
    id requested=request[@"Session"],identifier=request[@"RequestID"];
    return session.length&&[requested isKindOfClass:NSString.class]&&[requested isEqual:session]&&
        [identifier isKindOfClass:NSString.class]&&[identifier length]>0;
}
