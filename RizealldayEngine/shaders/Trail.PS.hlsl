struct TrailVSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

float4 main(TrailVSOutput input) : SV_TARGET0
{
    return gTexture.Sample(gSampler, input.uv) * input.color;
}
