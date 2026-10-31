#include "Post.hlsli"

// Dual-filter downsample. gParams[0].x = 1 on the first pass, which keeps only what is brighter than the threshold.
float4 main(PostVSOutput input) : SV_TARGET
{
    const float2 t = gTexelSize;
    float3 color = gScene.Sample(gLinearSampler, input.uv).rgb * 4.0f;
    color += gScene.Sample(gLinearSampler, input.uv + float2(-t.x, -t.y)).rgb;
    color += gScene.Sample(gLinearSampler, input.uv + float2(t.x, -t.y)).rgb;
    color += gScene.Sample(gLinearSampler, input.uv + float2(-t.x, t.y)).rgb;
    color += gScene.Sample(gLinearSampler, input.uv + float2(t.x, t.y)).rgb;
    color /= 8.0f;

    if (gParams[0].x > 0.5f) {
        const float brightness = max(color.r, max(color.g, color.b));
        // Soft knee so the cut does not show as an edge
        const float knee = gBloomThreshold * 0.5f;
        const float soft = clamp(brightness - gBloomThreshold + knee, 0.0f, 2.0f * knee);
        const float contribution = max(soft * soft / (4.0f * knee + 1e-4f), brightness - gBloomThreshold);
        color *= max(contribution, 0.0f) / max(brightness, 1e-4f);
    }
    return float4(min(color, 64.0f), 1.0f); // a stray huge pixel must not flash the whole screen
}
