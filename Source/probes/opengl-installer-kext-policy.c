// Finite native check of the exact loader's filtering policy. This does not
// qualify filesystem detection or a guest boot; those require the real app.
#include <stdio.h>
#include <string.h>
#define STATIC static
#define IN
#define CONST const
typedef int BOOLEAN;
typedef unsigned int UINT32;
typedef char CHAR8;
static int AsciiStrCmp(const char *a,const char *b){return strcmp(a,b);}
#include "../compat/PassthroughOpenGLBootPolicy.h"

int main(void) {
    for(unsigned mask=0;mask<16;mask++) {
        int expected=(mask&3)==3&&(mask&12)!=0;
        int actual=VimUseNativeInstallerDrivers(mask&1,mask&2,mask&4,mask&8);
        if(actual!=expected)return 1;
    }
    for(unsigned major=8;major<=26;major++) {
        if(VimDisableInstallerDrizzle(0,major))return 4;
        if(VimDisableInstallerDrizzle(1,major)!=(major==16))return 5;
    }
    const char *bundles[]={"VMsvga2.kext","VMsvga2Accel.kext","VoodooHDA.kext",
        "Lilu.kext","VirtualSMC.kext","System/Library/Extensions/IOGraphicsFamily.kext"};
    for(unsigned i=0;i<sizeof(bundles)/sizeof(bundles[0]);i++){
        if(VimSkipInstallerKext(0,bundles[i]))return 2;
        int smc=!strcmp(bundles[i],"Lilu.kext")||!strcmp(bundles[i],"VirtualSMC.kext");
        if(VimSkipInstallerKext(1,bundles[i])==smc)return 3;
    }
    puts("Installer filtering: native graphics retained, configured SMC/decryption preserved, installed boot unchanged");
    return 0;
}
