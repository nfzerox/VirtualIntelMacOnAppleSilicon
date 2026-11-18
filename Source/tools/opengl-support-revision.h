#pragma once
#import <Foundation/Foundation.h>

/* Validate library upgrades per guest family. Unchanged guests should not
 * restage their support or restart when another family's engine changes. */
static inline unsigned VIMOpenGLSupportRevision(NSString *guest)
{
    if([guest isEqualToString:@"tiger"]||[guest isEqualToString:@"leopard"]||
       [guest isEqualToString:@"snowleopard"])return 13;
    return 12;
}
