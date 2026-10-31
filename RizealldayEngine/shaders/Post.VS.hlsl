#include "Post.hlsli"

PostVSOutput main(uint vertexId : SV_VertexID)
{
    PostVSOutput output;
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.uv.x * 2.0f - 1.0f, 1.0f - output.uv.y * 2.0f, 0.0f, 1.0f);
    return output;
}
