#include <metal_stdlib>
using namespace metal;

vertex float4 pvgVertex(uint vertexID [[vertex_id]]) {
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(3.0, -1.0),
        float2(-1.0, 3.0),
    };
    return float4(positions[vertexID], 0.0, 1.0);
}

fragment float4 pvgFragment() {
    return float4(0.25, 0.5, 0.75, 1.0);
}
