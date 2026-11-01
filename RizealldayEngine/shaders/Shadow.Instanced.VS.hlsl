// Shadow casters without bones, drawn in one call per model. One light-space matrix per instance.
StructuredBuffer<float32_t4x4> gInstances : register(t0, space1);

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
    uint4 boneIndices : BLENDINDICES0;
    float4 boneWeights : BLENDWEIGHT0;
    uint instance : SV_InstanceID;
};

float32_t4 main(VertexShaderInput input) : SV_POSITION
{
    return mul(input.position, gInstances[input.instance]);
}
