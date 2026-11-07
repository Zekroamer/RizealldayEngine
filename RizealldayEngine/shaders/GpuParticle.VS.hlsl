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
        output.data = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return output;
    }
    const float t = p.age / p.life;
    const float size = Key(t, gSizeStart.xxxx, gSizeMiddle.xxxx, gSizeEnd.xxxx).x;
    float3 across = gCameraRight;
    float3 along = gCameraUp;
    float stretchLength = 1.0f;
    if (gUpright > 0.5f)
    {
        // Faces the camera's horizontal right; a camera looking straight down falls back to world X
        const float3 flatRight = float3(gCameraRight.x, 0.0f, gCameraRight.z);
        const float flatLength = sqrt(dot(flatRight, flatRight));
        across = flatLength > 0.001f ? flatRight / flatLength : float3(1.0f, 0.0f, 0.0f);
        along = float3(0.0f, 1.0f, 0.0f);
    }
    // Streak: the quad's long side follows the velocity as seen on screen; too slow or flying straight at the camera stays square
    const float2 onScreen = float2(dot(p.velocity, gCameraRight), dot(p.velocity, gCameraUp));
    const float screenSpeed = sqrt(dot(onScreen, onScreen));
    if (gStretch > 0.0f && screenSpeed > 0.01f)
    {
        const float2 dir = onScreen / screenSpeed;
        along = gCameraRight * dir.x + gCameraUp * dir.y;
        across = gCameraRight * dir.y - gCameraUp * dir.x;
        stretchLength = 1.0f + sqrt(dot(p.velocity, p.velocity)) * gStretch;
    }
    const float3 world = p.position + (across * corner.x + along * (corner.y * stretchLength)) * size * 0.5f;
    output.position = mul(float4(world, 1.0f), gViewProjection);
    output.color = Key(t, gColorStart, gColorMiddle, gColorEnd);
    // A new random number each time the slot is reused: its life is drawn at random on every spawn
    const float random = frac(sin(p.life * 12.9898f + instanceId * 78.233f) * 43758.5453f);
    output.data = float4(t, random, sqrt(dot(p.velocity, p.velocity)), 0.0f);
    return output;
}
