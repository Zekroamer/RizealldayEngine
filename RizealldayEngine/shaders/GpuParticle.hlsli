cbuffer DrawConstants : register(b0)
{
    float4x4 gViewProjection;
    float3 gCameraRight;
    float gSizeStart;
    float3 gCameraUp;
    float gSizeEnd;
    float4 gColorStart;
    float4 gColorEnd;
    float4 gColorMiddle;
    float gSizeMiddle;
    float gMiddleTime; // < 0: two keys only
    float gStretch;    // 0 = camera-facing squares; > 0 = streaks along the velocity
    float gUpright;    // 1 = quads stand on world Y and turn only about it
    float gTime;       // seconds (game time in Play), for materials that move
    float3 gDrawPad;
};

struct GpuParticleVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float4 data : TEXCOORD1; // x = age 0..1, y = random 0..1 per particle, z = speed
};
