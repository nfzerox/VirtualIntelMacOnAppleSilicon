#include <metal_stdlib>
using namespace metal;
kernel void ciBufferReference(uint2 p [[thread_position_in_grid]],
                              constant char *bytes [[buffer(0)]],
                              texture2d<float, access::write> out [[texture(0)]]) {
    out.write(float4(bytes[0]), p);
}
