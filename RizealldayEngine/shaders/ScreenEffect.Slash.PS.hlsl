#include "Post.hlsli"

// Screen effect: the picture is cut along a line and the two sides pull apart and slide.
// gParams[0].xy = point on the line (0..1), gParams[0].z = line angle (radians), gParams[0].w = opening (0..1)
// gParams[1].rgb = color inside the cut (HDR values glow), gParams[1].a = slide along the line
float4 main(PostVSOutput input) : SV_TARGET
{
    const float aspect = gTexelSize.y / gTexelSize.x;
    const float2 along = float2(cos(gParams[0].z), sin(gParams[0].z));
    const float2 normal = float2(-along.y, along.x);
    const float2 p = (input.uv - gParams[0].xy) * float2(aspect, 1.0f);
    const float distance = dot(p, normal);
    const float side = distance < 0.0f ? -1.0f : 1.0f;
    const float halfGap = gParams[0].w * 0.12f;

    if (abs(distance) < halfGap) {
        return float4(gParams[1].rgb, 1.0f);
    }

    // Each pixel reads a point closer to the cut, so the picture looks pushed away from it
    const float2 offset = normal * side * halfGap + along * side * gParams[1].a * gParams[0].w * 0.05f;
    const float2 uv = input.uv - offset / float2(aspect, 1.0f);
    return float4(gScene.SampleLevel(gLinearSampler, uv, 0).rgb, 1.0f);
}
