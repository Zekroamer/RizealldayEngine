#include "GpuParticle.hlsli"

struct Particle
{
    float3 position;
    float age;
    float3 velocity;
    float life;
};
StructuredBuffer<Particle> gParticles : register(t0);

// Same formula as GpuParticleKey in GpuParticles.h
float4 Key(float t, float4 start, float4 middle, float4 end)
{
    if (gMiddleTime < 0.0f)
    {
        return lerp(start, end, t);
    }
    return t < gMiddleTime ? lerp(start, middle, t / gMiddleTime) : lerp(middle, end, (t - gMiddleTime) / (1.0f - gMiddleTime));
}

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
    const float size = Key(t, gSizeStart.xxxx, gSizeMiddle.xxxx, gSizeEnd.xxxx).x;
    const float3 world = p.position + (gCameraRight * corner.x + gCameraUp * corner.y) * size * 0.5f;
    output.position = mul(float4(world, 1.0f), gViewProjection);
    output.color = Key(t, gColorStart, gColorMiddle, gColorEnd);
    return output;
}
