// Moves every particle of one emitter and respawns the ring range [emitStart, emitStart + emitCount).
struct Particle
{
    float3 position;
    float age;
    float3 velocity;
    float life; // age >= life: dead
};
RWStructuredBuffer<Particle> gParticles : register(u0);

cbuffer Constants : register(b0)
{
    float3 gOrigin;
    float gDeltaTime;
    float3 gDirection;
    float gCosSpread;
    float3 gGravity;
    float gDrag;
    float gLifeMin;
    float gLifeMax;
    float gSpeedMin;
    float gSpeedMax;
    uint gEmitStart;
    uint gEmitCount;
    uint gCapacity;
    uint gSeed;
};

uint Pcg(uint value)
{
    const uint state = value * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float Random(inout uint state)
{
    state = Pcg(state);
    return float(state) / 4294967295.0f;
}

float3 RandomInCone(inout uint state)
{
    const float z = lerp(gCosSpread, 1.0f, Random(state));
    const float angle = Random(state) * 6.2831853f;
    const float radius = sqrt(saturate(1.0f - z * z));
    const float3 local = float3(cos(angle) * radius, sin(angle) * radius, z);
    const float3 helper = abs(gDirection.y) < 0.99f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 tangent = normalize(cross(helper, gDirection));
    const float3 bitangent = cross(gDirection, tangent);
    return tangent * local.x + bitangent * local.y + gDirection * local.z;
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint index = id.x;
    if (index >= gCapacity)
    {
        return;
    }
    Particle p = gParticles[index];
    if ((index + gCapacity - gEmitStart) % gCapacity < gEmitCount)
    {
        uint state = index * 2654435761u + gSeed;
        p.position = gOrigin;
        p.velocity = RandomInCone(state) * lerp(gSpeedMin, gSpeedMax, Random(state));
        p.life = lerp(gLifeMin, gLifeMax, Random(state));
        p.age = 0.0f;
    }
    else if (p.age < p.life)
    {
        p.velocity += gGravity * gDeltaTime;
        p.velocity *= saturate(1.0f - gDrag * gDeltaTime);
        p.position += p.velocity * gDeltaTime;
        p.age += gDeltaTime;
    }
    gParticles[index] = p;
}
