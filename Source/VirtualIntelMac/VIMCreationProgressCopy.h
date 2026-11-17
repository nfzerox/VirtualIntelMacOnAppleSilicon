#pragma once
#import "VIMLocalization.h"

static inline NSString *VIMCreationProgressIntroduction(BOOL downloadOnly,BOOL hostInstall,BOOL automatic,
    NSString *preparation,NSString *hostPreparation,NSString *manualInstructions,NSString *automaticInstructions) {
    // Acquisition alone never asks the user to run an installer. Host setup
    // prepares the system before boot; only manual guest installation needs
    // instructions to choose an installation destination.
    if(downloadOnly)return preparation;
    if(hostInstall)return hostPreparation;
    return [NSString stringWithFormat:VIMLocalized(@"creation.installation-may-take-up-to-2-hours", @"%@ %@ Installation may take up to 2 hours."),
        preparation,automatic?automaticInstructions:manualInstructions];
}
