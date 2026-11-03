// Shared by host DVD preparation and the native Tiger installer handoff.
// Keep the same payload contract on both sides of that boundary.
#pragma once
static inline NSArray *VIMTigerControlNames(void){
    return [NSArray arrayWithObjects:@"opengl-graphics-check",@"opengl-guest-agent",@"opengl-desktop-status",@"opengl-setup-status",
        @"opengl-create-account",@"opengl-tiger-install-config",@"opengl-tiger-installer-bootstrap",@"opengl-audio-defaults",@"opengl-tiger-audio",
        @"opengl-tiger-console",@"guest-display",@"guest-clipboard",@"guest-host-connection",@"guest-ssh",
        @"opengl-file-sharing",@"tiger-smbd",@"tiger-opendirectory.so",@"TigerSMB.plist",@"Samba-COPYING.txt",
        @"tiger-smbfs",@"TigerSMBClient-Info.plist",@"TigerSMBClient.plist",@"SMB-COPYRIGHT.txt",@"SMB-VIM-LICENSE.txt",nil];
}
