#pragma pack_matrix(row_major)

#include "VertexFactory.hlsli"
#include "LightingCommon.hlsli"
#include "ColorSpace.hlsli"

#define MAX_SKIN_WEIGHT_RAMP_STOPS 16

cbuffer SkinWeightParams : register(b1)
{
	float4 RampStops[MAX_SKIN_WEIGHT_RAMP_STOPS];
    int StopCount;
    int SelectedBone;
    uint2 Padding;
};

struct VS_OUTPUT
{
    float4 SvPosition : SV_POSITION;
    float3 Normal : NORMAL;
    float Weight : TEXCOORD0;
};

VS_OUTPUT mainVS(FVertexFactoryInput Input)
{
    const FVertexFactoryIntermediates Intermediates = GetVertexFactoryIntermediates(Input);
    VS_OUTPUT Output;
    Output.SvPosition = mul(VertexFactoryGetWorldPosition(Input, Intermediates), ViewProjection);
    Output.Normal = VertexFactoryGetWorldNormal(Input, Intermediates);
    Output.Weight = 0.0f;
#if VF_GPU_SKIN
	const FSkinWeight W = SkinWeights[Input.VertexID];
	const uint4 B = uint4(W.PackedBones.x & 0xFFFF, W.PackedBones.x >> 16, W.PackedBones.y & 0xFFFF, W.PackedBones.y >> 16);
    Output.Weight = dot(W.Weights, float4(B == (uint)SelectedBone));
#endif
    return Output;
}

float3 SampleRamp(float T)
{
    float3 Color = RampStops[StopCount - 1].rgb;
    for (int i = 1; i < StopCount; ++i)
    {
        const float4 A = RampStops[i - 1];
        const float4 B = RampStops[i];
        if (T < B.w)
        {
            const float Alpha = saturate((T - A.w) / max(B.w - A.w, 1e-5f));
            Color = lerp(A.rgb, B.rgb, Alpha);
            break;
        }
    }
    return Color;
}

float4 mainPS(VS_OUTPUT Input) : SV_TARGET
{
	// 컬러 램프는 sRGB에서 보간
    const float3 Color = SRGBToLinear(SampleRamp(saturate(Input.Weight)));
    const float Shade = 0.4f + 0.6f * abs(normalize(Input.Normal).z);
    return float4(Color * Shade, 1.0f);
}
