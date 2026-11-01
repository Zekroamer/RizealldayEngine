#include "Post.hlsli"

// Screen-space ambient occlusion from the depth buffer alone (half resolution; blurred by Post.SsaoBlur.PS.hlsl).
// gParams[0] = tanHalfFovX, tanHalfFovY, near, far   gParams[1].x = radius (world units), .y = intensity

float ViewDepth(float2 uv)
{
    const float d = gDepth.SampleLevel(gPointSampler, uv, 0.0f);
    const float nearZ = gParams[0].z;
    const float farZ = gParams[0].w;
    return nearZ * farZ / (farZ - d * (farZ - nearZ));
}

float3 ViewPosition(float2 uv)
{
    const float z = ViewDepth(uv);
    return float3((uv.x * 2.0f - 1.0f) * gParams[0].x * z, (1.0f - uv.y * 2.0f) * gParams[0].y * z, z);
}

// At the screen edge one step leaves the image and comes back zero long: that one cannot give a normal
float3 SmallerStep(float3 a, float3 b)
{
    const float lengthA = dot(a, a);
    const float lengthB = dot(b, b);
    if (lengthA < 1.0e-10f) return b;
    if (lengthB < 1.0e-10f) return a;
    return lengthA < lengthB ? a : b;
}

float4 main(PostVSOutput input) : SV_TARGET
{
    if (gDepth.SampleLevel(gPointSampler, input.uv, 0.0f) >= 1.0f)
    {
        return float4(1.0f, 1.0f, 1.0f, 1.0f); // sky
    }

    const float3 P = ViewPosition(input.uv);
    // The smaller step of each axis: a step across a silhouette would tilt the normal
    const float2 texel = gTexelSize * 2.0f; // a wider step: one step of a quantized depth tilts the normal less
    const float3 left = P - ViewPosition(input.uv - float2(texel.x, 0.0f));
    const float3 right = ViewPosition(input.uv + float2(texel.x, 0.0f)) - P;
    const float3 up = ViewPosition(input.uv + float2(0.0f, -texel.y)) - P;
    const float3 down = P - ViewPosition(input.uv + float2(0.0f, texel.y));
    const float3 dx = SmallerStep(left, right);
    const float3 dy = SmallerStep(up, down);
    const float3 N = normalize(cross(dy, dx));

    const float radius = gParams[1].x;
    const float noise = frac(52.9829189f * frac(dot(input.position.xy, float2(0.06711056f, 0.00583715f))));
    float occlusion = 0.0f;
    const int kSamples = 12;
    for (int i = 0; i < kSamples; ++i)
    {
        // A spiral over the disc, turned per pixel; the blur hides the pattern
        const float t = (float(i) + 0.5f) / kSamples;
        const float angle = 2.39996323f * float(i) + noise * 6.2831853f;
        const float r = sqrt(t) * radius;
        const float2 offset = float2(cos(angle), sin(angle)) * r;
        const float2 sampleUv = input.uv + float2(offset.x / (2.0f * gParams[0].x * P.z), -offset.y / (2.0f * gParams[0].y * P.z));
        if (any(sampleUv < 0.0f) || any(sampleUv > 1.0f))
        {
            continue;
        }
        const float3 v = ViewPosition(sampleUv) - P;
        const float distSq = dot(v, v);
        const float height = dot(N, v);
        if (height < P.z * 0.006f)
        {
            continue; // within the depth buffer's error: a flat floor must not shade itself in lines
        }
        const float NdotV = height / sqrt(max(distSq, 0.0001f));
        const float falloff = saturate(1.0f - distSq / (radius * radius));
        occlusion += saturate(NdotV - 0.1f) * falloff;
    }
    const float ao = saturate(1.0f - occlusion / kSamples * 2.0f * gParams[1].y);
    return float4(ao, ao, ao, 1.0f);
}
