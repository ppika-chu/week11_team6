cbuffer UIImageTransform : register(b0)
{
    float2 ClipOrigin;
    float2 ClipSize;
    float Ratio;
    float3 Padding;
};

Texture2D UIImageTexture : register(t0);
SamplerState LinearClamp : register(s0);

struct PSInput
{
    float4 Position : SV_POSITION;
    float2 UV : TEXCOORD0;
};

PSInput mainVS(uint VertexID : SV_VertexID)
{
    const float2 UVs[6] = {
        float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(0.0f, 1.0f),
        float2(0.0f, 1.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f)
    };

    PSInput Output;
    Output.UV = UVs[VertexID];
    Output.Position = float4(ClipOrigin + Output.UV * ClipSize, 0.0f, 1.0f);
    return Output;
}

float4 mainPS(PSInput Input) : SV_TARGET
{
    // 이미지 크기와 UV를 유지하고 잔량보다 오른쪽인 부분만 가린다.
    clip(Ratio - Input.UV.x);
    return UIImageTexture.Sample(LinearClamp, Input.UV);
}
