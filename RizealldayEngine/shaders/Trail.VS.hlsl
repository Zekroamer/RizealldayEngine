// Trail ribbon (TrailRenderer). View-projection is passed as root constants.
struct TrailConstants
{
    float4x4 viewProjection;
};
ConstantBuffer<TrailConstants> gTrail : register(b0);

struct TrailVSInput
{
    float4 position : POSITION0;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct TrailVSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

TrailVSOutput main(TrailVSInput input)
{
    TrailVSOutput output;
    output.position = mul(input.position, gTrail.viewProjection);
    output.color = input.color;
    output.uv = input.uv;
    return output;
}
