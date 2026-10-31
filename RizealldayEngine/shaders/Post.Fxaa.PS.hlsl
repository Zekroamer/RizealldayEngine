#include "Post.hlsli"

// Compact FXAA: finds the local edge direction from luma and blends along it
float Luma(float3 c)
{
    return sqrt(dot(c, float3(0.299f, 0.587f, 0.114f))); // perceptual: the source is read as linear
}

float4 main(PostVSOutput input) : SV_TARGET
{
    const float2 t = gTexelSize;
    const float3 center = gScene.SampleLevel(gPointSampler, input.uv, 0).rgb;
    const float lumaC = Luma(center);
    const float lumaNW = Luma(gScene.SampleLevel(gLinearSampler, input.uv + float2(-t.x, -t.y), 0).rgb);
    const float lumaNE = Luma(gScene.SampleLevel(gLinearSampler, input.uv + float2(t.x, -t.y), 0).rgb);
    const float lumaSW = Luma(gScene.SampleLevel(gLinearSampler, input.uv + float2(-t.x, t.y), 0).rgb);
    const float lumaSE = Luma(gScene.SampleLevel(gLinearSampler, input.uv + float2(t.x, t.y), 0).rgb);

    const float lumaMin = min(lumaC, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    const float lumaMax = max(lumaC, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    if (lumaMax - lumaMin < max(0.04f, lumaMax * 0.1f)) {
        return float4(center, 1.0f);
    }

    float2 dir = float2(-((lumaNW + lumaNE) - (lumaSW + lumaSE)), (lumaNW + lumaSW) - (lumaNE + lumaSE));
    const float reduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125f, 0.0078125f);
    dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), -8.0f, 8.0f) * t;

    const float3 inner = 0.5f * (gScene.SampleLevel(gLinearSampler, input.uv + dir * (1.0f / 3.0f - 0.5f), 0).rgb +
        gScene.SampleLevel(gLinearSampler, input.uv + dir * (2.0f / 3.0f - 0.5f), 0).rgb);
    const float3 outer = inner * 0.5f + 0.25f * (gScene.SampleLevel(gLinearSampler, input.uv - dir * 0.5f, 0).rgb +
        gScene.SampleLevel(gLinearSampler, input.uv + dir * 0.5f, 0).rgb);
    const float lumaOuter = Luma(outer);
    return float4((lumaOuter < lumaMin || lumaOuter > lumaMax) ? inner : outer, 1.0f);
}
