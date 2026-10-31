#include "Post.hlsli"

// Screen effect: gParams[0].xy = center (0..1), gParams[0].z = strength (0..1)
float4 main(PostVSOutput input) : SV_TARGET
{
    const float2 toCenter = gParams[0].xy - input.uv;
    float3 color = 0.0f;
    for (int i = 0; i < 12; ++i) {
        color += gScene.SampleLevel(gLinearSampler, input.uv + toCenter * gParams[0].z * (i / 12.0f), 0).rgb;
    }
    return float4(color / 12.0f, 1.0f);
}
