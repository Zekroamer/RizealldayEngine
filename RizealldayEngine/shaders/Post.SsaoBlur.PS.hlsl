#include "Post.hlsli"

// 4 x 4 box over the half-resolution occlusion. Samples far from this pixel's depth are skipped so it does not bleed over edges.
float4 main(PostVSOutput input) : SV_TARGET
{
    const float center = gDepth.SampleLevel(gPointSampler, input.uv, 0.0f);
    float sum = 0.0f;
    float weight = 0.0f;
    for (int y = -2; y < 2; ++y)
    {
        for (int x = -2; x < 2; ++x)
        {
            const float2 uv = input.uv + (float2(x, y) + 0.5f) * gTexelSize;
            const float difference = abs(gDepth.SampleLevel(gPointSampler, uv, 0.0f) - center);
            if (difference < 0.002f)
            {
                sum += gScene.SampleLevel(gPointSampler, uv, 0.0f).r;
                weight += 1.0f;
            }
        }
    }
    const float ao = weight > 0.0f ? sum / weight : 1.0f;
    return float4(ao, ao, ao, 1.0f);
}
