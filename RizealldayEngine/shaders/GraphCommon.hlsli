// Helper functions used by the code that effect node graphs generate (RizealldaySandbox/GraphNodes.cpp)

float GraphHash21(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

// Smooth value noise in 0..1
float GraphValueNoise(float2 p)
{
    const float2 cell = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * (3.0f - 2.0f * f);
    const float a = GraphHash21(cell);
    const float b = GraphHash21(cell + float2(1.0f, 0.0f));
    const float c = GraphHash21(cell + float2(0.0f, 1.0f));
    const float d = GraphHash21(cell + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// Three octaves of value noise: soft clouds and flames
float GraphFractalNoise(float2 p)
{
    return GraphValueNoise(p) * 0.5333f + GraphValueNoise(p * 2.0f + 17.0f) * 0.2667f + GraphValueNoise(p * 4.0f + 31.0f) * 0.2f;
}

float2 GraphRotate(float2 uv, float2 center, float angle)
{
    const float s = sin(angle);
    const float c = cos(angle);
    const float2 d = uv - center;
    return center + float2(d.x * c - d.y * s, d.x * s + d.y * c);
}

// Cell `frame` of a columns x rows sprite sheet, read left to right, top to bottom
float2 GraphFlipbook(float2 uv, float columns, float rows, float frame)
{
    const float cols = max(floor(columns), 1.0f);
    const float rws = max(floor(rows), 1.0f);
    const float index = clamp(floor(frame), 0.0f, cols * rws - 1.0f);
    const float2 cell = float2(fmod(index, cols), floor(index / cols));
    return (cell + saturate(uv)) / float2(cols, rws);
}

// x = angle around the centre (0..1), y = distance from the centre (0.5 = the edge of the quad)
float2 GraphPolar(float2 uv, float2 center)
{
    const float2 d = uv - center;
    return float2(atan2(d.y, d.x) / 6.2831853f + 0.5f, length(d));
}

// ---- Particle graphs (compute shader) ----

uint GraphPcg(uint value)
{
    const uint state = value * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// 0..1, fixed for one particle (seed) and one node (salt): the same particle keeps its value all its life
float GraphRandom(uint seed, uint salt)
{
    return float(GraphPcg(seed ^ GraphPcg(salt + 0x9E3779B9u)) >> 8u) / 16777215.0f;
}

float4 GraphRandom4(uint seed, uint salt)
{
    return float4(GraphRandom(seed, salt * 4u), GraphRandom(seed, salt * 4u + 1u), GraphRandom(seed, salt * 4u + 2u), GraphRandom(seed, salt * 4u + 3u));
}

// Uniform direction on the unit sphere
float3 GraphUnitSphere(float u1, float u2)
{
    const float z = 1.0f - 2.0f * u1;
    const float ring = sqrt(max(1.0f - z * z, 0.0f));
    const float angle = u2 * 6.2831853f;
    return float3(cos(angle) * ring, sin(angle) * ring, z);
}

// Two unit vectors across `direction` (normalized)
void GraphTangents(float3 direction, out float3 tangent, out float3 bitangent)
{
    const float3 helper = abs(direction.y) < 0.99f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    tangent = normalize(cross(helper, direction));
    bitangent = cross(direction, tangent);
}

float3 GraphSafeNormalize(float3 v, float3 fallback)
{
    const float len = length(v);
    return len > 1.0e-5f ? v / len : fallback;
}

// A random direction at most `degrees` away from `direction`
float3 GraphConeDirection(float3 direction, float degrees, float u1, float u2)
{
    const float3 d = GraphSafeNormalize(direction, float3(0.0f, 1.0f, 0.0f));
    float3 t, b;
    GraphTangents(d, t, b);
    const float cosSpread = cos(clamp(degrees, 0.0f, 180.0f) * 0.01745329f);
    const float z = lerp(1.0f, cosSpread, u1);
    const float ring = sqrt(max(1.0f - z * z, 0.0f));
    const float angle = u2 * 6.2831853f;
    return d * z + (t * cos(angle) + b * sin(angle)) * ring;
}

float GraphHash31(float3 p)
{
    const int3 c = int3(floor(p));
    return float(GraphPcg(uint(c.x) + GraphPcg(uint(c.y) + GraphPcg(uint(c.z)))) >> 8u) / 16777215.0f;
}

float GraphValueNoise3(float3 p)
{
    const float3 cell = floor(p);
    const float3 f = frac(p);
    const float3 u = f * f * (3.0f - 2.0f * f);
    const float a = lerp(GraphHash31(cell), GraphHash31(cell + float3(1, 0, 0)), u.x);
    const float b = lerp(GraphHash31(cell + float3(0, 1, 0)), GraphHash31(cell + float3(1, 1, 0)), u.x);
    const float c = lerp(GraphHash31(cell + float3(0, 0, 1)), GraphHash31(cell + float3(1, 0, 1)), u.x);
    const float d = lerp(GraphHash31(cell + float3(0, 1, 1)), GraphHash31(cell + float3(1, 1, 1)), u.x);
    return lerp(lerp(a, b, u.y), lerp(c, d, u.y), u.z);
}

// Swirling flow without sources or sinks: the curl of three noise fields (central differences)
float3 GraphCurlNoise(float3 p)
{
    const float e = 0.1f;
    const float3 o1 = float3(31.4f, 0.0f, 0.0f), o2 = float3(0.0f, 47.2f, 0.0f);
    const float3 dx = float3(e, 0, 0), dy = float3(0, e, 0), dz = float3(0, 0, e);
    // Potential (A, B, C) = noise at three offsets
    const float dCdy = GraphValueNoise3(p + o2 + dy) - GraphValueNoise3(p + o2 - dy);
    const float dBdz = GraphValueNoise3(p + o1 + dz) - GraphValueNoise3(p + o1 - dz);
    const float dAdz = GraphValueNoise3(p + dz) - GraphValueNoise3(p - dz);
    const float dCdx = GraphValueNoise3(p + o2 + dx) - GraphValueNoise3(p + o2 - dx);
    const float dBdx = GraphValueNoise3(p + o1 + dx) - GraphValueNoise3(p + o1 - dx);
    const float dAdy = GraphValueNoise3(p + dy) - GraphValueNoise3(p - dy);
    return float3(dCdy - dBdz, dAdz - dCdx, dBdx - dAdy) / (2.0f * e);
}
