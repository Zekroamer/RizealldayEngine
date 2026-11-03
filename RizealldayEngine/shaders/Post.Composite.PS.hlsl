#include "Post.hlsli"

// ACES filmic curve (Narkowicz fit)
float3 Aces(float3 c)
{
    c *= 0.6f; // the fit expects this scale; without it the picture looks washed out
    return saturate((c * (2.51f * c + 0.03f)) / (c * (2.43f * c + 0.59f) + 0.14f));
}

// gParams: [0] color, density  [1] height, falloff, camera height  [2] height component of the view ray's x, y, z  [3] tanHalfFovX, tanHalfFovY, near, far
float3 ApplyFog(float3 color, float2 uv)
{
    const float d = gDepth.SampleLevel(gPointSampler, uv, 0.0f);
    const float nearZ = gParams[3].z;
    const float farZ = gParams[3].w;
    const float z = nearZ * farZ / (farZ - d * (farZ - nearZ)); // the sky counts as the far plane
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    const float rayLength = length(float3(ndc * gParams[3].xy, 1.0f)); // per unit of view depth
    const float climb = dot(float3(ndc, 1.0f), gParams[2].xyz) * z; // height gained from the camera to the point

    const float falloff = gParams[1].y;
    float optical = gParams[0].w * rayLength * z;
    if (falloff > 0.0f) {
        // Density falls exponentially with height: the integral along the ray has a closed form
        optical *= exp(-falloff * (gParams[1].z - gParams[1].x));
        const float exponent = falloff * climb;
        optical *= abs(exponent) > 0.001f ? (1.0f - exp(-exponent)) / exponent : 1.0f;
    }
    return lerp(gParams[0].rgb, color, exp(-optical));
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
    if (gFlags & 16) {
        color = ApplyFog(color, input.uv);
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
