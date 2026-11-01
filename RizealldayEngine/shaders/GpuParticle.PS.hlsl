#include "GpuParticle.hlsli"

Texture2D<float4> gTexture : register(t1);
SamplerState gSampler : register(s0);

float4 main(GpuParticleVSOutput input) : SV_TARGET0
{
    return gTexture.Sample(gSampler, input.uv) * input.color;
}
