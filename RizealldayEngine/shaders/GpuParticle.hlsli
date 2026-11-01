cbuffer DrawConstants : register(b0)
{
    float4x4 gViewProjection;
    float3 gCameraRight;
    float gSizeStart;
    float3 gCameraUp;
    float gSizeEnd;
    float4 gColorStart;
    float4 gColorEnd;
};

struct GpuParticleVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};
