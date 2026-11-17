#pragma once
#import <AppKit/AppKit.h>
#include <math.h>

static inline double VIMCreationDownloadFraction(int64_t received,int64_t responseTotal,int64_t knownTotal) {
    // A CDN can stream without Content-Length even though HEAD/the catalog
    // already supplied its size. This is display information, not validation.
    int64_t total=responseTotal>0?responseTotal:knownTotal;
    return total>0?fmin(1,fmax(0,(double)received/total)):0;
}

static inline void VIMSetDeterminateCreationProgress(NSProgressIndicator *progress,double fraction) {
    [progress stopAnimation:nil];
    progress.indeterminate=NO;
    progress.doubleValue=fraction;
}

static inline void VIMConstrainCreationProgressRow(NSTextField *field) {
    // These labels change while the centered page is visible. Wrapping or
    // changing their intrinsic height would move the entire progress group.
    field.maximumNumberOfLines=1;
    field.usesSingleLineMode=YES;
    field.cell.wraps=NO;
    field.lineBreakMode=NSLineBreakByTruncatingMiddle;
    field.allowsExpansionToolTips=YES;
    CGFloat height=ceil([[NSLayoutManager new] defaultLineHeightForFont:field.font])+2;
    [field.heightAnchor constraintEqualToConstant:height].active=YES;
}
