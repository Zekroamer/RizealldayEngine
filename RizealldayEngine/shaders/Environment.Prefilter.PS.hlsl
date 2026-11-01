// One face of one mip of the environment cube: the captured sky convolved with a GGX lobe of this roughness.
struct PostVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

cbuffer Face : register(b0)
{
    float3 gRight;
    float3 gUp;
    float3 gForward;
    float gRoughness;
};

TextureCube<float4> gSky : register(t0);
SamplerState gSampler : register(s0);

static const uint kSamples = 128;
static const float kPi = 3.14159265f;

float2 Hammersley(uint i)
{
    return float2(float(i) / kSamples, reversebits(i) * 2.3283064365386963e-10f);
}

float4 main(PostVSOutput input) : SV_TARGET
{
    const float2 clip = float2(input.uv.x * 2.0f - 1.0f, 1.0f - input.uv.y * 2.0f);
    const float3 N = normalize(gRight * clip.x + gUp * clip.y + gForward);
    if (gRoughness <= 0.0f)
    {
        return float4(gSky.SampleLevel(gSampler, N, 0.0f).rgb, 1.0f);
    }

    const float3 helper = abs(N.y) < 0.99f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 T = normalize(cross(helper, N));
    const float3 B = cross(N, T);
    const float a = gRoughness * gRoughness;

    float3 sum = float3(0.0f, 0.0f, 0.0f);
    float weight = 0.0f;
    for (uint i = 0; i < kSamples; ++i)
    {
        const float2 xi = Hammersley(i);
        const float phi = 2.0f * kPi * xi.x;
        const float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
        const float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
        const float3 H = normalize(T * (cos(phi) * sinTheta) + B * (sin(phi) * sinTheta) + N * cosTheta);
        const float3 L = reflect(-N, H);
        const float NdotL = dot(N, L);
        if (NdotL > 0.0f)
        {
            sum += gSky.SampleLevel(gSampler, L, 0.0f).rgb * NdotL;
            weight += NdotL;
        }
    }
    return float4(sum / max(weight, 0.0001f), 1.0f);
}
