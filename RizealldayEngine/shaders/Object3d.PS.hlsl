#include "Object3d.hlsli"

struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    int32_t lightingType;  // 0=Unlit, 1=Lambert, 2=HalfLambert, 3=Phong, 4=BlinnPhong, 5=Toon, 6=PBR
    float32_t shininess;
    int32_t receiveShadows;
    float32_t3 specularColor;
    float32_t explodeAmount;
    float32_t2 uvOffset;
    float32_t2 uvTiling;
    float32_t uvRotation;
    float32_t outlineWidth;
    float32_t2 pad;
    float32_t4 outlineColor;
    float32_t metallic;
    float32_t roughness;
    float32_t normalStrength;
    float32_t emissiveStrength;
    float32_t3 emissiveColor;
    float32_t pad2;
};

struct DirectionalLightData
{
    float32_t4 color;
    float32_t3 direction;
    float32_t intensity;
    int32_t active;
    float32_t3 pad;
};

struct LightData
{
    float32_t3 position;
    float32_t radius;
    float32_t3 color;
    float32_t intensity;
    float32_t3 direction;
    float32_t decay;
    float32_t cosAngle;
    float32_t cosFalloffStart;
    int32_t isSpot;
    float32_t pad;
};

ConstantBuffer<Material> gMaterial : register(b3);

cbuffer LightGroup : register(b1)
{
    float32_t3 ambientColor;
    float32_t ambientIntensity;
    DirectionalLightData dirLight;
    float32_t4x4 cascadeMatrix[4];
    float32_t4 cascadeEnd;         // view depth where each cascade ends
    float32_t4 cascadeDepthScale;  // depth range of the first shadow map / of this cascade
    float32_t4 cascadeNormalScale; // texel size of this cascade / of the first shadow map
    int32_t shadowActive;
    float32_t shadowBias;
    float32_t shadowStrength;
    float32_t shadowNormalBias;
    float32_t4 viewDepthRow;       // dot(float4(world, 1), this) = view depth
    float32_t2 screenSize;
    float32_t clusterNear;
    float32_t clusterFar;
    float32_t environmentIntensity;
    float32_t environmentMaxMip;
    float32_t2 pad3;
};

struct CameraForGPU
{
    float32_t3 worldPosition;
    float32_t pad;
};

ConstantBuffer<CameraForGPU> gCamera : register(b2);

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

Texture2D<float> gShadowMap : register(t1);
SamplerComparisonState gShadowSampler : register(s1);

Texture2D<float32_t4> gNormalMap : register(t2);
Texture2D<float32_t4> gOrmMap : register(t3);      // R occlusion, G roughness, B metallic (glTF layout)
Texture2D<float32_t4> gEmissiveMap : register(t4);
TextureCube<float32_t4> gEnvironment : register(t5); // mip = roughness * environmentMaxMip

StructuredBuffer<LightData> gLights : register(t6);
StructuredBuffer<uint> gClusters : register(t7);       // offset | count << 24, see render/LightClusters.h
StructuredBuffer<uint> gClusterLights : register(t8);

static const uint kClusterX = 16;
static const uint kClusterY = 9;
static const uint kClusterZ = 16;
static const float32_t kPi = 3.14159265f;
static const float32_t kShadowTileTexel = 1.0f / 2048.0f; // the atlas is 2 x 2 tiles of 2048

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

