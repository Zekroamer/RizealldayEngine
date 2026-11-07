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
    float gFloorHeight;
    float gBounce;
    uint gCollideWithFloor;
    uint gShape;        // 0 Point, 1 Sphere, 2 Box, 3 Cone (base disc), 4 Ring
    float3 gShapeAxisX; // Box half-axes in world space
    float gShapeRadius;
    float3 gShapeAxisY;
    uint gVelocityMode; // 0 Direction (cone around gDirection), 1 Radial (away from the centre)
    float3 gShapeAxisZ;
    float gPad;
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

// Same pair as GpuParticleTangents in GpuParticles.h
void Tangents(out float3 tangent, out float3 bitangent)
{
    const float3 helper = abs(gDirection.y) < 0.99f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    tangent = normalize(cross(helper, gDirection));
    bitangent = cross(gDirection, tangent);
}

float3 RandomInCone(inout uint state)
{
    const float z = lerp(gCosSpread, 1.0f, Random(state));
    const float angle = Random(state) * 6.2831853f;
    const float radius = sqrt(saturate(1.0f - z * z));
    float3 tangent, bitangent;
    Tangents(tangent, bitangent);
    return tangent * (cos(angle) * radius) + bitangent * (sin(angle) * radius) + gDirection * z;
}

float3 UnitSphere(float u1, float u2)
{
    const float z = 1.0f - 2.0f * u1;
    const float ring = sqrt(saturate(1.0f - z * z));
    const float angle = u2 * 6.2831853f;
    return float3(cos(angle) * ring, sin(angle) * ring, z);
}

// Same formulas as GpuParticleSpawnOffset in GpuParticles.h
float3 SpawnOffset(float u1, float u2, float u3)
{
    if (gShape == 1u)
    {
        return UnitSphere(u1, u2) * (gShapeRadius * pow(u3, 1.0f / 3.0f));
    }
    if (gShape == 2u)
    {
        return gShapeAxisX * (u1 * 2.0f - 1.0f) + gShapeAxisY * (u2 * 2.0f - 1.0f) + gShapeAxisZ * (u3 * 2.0f - 1.0f);
    }
    if (gShape == 3u || gShape == 4u)
    {
        float3 tangent, bitangent;
        Tangents(tangent, bitangent);
        const float angle = u1 * 6.2831853f;
        const float r = gShapeRadius * (gShape == 3u ? sqrt(u2) : 1.0f);
        return tangent * (cos(angle) * r) + bitangent * (sin(angle) * r);
    }
    return float3(0.0f, 0.0f, 0.0f);
}

// Same as GpuParticleRadialDirection in GpuParticles.h
float3 RadialDirection(float3 offset, float u1, float u2)
{
    const float len = length(offset);
    return len > 1.0e-4f ? offset / len : UnitSphere(u1, u2);
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
        const float u1 = Random(state);
        const float u2 = Random(state);
        const float u3 = Random(state);
        const float3 offset = SpawnOffset(u1, u2, u3);
        p.position = gOrigin + offset;
        const float3 heading = gVelocityMode == 1u ? RadialDirection(offset, Random(state), Random(state)) : RandomInCone(state);
        p.velocity = heading * lerp(gSpeedMin, gSpeedMax, Random(state));
        p.life = lerp(gLifeMin, gLifeMax, Random(state));
        p.age = 0.0f;
    }
    else if (p.age < p.life)
    {
        p.velocity += gGravity * gDeltaTime;
        p.velocity *= saturate(1.0f - gDrag * gDeltaTime);
        p.position += p.velocity * gDeltaTime;
        if (gCollideWithFloor != 0u && p.position.y < gFloorHeight)
        {
            p.position.y = gFloorHeight;
            if (p.velocity.y < 0.0f)
            {
                p.velocity.y *= -gBounce;
                p.velocity.xz *= 0.8f; // friction on every touch, so they come to rest
            }
        }
        p.age += gDeltaTime;
    }
    gParticles[index] = p;
}
