#include "Post.hlsli"

// ACES filmic curve (Narkowicz fit)
float3 Aces(float3 c)
{
    c *= 0.6f; // the fit expects this scale; without it the picture looks washed out
    return saturate((c * (2.51f * c + 0.03f)) / (c * (2.43f * c + 0.59f) + 0.14f));
}

float4 main(PostVSOutput input) : SV_TARGET
{
    float3 color = gScene.Sample(gLinearSampler, input.uv).rgb;
    if ((gFlags & 1) == 0) {
        return float4(saturate(color), 1.0f); // the sRGB render target encodes it, like rendering directly into it
    }

    if (gFlags & 8) {
        color *= gOcclusion.Sample(gLinearSampler, input.uv);
    }
    if (gFlags & 4) {
        color += gBloom.Sample(gLinearSampler, input.uv).rgb * gBloomIntensity;
    }
    color *= gExposure;
    if (gFlags & 2) {
        color = Aces(color);
    }

    color *= gTint;
    const float luma = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    color = lerp(luma.xxx, color, gSaturation);
    color = pow(max(color, 0.0f) / 0.18f, gContrast) * 0.18f;

    const float2 centered = input.uv - 0.5f;
    color *= 1.0f - gVignette * smoothstep(0.2f, 0.75f, length(centered));
    if (input.uv.y < gLetterbox || input.uv.y > 1.0f - gLetterbox) {
        color = 0.0f;
    }
    return float4(saturate(color), 1.0f);
}