float32_t CalculateShadow(float32_t3 worldPos, float32_t3 normal, float32_t3 lightDir, float32_t viewDepth)
{
    if (shadowActive == 0)
    {
        return 1.0f;
    }

    int cascade = 0;
    while (cascade < 4 && viewDepth >= cascadeEnd[cascade])
    {
        ++cascade;
    }
    if (cascade == 4)
    {
        return 1.0f; // beyond the shadow distance
    }

    // Normal offset bias to prevent shadow acne on curved/sloped surfaces
    float32_t NdotL = dot(normal, -lightDir);
    float32_t normalBias = shadowNormalBias * cascadeNormalScale[cascade] * saturate(1.0f - saturate(NdotL));
    float32_t3 biasedWorldPos = worldPos + normal * normalBias;

    float32_t4 lightSpacePos = mul(float32_t4(biasedWorldPos, 1.0f), cascadeMatrix[cascade]);
    if (lightSpacePos.w <= 0.0001f)
    {
        return 1.0f;
    }

    float32_t3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // Convert NDC [-1, 1] to UV [0, 1] (invert Y for texture coords)
    float32_t2 shadowUV;
    shadowUV.x = projCoords.x * 0.5f + 0.5f;
    shadowUV.y = -projCoords.y * 0.5f + 0.5f;

    // Outside the light frustum: not in shadow. The margin keeps the filter inside this tile of the atlas.
    const float32_t margin = kShadowTileTexel * 1.5f;
    if (shadowUV.x < margin || shadowUV.x > 1.0f - margin ||
        shadowUV.y < margin || shadowUV.y > 1.0f - margin ||
        projCoords.z < 0.0f || projCoords.z > 1.0f)
    {
        return 1.0f;
    }

    // Slope-scaled depth bias
    float32_t cosTheta = saturate(NdotL);
    float32_t slope = sqrt(saturate(1.0f - cosTheta * cosTheta)) / max(0.001f, cosTheta);
    float32_t slopeBias = shadowBias * min(slope, 3.0f);
    float32_t compareDepth = projCoords.z - (shadowBias + slopeBias) * cascadeDepthScale[cascade];

    const float32_t2 tile = float32_t2(cascade % 2, cascade / 2) * 0.5f;
    const float32_t2 atlasUV = shadowUV * 0.5f + tile;

    // 3x3 PCF (Percentage Closer Filtering)
    float32_t shadow = 0.0f;
    const float32_t texelSize = kShadowTileTexel * 0.5f;
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            float32_t2 offset = float32_t2(x, y) * texelSize;
            shadow += gShadowMap.SampleCmpLevelZero(gShadowSampler, atlasUV + offset, compareDepth);
        }
    }
    return shadow / 9.0f;
}

// Diffuse term of the material's reflection model (lightingType 1-5)
float32_t DiffuseTerm(float32_t NdotL)
{
    if (gMaterial.lightingType == 2) {
        return pow(NdotL * 0.5f + 0.5f, 2.0f); // Half-Lambert
    }
    if (gMaterial.lightingType == 5) {
        // Toon: hard bands 0 / 0.6 / 1 (narrow smoothstep keeps the edges from aliasing)
        float32_t d = saturate(NdotL);
        return 0.6f * smoothstep(0.0f, 0.04f, d) + 0.4f * smoothstep(0.5f, 0.54f, d);
    }
    return saturate(NdotL); // Lambert / Phong / Blinn-Phong
}

// Specular term (Phong 02_00, Blinn-Phong 02_01, Toon = a hard Blinn-Phong highlight)
float32_t SpecularTerm(float32_t3 lightDir, float32_t3 N, float32_t3 toEye, float shininess)
{
    if (gMaterial.lightingType == 3) {
        float32_t3 reflectLight = reflect(lightDir, N);
        return pow(saturate(dot(reflectLight, toEye)), shininess);
    }
    if (gMaterial.lightingType == 4 || gMaterial.lightingType == 5) {
        float32_t3 halfVector = normalize(-lightDir + toEye);
        float32_t blinn = pow(saturate(dot(N, halfVector)), shininess);
        return gMaterial.lightingType == 5 ? smoothstep(0.5f, 0.53f, blinn) : blinn;
    }
    return 0.0f;
}

// Tangent frame from screen-space derivatives: works for every mesh, skinned or not, without vertex tangents
float32_t3 ApplyNormalMap(float32_t3 N, float32_t3 worldPos, float32_t2 uv)
{
    const float32_t3 dp1 = ddx(worldPos);
    const float32_t3 dp2 = ddy(worldPos);
    const float32_t2 duv1 = ddx(uv);
    const float32_t2 duv2 = ddy(uv);
    const float32_t2 sampled = gNormalMap.Sample(gSampler, uv).xy;
    // 8-bit 128 is exactly flat, so meshes without a normal map are not bent at all
    const float32_t2 tilt = (sampled * 255.0f - 128.0f) / 127.0f * gMaterial.normalStrength;
    if (tilt.x == 0.0f && tilt.y == 0.0f)
    {
        return N;
    }
    const float32_t3 perp2 = cross(dp2, N);
    const float32_t3 perp1 = cross(N, dp1);
    const float32_t det = dot(dp1, perp2);
    if (det == 0.0f)
    {
        return N;
    }
    const float32_t3 T = (perp2 * duv1.x + perp1 * duv2.x) / det;
    const float32_t3 B = (perp2 * duv1.y + perp1 * duv2.y) / det;
    const float32_t lenT = length(T);
    const float32_t lenB = length(B);
    if (lenT == 0.0f || lenB == 0.0f)
    {
        return N;
    }
    // Green points up in the image, and image up is -v
    const float32_t z = sqrt(saturate(1.0f - dot(tilt, tilt)));
    return normalize(T / lenT * tilt.x - B / lenB * tilt.y + N * max(z, 0.001f));
}

