#include <metal_stdlib>
using namespace metal;
fragment float4 textureReadReference(texture2d<float,access::sample> a [[texture(0)]],
                                     texture3d<float,access::sample> b [[texture(1)]]) {
    uint3 p=uint3(b.get_width()-1,b.get_height()-1,b.get_depth()-1);
    return a.read(uint2(0),0)+b.read(p,0);
}
