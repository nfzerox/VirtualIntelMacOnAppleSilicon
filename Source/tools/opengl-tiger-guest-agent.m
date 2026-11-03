// Keep Tiger-only cold startup outside the shared10.5–10.9 control service.
// The common native protocol/process implementation remains exactly the same.
#define main VIMTigerControlMain
#include "opengl-guest-agent.m"
#undef main
#include "opengl-tiger-firmware-media.h"
int main(int argc,char **argv) {
    if(argc!=3||strcmp(argv[1],"--device")||strcmp(argv[2],"/dev/cu.serial2"))return 64;
    if(!VIMTigerStartFirmwareMediaObserver())
        fputs("Tiger firmware media startup was not available.\n",stderr);
    return VIMTigerControlMain(argc,argv);
}
