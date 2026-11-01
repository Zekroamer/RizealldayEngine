#include "Object3d.hlsli"

// Static meshes with the same model and material drawn in one call. No skinning: objects with bones are drawn one by one.
struct Instance
{
    float32_t4x4 WVP;
    float32_t4x4 World;
    float32_t4x4 WorldInverseTranspose;
};
StructuredBuffer<Instance> gInstances : register(t0, space1);

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
    uint4 boneIndices : BLENDINDICES0;
    float4 boneWeights : BLENDWEIGHT0;
    uint instance : SV_InstanceID;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    const Instance instance = gInstances[input.instance];
    output.position = mul(input.position, instance.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float32_t3x3) instance.WorldInverseTranspose));
    output.worldPosition = mul(input.position, instance.World).xyz;
    return output;
}
