// Particles of a particle graph (64 bytes, written by the generated compute shader; see GpuParticles::ParticleGraphShaderSource).
// Colour, size and rotation come from the particle itself; stretch and upright work as in GpuParticle.VS.hlsl.
#include "GpuParticle.hlsli"

struct GraphParticle
{
    float3 position;
    float age;
    float3 velocity;
    float life;
    float4 color;
    float size;
    float rotation; // degrees
    uint seed;
    float custom;
};
StructuredBuffer<GraphParticle> gParticles : register(t0);

GpuParticleVSOutput main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    static const float2 kCorners[6] = {
        float2(-1.0f, 1.0f), float2(1.0f, 1.0f), float2(-1.0f, -1.0f),
        float2(-1.0f, -1.0f), float2(1.0f, 1.0f), float2(1.0f, -1.0f)
    };
    GpuParticleVSOutput output;
    const GraphParticle p = gParticles[instanceId];
    const float2 baseCorner = kCorners[vertexId];
    output.uv = baseCorner * float2(0.5f, -0.5f) + 0.5f;
    if (p.age >= p.life || !(p.size > 0.0f))
    {
        output.position = float4(0.0f, 0.0f, 0.0f, 0.0f); // clipped
        output.color = float4(0.0f, 0.0f, 0.0f, 0.0f);
        output.data = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return output;
    }
    const float angle = p.rotation * 0.01745329f;
    const float2 corner = float2(baseCorner.x * cos(angle) - baseCorner.y * sin(angle), baseCorner.x * sin(angle) + baseCorner.y * cos(angle));
    float3 across = gCameraRight;
    float3 along = gCameraUp;
    float stretchLength = 1.0f;
    if (gUpright > 0.5f)
    {
        const float3 flatRight = float3(gCameraRight.x, 0.0f, gCameraRight.z);
        const float flatLength = sqrt(dot(flatRight, flatRight));
        across = flatLength > 0.001f ? flatRight / flatLength : float3(1.0f, 0.0f, 0.0f);
        along = float3(0.0f, 1.0f, 0.0f);
    }
    const float2 onScreen = float2(dot(p.velocity, gCameraRight), dot(p.velocity, gCameraUp));
    const float screenSpeed = sqrt(dot(onScreen, onScreen));
    if (gStretch > 0.0f && screenSpeed > 0.01f)
    {
        const float2 dir = onScreen / screenSpeed;
        along = gCameraRight * dir.x + gCameraUp * dir.y;
        across = gCameraRight * dir.y - gCameraUp * dir.x;
        stretchLength = 1.0f + sqrt(dot(p.velocity, p.velocity)) * gStretch;
    }
    const float3 world = p.position + (across * corner.x + along * (corner.y * stretchLength)) * p.size * 0.5f;
    output.position = mul(float4(world, 1.0f), gViewProjection);
    output.color = p.color;
    const float random = float(p.seed >> 8u) / 16777215.0f;
    output.data = float4(saturate(p.age / p.life), random, sqrt(dot(p.velocity, p.velocity)), 0.0f);
    return output;
}
