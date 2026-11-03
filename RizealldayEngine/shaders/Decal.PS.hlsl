#include "Decal.hlsli"

// Paints whatever the depth buffer shows inside the box, projected along the box's Y axis.
Texture2D<float> gDepth : register(t0);
Texture2D<float4> gTexture : register(t1);
SamplerState gSampler : register(s0);

float4 main(DecalVSOutput input) : SV_TARGET0
{
    const float depth = gDepth.Load(int3(int2(input.position.xy), 0));
    if (depth >= 1.0f)
    {
        discard; // sky
    }
    const float2 uv = input.position.xy / gTargetSize;
    float4 world = mul(float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f), gInverseViewProjection);
    world /= world.w;
    const float3 local = mul(world, gInverseWorld).xyz;
    if (any(abs(local) > 0.5f))
    {
        discard;
    }
    const float height = 1.0f - abs(local.y) * 2.0f; // 1 in the middle of the box, 0 at its top and bottom
    const float fade = gEdgeFade > 0.0f ? saturate(height / gEdgeFade) : 1.0f;
    const float4 color = gTexture.Sample(gSampler, float2(local.x + 0.5f, 0.5f - local.z)) * gColor;
    return float4(color.rgb, color.a * fade);
}
