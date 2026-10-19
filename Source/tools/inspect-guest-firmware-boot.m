#import "VIMFirmwareBoot.h"
int main(int argc,const char **argv) {@autoreleasepool {
    if(argc!=2){fprintf(stderr,"usage: inspect-guest-firmware-boot NVRAM.fd\n");return 64;}
    VIMFirmwareBootTarget target=VIMFirmwareBootTargetAtURL([NSURL fileURLWithPath:@(argv[1])]);
    puts(target==VIMFirmwareBootInstaller?"installer":target==VIMFirmwareBootInstalledSystem?"installed-system":"unknown");
    return 0;
}}
