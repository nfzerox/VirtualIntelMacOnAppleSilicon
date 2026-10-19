// Host-only checks for the actual local QEMU paging hooks, without a VM/GPU.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "apple-gfx-paging-probe.h"

static unsigned calls;
static id expectedCompletion;
static BOOL throwFromSync;

static void fakeSync(id self, SEL selector, const uint32_t *resources,
                     uint32_t count, BOOL discard, id completion)
{
    NSCAssert(apple_gfx_paging_scope == 1, @"paging scope missing");
    NSCAssert(completion == expectedCompletion, @"quiet path wrapped completion");
    calls++;
    if (throwFromSync) [NSException raise:@"ExpectedTestException" format:@"test"];
    ((void (^)(uint32_t, NSError *))completion)(1, nil);
}

static void fakePage(id self, SEL selector, id encoder, BOOL download)
{
    calls++;
}

@interface QuietTexture : NSObject
@property(readonly) MTLPixelFormat pixelFormat;
@end
@implementation QuietTexture
- (MTLPixelFormat)pixelFormat { return MTLPixelFormatBGRA8Unorm; }
@end
@interface QuietResource : NSObject {
@public
    id _texture;
}
@end
@implementation QuietResource
@end

int main(void)
{
    @autoreleasepool {
        apple_gfx_paging_trace_enabled = false;
        apple_gfx_msaa_transfer_enabled = true;
        apple_gfx_original_sync_resources = (IMP)fakeSync;
        __block unsigned completions = 0;
        void (^completion)(uint32_t, NSError *) = ^(uint32_t status, NSError *error) {
            NSCAssert(status == 1 && !error, @"completion changed");
            completions++;
        };
        expectedCompletion = completion;
        apple_gfx_trace_sync_resources(nil, NULL, NULL, 0, NO, completion);
        NSCAssert(apple_gfx_paging_scope == 0 && completions == 1, @"scope/completion leak");
        throwFromSync = YES;
        @try {
            apple_gfx_trace_sync_resources(nil, NULL, NULL, 0, NO, completion);
            return 2;
        } @catch (NSException *error) {
            if (![error.name isEqualToString:@"ExpectedTestException"]) @throw;
        }
        NSCAssert(apple_gfx_paging_scope == 0, @"exception leaked scope");
        apple_gfx_original_page_texture = (IMP)fakePage;
        QuietResource *resource = [QuietResource new];
        resource->_texture = [QuietTexture new];
        apple_gfx_trace_page_texture(resource, NULL, nil, NO);
        apple_gfx_trace_page_texture(resource, NULL, nil, YES);
        NSCAssert(calls == 4, @"original callback count changed");
        NSCAssert(atomic_load(&apple_gfx_page_trace_count) == 0 &&
                  atomic_load(&apple_gfx_sync_trace_count) == 0 &&
                  atomic_load(&apple_gfx_download_trace_count) == 0 &&
                  atomic_load(&apple_gfx_depth_page_trace_count) == 0,
                  @"quiet path touched trace counters");
        [resource->_texture release]; [resource release];
        fprintf(stderr, "PASS quiet paging: original completion identity, scope cleanup, ordinary forwarding, zero trace counters\n");
    }
    return 0;
}