// Cook-Torrance GGX for one light. Lights are in the same units as the older models: a white Lambert surface
// facing a light of intensity 1 shows albedo, so the BRDF is multiplied by pi.
float32_t3 PbrLight(float32_t3 N, float32_t3 V, float32_t3 L, float32_t3 radiance, float32_t3 diffuseColor, float32_t3 F0, float32_t roughness)
{
    const float32_t NdotL = saturate(dot(N, L));
    if (NdotL <= 0.0f)
    {
        return float32_t3(0.0f, 0.0f, 0.0f);
    }
    const float32_t3 H = normalize(L + V);
    const float32_t NdotV = max(dot(N, V), 0.0001f);
    const float32_t NdotH = saturate(dot(N, H));
    const float32_t VdotH = saturate(dot(V, H));
    const float32_t a = max(roughness * roughness, 0.002f);
    const float32_t a2 = a * a;
    const float32_t denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    const float32_t D = a2 / (kPi * denom * denom);
    const float32_t visibility = 0.5f / (NdotL * sqrt(NdotV * NdotV * (1.0f - a2) + a2) + NdotV * sqrt(NdotL * NdotL * (1.0f - a2) + a2) + 0.0001f);
    const float32_t3 F = F0 + (1.0f - F0) * pow(1.0f - VdotH, 5.0f);
    return (diffuseColor * (1.0f - F) + D * visibility * F * kPi) * NdotL * radiance;
}

