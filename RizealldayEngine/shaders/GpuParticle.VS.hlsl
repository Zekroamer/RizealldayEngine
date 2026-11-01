#include "GpuParticle.hlsli"

struct Particle
{
    float3 position;
    float age;
    float3 velocity;
    float life;
};
StructuredBuffer<Particle> gParticles : register(t0);

GpuParticleVSOutput main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    static const float2 kCorners[6] = {
        float2(-1.0f, 1.0f), float2(1.0f, 1.0f), float2(-1.0f, -1.0f),
        float2(-1.0f, -1.0f), float2(1.0f, 1.0f), float2(1.0f, -1.0f)
    };
    GpuParticleVSOutput output;
    const Particle p = gParticles[instanceId];
    const float2 corner = kCorners[vertexId];
    output.uv = corner * float2(0.5f, -0.5f) + 0.5f;
    if (p.age >= p.life)
    {
        output.position = float4(0.0f, 0.0f, 0.0f, 0.0f); // clipped
        output.color = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return output;
    }
    const float t = p.age / p.life;
    const float size = lerp(gSizeStart, gSizeEnd, t);
    const float3 world = p.position + (gCameraRight * corner.x + gCameraUp * corner.y) * size * 0.5f;
    output.position = mul(float4(world, 1.0f), gViewProjection);
    output.color = lerp(gColorStart, gColorEnd, t);
    return output;
}
