/* Local Intel PVG experiment. Preserve the caller's command-buffer ordering.
 * This adapter owns each real blit encoder; callers only see the adapter.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
static IMP apple_gfx_paging_native_blit;

@interface AppleGFXPagingEncoder : NSObject {
    id<MTLCommandBuffer> commandBuffer;
    id<MTLBlitCommandEncoder> blit;
    BOOL ended;
}
- (instancetype)initWithCommandBuffer:(id<MTLCommandBuffer>)buffer;
- (void)performTransfer:(void (^)(id<MTLCommandBuffer>))transfer;
- (void)endEncoding;
@end

@implementation AppleGFXPagingEncoder
- (instancetype)initWithCommandBuffer:(id<MTLCommandBuffer>)buffer
{
    self = [super init];
    if (self) {
        commandBuffer = [buffer retain];
    }
    return self;
}
- (id)forwardingTargetForSelector:(SEL)selector
{
    if (ended) {
        [NSException raise:NSInternalInconsistencyException
                    format:@"Paging encoder used after endEncoding"];
    }
    if (!blit) {
        id value = apple_gfx_paging_native_blit ?
            ((id (*)(id, SEL))apple_gfx_paging_native_blit)(commandBuffer,
                @selector(blitCommandEncoder)) : [commandBuffer blitCommandEncoder];
        blit = [value retain];
        if (!blit) {
            [NSException raise:NSInternalInconsistencyException
                        format:@"Unable to create paging blit encoder"];
        }
    }
    return blit;
}
- (void)performTransfer:(void (^)(id<MTLCommandBuffer>))transfer
{
    if (ended || !transfer) {
        [NSException raise:NSInternalInconsistencyException
                    format:@"Invalid paging transfer lifetime"];
    }
    [blit endEncoding];
    [blit release];
    blit = nil;
    /* The transfer owns and ends its render/compute encoder. It must never
     * commit, wait, or change the caller's event signal/wait sequence.
     */
    transfer(commandBuffer);
}
- (void)endEncoding
{
    if (!ended) {
        [blit endEncoding];
        [blit release];
        blit = nil;
        ended = YES;
    }
}
- (void)dealloc
{
    [blit release];
    [commandBuffer release];
    [super dealloc];
}
@end