// Karis' fit of the environment BRDF, so no lookup texture is needed
float32_t3 EnvironmentBrdf(float32_t3 F0, float32_t roughness, float32_t NdotV)
{
    const float32_t4 c0 = float32_t4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float32_t4 c1 = float32_t4(1.0f, 0.0425f, 1.04f, -0.04f);
    const float32_t4 r = roughness * c0 + c1;
    const float32_t a004 = min(r.x * r.x, exp2(-9.28f * NdotV)) * r.x + r.y;
    const float32_t2 ab = float32_t2(-1.04f, 1.04f) * a004 + r.zw;
    return F0 * ab.x + ab.y;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    // UV transformation: Tiling, Rotation, Offset
    float32_t2 uv = input.texcoord;
    uv = uv * gMaterial.uvTiling;
    float cosRot = cos(gMaterial.uvRotation);
    float sinRot = sin(gMaterial.uvRotation);
    uv = float32_t2(
        uv.x * cosRot - uv.y * sinRot,
        uv.x * sinRot + uv.y * cosRot
    );
    uv = uv + gMaterial.uvOffset;

    float32_t4 texColor = gTexture.Sample(gSampler, uv);
    PixelShaderOutput output;

    if (gMaterial.enableLighting == 0 || gMaterial.lightingType == 0)
    {
        output.color = gMaterial.color * texColor;
        return output;
    }

    float32_t3 worldPos = input.worldPosition;
    float32_t3 N = ApplyNormalMap(normalize(input.normal), worldPos, uv);
    float32_t3 toEye = normalize(gCamera.worldPosition - worldPos);
    const float32_t viewDepth = dot(float32_t4(worldPos, 1.0f), viewDepthRow);
    const bool isPbr = gMaterial.lightingType == 6;

    const float32_t3 albedo = gMaterial.color.rgb * texColor.rgb;
    const float32_t4 orm = isPbr ? gOrmMap.Sample(gSampler, uv) : float32_t4(1.0f, 1.0f, 1.0f, 1.0f);
    const float32_t roughness = saturate(gMaterial.roughness * orm.g);
    const float32_t metallic = saturate(gMaterial.metallic * orm.b);
    const float32_t3 diffuseColor = albedo * (1.0f - metallic);
    const float32_t3 F0 = lerp(float32_t3(0.04f, 0.04f, 0.04f), albedo, metallic);

    float32_t3 totalDiffuse = ambientColor * ambientIntensity;
    float32_t3 totalSpecular = float32_t3(0.0f, 0.0f, 0.0f);
    float32_t3 pbrColor = float32_t3(0.0f, 0.0f, 0.0f);

    float shininess = max(1.0f, gMaterial.shininess);
    float32_t3 specColor = gMaterial.specularColor;

    // 1. Directional Light
    if (dirLight.active != 0)
    {
        float32_t3 lightDir = normalize(dirLight.direction);

        // Shadow calculation: modulates Directional Light diffuse and specular only
        float32_t shadowFactor = 1.0f;
        if (gMaterial.receiveShadows != 0)
        {
            float32_t shadow = CalculateShadow(worldPos, N, lightDir, viewDepth);
            shadowFactor = 1.0f - (1.0f - shadow) * shadowStrength;
        }

        const float32_t3 radiance = dirLight.color.rgb * dirLight.intensity * shadowFactor;
        if (isPbr)
        {
            pbrColor += PbrLight(N, toEye, -lightDir, radiance, diffuseColor, F0, roughness);
        }
        else
        {
            totalDiffuse += radiance * DiffuseTerm(dot(N, -lightDir));
            totalSpecular += radiance * specColor * SpecularTerm(lightDir, N, toEye, shininess);
        }
    }

    // 2. Point and spot lights of this pixel's cluster
    const uint tileX = min(uint(input.position.x / screenSize.x * kClusterX), kClusterX - 1);
    const uint tileY = min(uint(input.position.y / screenSize.y * kClusterY), kClusterY - 1);
    const float32_t sliceT = log(max(viewDepth, clusterNear) / clusterNear) / log(clusterFar / clusterNear);
    const uint slice = min(uint(sliceT * kClusterZ), kClusterZ - 1);
    const uint cluster = gClusters[(slice * kClusterY + tileY) * kClusterX + tileX];
    const uint firstLight = cluster & 0xFFFFFFu;
    const uint lightCount = cluster >> 24;
    for (uint i = 0; i < lightCount; i++)
    {
        const LightData light = gLights[gClusterLights[firstLight + i]];
        float32_t3 lightVec = worldPos - light.position;
        float32_t dist = length(lightVec);
        float32_t3 lightDir = lightVec / max(dist, 0.0001f);

        // Attenuation: pow(saturate(-distance / radius + 1.0f), decay)
        float32_t attenuation = pow(saturate(-dist / max(0.001f, light.radius) + 1.0f), max(0.001f, light.decay));
        if (light.isSpot != 0)
        {
            float32_t cosTheta = dot(lightDir, light.direction);
            float32_t denom = max(0.001f, light.cosFalloffStart - light.cosAngle);
            attenuation *= saturate((cosTheta - light.cosAngle) / denom);
        }

        const float32_t3 radiance = light.color * light.intensity * attenuation;
        if (isPbr)
        {
            pbrColor += PbrLight(N, toEye, -lightDir, radiance, diffuseColor, F0, roughness);
        }
        else
        {
            totalDiffuse += radiance * DiffuseTerm(dot(N, -lightDir));
            totalSpecular += radiance * specColor * SpecularTerm(lightDir, N, toEye, shininess);
        }
    }

    if (isPbr)
    {
        // Image-based light from the sky: the blurriest mip stands in for irradiance
        const float32_t NdotV = max(dot(N, toEye), 0.0001f);
        const float32_t3 reflected = reflect(-toEye, N);
        const float32_t3 irradiance = gEnvironment.SampleLevel(gSampler, N, environmentMaxMip).rgb;
        const float32_t3 prefiltered = gEnvironment.SampleLevel(gSampler, reflected, roughness * environmentMaxMip).rgb;
        const float32_t3 ambient = (irradiance + ambientColor * ambientIntensity) * diffuseColor
            + prefiltered * EnvironmentBrdf(F0, roughness, NdotV);
        pbrColor += ambient * environmentIntensity * orm.r;
        pbrColor += gEmissiveMap.Sample(gSampler, uv).rgb * gMaterial.emissiveColor * gMaterial.emissiveStrength;
        output.color = float32_t4(pbrColor, gMaterial.color.a * texColor.a);
        return output;
    }

    float32_t3 finalColor = albedo * totalDiffuse + totalSpecular;
    output.color = float32_t4(finalColor, gMaterial.color.a * texColor.a);
    return output;
}
