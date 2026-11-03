#include "Decal.hlsli"

// A unit box (-0.5 .. 0.5) from the vertex id, no vertex buffer
static const float3 kCorners[8] = {
    float3(-0.5f, -0.5f, -0.5f), float3(0.5f, -0.5f, -0.5f), float3(0.5f, 0.5f, -0.5f), float3(-0.5f, 0.5f, -0.5f),
    float3(-0.5f, -0.5f, 0.5f), float3(0.5f, -0.5f, 0.5f), float3(0.5f, 0.5f, 0.5f), float3(-0.5f, 0.5f, 0.5f)
};
static const uint kIndices[36] = {
    0, 1, 2, 0, 2, 3,  5, 4, 7, 5, 7, 6,  4, 0, 3, 4, 3, 7,
    1, 5, 6, 1, 6, 2,  3, 2, 6, 3, 6, 7,  4, 5, 1, 4, 1, 0
};

DecalVSOutput main(uint vertexId : SV_VertexID)
{
    DecalVSOutput output;
    const float4 world = mul(float4(kCorners[kIndices[vertexId]], 1.0f), gWorld);
    output.position = mul(world, gViewProjection);
    return output;
}
