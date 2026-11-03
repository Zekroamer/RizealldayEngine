// Shared by the post-process passes and by screen effects: #include "Post.hlsli" and write
//   float4 main(PostVSOutput input) : SV_TARGET
// A screen effect reads the HDR scene and returns the HDR scene. Set its parameters from a script with SetScreenEffect.
struct PostVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

cbuffer PostConstants : register(b0)
{
    float gExposure;
    float gContrast;
    float gSaturation;
    float gVignette;
    float3 gTint;
    float gLetterbox;
    float gBloomIntensity;
    float gBloomThreshold;
    uint gFlags; // 1 = enabled, 2 = tone mapping, 4 = bloom, 8 = ambient occlusion, 16 = fog (composite only)
    float gTime; // game time in seconds
    float2 gTexelSize;
    float2 gPad;
    float4 gParams[4]; // SetScreenEffect parameters
};

Texture2D<float4> gScene : register(t0); // HDR scene (effects) / source of this pass
Texture2D<float4> gBloom : register(t1);
Texture2D<float> gDepth : register(t2); // 0 near .. 1 far
Texture2D<float> gOcclusion : register(t3); // ambient occlusion (composite only)
SamplerState gLinearSampler : register(s0);
SamplerState gPointSampler : register(s1);
