#import <Foundation/Foundation.h>

// Older saved jobs always created an account. Manual account setup is explicit:
// the same host disk/graphics recipe runs, but Apple's first-run UI stays intact.
NS_INLINE BOOL VIMHostInstallCreatesAccount(NSDictionary *request) {
    return ![request[@"CreateAccount"] isEqual:@NO];
}
