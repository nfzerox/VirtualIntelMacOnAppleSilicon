#include "../compat/PVGOpenGLProbe/PVGGLSwizzle.h"
#include <stdio.h>
static uint32_t key(unsigned n) {
    uint32_t result=0;
    for(unsigned i=0;i<4;i++){result|=(n%6)<<(i*8);n/=6;}
    return result;
}
int main(void) {
    unsigned checked=0;
    for(unsigned a=0;a<1296;a++)for(unsigned b=0;b<1296;b++) {
        uint32_t base=key(a),view=key(b),actual=PVGGLComposeSwizzle(base,view);
        unsigned values[6]={0,1,base&255,(base>>8)&255,(base>>16)&255,base>>24};
        for(unsigned c=0;c<4;c++)
            if(((actual>>(c*8))&255)!=values[(view>>(c*8))&255])return 1;
        checked++;
    }
    printf("PASS swizzle compositions=%u\n",checked);return 0;
}
