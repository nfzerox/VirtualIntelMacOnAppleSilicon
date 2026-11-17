#import "../VirtualIntelMac/VIMDiagnosticSession.h"
#include <assert.h>
int main(void) {@autoreleasepool {
    NSDictionary *request=@{@"Session":@"first-boot",@"RequestID":@"finite-request",@"Executable":@"/usr/bin/true"};
    assert(VIMDiagnosticRequestMatchesSession(request,@"first-boot"));
    assert(!VIMDiagnosticRequestMatchesSession(request,@"next-boot"));
    assert(!VIMDiagnosticRequestMatchesSession(request,@""));
    assert(!VIMDiagnosticRequestMatchesSession(@{@"RequestID":@"finite-request"},@"first-boot"));
    assert(!VIMDiagnosticRequestMatchesSession(@{@"Session":@"first-boot"},@"first-boot"));
    assert(!VIMDiagnosticRequestMatchesSession(@{@"Session":@1,@"RequestID":@1},@"first-boot"));
    puts("PASS diagnostic requests remain bound to their emulator session");return 0;
}}
