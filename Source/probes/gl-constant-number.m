#import <Foundation/Foundation.h>
#import <objc/runtime.h>
int main(void) {
    @autoreleasepool {
        Class cls=objc_getClass("NSConstantIntegerNumber");
        if(!cls||class_getInstanceSize(cls)!=24||ivar_getOffset(class_getInstanceVariable(cls,"_value"))!=16)return 2;
        struct {Class isa;const char *encoding;long long value;} object={cls,"q",4510};
        NSNumber *n=(id)&object;
        long long value=0;[n getValue:&value];
        BOOL ok=value==4510&&n.intValue==4510&&[n isEqualToNumber:@4510]&&[n copy]==n;
        NSDictionary *dictionary=@{n:@"constant"};
        ok=ok&&[dictionary[@4510] isEqual:@"constant"];
        [n retain];[n release];[n autorelease];
        printf("constant integer: %s\n",ok?"PASS":"FAIL");return ok?0:1;
    }
}
