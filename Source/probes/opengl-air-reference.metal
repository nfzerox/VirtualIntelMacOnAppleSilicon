#include <metal_stdlib>
using namespace metal;
struct VertexInput { float2 position [[attribute(0)]]; };
struct VertexOutput { float4 position [[position]]; };
vertex VertexOutput referenceVertex(VertexInput in [[stage_in]]) {
    return {float4(in.position, 0.0, 1.0)};
}
fragment float4 referenceFragment() { return float4(1.0, 0.0, 1.0, 1.0); }
