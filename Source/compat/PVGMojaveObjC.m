#include <objc/message.h>
#include <objc/runtime.h>
#include <dispatch/dispatch.h>
#import <Foundation/Foundation.h>
#include <os/lock.h>

// Catalina Metal 212.8's five-method superclass, used by the working donor
// plugin there. Disassembly establishes NSObject + label@8 + unfair-lock@16.
// No associations or allocations on the label-read fast path beyond Apple's
// retained/autoreleased string contract.
@interface _MTLObjectWithLabel : NSObject {
    NSString *_label;
    os_unfair_lock _labelLock;
}
- (NSString *)retainedLabel;
@property(copy) NSString *label;
@end

@implementation _MTLObjectWithLabel
- (instancetype)init {
    if ((self = [super init])) {
        _label = nil;
        _labelLock = OS_UNFAIR_LOCK_INIT;
    }
    return self;
}
- (void)setLabel:(NSString *)label {
    if (_label == label) return;
    NSString *replacement = [label copy];
    os_unfair_lock_lock(&_labelLock);
    NSString *old = _label;
    _label = replacement;
    os_unfair_lock_unlock(&_labelLock);
    [old release];
}
- (NSString *)retainedLabel {
    os_unfair_lock_lock(&_labelLock);
    NSString *result = [_label retain];
    os_unfair_lock_unlock(&_labelLock);
    return result;
}
- (NSString *)label { return [[self retainedLabel] autorelease]; }
- (void)dealloc {
    [_label release];
    _label = nil;
    [super dealloc];
}
@end

// Apple objc4-818.2 NSObject.mm's semantic fallback paths. The optimized
// helpers do not exist in Mojave; retain the Objective-C method dispatch so
// custom +new and -respondsToSelector: overrides continue to work.
id objc_opt_new(Class cls) {
    return ((id (*)(id, SEL))objc_msgSend)((id)cls, @selector(new));
}

Class objc_opt_class(id object) {
    return ((Class (*)(id, SEL))objc_msgSend)(object, @selector(class));
}

BOOL objc_opt_respondsToSelector(id object, SEL selector) {
    return ((BOOL (*)(id, SEL, SEL))objc_msgSend)(
        object, @selector(respondsToSelector:), selector);
}

// libdispatch-1271.120.2 xcodeconfig/libdispatch.aliases exports this as an
// alias of dispatch_async. No channel implementation or queue shim is needed.
void dispatch_channel_async(dispatch_queue_t queue, dispatch_block_t block) {
    dispatch_async(queue, block);
}
