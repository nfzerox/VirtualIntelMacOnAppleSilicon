#pragma once
#include <string.h>
/* Original recovery app names, verified in Apple's 11G63/12F45/13F34 media.
 * The automatic copy keeps the original executable name and resources. */
#define VIM_LION_INSTALLER "/System/Installation/CDIS/Mac OS X Installer.app/Contents/MacOS/Mac OS X Installer"
#define VIM_OSX_INSTALLER "/System/Installation/CDIS/OS X Installer.app/Contents/MacOS/OS X Installer"
#define VIM_MACOS_INSTALLER "/System/Installation/CDIS/macOS Installer.app/Contents/MacOS/macOS Installer"
static inline const char *VIMOpenGLRecoveryInstaller(int lion)
{return lion?VIM_LION_INSTALLER:VIM_OSX_INSTALLER;}
/* Sierra and High Sierra recovery renamed both the application and executable.
 * Share the exact path between media verification, the early guest service
 * and the host setup coordinator; do not rename Apple's signed installer. */
static inline const char *VIMOpenGLRecoveryInstallerForGuest(const char *guest)
{
    if(!strcmp(guest,"sierra")||!strcmp(guest,"highsierra"))return VIM_MACOS_INSTALLER;
    return VIMOpenGLRecoveryInstaller(!strcmp(guest,"leopard")||!strcmp(guest,"snowleopard")||!strcmp(guest,"lion"));
}
static inline const char *VIMOpenGLAutomaticInstaller(int lion)
{return lion?"/System/Installation/CDIS/VirtualIntelMac Installer.app/Contents/MacOS/Mac OS X Installer":
    "/System/Installation/CDIS/VirtualIntelMac Installer.app/Contents/MacOS/OS X Installer";}
