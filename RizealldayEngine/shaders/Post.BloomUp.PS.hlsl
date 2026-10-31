#include "Post.hlsli"

// Dual-filter upsample, blended additively into the next larger level
float4 main(PostVSOutput input) : SV_TARGET
{
    const float2 t = gTexelSize;
    float3 color = gScene.Sample(gLinearSampler, input.uv + float2(-t.x * 2.0f, 0.0f)).rgb;
    color += gScene.Sample(gLinearSampler, input.uv + float2(t.x * 2.0f, 0.0f)).rgb;
    color += gScene.Sample(gLinearSampler, input.uv + float2(0.0f, -t.y * 2.0f)).rgb;
    color += gScene.Sample(gLinearSampler, input.uv + float2(0.0f, t.y * 2.0f)).rgb;
    color += gScene.Sample(gLinearSampler, input.uv + float2(-t.x, -t.y)).rgb * 2.0f;
    color += gScene.Sample(gLinearSampler, input.uv + float2(t.x, -t.y)).rgb * 2.0f;
    color += gScene.Sample(gLinearSampler, input.uv + float2(-t.x, t.y)).rgb * 2.0f;
    color += gScene.Sample(gLinearSampler, input.uv + float2(t.x, t.y)).rgb * 2.0f;
    return float4(color / 12.0f, 1.0f);
}
