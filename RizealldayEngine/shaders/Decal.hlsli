cbuffer DecalConstants : register(b0)
{
    float4x4 gViewProjection;
    float4x4 gInverseViewProjection;
    float4x4 gWorld;
    float4x4 gInverseWorld;
    float4 gColor;
    float2 gTargetSize;
    float gEdgeFade; // part of the box height where the decal fades, towards its top and bottom
    float gPad;
};

struct DecalVSOutput
{
    float4 position : SV_POSITION;
};
